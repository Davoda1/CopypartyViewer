// Copyparty Viewer 2.1.0 — Copyright (C) 2026 davoda1
// SPDX-License-Identifier: MIT
#include "text_encoding.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <string_view>

namespace cpv {
namespace {
constexpr std::uint32_t replacement = 0xfffd;
enum class Encoding { Unknown, Utf8, Utf16LE, Utf16BE, Utf32LE, Utf32BE, Cp1250, Cp1252 };

// Unicode's Microsoft mapping tables; undefined bytes become U+FFFD.
// https://www.unicode.org/Public/MAPPINGS/VENDORS/MICSFT/WINDOWS/CP1250.TXT
// https://www.unicode.org/Public/MAPPINGS/VENDORS/MICSFT/WINDOWS/CP1252.TXT
constexpr std::array<std::uint32_t,128> cp1250 = {{
    0x20ac,0xfffd,0x201a,0xfffd,0x201e,0x2026,0x2020,0x2021,
    0xfffd,0x2030,0x0160,0x2039,0x015a,0x0164,0x017d,0x0179,
    0xfffd,0x2018,0x2019,0x201c,0x201d,0x2022,0x2013,0x2014,
    0xfffd,0x2122,0x0161,0x203a,0x015b,0x0165,0x017e,0x017a,
    0x00a0,0x02c7,0x02d8,0x0141,0x00a4,0x0104,0x00a6,0x00a7,
    0x00a8,0x00a9,0x015e,0x00ab,0x00ac,0x00ad,0x00ae,0x017b,
    0x00b0,0x00b1,0x02db,0x0142,0x00b4,0x00b5,0x00b6,0x00b7,
    0x00b8,0x0105,0x015f,0x00bb,0x013d,0x02dd,0x013e,0x017c,
    0x0154,0x00c1,0x00c2,0x0102,0x00c4,0x0139,0x0106,0x00c7,
    0x010c,0x00c9,0x0118,0x00cb,0x011a,0x00cd,0x00ce,0x010e,
    0x0110,0x0143,0x0147,0x00d3,0x00d4,0x0150,0x00d6,0x00d7,
    0x0158,0x016e,0x00da,0x0170,0x00dc,0x00dd,0x0162,0x00df,
    0x0155,0x00e1,0x00e2,0x0103,0x00e4,0x013a,0x0107,0x00e7,
    0x010d,0x00e9,0x0119,0x00eb,0x011b,0x00ed,0x00ee,0x010f,
    0x0111,0x0144,0x0148,0x00f3,0x00f4,0x0151,0x00f6,0x00f7,
    0x0159,0x016f,0x00fa,0x0171,0x00fc,0x00fd,0x0163,0x02d9
}};
constexpr std::array<std::uint32_t,128> cp1252 = {{
    0x20ac,0xfffd,0x201a,0x0192,0x201e,0x2026,0x2020,0x2021,
    0x02c6,0x2030,0x0160,0x2039,0x0152,0xfffd,0x017d,0xfffd,
    0xfffd,0x2018,0x2019,0x201c,0x201d,0x2022,0x2013,0x2014,
    0x02dc,0x2122,0x0161,0x203a,0x0153,0xfffd,0x017e,0x0178,
    0x00a0,0x00a1,0x00a2,0x00a3,0x00a4,0x00a5,0x00a6,0x00a7,
    0x00a8,0x00a9,0x00aa,0x00ab,0x00ac,0x00ad,0x00ae,0x00af,
    0x00b0,0x00b1,0x00b2,0x00b3,0x00b4,0x00b5,0x00b6,0x00b7,
    0x00b8,0x00b9,0x00ba,0x00bb,0x00bc,0x00bd,0x00be,0x00bf,
    0x00c0,0x00c1,0x00c2,0x00c3,0x00c4,0x00c5,0x00c6,0x00c7,
    0x00c8,0x00c9,0x00ca,0x00cb,0x00cc,0x00cd,0x00ce,0x00cf,
    0x00d0,0x00d1,0x00d2,0x00d3,0x00d4,0x00d5,0x00d6,0x00d7,
    0x00d8,0x00d9,0x00da,0x00db,0x00dc,0x00dd,0x00de,0x00df,
    0x00e0,0x00e1,0x00e2,0x00e3,0x00e4,0x00e5,0x00e6,0x00e7,
    0x00e8,0x00e9,0x00ea,0x00eb,0x00ec,0x00ed,0x00ee,0x00ef,
    0x00f0,0x00f1,0x00f2,0x00f3,0x00f4,0x00f5,0x00f6,0x00f7,
    0x00f8,0x00f9,0x00fa,0x00fb,0x00fc,0x00fd,0x00fe,0x00ff
}};

bool space(char c) { return c==' ' || c=='\t' || c=='\r' || c=='\n'; }
std::string normalized(std::string_view value) {
    while(!value.empty() && space(value.front())) value.remove_prefix(1);
    while(!value.empty() && space(value.back())) value.remove_suffix(1);
    std::string result;
    for(char c:value) {
        if(c=='-' || c=='_') continue;
        result.push_back(c>='A'&&c<='Z'?char(c-'A'+'a'):c);
    }
    return result;
}
Encoding named(std::string_view value) {
    const auto n=normalized(value);
    if(n=="utf8" || n=="unicode11utf8" || n=="cp65001" || n=="windows65001" || n=="ascii" || n=="usascii") return Encoding::Utf8;
    if(n=="utf16le" || n=="unicode") return Encoding::Utf16LE;
    // Generic MIME UTF-16 without a BOM defaults to big endian (RFC 2781).
    if(n=="utf16be" || n=="utf16") return Encoding::Utf16BE;
    if(n=="utf32le") return Encoding::Utf32LE;
    if(n=="utf32be" || n=="utf32") return Encoding::Utf32BE;
    if(n=="cp1250" || n=="windows1250" || n=="xcp1250" || n=="1250") return Encoding::Cp1250;
    if(n=="cp1252" || n=="windows1252" || n=="xcp1252" || n=="1252") return Encoding::Cp1252;
    return Encoding::Unknown;
}
Encoding declared(const std::string& content_type) {
    std::size_t pos=content_type.find(';');
    while(pos!=std::string::npos && pos<content_type.size()) {
        ++pos; while(pos<content_type.size() && space(content_type[pos])) ++pos;
        const auto start=pos;
        while(pos<content_type.size() && content_type[pos]!='=' && content_type[pos]!=';') ++pos;
        const auto key=normalized(std::string_view(content_type).substr(start,pos-start));
        if(pos==content_type.size()) break;
        if(content_type[pos]==';') continue;
        ++pos; while(pos<content_type.size() && space(content_type[pos])) ++pos;
        std::string value;
        if(pos<content_type.size() && (content_type[pos]=='\"' || content_type[pos]=='\'')) {
            const char quote=content_type[pos++];
            while(pos<content_type.size() && content_type[pos]!=quote) {
                if(content_type[pos]=='\\' && pos+1<content_type.size()) ++pos;
                value.push_back(content_type[pos++]);
            }
            if(pos<content_type.size()) ++pos;
        } else {
            const auto first=pos;
            while(pos<content_type.size() && content_type[pos]!=';') ++pos;
            value=content_type.substr(first,pos-first);
        }
        if(key=="charset") return named(value);
        pos=content_type.find(';',pos);
    }
    return Encoding::Unknown;
}
bool starts(const std::vector<unsigned char>& b,std::initializer_list<unsigned char> signature) {
    return b.size()>=signature.size() && std::equal(signature.begin(),signature.end(),b.begin());
}
Encoding bom(const std::vector<unsigned char>& b) {
    if(starts(b,{0xff,0xfe,0,0})) return Encoding::Utf32LE;
    if(starts(b,{0,0,0xfe,0xff})) return Encoding::Utf32BE;
    if(starts(b,{0xef,0xbb,0xbf})) return Encoding::Utf8;
    if(starts(b,{0xff,0xfe})) return Encoding::Utf16LE;
    if(starts(b,{0xfe,0xff})) return Encoding::Utf16BE;
    return Encoding::Unknown;
}
std::size_t bom_size(const std::vector<unsigned char>& b,Encoding e) {
    if(e==Encoding::Utf8 && starts(b,{0xef,0xbb,0xbf})) return 3;
    if(e==Encoding::Utf16LE && starts(b,{0xff,0xfe})) return 2;
    if(e==Encoding::Utf16BE && starts(b,{0xfe,0xff})) return 2;
    if(e==Encoding::Utf32LE && starts(b,{0xff,0xfe,0,0})) return 4;
    if(e==Encoding::Utf32BE && starts(b,{0,0,0xfe,0xff})) return 4;
    return 0;
}
bool scalar(std::uint32_t c) { return c<=0x10ffff && !(c>=0xd800 && c<=0xdfff); }
void append(std::string& out,std::uint32_t c) {
    if(!scalar(c)) c=replacement;
    if(c<0x80) out.push_back(char(c));
    else if(c<0x800) {out.push_back(char(0xc0|(c>>6)));out.push_back(char(0x80|(c&63)));}
    else if(c<0x10000) {out.push_back(char(0xe0|(c>>12)));out.push_back(char(0x80|((c>>6)&63)));out.push_back(char(0x80|(c&63)));}
    else {out.push_back(char(0xf0|(c>>18)));out.push_back(char(0x80|((c>>12)&63)));out.push_back(char(0x80|((c>>6)&63)));out.push_back(char(0x80|(c&63)));}
}
// Invalid input consumes one maximal valid prefix, always making progress.
bool utf8(const unsigned char* p,std::size_t left,std::uint32_t& c,std::size_t& used,bool& incomplete) {
    used=1; incomplete=false; c=replacement;
    const unsigned first=p[0];
    if(first<0x80) {c=first;return true;}
    const unsigned n=first>=0xc2&&first<=0xdf?2:first>=0xe0&&first<=0xef?3:first>=0xf0&&first<=0xf4?4:0;
    if(!n) return false;
    c=first & (n==2?31:n==3?15:7);
    for(unsigned i=1;i<n;++i) {
        if(i>=left) {incomplete=true;c=replacement;return false;}
        const unsigned next=p[i];
        if((next&0xc0)!=0x80 || (i==1 && ((first==0xe0&&next<0xa0)||(first==0xed&&next>=0xa0)||(first==0xf0&&next<0x90)||(first==0xf4&&next>=0x90)))) {c=replacement;return false;}
        c=(c<<6)|(next&63);++used;
    }
    return true;
}
bool valid_utf8(const std::vector<unsigned char>& b,std::size_t limit,bool truncated) {
    for(std::size_t i=0;i<limit;) {
        std::uint32_t c;std::size_t used;bool incomplete;
        if(!utf8(b.data()+i,limit-i,c,used,incomplete)) return incomplete && truncated;
        i+=used;
    }
    return true;
}
std::uint32_t unit(const std::vector<unsigned char>& b,std::size_t i,unsigned width,bool little) {
    std::uint32_t c=0;
    for(unsigned k=0;k<width;++k) c=(c<<8)|b[i+(little?width-1-k:k)];
    return c;
}
bool ascii_text(std::uint32_t c) {return (c>=32&&c<=126)||c==9||c==10||c==13;}
Encoding aligned_unicode(const std::vector<unsigned char>& b,std::size_t limit) {
    const auto sample=std::min<std::size_t>(limit,4096);
    // At least four units, with repeated zero-byte lanes and mostly ASCII text.
    // Short or non-ASCII-only unlabelled Unicode requires explicit selection.
    for(unsigned width:{4u,2u}) for(bool little:{true,false}) {
        const auto count=sample/width;
        if(count<4) continue;
        std::size_t text=0,aligned=0;
        for(std::size_t i=0;i<count;++i) {
            const auto c=unit(b,i*width,width,little);
            if(ascii_text(c)) ++text;
            if(c<128) ++aligned;
        }
        if(text*100>=count*85 && aligned*100>=count*90)
            return width==4?(little?Encoding::Utf32LE:Encoding::Utf32BE):(little?Encoding::Utf16LE:Encoding::Utf16BE);
    }
    return Encoding::Unknown;
}
const char* label(Encoding e) {
    switch(e) {
    case Encoding::Utf16LE:return "UTF-16LE";case Encoding::Utf16BE:return "UTF-16BE";
    case Encoding::Utf32LE:return "UTF-32LE";case Encoding::Utf32BE:return "UTF-32BE";
    case Encoding::Cp1250:return "CP1250";case Encoding::Cp1252:return "CP1252";
    default:return "UTF-8";
    }
}
} // namespace

DecodedText decode_text(const std::vector<unsigned char>& bytes,const std::string& choice,const std::string& content_type,std::size_t byte_limit) {
    DecodedText result;
    const auto limit=std::min(bytes.size(),byte_limit);
    result.truncated=bytes.size()>limit;
    Encoding encoding=named(choice);
    if(encoding==Encoding::Unknown) {
        encoding=bom(bytes);
        if(encoding==Encoding::Unknown) encoding=declared(content_type);
        if(encoding==Encoding::Unknown) encoding=aligned_unicode(bytes,limit);
        if(encoding==Encoding::Unknown) encoding=valid_utf8(bytes,limit,result.truncated)?Encoding::Utf8:Encoding::Cp1250;
    }
    result.encoding=label(encoding);
    result.text.reserve(limit);
    std::size_t i=bom_size(bytes,encoding);
    while(i<limit) {
        std::uint32_t c=replacement;
        if(encoding==Encoding::Utf8) {
            std::size_t used;bool incomplete;
            utf8(bytes.data()+i,limit-i,c,used,incomplete);i+=used;
        } else if(encoding==Encoding::Utf16LE || encoding==Encoding::Utf16BE) {
            const bool little=encoding==Encoding::Utf16LE;
            if(limit-i<2) {i=limit;}
            else {
                c=unit(bytes,i,2,little);i+=2;
                if(c>=0xd800 && c<=0xdbff) {
                    if(limit-i>=2) {
                        const auto low=unit(bytes,i,2,little);
                        if(low>=0xdc00 && low<=0xdfff) {c=0x10000+((c-0xd800)<<10)+(low-0xdc00);i+=2;}
                        else c=replacement;
                    } else c=replacement;
                }
            }
        } else if(encoding==Encoding::Utf32LE || encoding==Encoding::Utf32BE) {
            if(limit-i<4) i=limit;
            else {c=unit(bytes,i,4,encoding==Encoding::Utf32LE);i+=4;}
        } else {
            const unsigned byte=bytes[i++];
            c=byte<128?byte:(encoding==Encoding::Cp1252?cp1252:cp1250)[byte-128];
        }
        append(result.text,c);
    }
    // Each input byte contributes at most three UTF-8 bytes. Scalar conversion
    // never creates invalid UTF-8, even when the bounded input ends mid-unit.
    return result;
}
} // namespace cpv
