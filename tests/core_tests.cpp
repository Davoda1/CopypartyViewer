// Copyright (C) 2026 davoda1 — SPDX-License-Identifier: MIT
#include "core.hpp"
#include <nlohmann/json.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <thread>
#ifndef _WIN32
# include <sys/stat.h>
#endif
using namespace cpv;
namespace {
int checks = 0;
void require(bool ok, const char* message) {
    ++checks;
    if (!ok) throw std::runtime_error(message);
}
template<class F> std::string rejects(F f, const char* message) {
    ++checks;
    try { f(); } catch (const std::exception& e) { return e.what(); }
    throw std::runtime_error(message);
}
std::string read(const std::filesystem::path& p) {
    std::ifstream file(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(file), {});
}
void write(const std::filesystem::path& p, const std::string& text) {
    std::ofstream file(p, std::ios::binary); file << text;
}
struct TempDirectory {
    std::filesystem::path path;
    TempDirectory() {
        const auto id = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() / ("copyparty-viewer-tests-" + std::to_string(id));
        std::filesystem::create_directories(path);
    }
    ~TempDirectory() { std::error_code error; std::filesystem::remove_all(path, error); }
};
// This release gate is opt-in so ordinary tests still support intentional
// custom builds with a different embedded server or account.
void public_defaults(const std::filesystem::path& dir) {
    const std::string public_url = "https://a.ocv.me/pub/";
    const auto defaults = default_config();
    require(defaults.server == public_url, "public release embeds the public server root");
    require(defaults.username.empty() && defaults.password.empty() && !defaults.credentials_saved, "public release embeds no account credentials");
    require(make_url(defaults.server, "/") == public_url && resolve_path(public_url, "/", defaults.server) == "/", "public server subdirectory is the application root");
    const auto path = dir / "public-defaults.json";
    std::string warning;
    auto restored = load_config(path, warning);
    require(warning.empty() && restored.server == public_url && restored.last_path == "/" && restored.username.empty() && restored.password.empty(), "first start uses the public root without credentials");
    write(path, "not-json");
    restored = load_config(path, warning);
    require(!warning.empty() && restored.server == public_url && restored.username.empty() && restored.password.empty(), "corrupt settings fall back to the public defaults");
    write(path, R"({"server_url":"file:///invalid"})");
    restored = load_config(path, warning);
    require(!warning.empty() && restored.server == public_url && restored.username.empty() && restored.password.empty(), "invalid saved server falls back to the public defaults");
    require(save_config(path, defaults, warning), "public defaults can be saved");
    const auto saved = nlohmann::json::parse(read(path));
    require(saved["server_url"] == public_url && saved["credentials"]["saved"] == false && saved["credentials"]["username"] == "" && saved["credentials"]["password"] == "", "saved public defaults contain only the public server and empty credential payloads");
    std::filesystem::remove(path);
}
void paths() {
    const std::string server = "https://example.test/base/";
    require(normalize_server(" HTTPS://Example.Test:443/base ") == server, "normalize scheme, host, port and slash");
    require(normalize_server("http://[::1]:8080/") == "http://[::1]:8080/", "IPv6 and port");
    require(normalize_server("https://example.test/a%20b/") == "https://example.test/a%20b/", "server encoded path");
    require(resolve_path("folder/", "/", server) == "/folder/", "relative path from root");
    require(resolve_path("other.txt", "/folder/", server) == "/folder/other.txt", "relative path from directory");
    require(resolve_path("other.txt", "/folder/file.txt", server) == "/folder/other.txt", "relative path from file parent");
    require(resolve_path("/folder/../a/./", "/x/", server) == "/a/", "dot path segments");
    require(resolve_path("..", "/x/y/", server) == "/x/", "parent with trailing slash");
    require(resolve_path("https://EXAMPLE.test:443/base/a%20b.txt", "/x/", server) == "/a b.txt", "full matching URL");
    require(resolve_path("https://example.test/base", "/", server) == "/", "full base URL without final slash");
    require(resolve_path("https://example.test/base/", "/", server) == "/", "full base URL");
    require(resolve_path("caf%C3%A9%20%E6%96%87%E4%BB%B6.txt", "/", server) == "/café 文件.txt", "UTF-8 percent decoding");
    require(resolve_path("100% done.txt", "/", server) == "/100% done.txt", "literal percent with no valid escape");
    require(resolve_path("%252e%252e.txt", "/", server) == "/%2e%2e.txt", "decode percent exactly once");
    require(resolve_path("%23name%3F.txt", "/", server) == "/#name?.txt", "escaped filename query characters");
    require(make_url(server, "/café 文件%20.txt") == "https://example.test/base/caf%C3%A9%20%E6%96%87%E4%BB%B6%2520.txt", "correct path encoding");
    require(make_url(server, "/#name?.txt") == "https://example.test/base/%23name%3F.txt", "never emit query from filename");
    require(parent_path("/a/b/") == "/a/", "parent directory");
    require(parent_path("/a") == "/", "parent root");
    require(cpv::basename("/a/b/") == "b", "directory basename");
    require(cpv::basename("/") == "/", "root basename");
    rejects([&]{normalize_server("file:///etc/passwd");}, "unsupported URL scheme");
    rejects([&]{normalize_server("https://u:p@example.test/");}, "URL credentials rejected");
    rejects([&]{normalize_server("https://example.test/?pw=x");}, "URL credential queries rejected");
    rejects([&]{normalize_server("https://example.test:65536/");}, "invalid port rejected");
    rejects([&]{normalize_server("https://example.test:12x/");}, "invalid port characters rejected");
    rejects([&]{resolve_path("https://example.test.evil/base/a", "/", server);}, "different hostname rejected");
    rejects([&]{resolve_path("http://example.test/base/a", "/", server);}, "HTTPS downgrade rejected");
    rejects([&]{resolve_path("https://example.test/base2/a", "/", server);}, "base boundary enforced");
    rejects([&]{resolve_path("https://example.test/base/%2e%2e/a", "/", server);}, "encoded full URL traversal rejected");
    rejects([&]{resolve_path("//evil.test/a", "/", server);}, "scheme-relative URL rejected");
    rejects([&]{resolve_path("../../a", "/b/", server);}, "root traversal rejected");
    rejects([&]{resolve_path("bad%00name", "/", server);}, "NUL filename rejected");
    rejects([&]{resolve_path("bad%C0%AFname", "/", server);}, "overlong UTF-8 rejected");
    rejects([&]{resolve_path("bad%ED%A0%80name", "/", server);}, "UTF-8 surrogate rejected");
    rejects([&]{resolve_path("bad\\name", "/", server);}, "backslash rejected");
    rejects([&]{resolve_path("a?move=/b", "/", server);}, "query injection rejected");
}
void configuration(const std::filesystem::path& dir) {
    Config c = default_config();
    require(c.modern_ui && c.show_directory_sizes, "default view is Modern with folder sizes");
    require(!c.credentials_saved, "compiled credentials start as unsaved");
    c.server = "https://example.test/base/";
    c.username = "test-user-λ"; c.password = "test-pw-文-!:$";
    c.credentials_saved = true;
    c.modern_ui = false; c.show_directory_sizes = false;
    c.last_path = "/café/file.txt";
    c.main_window = {-1500, 25, 1320, 810, true};
    c.settings_window = {45, 60, 650, 420, false};
    const auto path = dir / "config" / "settings.json";
    std::string error;
    require(save_config(path, c, error), "settings saved");
    auto text = read(path);
    require(text.find(c.username) == std::string::npos, "username not in clear text");
    require(text.find(c.password) == std::string::npos, "password not in clear text");
    require(text.find("test-pw") == std::string::npos, "password fragments not in JSON");
    auto restored = load_config(path, error);
    require(error.empty(), "valid config has no warnings");
    require(restored.server == c.server && restored.last_path == c.last_path, "server/path round trip");
    require(restored.username == c.username && restored.password == c.password, "both credentials round trip");
    require(restored.credentials_saved && !restored.modern_ui && !restored.show_directory_sizes, "saved credentials and view preferences round trip");
    require(restored.main_window.x == -1500 && restored.main_window.w == 1320 && restored.main_window.maximized, "main geometry round trip");
    require(restored.settings_window.y == 60 && restored.settings_window.h == 420, "settings geometry round trip");
    auto copy = dir / "portable-copy.json";
    write(copy, text);
    auto portable = load_config(copy, error);
    require(portable.password == c.password && portable.username == c.username, "copied JSON remains portable");
    auto legacy = nlohmann::json::parse(text);
    legacy.erase("modern_ui"); legacy.erase("show_directory_sizes"); legacy["credentials"].erase("saved");
    write(copy, legacy.dump());
    portable = load_config(copy, error);
    require(portable.modern_ui && portable.show_directory_sizes, "older JSON gains view defaults");
    require(portable.credentials_saved && portable.username == c.username && portable.password == c.password, "legacy explicit credentials remain saved");
    legacy["modern_ui"] = "false"; legacy["show_directory_sizes"] = 0;
    write(copy, legacy.dump()); portable = load_config(copy, error);
    require(portable.modern_ui && portable.show_directory_sizes, "malformed preference types do not silently change defaults");
    Config session = default_config();
    session.username = "session-only-user"; session.password = "session-only-value";
    require(save_config(copy, session, error), "ordinary session state can be saved");
    const auto session_json = nlohmann::json::parse(read(copy));
    require(session_json["credentials"]["saved"] == false && session_json["credentials"]["username"] == "" && session_json["credentials"]["password"] == "", "unsaved credentials have no serialized payloads");
    portable = load_config(copy, error);
    const auto compiled = default_config();
    require(!portable.credentials_saved && portable.username == compiled.username && portable.password == compiled.password, "unsaved config preserves compiled defaults only in memory");
    session.credentials_saved = true; session.username.clear(); session.password.clear();
    require(save_config(copy, session, error), "explicit blank credentials can be saved");
    portable = load_config(copy, error);
    require(portable.credentials_saved && portable.username.empty() && portable.password.empty(), "saved blank credentials override compiled defaults");
#ifndef _WIN32
    struct stat st{};
    require(stat(path.c_str(), &st) == 0 && (st.st_mode & 0777) == 0600, "settings mode is user-only");
    require(stat(path.parent_path().c_str(), &st) == 0 && (st.st_mode & 0777) == 0700, "settings directory mode is user-only");
#endif
    auto damaged = nlohmann::json::parse(text);
    damaged["credentials"]["password"] = "v1:this-is-not-valid";
    write(path, damaged.dump());
    restored = load_config(path, error);
    require(!error.empty(), "damaged password is reported");
    require(restored.password.empty() && restored.username == c.username, "damaged credential isolated");
    require(restored.server == c.server && restored.main_window.w == 1320, "damaged credential preserves other settings");
    damaged["credentials"]["format"] = "future-format";
    write(path, damaged.dump()); restored = load_config(path, error);
    require(restored.username.empty() && restored.password.empty() && !error.empty(), "unknown credential format is not treated as cleartext");
    write(path, "not-json"); restored = load_config(path, error);
    require(!error.empty() && restored.last_path == "/", "corrupt JSON uses defaults with warning");
    const auto encoded = obfuscate(c.password);
    require(encoded != obfuscate(c.password), "credential nonce changes payload");
    require(deobfuscate(encoded) == c.password, "portable obfuscation round trip");
    require(obfuscate("").empty() && deobfuscate("").empty(), "blank credentials stay blank");
    auto corrupt = encoded; corrupt[10] = corrupt[10] == 'A' ? 'B' : 'A';
    rejects([&]{deobfuscate(corrupt);}, "corruption checksum detected");
    rejects([&]{deobfuscate("plain secret");}, "plaintext not silently accepted");
    auto redacted = safe_error("failure for " + c.username + ":" + c.password, c);
    require(redacted.find(c.username) == std::string::npos && redacted.find(c.password) == std::string::npos, "errors redact credentials");
}
void listings() {
    const char* text = R"({"dirs":[{"href":"sub%20dir/","sz":123,"ts":1700000000}],"files":[{"href":"hello%20world.txt","sz":42,"ts":1700000000.5},{"href":"caf%C3%A9.png","sz":88},{"href":"%23x%3Fy.txt","sz":1},{"href":"../secret"},{"href":"//evil.test/file"},{"href":"https://evil.test/file"},{"href":"%2E%2E%2Fsecret"},{"href":"a?move=/b"},{"href":"%00bad"},{"href":"hello%20world.txt","sz":500}]})";
    auto list = parse_listing(text, "/a");
    require(list.path == "/a/", "listing path gains slash");
    require(list.entries.size() == 4, "unsafe and duplicate listing links omitted");
    require(list.entries[0].directory && list.entries[0].path == "/a/sub dir/", "clickable directory path");
    require(list.entries[0].size_known && list.entries[0].size == 123, "directory size is preserved from the server");
    require(list.entries[1].name == "hello world.txt" && list.entries[1].size == 42 && list.entries[1].modified == 1700000000, "file attributes parsed");
    require(list.entries[2].path == "/a/café.png", "unicode listing name");
    require(list.entries[3].path == "/a/#x?y.txt", "literal query chars in escaped listing filenames");
    rejects([]{parse_listing("<html>Sign in</html>", "/");}, "HTML not accepted as listing");
    rejects([]{parse_listing("{\"files\":[]}", "/");}, "nonlisting JSON rejected");
    require(human_size(1024) == "1.0 KiB", "human-readable size");
    require(format_time(0) == "—", "missing timestamp marked");
}
void terminal_listings() {
    const char* data = R"({"acct":"fixture","perms":["read","get"],"srvinf":"example</span> // <span>disk &amp; data","dirs":[{"href":"large/","sz":3221225472,"ts":1700000000,"tags":{".files":6}},{"href":"empty/","sz":0,"ts":1700000000},{"href":"unknown/","ts":1700000000}],"files":[{"href":"caf%C3%A9%20file.txt","sz":1024,"ts":1700000000},{"href":"negative.bin","sz":-1},{"href":"bad-size.bin","sz":"123"}]})";
    auto list = parse_listing(data,"/");
    require(list.account == "fixture" && list.permissions.size() == 2, "listing retains account and permissions");
    require(list.server_info == "example // disk & data", "server information markup becomes plain text");
    require(list.header_ansi == "# acct: fixture\n# perms: ['read', 'get']\n# srvinf: example // disk & data\n", "terminal header matches Copyparty metadata format");
    require(list.entries[0].size_known && list.entries[0].directory_size_indexed, "indexed folder size recognized");
    require(list.entries[1].size_known && list.entries[1].size == 0, "zero folder size is known");
    require(!list.entries[2].size_known && !list.entries[4].size_known && !list.entries[5].size_known, "missing negative and invalid sizes are unknown");
    require(terminal_size(0) == "0B" && terminal_size(1023) == "1023B", "terminal byte units");
    require(terminal_size(1024) == "1.0K" && terminal_size(10240) == "10.0K", "terminal KiB decimals");
    require(terminal_size(1024*1024) == "1.0M" && terminal_size(3ull*1024*1024*1024) == "3.0G", "terminal MiB and GiB units");
    require(terminal_size(58853872782ull) == "54.8G" && terminal_size(45590591) == "43.4M", "terminal size values use truncation");
    require(terminal_size(110821) == "108K" && terminal_size(102497) == "100K", "terminal integral-looking sizes omit trailing dot");
    require(size_color_index(1023) == 6 && size_color_index(1024) == 5 && size_color_index(1024*1024) == 1 && size_color_index(1024ull*1024*1024) == 3 && size_color_index(1024ull*1024*1024*1024) == 0, "Copyparty size color thresholds");
    const std::string raw_date="\033[0;7;36m\033[7m2023\033[27m1114\033[0;1m22\033[0;36m13\033[0m20";
    const std::string displayed_date="\033[0;7;36m\033[7m2023\033[27m-11-14\033[0;1m 22\033[0;36m:13\033[0m:20";
    require(terminal_time(1700000000) == "2023-11-14 22:13:20", "terminal timestamps use readable UTC date and time with seconds");
    require(terminal_time_ansi(1700000000) == displayed_date, "timestamp punctuation preserves year highlighting and component colors");
    require(terminal_time(0) == "---------- --:--:--" && terminal_time(-1) == "---------- --:--:--", "unknown and negative timestamps keep a full-width placeholder");
    require(terminal_time(std::numeric_limits<std::int64_t>::max()) == "---------- --:--:--", "out-of-range timestamps cannot produce malformed display dates");
    auto large = terminal_entry_ansi(list.entries[0]);
    require(large.compare(0,displayed_date.size(),displayed_date)==0, "JSON fallback rows display the readable styled timestamp");
    require(large.find("\033[0;33m  3.0G") != std::string::npos && large.find("\033[94mlarge/\033[0m") != std::string::npos, "folder size and name colors match terminal rendering");
    auto hidden = terminal_entry_ansi(list.entries[0],false);
    require(hidden.find("3.0G") == std::string::npos && hidden.find("large/") != std::string::npos, "folder size can be hidden without changing the link name");
    require(terminal_entry_ansi(list.entries[3],false).find("1.0K") != std::string::npos, "folder size preference leaves file sizes visible");
    require(terminal_entry_ansi(list.entries[2]).find("—") != std::string::npos, "unknown size has an explicit placeholder");
    // The actual server protocol remains compact. Do not construct these raw
    // rows from the display helper: that would conceal a protocol regression.
    const std::string raw_large=raw_date+"\033[0;33m  3.0G\033[0m \033[94mlarge/";
    const std::string raw_file=raw_date+"\033[0;35m  1.0K\033[0m café file.txt";
    auto actual = list.header_ansi + raw_large + "\n" + raw_file + "\n\033[0m";
    require(apply_terminal_listing(list,actual) == 2, "genuine terminal rows map to verified JSON names");
    require(!list.entries[0].terminal_line_ansi.empty() && list.entries[0].path == "/large/", "terminal row attachment preserves navigation identity");
    require(list.entries[0].terminal_line_ansi == displayed_date+"\033[0;33m  3.0G\033[0m \033[94mlarge/", "genuine compact row is normalized without changing size or name styles");
    require(list.entries[3].terminal_line_ansi == displayed_date+"\033[0;35m  1.0K\033[0m café file.txt" && list.entries[3].path == "/café file.txt", "genuine UTF-8 file row retains its exact click identity");
    require(terminal_entry_ansi(list.entries[0],false).find("3.0G") == std::string::npos, "hiding sizes also works after raw terminal attachment");
    require(terminal_entry_ansi(list.entries[0],false).compare(0,displayed_date.size(),displayed_date)==0, "hidden folder sizes retain readable UTC timestamps");
    auto hostile = raw_date + "\033[0;35m  1.0K\033[0m \033]8;;https://invalid.example/\033\\café file.txt\033]8;;\033\\\n";
    require(apply_terminal_listing(list,hostile) == 1, "terminal hyperlinks are mapped only by JSON entry identity");
    require(list.entries[3].terminal_line_ansi.find("invalid.example") == std::string::npos && list.entries[3].path == "/café file.txt", "remote terminal URL controls are discarded");
    auto foreign = raw_date + "  1.0K https://invalid.example/\n";
    require(apply_terminal_listing(list,foreign) == 0, "unlisted terminal filenames do not create links");
    require(apply_terminal_listing(list,displayed_date+"  1.0K café file.txt\n") == 0, "display formatting does not broaden compact server-row matching");
    const std::string leap="\033[36m20240229235959\033[0m  1.0K café file.txt\n";
    require(apply_terminal_listing(list,leap) == 1 && list.entries[3].terminal_line_ansi == "\033[36m2024-02-29 23:59:59\033[0m  1.0K café file.txt", "valid leap-day server timestamps preserve their original time and color");
    for(const char* invalid:{"20230229221320","20231314221320","20231131221320","20231114241320","20231114226020","20231114221360","00001114221320","--------------"}) {
        require(apply_terminal_listing(list,std::string(invalid)+"  1.0K café file.txt\n") == 0, "invalid or unknown raw dates cannot become trusted display rows");
    }
    require(list.entries[3].terminal_line_ansi.empty() && terminal_entry_ansi(list.entries[3]).compare(0,displayed_date.size(),displayed_date)==0 && list.entries[3].path == "/café file.txt", "invalid raw dates retain verified JSON date and click-identity fallback");
    auto unknown_date=list.entries[3];unknown_date.modified=0;
    require(terminal_entry_ansi(unknown_date).compare(0,terminal_time_ansi(0).size(),terminal_time_ansi(0))==0, "unknown JSON dates retain styled fixed-width placeholders");
}
void http_fixture(const std::string& origin, const std::filesystem::path& dir) {
    Config c = default_config(); c.server = normalize_server(origin); c.username = "fixture-user"; c.password = "fixture-password";
    TransferControl control;
    auto r = fetch(c, "/", FetchKind::Listing, control);
    require(r.status == 200 && parse_listing(std::string(r.bytes.begin(),r.bytes.end()), "/").entries.size() == 2, "HTTP JSON listing");
    auto directory = parse_listing(std::string(r.bytes.begin(),r.bytes.end()), "/");
    r = fetch(c,"/",FetchKind::Terminal,control);
    const std::string terminal(r.bytes.begin(),r.bytes.end());
    require(r.content_type.find("text/plain") != std::string::npos && terminal.find("\033[") != std::string::npos, "genuine terminal listing requested through ls=v");
    require(apply_terminal_listing(directory,terminal) == 2, "terminal response attaches to JSON directory entries");
    require(terminal.find("\033[27m1114") != std::string::npos && directory.entries[0].terminal_line_ansi.find("\033[27m-11-14") != std::string::npos, "HTTP terminal timestamps remain compact on the wire and become readable only for display");
    r = fetch(c,"/redirect-terminal",FetchKind::Terminal,control);
    require(r.status == 200 && r.final_url.find("?ls=v") != std::string::npos, "same-origin terminal redirect preserves the allowed query");
    r = fetch(c, "/file.txt", FetchKind::Auto, control);
    require(std::string(r.bytes.begin(),r.bytes.end()) == "fixture payload\n", "auto path preserves raw file");
    r = fetch(c, "/redirect", FetchKind::File, control);
    require(std::string(r.bytes.begin(),r.bytes.end()) == "fixture payload\n", "safe same-server redirect");
    require(rejects([&]{fetch(c, "/unauthorized", FetchKind::File, control);}, "401 rejected").find("401") != std::string::npos, "401 surfaced");
    require(rejects([&]{fetch(c, "/forbidden", FetchKind::File, control);}, "403 rejected").find("403") != std::string::npos, "403 surfaced");
    rejects([&]{fetch(c, "/missing", FetchKind::File, control);}, "404 rejected");
    require(rejects([&]{fetch(c, "/foreign", FetchKind::File, control);}, "cross-origin redirect rejected").find("outside the configured server") != std::string::npos, "foreign redirect rejected before connecting");
    rejects([&]{fetch(c, "/loop", FetchKind::File, control);}, "redirect loop rejected");
    rejects([&]{fetch(c, "/query", FetchKind::File, control);}, "redirect arbitrary query rejected");
    rejects([&]{fetch(c, "/huge", FetchKind::File, control, 1024);}, "declared size cap enforced");
    rejects([&]{fetch(c, "/unbounded", FetchKind::File, control, 1024);}, "streamed size cap enforced");
    rejects([&]{fetch(c, "/truncated", FetchKind::File, control);}, "truncated file is not success");
    const auto dest = dir / "download.txt";
    write(dest, "existing content");
    rejects([&]{download(c, "/truncated", dest, control);}, "truncated download rejected");
    require(read(dest) == "existing content", "failed download preserves destination");
    download(c, "/file.txt", dest, control);
    require(read(dest) == "fixture payload\n", "download atomically replaces destination");
    require(control.total.load() == 16 && control.received.load() == 16, "download progress counters");
    const auto start = std::chrono::steady_clock::now();
    std::thread cancel([&]{std::this_thread::sleep_for(std::chrono::milliseconds(250)); control.cancel.store(true);});
    std::string cancellation;
    try { download(c, "/slow", dest, control); } catch(const std::exception& e) { cancellation=e.what(); }
    cancel.join();
    require(cancellation.find("Cancelled") != std::string::npos, "download cancellation surfaced");
    require(std::chrono::steady_clock::now()-start < std::chrono::seconds(3), "cancellation responds promptly");
    require(read(dest) == "fixture payload\n", "cancelled download preserves destination");
    control.cancel.store(false);
    const auto header_start = std::chrono::steady_clock::now();
    std::thread cancel_headers([&]{std::this_thread::sleep_for(std::chrono::milliseconds(250)); control.cancel.store(true);});
    cancellation.clear();
    try { fetch(c, "/slow-headers", FetchKind::File, control); } catch(const std::exception& e) { cancellation=e.what(); }
    cancel_headers.join();
    require(cancellation.find("Cancelled") != std::string::npos, "waiting for headers can be cancelled");
    require(std::chrono::steady_clock::now()-header_start < std::chrono::seconds(3), "header wait cancellation responds promptly");
    for (const auto& item : std::filesystem::directory_iterator(dir)) require(item.path().extension() != ".tmp", "temporary download removed");
}
} // namespace
int main(int argc, char** argv) {
    try {
        bool check_public_defaults = false;
        std::string fixture;
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--public-defaults") check_public_defaults = true;
            else if (arg == "--http-fixture" && i + 1 < argc) fixture = argv[++i];
            else throw std::runtime_error("Unknown or incomplete core-test argument: " + arg);
        }
        TempDirectory temp;
        paths(); configuration(temp.path); listings(); terminal_listings();
        if (check_public_defaults) public_defaults(temp.path);
        if (!fixture.empty()) http_fixture(fixture, temp.path);
        std::cout << "PASS: " << checks << " core checks\n";
        network_cleanup();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL after " << checks << " checks: " << e.what() << '\n';
        return 1;
    }
}
