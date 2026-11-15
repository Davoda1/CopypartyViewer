// Copyparty Viewer 2.0.0 — Copyright (C) 2026 davoda1
// SPDX-License-Identifier: MIT
#include "core.hpp"
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <limits>
#include <memory>
#include <mutex>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <unordered_map>
#ifdef _WIN32
# ifndef NOMINMAX
#  define NOMINMAX
# endif
# include <windows.h>
# include <io.h>
# include <fcntl.h>
# include <sys/stat.h>
#else
# include <fcntl.h>
# include <unistd.h>
# include <sys/stat.h>
#endif
#ifndef CPV_DEFAULT_URL
# define CPV_DEFAULT_URL "https://a.ocv.me/pub/"
#endif
#ifndef CPV_DEFAULT_USERNAME
# define CPV_DEFAULT_USERNAME ""
#endif
#ifndef CPV_DEFAULT_PASSWORD
# define CPV_DEFAULT_PASSWORD ""
#endif

namespace cpv {
namespace {
using json = nlohmann::json;
constexpr std::size_t config_limit = 1024u * 1024u;
constexpr std::size_t listing_limit = 16u * 1024u * 1024u;
std::string trim(std::string s) {
    const auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    return s.substr(first, s.find_last_not_of(" \t\r\n") - first + 1);
}
std::string lower(std::string s) {
    for (char& c : s) if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    return s;
}
int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
void valid_utf8(const std::string& s) {
    // Reject malformed UTF-8, controls and lone surrogates before passing data to a UI.
    for (std::size_t i = 0; i < s.size();) {
        unsigned c = static_cast<unsigned char>(s[i++]);
        if (c < 0x80) { if (c < 0x20 || c == 0x7f) throw std::runtime_error("Paths cannot contain control characters."); continue; }
        unsigned need = 0, min = 0, value = 0;
        if (c >= 0xc2 && c <= 0xdf) { need = 1; min = 0x80; value = c & 31; }
        else if (c >= 0xe0 && c <= 0xef) { need = 2; min = 0x800; value = c & 15; }
        else if (c >= 0xf0 && c <= 0xf4) { need = 3; min = 0x10000; value = c & 7; }
        else throw std::runtime_error("The path is not valid UTF-8.");
        if (need > s.size() - i) throw std::runtime_error("The path is not valid UTF-8.");
        while (need--) { unsigned x = static_cast<unsigned char>(s[i++]); if ((x & 0xc0) != 0x80) throw std::runtime_error("The path is not valid UTF-8."); value = (value << 6) | (x & 63); }
        if (value < min || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) throw std::runtime_error("The path is not valid UTF-8.");
    }
}
std::string unescape(const std::string& s, bool strict = true) {
    std::string r; r.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size() && hexval(s[i+1]) >= 0 && hexval(s[i+2]) >= 0) {
            r += char((hexval(s[i+1]) << 4) | hexval(s[i+2])); i += 2;
        } else {
            if (strict && s[i] == '%') throw std::runtime_error("Invalid percent escape in URL.");
            r += s[i];
        }
    }
    valid_utf8(r);
    return r;
}
std::string escape_path(const std::string& s) {
    const char* hex = "0123456789ABCDEF";
    std::string r; r.reserve(s.size());
    for (unsigned char c : s) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~' || c == '/') r += char(c);
        else { r += '%'; r += hex[c >> 4]; r += hex[c & 15]; }
    }
    return r;
}
std::string canonical_path(const std::string& input) {
    if (input.size() > 32768) throw std::runtime_error("The path is too long.");
    valid_utf8(input);
    if (input.find('\\') != std::string::npos) throw std::runtime_error("Use / to separate server directories.");
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (start <= input.size()) {
        auto end = input.find('/', start);
        if (end == std::string::npos) end = input.size();
        auto part = input.substr(start, end - start);
        if (part == "..") { if (parts.empty()) throw std::runtime_error("The path goes above the server root."); parts.pop_back(); }
        else if (!part.empty() && part != ".") parts.push_back(std::move(part));
        start = end + 1;
    }
    std::string r = "/";
    for (std::size_t i = 0; i < parts.size(); ++i) { if (i) r += '/'; r += parts[i]; }
    const bool trailing = !input.empty() && (input.back() == '/' || input == "." || input == ".." || (input.size() >= 2 && input.compare(input.size()-2, 2, "/.") == 0) || (input.size() >= 3 && input.compare(input.size()-3, 3, "/..") == 0));
    if (trailing && r != "/") r += '/';
    return r;
}
struct Url { std::string origin, path, query; };
Url split_url(const std::string& raw, bool allow_query = false) {
    auto pos = raw.find("://");
    if (pos == std::string::npos) throw std::runtime_error("Enter a full http:// or https:// server URL.");
    const auto scheme = lower(raw.substr(0, pos));
    if (scheme != "http" && scheme != "https") throw std::runtime_error("Only HTTP and HTTPS server URLs are supported.");
    if (raw.find('#') != std::string::npos) throw std::runtime_error("Remove the fragment from the URL.");
    auto end = raw.find_first_of("/?", pos+3);
    if (end == std::string::npos) end = raw.size();
    auto authority = lower(raw.substr(pos+3, end-pos-3));
    if (authority.empty() || authority.find('@') != std::string::npos) throw std::runtime_error("Use the username and password fields, not credentials in the URL.");
    std::string host, port;
    if (authority.front() == '[') {
        auto close = authority.find(']');
        if (close == std::string::npos || close == 1) throw std::runtime_error("Invalid IPv6 server address.");
        host = authority.substr(0, close+1);
        if (close+1 < authority.size()) { if (authority[close+1] != ':') throw std::runtime_error("Invalid server address."); port = authority.substr(close+2); if (port.empty()) throw std::runtime_error("Invalid server port."); }
        for (std::size_t i=1; i<close; ++i) if (hexval(host[i]) < 0 && host[i] != ':' && host[i] != '.') throw std::runtime_error("Invalid IPv6 server address.");
    } else {
        auto colon = authority.find(':'); host = authority.substr(0, colon);
        if (colon != std::string::npos) { port = authority.substr(colon+1); if (port.empty()) throw std::runtime_error("Invalid server port."); }
        if (host.empty()) throw std::runtime_error("Enter a server hostname.");
        for (char c : host) if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '-')) throw std::runtime_error("Invalid server hostname; use an ASCII hostname or IPv6 address.");
    }
    if (!port.empty()) {
        unsigned number = 0;
        for (char c : port) { if (c < '0' || c > '9' || number > 6553) throw std::runtime_error("Invalid server port."); number = number*10 + unsigned(c-'0'); }
        if (number == 0 || number > 65535) throw std::runtime_error("Invalid server port.");
        port = std::to_string(number);
        if ((scheme == "https" && port == "443") || (scheme == "http" && port == "80")) port.clear();
    }
    Url u; u.origin = scheme + "://" + host + (port.empty() ? "" : ":" + port);
    auto query = raw.find('?', end);
    if (query != std::string::npos) { if (!allow_query) throw std::runtime_error("Remove query parameters from the server URL or path."); u.query = raw.substr(query+1); }
    u.path = canonical_path(unescape(raw.substr(end, query == std::string::npos ? std::string::npos : query-end)));
    return u;
}
bool below_base(const std::string& full, const std::string& base) {
    if (base == "/") return true;
    return full == base.substr(0, base.size()-1) || full.compare(0, base.size(), base) == 0;
}
std::uint64_t mix(std::uint64_t& x) {
    std::uint64_t z = (x += 0x9e3779b97f4a7c15ULL);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}
std::uint64_t random_u64() {
    static std::mutex mutex;
    static std::random_device rd;
    std::lock_guard<std::mutex> guard(mutex);
    return (std::uint64_t(rd()) << 32) ^ std::uint64_t(rd()) ^ std::uint64_t(std::chrono::high_resolution_clock::now().time_since_epoch().count());
}
std::uint64_t checksum(const std::string& s, std::uint64_t salt) {
    std::uint64_t h = 14695981039346656037ULL ^ salt;
    for (unsigned char c : s) { h ^= c; h *= 1099511628211ULL; }
    return h;
}
void append64(std::string& s, std::uint64_t v) { for (int i=0;i<8;++i) s += char(v >> (i*8)); }
std::uint64_t read64(const std::string& s, std::size_t at) { std::uint64_t v=0; for (int i=0;i<8;++i) v |= std::uint64_t(static_cast<unsigned char>(s[at+i])) << (i*8); return v; }
std::string b64encode(const std::string& s) {
    const char* alphabet="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string r; r.reserve((s.size()+2)/3*4);
    for (std::size_t i=0;i<s.size();i+=3) {
        unsigned n=unsigned(static_cast<unsigned char>(s[i]))<<16;
        if (i+1<s.size()) n |= unsigned(static_cast<unsigned char>(s[i+1]))<<8;
        if (i+2<s.size()) n |= unsigned(static_cast<unsigned char>(s[i+2]));
        r+=alphabet[(n>>18)&63]; r+=alphabet[(n>>12)&63]; r+=i+1<s.size()?alphabet[(n>>6)&63]:'='; r+=i+2<s.size()?alphabet[n&63]:'=';
    }
    return r;
}
std::string b64decode(const std::string& s) {
    if (s.empty() || s.size()%4 || s.size()>16384) throw std::runtime_error("Invalid saved credential data.");
    std::string r;
    auto val=[](char c)->int { if(c>='A'&&c<='Z')return c-'A'; if(c>='a'&&c<='z')return c-'a'+26; if(c>='0'&&c<='9')return c-'0'+52; if(c=='+')return 62; if(c=='/')return 63; return -1; };
    for(std::size_t i=0;i<s.size();i+=4) {
        int a=val(s[i]),b=val(s[i+1]),c=s[i+2]=='='?0:val(s[i+2]),d=s[i+3]=='='?0:val(s[i+3]);
        if(a<0||b<0||c<0||d<0||(s[i+2]=='='&&s[i+3]!='=')||((s[i+2]=='='||s[i+3]=='=')&&i+4!=s.size())) throw std::runtime_error("Invalid saved credential data.");
        unsigned n=unsigned(a<<18|b<<12|c<<6|d); r+=char(n>>16);
        if(s[i+2]!='=')r+=char(n>>8);
        if(s[i+3]!='=')r+=char(n);
    }
    return r;
}
class AtomicFile {
public:
    std::filesystem::path target, temporary;
    FILE* file = nullptr;
    bool committed = false;
    explicit AtomicFile(const std::filesystem::path& dest): target(dest) {
        for (int tries=0; tries<20; ++tries) {
            std::ostringstream suffix; suffix << ".cpv-" << std::hex << random_u64() << ".tmp";
            temporary = target; temporary += suffix.str();
#ifdef _WIN32
            int fd = _wopen(temporary.c_str(), _O_CREAT|_O_EXCL|_O_WRONLY|_O_BINARY, _S_IREAD|_S_IWRITE);
            if (fd >= 0) { file = _fdopen(fd, "wb"); if (!file) _close(fd); }
#else
            int fd = ::open(temporary.c_str(), O_CREAT|O_EXCL|O_WRONLY|O_CLOEXEC, 0600);
            if (fd >= 0) { file = fdopen(fd, "wb"); if (!file) ::close(fd); }
#endif
            if (file) return;
            if (errno != EEXIST) break;
        }
        throw std::runtime_error("Cannot create the temporary file in the selected directory.");
    }
    AtomicFile(const AtomicFile&) = delete;
    ~AtomicFile() { if (file) fclose(file); if (!committed) { std::error_code ignored; std::filesystem::remove(temporary, ignored); } }
    void commit() {
        if (!file) throw std::runtime_error("The destination file is not open.");
        if (fflush(file) != 0) throw std::runtime_error("Cannot flush the saved file; check available disk space.");
#ifdef _WIN32
        if (_commit(_fileno(file)) != 0) throw std::runtime_error("Cannot synchronize the saved file.");
#else
        if (::fsync(fileno(file)) != 0) throw std::runtime_error("Cannot synchronize the saved file.");
#endif
        int close_result = fclose(file); file = nullptr;
        if (close_result != 0) throw std::runtime_error("Cannot finish writing the saved file.");
#ifdef _WIN32
        if (!MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) throw std::runtime_error("Cannot replace the destination file; it may be in use.");
#else
        if (::rename(temporary.c_str(), target.c_str()) != 0) throw std::runtime_error("Cannot replace the destination file.");
        auto parent = target.parent_path(); if (parent.empty()) parent = ".";
        int dfd = ::open(parent.c_str(), O_RDONLY|O_DIRECTORY|O_CLOEXEC); if (dfd >= 0) { ::fsync(dfd); ::close(dfd); }
#endif
        committed = true;
    }
};
json geometry_json(const Geometry& g) { return {{"x",g.x},{"y",g.y},{"width",g.w},{"height",g.h},{"maximized",g.maximized}}; }
Geometry read_geometry(const json& j, Geometry g) {
    if (!j.is_object()) return g;
    auto integer=[&](const char* key,int fallback,int lo,int hi) { if(!j.contains(key)||!j[key].is_number_integer())return fallback; auto n=j[key].get<std::int64_t>(); return int(std::max<std::int64_t>(lo,std::min<std::int64_t>(hi,n))); };
    g.x=integer("x",g.x,-100000,100000); g.y=integer("y",g.y,-100000,100000);
    g.w=integer("width",g.w,320,20000); g.h=integer("height",g.h,240,20000);
    if (j.contains("maximized") && j["maximized"].is_boolean()) g.maximized=j["maximized"].get<bool>();
    return g;
}
std::string text_field(const json& j,const char* key,const std::string& fallback,std::size_t limit) {
    if(!j.contains(key)||!j[key].is_string())return fallback;
    auto value=j[key].get<std::string>(); return value.size()<=limit?value:fallback;
}
bool valid_code_theme(const std::string& value) {
    for (const char* theme : {"default", "vscode", "github", "monokai", "nord",
                              "dracula", "solarized", "codex", "xcode", "ayu"})
        if (value == theme) return true;
    return false;
}
void add_warning(std::string& warning,const std::string& text) { if(!warning.empty())warning+=" "; warning+=text; }

// Metadata is displayed as text, never as server-provided markup or controls.
std::string plain_metadata(const std::string& value, bool remove_html=false) {
    std::string out;
    out.reserve(value.size());
    for(std::size_t i=0;i<value.size();++i) {
        const auto c=static_cast<unsigned char>(value[i]);
        if(remove_html&&c=='<') {
            auto end=value.find('>',i+1);
            if(end!=std::string::npos&&end-i<256) {i=end;continue;}
        }
        if(c<32||c==127) {if(out.empty()||out.back()!=' ')out+=' ';}
        else out+=char(c);
    }
    if(remove_html) {
        for(const auto& entity:std::array<std::pair<const char*,const char*>,6>{{
                {"&lt;","<"},{"&gt;",">"},{"&quot;","\""},{"&#39;","'"},{"&apos;","'"},{"&amp;","&"}}}) {
            std::size_t at=0;const std::string from=entity.first,to=entity.second;
            while((at=out.find(from,at))!=std::string::npos){out.replace(at,from.size(),to);at+=to.size();}
        }
    }
    return out;
}
std::string quoted_permission(const std::string& value) {
    std::string out="'";
    for(char c:value){if(c=='\\'||c=='\'')out+='\\';out+=c;}
    return out+"'";
}
// Keep only printable text and bounded SGR. Other terminal controls must never
// change row identity, cursor position, titles, or hyperlinks in the viewer.
std::pair<std::string,std::string> terminal_line(const std::string& line) {
    std::string ansi,plain;
    for(std::size_t i=0;i<line.size();) {
        const auto c=static_cast<unsigned char>(line[i]);
        if(c==27) {
            ++i;if(i==line.size())break;
            if(line[i]=='[') {
                const auto start=i-1;++i;bool sgr_parameters=true;
                while(i<line.size()&&!(line[i]>=0x40&&line[i]<=0x7e)) {
                    if(!((line[i]>='0'&&line[i]<='9')||line[i]==';'))sgr_parameters=false;
                    ++i;
                }
                if(i<line.size()){if(line[i]=='m'&&sgr_parameters&&i-start<=64)ansi.append(line,start,i-start+1);++i;}
            } else if(line[i]==']') {
                ++i;while(i<line.size()){if(line[i]==7){++i;break;}if(line[i]==27&&i+1<line.size()&&line[i+1]=='\\'){i+=2;break;}++i;}
            } else ++i;
            continue;
        }
        ++i;if(c<32||c==127)continue;
        ansi+=char(c);plain+=char(c);
    }
    valid_utf8(plain);
    return {std::move(ansi),std::move(plain)};
}
bool valid_compact_terminal_date(const std::string& text) {
    if(text.size()<14||!std::all_of(text.begin(),text.begin()+14,[](char c){return c>='0'&&c<='9';}))return false;
    auto number=[&](std::size_t at,std::size_t length){int value=0;for(std::size_t i=at;i<at+length;++i)value=value*10+(text[i]-'0');return value;};
    const auto year=number(0,4),month=number(4,2),day=number(6,2);
    if(year<1||month<1||month>12||day<1||number(8,2)>23||number(10,2)>59||number(12,2)>59)return false;
    static constexpr int days[]={31,28,31,30,31,30,31,31,30,31,30,31};
    const bool leap=year%4==0&&(year%100!=0||year%400==0);
    return day<=days[month-1]+(month==2&&leap?1:0);
}
std::string display_terminal_date(const std::string& ansi) {
    // Called only after the sanitized visible prefix has passed compact-date
    // validation. Copy each original SGR unchanged; insert punctuation before
    // the next digit, after any style transition, so the year alone stays reversed.
    std::string result;result.reserve(ansi.size()+5);
    std::size_t at=0,visible=0;
    while(at<ansi.size()&&visible<14) {
        if(ansi[at]==27) {
            auto end=ansi.find('m',at);
            if(end==std::string::npos)return {};
            result.append(ansi,at,end-at+1);at=end+1;continue;
        }
        if(visible==4||visible==6)result+='-';
        else if(visible==8)result+=' ';
        else if(visible==10||visible==12)result+=':';
        result+=ansi[at++];++visible;
    }
    if(visible!=14)return {};
    result.append(ansi,at,std::string::npos);
    return result;
}

struct CurlDeleter { void operator()(CURL* c)const{if(c)curl_easy_cleanup(c);} };
using Curl = std::unique_ptr<CURL,CurlDeleter>;
struct Transfer {
    TransferControl* control = nullptr;
    Response* response = nullptr;
    FILE* output = nullptr;
    std::size_t maximum = 0, headers = 0;
    long status = 0;
    std::string error, location;
};
size_t write_body(char* data,size_t size,size_t count,void* opaque) noexcept {
    auto& t=*static_cast<Transfer*>(opaque);
    if (size && count>std::numeric_limits<std::size_t>::max()/size) return 0;
    const auto n=size*count;
    try {
        if(t.control->cancel.load(std::memory_order_relaxed))return 0;
        // Stop on the first error/redirect body chunk. A hostile error page
        // must not consume the file-download time or memory allowance.
        if(t.status<200||t.status>=300)return 0;
        if(t.output) { auto written=fwrite(data,1,n,t.output); if(written!=n)t.error="Cannot write the download; check disk space and folder permissions."; return written; }
        if(n>t.maximum-t.response->bytes.size()){t.error="The preview exceeds its size limit. Use Download to save the file.";return 0;}
        t.response->bytes.insert(t.response->bytes.end(),reinterpret_cast<unsigned char*>(data),reinterpret_cast<unsigned char*>(data)+n);
        return n;
    } catch (...) { t.error="Not enough memory to prepare this preview."; return 0; }
}
size_t read_header(char* data,size_t size,size_t count,void* opaque) noexcept {
    auto& t=*static_cast<Transfer*>(opaque);
    if(size&&count>std::numeric_limits<std::size_t>::max()/size)return 0;
    const auto n=size*count;
    t.headers+=n;
    if(n>16384||t.headers>256u*1024u){t.error="The server response headers are too large.";return 0;}
    if(n>5 && std::memcmp(data,"HTTP/",5)==0) {
        const char* space=static_cast<const char*>(std::memchr(data,' ',n));
        if(space && data+n-space>=4) t.status=(space[1]-'0')*100+(space[2]-'0')*10+(space[3]-'0');
        t.location.clear();
    } else if(n>9) {
        try {
            if(lower(std::string(data,9))=="location:")t.location=trim(std::string(data+9,n-9));
        } catch (...) { return 0; }
    }
    return n;
}
int transfer_progress(void* opaque,curl_off_t total,curl_off_t current,curl_off_t,curl_off_t) noexcept {
    auto& t=*static_cast<Transfer*>(opaque);
    t.control->total.store(total>0?std::uint64_t(total):0,std::memory_order_relaxed);
    t.control->received.store(current>0?std::uint64_t(current):0,std::memory_order_relaxed);
    return t.control->cancel.load(std::memory_order_relaxed)?1:0;
}
std::string http_error(long code) {
    switch(code) {
        case 401:return "Sign-in required or the username/password was rejected (HTTP 401).";
        case 403:return "Access denied. Check the account permissions and credentials (HTTP 403).";
        case 404:return "The requested file or directory was not found (HTTP 404).";
        case 408:return "The server timed out while processing the request (HTTP 408).";
        case 429:return "The server is limiting requests. Please try again shortly (HTTP 429).";
        case 500:return "The server encountered an internal error (HTTP 500).";
        case 502:return "The server gateway could not reach Copyparty (HTTP 502).";
        case 503:return "The server is temporarily unavailable (HTTP 503).";
        case 504:return "The server gateway timed out (HTTP 504).";
        default:return "The server returned HTTP "+std::to_string(code)+".";
    }
}
Response perform(const Config& config,const std::string& path,FetchKind kind,TransferControl& control,std::size_t maximum,FILE* output) {
    network_init();
    if(control.cancel.load())throw std::runtime_error("Cancelled.");
    if(config.username.size()>4096||config.password.size()>4096)throw std::runtime_error("The username or password is too long.");
    if(config.username.find(':')!=std::string::npos)throw std::runtime_error("A Basic authentication username cannot contain a colon.");
    if(config.username.find('\0')!=std::string::npos||config.password.find('\0')!=std::string::npos)throw std::runtime_error("Credentials cannot contain a NUL character.");
    const auto server=normalize_server(config.server);
    auto base=split_url(server);
    const std::string query=kind==FetchKind::File?"dl":kind==FetchKind::Terminal?"ls=v":"ls";
    const std::string user_agent="CopypartyViewer/"+std::string(version);
    std::string url=make_url(server,path)+"?"+query;
    Response response;
    std::set<std::string> visited;
    for(int redirects=0;redirects<=5;++redirects) {
        if(control.cancel.load())throw std::runtime_error("Cancelled.");
        if(!visited.insert(url).second)throw std::runtime_error("The server returned a redirect loop.");
        Curl curl(curl_easy_init());
        if(!curl)throw std::runtime_error("Cannot initialize the network request.");
        response.bytes.clear();
        Transfer transfer{&control,&response,output,maximum,0,0,{},{}};
        std::array<char,CURL_ERROR_SIZE> err{};
        curl_easy_setopt(curl.get(),CURLOPT_URL,url.c_str());
        curl_easy_setopt(curl.get(),CURLOPT_USERAGENT,user_agent.c_str());
        curl_easy_setopt(curl.get(),CURLOPT_PROTOCOLS_STR,"http,https");
        curl_easy_setopt(curl.get(),CURLOPT_REDIR_PROTOCOLS_STR,"http,https");
        curl_easy_setopt(curl.get(),CURLOPT_FOLLOWLOCATION,0L);
        curl_easy_setopt(curl.get(),CURLOPT_NOSIGNAL,1L);
        curl_easy_setopt(curl.get(),CURLOPT_CONNECTTIMEOUT,12L);
        curl_easy_setopt(curl.get(),CURLOPT_TIMEOUT,output?86400L:120L);
        curl_easy_setopt(curl.get(),CURLOPT_LOW_SPEED_LIMIT,1L);
        curl_easy_setopt(curl.get(),CURLOPT_LOW_SPEED_TIME,30L);
        curl_easy_setopt(curl.get(),CURLOPT_TCP_KEEPALIVE,1L);
        curl_easy_setopt(curl.get(),CURLOPT_SSL_VERIFYPEER,1L);
        curl_easy_setopt(curl.get(),CURLOPT_SSL_VERIFYHOST,2L);
        curl_easy_setopt(curl.get(),CURLOPT_ACCEPT_ENCODING,"gzip, deflate");
        curl_easy_setopt(curl.get(),CURLOPT_HTTPAUTH,CURLAUTH_BASIC);
        if(!config.username.empty()||!config.password.empty()) {
            curl_easy_setopt(curl.get(),CURLOPT_USERNAME,config.username.c_str());
            curl_easy_setopt(curl.get(),CURLOPT_PASSWORD,config.password.c_str());
        }
        curl_easy_setopt(curl.get(),CURLOPT_WRITEFUNCTION,write_body);
        curl_easy_setopt(curl.get(),CURLOPT_WRITEDATA,&transfer);
        curl_easy_setopt(curl.get(),CURLOPT_HEADERFUNCTION,read_header);
        curl_easy_setopt(curl.get(),CURLOPT_HEADERDATA,&transfer);
        curl_easy_setopt(curl.get(),CURLOPT_XFERINFOFUNCTION,transfer_progress);
        curl_easy_setopt(curl.get(),CURLOPT_XFERINFODATA,&transfer);
        curl_easy_setopt(curl.get(),CURLOPT_NOPROGRESS,0L);
        curl_easy_setopt(curl.get(),CURLOPT_ERRORBUFFER,err.data());
        if(!output)curl_easy_setopt(curl.get(),CURLOPT_MAXFILESIZE_LARGE,static_cast<curl_off_t>(maximum));
        control.received.store(0); control.total.store(0);
        const auto result=curl_easy_perform(curl.get());
        curl_easy_getinfo(curl.get(),CURLINFO_RESPONSE_CODE,&response.status);
        if(control.cancel.load())throw std::runtime_error("Cancelled.");
        if(!transfer.error.empty())throw std::runtime_error(transfer.error);
        if(response.status>=400)throw std::runtime_error(http_error(response.status));
        const bool redirect_body_stopped=response.status>=300&&response.status<400&&
            (result==CURLE_WRITE_ERROR||result==CURLE_FILESIZE_EXCEEDED);
        if(result!=CURLE_OK&&!redirect_body_stopped) {
            if(result==CURLE_OPERATION_TIMEDOUT)throw std::runtime_error("The request timed out. Check the connection and try again.");
            if(result==CURLE_FILESIZE_EXCEEDED)throw std::runtime_error("The preview exceeds its size limit. Use Download to save the file.");
            if(result==CURLE_PARTIAL_FILE)throw std::runtime_error("The connection ended before the complete file arrived. Please retry.");
            if(result==CURLE_PEER_FAILED_VERIFICATION)throw std::runtime_error("The server TLS certificate could not be verified. Check the server certificate and system clock.");
            throw std::runtime_error(safe_error(std::string("Network error: ")+(err[0]?err.data():curl_easy_strerror(result)),config));
        }
        if(response.status>=300&&response.status<400) {
            if(redirects==5)throw std::runtime_error("The server returned too many redirects.");
            if(transfer.location.empty())throw std::runtime_error("The server returned a redirect without a destination.");
            // CURLINFO_REDIRECT_URL may include CURLOPT_USERNAME/PASSWORD in its
            // synthesized URL. Resolve the actual Location header against our
            // credential-free request URL, and reject URL userinfo explicitly.
            std::unique_ptr<CURLU,decltype(&curl_url_cleanup)> resolved(curl_url(),curl_url_cleanup);
            if(!resolved||curl_url_set(resolved.get(),CURLUPART_URL,url.c_str(),CURLU_DISALLOW_USER)!=CURLUE_OK||
               curl_url_set(resolved.get(),CURLUPART_URL,transfer.location.c_str(),CURLU_DISALLOW_USER)!=CURLUE_OK)
                throw std::runtime_error("The server returned an invalid redirect URL.");
            char* destination=nullptr;
            if(curl_url_get(resolved.get(),CURLUPART_URL,&destination,0)!=CURLUE_OK)
                throw std::runtime_error("The server returned an invalid redirect URL.");
            std::unique_ptr<char,decltype(&curl_free)> destination_guard(destination,curl_free);
            auto redirect=split_url(destination,true);
            if(redirect.origin!=base.origin||!below_base(redirect.path,base.path))throw std::runtime_error("The server redirected outside the configured server. Change the server URL in Settings if intended.");
            if(!redirect.query.empty()&&redirect.query!=query)throw std::runtime_error("The server redirected to an unsupported URL. Check the server URL in Settings.");
            url=redirect.origin+escape_path(redirect.path)+"?"+query;
            continue;
        }
        if(response.status<200||response.status>=300)throw std::runtime_error(http_error(response.status));
        if(response.status==206)throw std::runtime_error("The server unexpectedly returned only part of the requested file.");
        char* type=nullptr; curl_easy_getinfo(curl.get(),CURLINFO_CONTENT_TYPE,&type);
        response.content_type=type?type:"";
        response.final_url=url;
        return response;
    }
    throw std::runtime_error("The server returned too many redirects.");
}
} // anonymous namespace

std::string obfuscate(const std::string& value) {
    if(value.empty())return {};
    if(value.size()>4096)throw std::runtime_error("The username or password is too long.");
    const auto nonce=random_u64(); std::string payload; append64(payload,nonce);
    std::uint64_t state=nonce^0x93717e4b5bc1a204ULL,stream=0;
    for(std::size_t i=0;i<value.size();++i){if(i%8==0)stream=mix(state);payload+=char(static_cast<unsigned char>(value[i])^unsigned((stream>>((i%8)*8))&255));}
    append64(payload,checksum(value,nonce));
    // Portable reversible obfuscation only: the implementation contains the key.
    return "v1:"+b64encode(payload);
}
std::string deobfuscate(const std::string& value) {
    if(value.empty())return {};
    if(value.compare(0,3,"v1:")!=0)throw std::runtime_error("Unsupported saved credential format.");
    const auto payload=b64decode(value.substr(3));
    if(payload.size()<17||payload.size()>4112)throw std::runtime_error("Invalid saved credential data.");
    auto nonce=read64(payload,0);std::uint64_t state=nonce^0x93717e4b5bc1a204ULL,stream=0;
    std::string result;result.reserve(payload.size()-16);
    for(std::size_t i=8;i<payload.size()-8;++i){auto n=i-8;if(n%8==0)stream=mix(state);result+=char(static_cast<unsigned char>(payload[i])^unsigned((stream>>((n%8)*8))&255));}
    if(read64(payload,payload.size()-8)!=checksum(result,nonce))throw std::runtime_error("Saved credentials are damaged; enter them again.");
    return result;
}
std::filesystem::path settings_path() {
#ifdef _WIN32
    const wchar_t* appdata=_wgetenv(L"APPDATA");
    if(appdata&&*appdata)return std::filesystem::path(appdata)/L"CopypartyViewer"/L"settings.json";
    const wchar_t* profile=_wgetenv(L"USERPROFILE");
    if(profile&&*profile)return std::filesystem::path(profile)/L"AppData"/L"Roaming"/L"CopypartyViewer"/L"settings.json";
    throw std::runtime_error("Windows has no user application-data directory.");
#else
    const char* xdg=std::getenv("XDG_CONFIG_HOME");
    if(xdg&&*xdg&&std::filesystem::path(xdg).is_absolute())return std::filesystem::path(xdg)/"copyparty-viewer"/"settings.json";
    const char* home=std::getenv("HOME");
    if(home&&*home)return std::filesystem::path(home)/".config"/"copyparty-viewer"/"settings.json";
    throw std::runtime_error("No home directory is available for settings.");
#endif
}
Config default_config() {
    Config c;c.server=normalize_server(CPV_DEFAULT_URL);c.username=CPV_DEFAULT_USERNAME;c.password=CPV_DEFAULT_PASSWORD;return c;
}
Config load_config(const std::filesystem::path& path,std::string& warning) {
    warning.clear();Config c=default_config();
    std::error_code ec;if(!std::filesystem::exists(path,ec)){if(ec)warning="Cannot inspect the settings file. Using defaults.";return c;}
    try {
        auto size=std::filesystem::file_size(path);if(size>config_limit)throw std::runtime_error("The settings file is too large.");
        std::ifstream input(path,std::ios::binary);if(!input)throw std::runtime_error("Cannot open the settings file.");
        std::string bytes(config_limit+1, '\0');
        input.read(bytes.data(),static_cast<std::streamsize>(bytes.size()));
        bytes.resize(static_cast<std::size_t>(input.gcount()));
        if(bytes.size()>config_limit)throw std::runtime_error("The settings file is too large.");
        auto j=json::parse(bytes);if(!j.is_object())throw std::runtime_error("Settings must be a JSON object.");
        if(j.contains("schema_version")&&j["schema_version"]!=1)add_warning(warning,"Unknown settings schema; recognized fields were restored.");
        if(j.contains("modern_ui")&&j["modern_ui"].is_boolean())c.modern_ui=j["modern_ui"].get<bool>();
        if(j.contains("show_directory_sizes")&&j["show_directory_sizes"].is_boolean())c.show_directory_sizes=j["show_directory_sizes"].get<bool>();
        if(j.contains("appearance")&&j["appearance"].is_object()) {
            const auto theme=text_field(j["appearance"],"code_theme","default",32);
            if(valid_code_theme(theme))c.code_theme=theme;
        }
        try{c.server=normalize_server(text_field(j,"server_url",c.server,32768));}catch(...){add_warning(warning,"Invalid saved server URL; using the compiled default.");}
        auto saved=text_field(j,"last_path","/",32768);
        try{c.last_path=canonical_path(saved);}catch(...){add_warning(warning,"Invalid saved path; opening the server root.");}
        if(j.contains("window"))c.main_window=read_geometry(j["window"],c.main_window);
        if(j.contains("settings_window"))c.settings_window=read_geometry(j["settings_window"],c.settings_window);
        if(j.contains("credentials")&&j["credentials"].is_object()){
            const auto& creds=j["credentials"];
            // Existing 1.0.0 configurations predate the explicit saved marker.
            c.credentials_saved=!creds.contains("saved")||(creds["saved"].is_boolean()&&creds["saved"].get<bool>());
            if(creds.contains("saved")&&!creds["saved"].is_boolean())add_warning(warning,"Invalid credential preference; using the compiled defaults.");
            if(c.credentials_saved) {
                c.username.clear();c.password.clear();
                if(text_field(creds,"format","",100)!="cpv-obfuscated-v1")add_warning(warning,"Unknown credential format; enter your credentials again.");
                else{
                    try{c.username=deobfuscate(text_field(creds,"username","",16384));}catch(...){add_warning(warning,"The saved username is damaged; enter it again.");}
                    try{c.password=deobfuscate(text_field(creds,"password","",16384));}catch(...){add_warning(warning,"The saved password is damaged; enter it again.");}
                }
            }
        }
    }catch(const std::exception&){warning="Cannot read the settings file. Defaults are being used; the file may be damaged or unreadable.";}
    return c;
}
bool save_config(const std::filesystem::path& path,const Config& c,std::string& error) {
    error.clear();
    try{
        auto parent=path.parent_path();if(!parent.empty())std::filesystem::create_directories(parent);
#ifndef _WIN32
        if(!parent.empty()&&::chmod(parent.c_str(),0700)!=0)throw std::runtime_error("Cannot protect the settings directory permissions.");
#endif
        json j={{"schema_version",1},{"server_url",normalize_server(c.server)},{"last_path",canonical_path(c.last_path)},
            {"modern_ui",c.modern_ui},{"show_directory_sizes",c.show_directory_sizes},
            {"appearance",{{"code_theme",valid_code_theme(c.code_theme)?c.code_theme:"default"}}},
            {"credentials",{{"format","cpv-obfuscated-v1"},{"saved",c.credentials_saved},
                {"username",c.credentials_saved?obfuscate(c.username):""},{"password",c.credentials_saved?obfuscate(c.password):""}}},
            {"window",geometry_json(c.main_window)},{"settings_window",geometry_json(c.settings_window)}};
        auto encoded=j.dump(2)+"\n";AtomicFile out(path);
        if(fwrite(encoded.data(),1,encoded.size(),out.file)!=encoded.size())throw std::runtime_error("Cannot write the settings file.");
        out.commit();return true;
    }catch(const std::exception& ex){error=safe_error(ex.what(),c);return false;}
}
std::string normalize_server(const std::string& input) {
    auto u=split_url(trim(input));
    if(u.path.back()!='/')u.path+='/';
    return u.origin+escape_path(u.path);
}
std::string resolve_path(const std::string& input,const std::string& current,const std::string& server) {
    const auto s=trim(input);if(s.empty())return "/";
    if(s.find("://")!=std::string::npos){
        auto base=split_url(normalize_server(server));auto full=split_url(s);
        if(base.origin!=full.origin||!below_base(full.path,base.path))throw std::runtime_error("The URL belongs to a different server or root. Change the server URL in Settings.");
        if(full.path==base.path.substr(0,base.path.size()-1))return "/";
        return canonical_path("/"+full.path.substr(base.path.size()));
    }
    if(s.compare(0,2,"//")==0)throw std::runtime_error("Use a full URL or a path within the configured server.");
    if(s.find('?')!=std::string::npos||s.find('#')!=std::string::npos)throw std::runtime_error("Enter a path without query parameters or a fragment. Encode literal ? or # characters as %3F or %23.");
    auto decoded=unescape(s,false);
    if(decoded.front()=='/')return canonical_path(decoded);
    auto base=canonical_path(current.empty()?"/":current);
    if(base.back()!='/')base=parent_path(base);
    return canonical_path(base+decoded);
}
std::string make_url(const std::string& server,const std::string& path) {
    auto normalized=normalize_server(server);auto p=canonical_path(path);
    return normalized+escape_path(p.substr(1));
}
DocumentLink resolve_document_link(const std::string& input,const std::string& current_file_path,const std::string& server) {
    if(input.size()>32768)throw std::runtime_error("The document link is too long.");
    valid_utf8(input);
    if(input.find('\\')!=std::string::npos)throw std::runtime_error("Document links cannot contain backslashes.");
    const auto value=trim(input);
    const auto hash=value.find('#');
    const auto raw_anchor=hash==std::string::npos?std::string():value.substr(hash+1);
    const auto anchor=unescape(raw_anchor,false);
    if(anchor.find('\\')!=std::string::npos)throw std::runtime_error("Document link anchors cannot contain backslashes.");
    const auto destination=value.substr(0,hash);
    if(destination.empty())return {DocumentLink::Kind::Anchor,"",anchor};
    if(destination.compare(0,2,"//")==0)throw std::runtime_error("Use a full HTTP or HTTPS URL instead of a network-path link.");

    const auto colon=destination.find(':');
    const auto separator=destination.find_first_of("/?");
    const bool has_scheme=colon!=std::string::npos&&(separator==std::string::npos||colon<separator);
    const auto query=destination.find('?');
    if(has_scheme) {
        const auto scheme=lower(destination.substr(0,colon));
        if((scheme!="http"&&scheme!="https")||destination.compare(colon,3,"://")!=0)
            throw std::runtime_error("Only HTTP and HTTPS document links can open outside the viewer.");
        const auto full=split_url(destination,true);
        const auto base=split_url(normalize_server(server));
        if(query==std::string::npos&&full.origin==base.origin&&below_base(full.path,base.path))
            return {DocumentLink::Kind::ServerPath,resolve_path(destination,current_file_path,server),anchor};
        // Preserve the explicitly clicked, credential-free URL. Queries are
        // browser-only; they must never enter an authenticated viewer request.
        return {DocumentLink::Kind::Browser,value,anchor};
    }

    const auto path_part=destination.substr(0,query);
    const auto path=path_part.empty()?canonical_path(current_file_path.empty()?"/":current_file_path)
                                    :resolve_path(path_part,current_file_path,server);
    if(query!=std::string::npos) {
        auto url=make_url(server,path)+destination.substr(query);
        if(hash!=std::string::npos)url+="#"+raw_anchor;
        return {DocumentLink::Kind::Browser,std::move(url),anchor};
    }
    return {DocumentLink::Kind::ServerPath,path,anchor};
}
std::string parent_path(const std::string& path) {
    auto p=canonical_path(path);if(p=="/")return p;if(p.back()=='/')p.pop_back();
    auto at=p.rfind('/');return p.substr(0,at+1);
}
std::string basename(const std::string& path) {
    auto p=canonical_path(path);if(p=="/")return "/";if(p.back()=='/')p.pop_back();return p.substr(p.rfind('/')+1);
}
std::string human_size(std::uint64_t bytes) {
    const char* units[]={"B","KiB","MiB","GiB","TiB","PiB","EiB"};unsigned unit=0;double value=double(bytes);
    while(value>=1024&&unit<6){value/=1024;++unit;}
    std::ostringstream s;if(unit)s<<std::fixed<<std::setprecision(value<10?1:0);s<<value<<' '<<units[unit];return s.str();
}
std::string terminal_size(std::uint64_t bytes) {
    // Copyparty humansize(..., terse=True) truncates the decimal representation
    // to four characters; it does not round to the nearest display unit.
    static constexpr const char* units="BKMGTPE";
    unsigned unit=0;double value=static_cast<double>(bytes);
    while(value>=1024.0&&unit<6){value/=1024.0;++unit;}
    if(!unit)return std::to_string(bytes)+"B";
    // The JSON dependency already supplies shortest round-trip decimal output
    // (including .0 for integral doubles), matching Python's str(float) here.
    auto number=json(value).dump().substr(0,4);
    if(!number.empty()&&number.back()=='.')number.pop_back();
    return number+units[unit];
}
int size_color_index(std::uint64_t bytes) {
    unsigned unit=0;while(bytes>=1024&&unit<6){bytes/=1024;++unit;}
    static constexpr int colors[]={6,5,1,3,0,0,0};
    return colors[unit];
}
std::string terminal_time(std::int64_t epoch) {
    constexpr const char* unknown="---------- --:--:--";
    if(epoch<=0||epoch>253402300799LL)return unknown;
    std::time_t raw=static_cast<std::time_t>(epoch);std::tm result{};
#ifdef _WIN32
    if(gmtime_s(&result,&raw)!=0)return unknown;
#else
    if(!gmtime_r(&raw,&result))return unknown;
#endif
    char text[32];
    if(std::strftime(text,sizeof(text),"%Y-%m-%d %H:%M:%S",&result)!=19)return unknown;
    return text;
}
std::string terminal_time_ansi(std::int64_t epoch) {
    const auto date=terminal_time(epoch);
    return "\033[0;7;36m\033[7m"+date.substr(0,4)+"\033[27m"+date.substr(4,6)+
        "\033[0;1m"+date.substr(10,3)+"\033[0;36m"+date.substr(13,3)+"\033[0m"+date.substr(16,3);
}
std::string terminal_entry_ansi(const Entry& entry,bool show_directory_sizes) {
    const bool visible=!entry.directory||show_directory_sizes;
    if(visible&&entry.size_known&&!entry.terminal_line_ansi.empty())return entry.terminal_line_ansi+"\033[0m";
    const auto size=visible?(entry.size_known?terminal_size(entry.size):"—"):"";
    const auto width=size=="—"?1u:static_cast<unsigned>(size.size());
    const auto padding=width<5?std::string(5-width,' '):"";
    const auto color=visible&&entry.size_known?"\033[0;3"+std::to_string(size_color_index(entry.size))+"m":"\033[0m";
    return terminal_time_ansi(entry.modified)+color+" "+padding+size+"\033[0m "+
        (entry.directory?"\033[94m":"")+entry.name+(entry.directory?"/":"")+"\033[0m";
}
std::string format_time(std::int64_t epoch) {
    if(epoch<=0)return "—";
    std::time_t raw=static_cast<std::time_t>(epoch);std::tm result{};
#ifdef _WIN32
    if(localtime_s(&result,&raw)!=0)return "—";
#else
    if(!localtime_r(&raw,&result))return "—";
#endif
    char text[64];if(!std::strftime(text,sizeof(text),"%Y-%m-%d %H:%M",&result))return "—";return text;
}
std::string safe_error(const std::string& error,const Config& config) {
    std::string out=error;
    for(const auto* secret:{&config.password,&config.username})if(!secret->empty()){
        std::size_t at=0;while((at=out.find(*secret,at))!=std::string::npos){out.replace(at,secret->size(),"[redacted]");at+=10;}
        auto escaped=escape_path(*secret);if(escaped!=*secret){at=0;while((at=out.find(escaped,at))!=std::string::npos){out.replace(at,escaped.size(),"[redacted]");at+=10;}}
    }
    // Do not echo userinfo from malformed URLs supplied by a server.
    auto scheme=out.find("://");
    while(scheme!=std::string::npos){auto end=out.find_first_of("/ \t\r\n",scheme+3);auto at=out.find('@',scheme+3);if(at!=std::string::npos&&(end==std::string::npos||at<end))out.replace(scheme+3,at-scheme-2,"[redacted]@");scheme=out.find("://",scheme+3);}
    return out;
}
Listing parse_listing(const std::string& text,const std::string& requested_path) {
    if(text.size()>listing_limit)throw std::runtime_error("The directory listing is too large to display.");
    json j;
    try{j=json::parse(text);}catch(...){throw std::runtime_error("The server did not return a Copyparty directory listing. Check the path and account permissions.");}
    if(!j.is_object()||!j.contains("dirs")||!j.contains("files")||!j["dirs"].is_array()||!j["files"].is_array())throw std::runtime_error("This is not a Copyparty directory listing.");
    Listing listing;listing.path=canonical_path(requested_path);if(listing.path.back()!='/')listing.path+='/';
    if(j.contains("acct")&&j["acct"].is_string()) {
        listing.account=plain_metadata(text_field(j,"acct","",4096));
        listing.header_ansi+="# acct: "+listing.account+"\n";
    }
    if(j.contains("perms")&&j["perms"].is_array()) {
        std::string rendered;
        for(const auto& permission:j["perms"]) {
            if(!permission.is_string()||listing.permissions.size()>=32)continue;
            auto value=permission.get<std::string>();if(value.size()>256)continue;
            value=plain_metadata(value);listing.permissions.push_back(value);
            if(!rendered.empty())rendered+=", ";
            rendered+=quoted_permission(value);
        }
        listing.header_ansi+="# perms: ["+rendered+"]\n";
    }
    if(j.contains("srvinf")&&j["srvinf"].is_string()) {
        listing.server_info=plain_metadata(text_field(j,"srvinf","",8192),true);
        listing.header_ansi+="# srvinf: "+listing.server_info+"\n";
    }
    if(j["dirs"].size()+j["files"].size()>100000)throw std::runtime_error("This directory contains too many items to display (limit: 100,000).");
    listing.entries.reserve(j["dirs"].size()+j["files"].size());
    std::set<std::string> seen;
    std::size_t stored_path_bytes=0;
    auto append=[&](const json& items,bool directory){for(const auto& item:items){
        if(!item.is_object()||!item.contains("href")||!item["href"].is_string())continue;
        auto href=item["href"].get<std::string>();if(href.empty()||href.size()>32768)continue;
        try{
            if(href.find("://")!=std::string::npos||href.front()=='/'||href.find('?')!=std::string::npos||href.find('#')!=std::string::npos)continue;
            auto name=unescape(href);if(directory&&name.back()=='/')name.pop_back();
            if(name.empty()||name=="."||name==".."||name.find('/')!=std::string::npos||name.find('\\')!=std::string::npos)continue;
            Entry entry;entry.name=name;entry.directory=directory;entry.path=listing.path+name+(directory?"/":"");
            if(!seen.insert(entry.path).second)continue;
            if(entry.path.size()+entry.name.size()>64u*1024u*1024u-stored_path_bytes)
                throw std::length_error("The directory contains too much path data to display safely.");
            stored_path_bytes+=entry.path.size()+entry.name.size();
            if(item.contains("sz")&&item["sz"].is_number_unsigned()){entry.size=item["sz"].get<std::uint64_t>();entry.size_known=true;}
            else if(item.contains("sz")&&item["sz"].is_number_integer()){auto size=item["sz"].get<std::int64_t>();if(size>=0){entry.size=std::uint64_t(size);entry.size_known=true;}}
            if(directory&&item.contains("tags")&&item["tags"].is_object()&&item["tags"].contains(".files")) {
                const auto& count=item["tags"][".files"];
                entry.directory_size_indexed=count.is_number_unsigned()||(count.is_number_integer()&&count.get<std::int64_t>()>=0);
            }
            if(item.contains("ts")&&item["ts"].is_number()){double t=item["ts"].get<double>();if(t>0&&t<32503680000.0)entry.modified=std::int64_t(t);}
            listing.entries.push_back(std::move(entry));
        }catch(const std::length_error&){throw;}
        catch(const std::exception&){continue;}
    }};
    append(j["dirs"],true);append(j["files"],false);
    return listing;
}
std::size_t apply_terminal_listing(Listing& listing,const std::string& terminal) {
    if(terminal.size()>listing_limit)throw std::runtime_error("The terminal listing is too large to display.");
    std::unordered_map<std::string,std::size_t> by_name;
    for(std::size_t i=0;i<listing.entries.size();++i) {
        auto& entry=listing.entries[i];entry.terminal_line_ansi.clear();
        by_name.emplace(entry.name+(entry.directory?"/":""),i);
    }
    std::set<std::size_t> matched;
    std::string header;
    std::size_t start=0;
    while(start<terminal.size()) {
        auto end=terminal.find('\n',start);if(end==std::string::npos)end=terminal.size();
        const auto length=end-start;
        if(length<=65536) {
            try {
                auto line=terminal_line(terminal.substr(start,length));
                const auto& text=line.second;
                if(text.compare(0,8,"# acct: ")==0||text.compare(0,9,"# perms: ")==0||text.compare(0,10,"# srvinf: ")==0) {
                    if(header.size()+line.first.size()+1<=16384)header+=line.first+"\n";
                } else if(text.size()>16&&text[14]==' '&&valid_compact_terminal_date(text)) {
                    auto size_start=text.find_first_not_of(' ',14);
                    auto size_end=size_start==std::string::npos?std::string::npos:text.find(' ',size_start);
                    if(size_end!=std::string::npos) {
                        auto found=by_name.find(text.substr(size_end+1));
                        if(found!=by_name.end()&&!matched.count(found->second)) {
                            auto displayed=display_terminal_date(line.first);
                            if(!displayed.empty()) {
                                matched.insert(found->second);
                                listing.entries[found->second].terminal_line_ansi=std::move(displayed);
                            }
                        }
                    }
                }
            } catch(const std::exception&) { /* Ignore malformed rows, keeping the verified JSON identities. */ }
        }
        start=end+1;
    }
    if(!header.empty())listing.header_ansi=std::move(header);
    return matched.size();
}
Response fetch(const Config& config,const std::string& path,FetchKind kind,TransferControl& control,std::size_t maximum) {
    if(kind==FetchKind::Listing||kind==FetchKind::Terminal)maximum=std::min(maximum,listing_limit);
    return perform(config,path,kind,control,maximum,nullptr);
}
void download(const Config& config,const std::string& path,const std::filesystem::path& target,TransferControl& control) {
    if(control.cancel.load())throw std::runtime_error("Cancelled.");
    AtomicFile out(target);perform(config,path,FetchKind::File,control,0,out.file);
    if(control.cancel.load())throw std::runtime_error("Cancelled.");
    out.commit();
}
void network_init(){static std::once_flag once;static CURLcode result=CURLE_OK;std::call_once(once,[]{result=curl_global_init(CURL_GLOBAL_DEFAULT);});if(result!=CURLE_OK)throw std::runtime_error("Cannot initialize networking.");}
void network_cleanup(){curl_global_cleanup();}
} // namespace cpv
