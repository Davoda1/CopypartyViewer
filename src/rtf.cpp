// Copyright (C) 2026 davoda1 — SPDX-License-Identifier: MIT
#include "rtf.hpp"
#include <algorithm>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace cpv::document {
namespace {
constexpr std::size_t max_input = 25u * 1024u * 1024u, max_output = 8u * 1024u * 1024u;
constexpr std::size_t max_depth = 256, max_colors = 4096;
bool letter(unsigned char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
bool digit(unsigned char c) { return c >= '0' && c <= '9'; }
bool white(unsigned char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
int hex(unsigned char c) {
    if (digit(c)) return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
std::string lower(std::string value) {
    for (auto& c : value) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
    return value;
}
std::string valid_color(const std::string& color, const char* fallback) {
    if (color.size() != 7 || color[0] != '#') return fallback;
    for (std::size_t i = 1; i < color.size(); ++i) if (hex(color[i]) < 0) return fallback;
    return color;
}
void utf8(std::string& out, unsigned c) {
    if (c == 0 || c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff)) c = 0xfffd;
    if (c < 0x80) out += static_cast<char>(c);
    else if (c < 0x800) { out += static_cast<char>(0xc0 | (c >> 6)); out += static_cast<char>(0x80 | (c & 63)); }
    else if (c < 0x10000) {
        out += static_cast<char>(0xe0 | (c >> 12)); out += static_cast<char>(0x80 | ((c >> 6) & 63)); out += static_cast<char>(0x80 | (c & 63));
    } else {
        out += static_cast<char>(0xf0 | (c >> 18)); out += static_cast<char>(0x80 | ((c >> 12) & 63));
        out += static_cast<char>(0x80 | ((c >> 6) & 63)); out += static_cast<char>(0x80 | (c & 63));
    }
}
constexpr unsigned cp1252[32] = {
    0x20ac,0xfffd,0x201a,0x0192,0x201e,0x2026,0x2020,0x2021,
    0x02c6,0x2030,0x0160,0x2039,0x0152,0xfffd,0x017d,0xfffd,
    0xfffd,0x2018,0x2019,0x201c,0x201d,0x2022,0x2013,0x2014,
    0x02dc,0x2122,0x0161,0x203a,0x0153,0xfffd,0x017e,0x0178
};
constexpr unsigned cp1250[128] = {
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
};
enum class Destination { body, skip, colors };
enum class Align { left, center, right };
struct State {
    bool bold = false, italic = false, underline = false, hidden = false;
    int half_points = 24, color = 0, unicode_fallback = 1, codepage = 1252;
    Align align = Align::left;
    Destination destination = Destination::body;
    int red = -1, green = -1, blue = -1;
};
struct Format {
    bool bold, italic, underline;
    int size;
    std::string color;
    bool operator==(const Format& other) const {
        return bold == other.bold && italic == other.italic && underline == other.underline && size == other.size && color == other.color;
    }
};
class Parser {
    const std::string& source;
    std::size_t at = 0;
    State state;
    std::vector<State> stack;
    std::vector<std::string> colors;
    std::string foreground, background, link, html, paragraph, bytes;
    std::optional<Format> active;
    Align paragraph_align = Align::left;
    bool header_seen = false, root_closed = false, paragraph_start = true, previous_space = false;
    int skip_fallback = 0;
    unsigned high_surrogate = 0;
    State surrogate_state;

    [[noreturn]] void fail(const char* message) const { throw std::runtime_error(message); }
    void bound() const {
        if (html.size() + paragraph.size() > max_output) fail("RTF formatted output is too large. Use Source.");
    }
    static int html_size(int half_points) {
        if (half_points <= 16) return 1;
        if (half_points <= 20) return 2;
        if (half_points <= 26) return 3;
        if (half_points <= 32) return 4;
        if (half_points <= 40) return 5;
        if (half_points <= 56) return 6;
        return 7;
    }
    void close_format() {
        if (!active) return;
        if (active->underline) paragraph += "</U>";
        if (active->italic) paragraph += "</I>";
        if (active->bold) paragraph += "</B>";
        paragraph += "</FONT>";
        active.reset();
    }
    void format(const State& current) {
        const std::string color = current.color >= 0 && static_cast<std::size_t>(current.color) < colors.size() && !colors[current.color].empty() ? colors[current.color] : foreground;
        Format requested{current.bold, current.italic, current.underline, html_size(current.half_points), color};
        if (active && *active == requested) return;
        close_format(); active = requested;
        paragraph += "<FONT FACE=\"sans\" SIZE=\"" + std::to_string(requested.size) + "\" COLOR=\"" + color + "\">";
        if (requested.bold) paragraph += "<B>";
        if (requested.italic) paragraph += "<I>";
        if (requested.underline) paragraph += "<U>";
    }
    void text(std::string_view value, const State& current) {
        if (current.destination != Destination::body || current.hidden || value.empty()) return;
        if (paragraph.empty()) paragraph_align = current.align;
        format(current);
        for (const char c : value) {
            if (c == ' ') {
                paragraph += paragraph_start || previous_space ? "&nbsp;" : " ";
                previous_space = true;
            } else {
                previous_space = false;
                switch (c) {
                case '&': paragraph += "&amp;"; break;
                case '<': paragraph += "&lt;"; break;
                case '>': paragraph += "&gt;"; break;
                case '"': paragraph += "&quot;"; break;
                case '\'': paragraph += "&#39;"; break;
                default: paragraph += c;
                }
            }
            paragraph_start = false;
        }
        bound();
    }
    void codepoint(unsigned cp, const State& current) {
        std::string value; utf8(value, cp); text(value, current);
    }
    void finish_surrogate() {
        if (high_surrogate) { codepoint(0xfffd, surrogate_state); high_surrogate = 0; }
    }
    void unicode_unit(unsigned unit) {
        if (state.hidden) return;
        if (high_surrogate) {
            if (unit >= 0xdc00 && unit <= 0xdfff) {
                const unsigned cp = 0x10000 + ((high_surrogate - 0xd800) << 10) + unit - 0xdc00;
                high_surrogate = 0; codepoint(cp, surrogate_state); return;
            }
            finish_surrogate();
        }
        if (unit >= 0xd800 && unit <= 0xdbff) { high_surrogate = unit; surrogate_state = state; }
        else codepoint(unit, state);
    }
    void flush_bytes() {
        if (bytes.empty()) return;
        finish_surrogate();
        if (state.codepage == 65001) {
            for (std::size_t i = 0; i < bytes.size();) {
                const auto c = static_cast<unsigned char>(bytes[i++]);
                unsigned cp = c, count = 0, minimum = 0;
                if (c < 0x80) { if (!c) fail("Invalid NUL in RTF text."); }
                else if (c >= 0xc2 && c <= 0xdf) { cp = c & 31; count = 1; minimum = 0x80; }
                else if (c >= 0xe0 && c <= 0xef) { cp = c & 15; count = 2; minimum = 0x800; }
                else if (c >= 0xf0 && c <= 0xf4) { cp = c & 7; count = 3; minimum = 0x10000; }
                else fail("Invalid UTF-8 in RTF text. Use Source.");
                if (count > bytes.size() - i) fail("Incomplete UTF-8 in RTF text. Use Source.");
                for (unsigned j = 0; j < count; ++j) {
                    const auto next = static_cast<unsigned char>(bytes[i++]);
                    if ((next & 0xc0) != 0x80) fail("Invalid UTF-8 in RTF text. Use Source.");
                    cp = (cp << 6) | (next & 63);
                }
                if ((count && cp < minimum) || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) fail("Invalid UTF-8 in RTF text. Use Source.");
                codepoint(cp, state);
            }
        } else {
            std::string decoded; decoded.reserve(bytes.size());
            for (const unsigned char c : bytes) {
                if (!c) fail("Invalid NUL in RTF text.");
                unsigned cp = c;
                if (state.codepage == 1250 && c >= 128) cp = cp1250[c - 128];
                else if (c >= 128 && c < 160) cp = cp1252[c - 128];
                utf8(decoded, cp);
            }
            text(decoded, state);
        }
        bytes.clear();
    }
    void byte(unsigned char c) {
        if (skip_fallback > 0) { --skip_fallback; return; }
        if (state.destination == Destination::colors) {
            if (c == ';') {
                if (colors.size() >= max_colors) fail("RTF color table is too large.");
                if (state.red < 0 && state.green < 0 && state.blue < 0) colors.emplace_back();
                else {
                    constexpr char digits[] = "0123456789abcdef";
                    std::string color = "#";
                    for (int channel : {state.red,state.green,state.blue}) { channel = std::max(channel,0); color += digits[channel >> 4]; color += digits[channel & 15]; }
                    colors.push_back(std::move(color));
                }
                state.red = state.green = state.blue = -1;
            }
        } else if (state.destination == Destination::body && !state.hidden) {
            if (c < 32 && c != '\t') fail("Invalid control byte in RTF text.");
            if (c == '\t') { flush_bytes(); finish_surrogate(); text("    ",state); }
            else bytes += static_cast<char>(c);
        }
    }
    void paragraph_break(bool force) {
        flush_bytes(); finish_surrogate(); close_format();
        if (!paragraph.empty() || force) {
            const char* align = paragraph_align == Align::center ? "CENTER" : paragraph_align == Align::right ? "RIGHT" : "LEFT";
            html += "<P ALIGN=\"" + std::string(align) + "\">" + (paragraph.empty() ? "&nbsp;" : paragraph) + "</P>\n";
            paragraph.clear(); bound();
        }
        paragraph_start = true; previous_space = false; paragraph_align = state.align;
    }
    static bool ignored_destination(std::string_view word) {
        constexpr std::string_view ignored = " fonttbl stylesheet info pict object objdata header headerl headerr headerf footer footerl footerr footerf listtable listoverridetable filetbl revtbl rsidtbl xmlnstbl datastore themedata colorschememapping latentstyles generator annotation footnote endnote fldinst nonshppict shppict shp shpinst shprslt htmltag mhtmltag docvar userprops private pnseclvl background bookmark bkmkstart bkmkend ";
        return ignored.find(" " + std::string(word) + " ") != std::string_view::npos;
    }
    void set_codepage(int page) {
        if (page == 0) page = 1252;
        if (page != 1252 && page != 1250 && page != 65001)
            throw std::runtime_error("RTF code page " + std::to_string(page) + " is not supported. Use Source.");
        state.codepage = page;
    }
    void control() {
        if (++at >= source.size()) fail("Incomplete RTF escape.");
        const unsigned char first = static_cast<unsigned char>(source[at]);
        if (!letter(first)) {
            ++at;
            if (first == '\'') {
                if (source.size() - at < 2 || hex(source[at]) < 0 || hex(source[at+1]) < 0) fail("Invalid RTF hexadecimal escape.");
                const auto value = static_cast<unsigned char>(hex(source[at]) * 16 + hex(source[at+1])); at += 2; byte(value); return;
            }
            if (skip_fallback > 0) { --skip_fallback; return; }
            if (first == '{' || first == '}' || first == '\\') { byte(first); return; }
            flush_bytes();
            if (first == '*') { state.destination = Destination::skip; return; }
            if (state.destination != Destination::body || state.hidden) return;
            finish_surrogate();
            if (first == '~') codepoint(0xa0,state);
            else if (first == '_') codepoint(0x2011,state);
            else if (first == '-') codepoint(0xad,state);
            return;
        }
        const auto begin = at;
        while (at < source.size() && letter(source[at])) {
            if (at - begin >= 32) fail("RTF control word is too long.");
            ++at;
        }
        const std::string_view word(source.data() + begin, at - begin);
        bool negative = false, has_value = false;
        if (at < source.size() && source[at] == '-') { negative = true; ++at; }
        std::uint64_t magnitude = 0;
        while (at < source.size() && digit(source[at])) {
            has_value = true; magnitude = magnitude * 10 + static_cast<unsigned>(source[at++] - '0');
            if (magnitude > (negative ? 2147483648ULL : 2147483647ULL)) fail("RTF numeric parameter is out of range.");
        }
        if (negative && !has_value) fail("Invalid RTF numeric parameter.");
        const int value = static_cast<int>(negative ? -static_cast<std::int64_t>(magnitude) : static_cast<std::int64_t>(magnitude));
        if (at < source.size() && source[at] == ' ') ++at;
        // Binary data is structural even in ignored destinations or Unicode
        // fallback text. Braces and backslashes inside it are never parsed.
        if (word == "bin") {
            if (!has_value || value < 0 || static_cast<std::size_t>(value) > source.size() - at) fail("Invalid or truncated RTF binary data.");
            flush_bytes(); at += static_cast<std::size_t>(value);
            if (skip_fallback > 0) --skip_fallback;
            return;
        }
        if (skip_fallback > 0) { --skip_fallback; return; }
        flush_bytes();
        if (word == "rtf") {
            if (header_seen || stack.size() != 1 || !has_value || value != 1) fail("Unsupported or invalid RTF header.");
            header_seen = true; return;
        }
        if (state.destination == Destination::skip) return;
        if (ignored_destination(word)) { state.destination = Destination::skip; return; }
        if (word == "colortbl") { state.destination = Destination::colors; state.red = state.green = state.blue = -1; return; }
        if (state.destination == Destination::colors) {
            if (word == "red" || word == "green" || word == "blue") {
                if (!has_value || value < 0 || value > 255) fail("Invalid RTF color component.");
                if (word == "red") state.red = value; else if (word == "green") state.green = value; else state.blue = value;
            }
            return;
        }
        if (word == "ansi") set_codepage(1252);
        else if (word == "ansicpg" || word == "cpg") { if (!has_value) fail("Missing RTF code page."); set_codepage(value); }
        else if (word == "mac" || word == "pc" || word == "pca") fail("This RTF character set is not supported. Use Source.");
        else if (word == "uc") { if (!has_value || value < 0 || value > 32767) fail("Invalid RTF Unicode fallback count."); state.unicode_fallback = value; }
        else if (word == "u") {
            if (!has_value || value < -32768 || value > 65535) fail("Invalid RTF Unicode escape.");
            unicode_unit(static_cast<unsigned>(value) & 0xffffU); skip_fallback = state.unicode_fallback;
        }
        else if (word == "b") state.bold = !has_value || value != 0;
        else if (word == "i") state.italic = !has_value || value != 0;
        else if (word == "ul" || word == "uld" || word == "uldb") state.underline = !has_value || value != 0;
        else if (word == "ulnone") state.underline = false;
        else if (word == "v") state.hidden = !has_value || value != 0;
        else if (word == "plain") { state.bold = state.italic = state.underline = state.hidden = false; state.half_points = 24; state.color = 0; }
        else if (word == "fs") { if (!has_value || value <= 0) fail("Invalid RTF font size."); state.half_points = std::clamp(value,12,144); }
        else if (word == "cf") { if (!has_value || value < 0 || value >= static_cast<int>(max_colors)) fail("Invalid RTF color index."); state.color = value; }
        else if (word == "pard" || word == "ql" || word == "qc" || word == "qr" || word == "qj") {
            state.align = word == "qc" ? Align::center : word == "qr" ? Align::right : Align::left;
            paragraph_align = state.align;
        }
        else if (!state.hidden && (word == "par" || word == "page" || word == "sect" || word == "row")) paragraph_break(true);
        else if (!state.hidden && word == "line") { finish_surrogate(); close_format(); paragraph += "<BR>\n"; paragraph_start = true; previous_space = false; }
        else if (!state.hidden && (word == "tab" || word == "cell")) { finish_surrogate(); text("    ",state); }
        else if (!state.hidden) {
            unsigned symbol = 0;
            if (word == "emdash") symbol = 0x2014; else if (word == "endash") symbol = 0x2013;
            else if (word == "bullet") symbol = 0x2022; else if (word == "lquote") symbol = 0x2018;
            else if (word == "rquote") symbol = 0x2019; else if (word == "ldblquote") symbol = 0x201c;
            else if (word == "rdblquote") symbol = 0x201d; else if (word == "enspace") symbol = 0x2002;
            else if (word == "emspace") symbol = 0x2003; else if (word == "qmspace") symbol = 0x2005;
            if (symbol) { finish_surrogate(); codepoint(symbol,state); }
        }
    }
public:
    Parser(const std::string& raw, const std::string& fg, const std::string& bg, const std::string& link_color)
        : source(raw), foreground(valid_color(fg,"#202020")), background(valid_color(bg,"#ffffff")), link(valid_color(link_color,"#165ea8")) {}
    RenderedDocument run() {
        if (source.size() > max_input) fail("RTF is too large for formatted preview. Use Source.");
        if (source.compare(0,3,"\xef\xbb\xbf") == 0) at = 3;
        while (at < source.size() && white(source[at])) ++at;
        if (source.compare(at,6,"{\\rtf1") != 0) fail("This file does not have a valid RTF header.");
        html = "<HTML><BODY TEXT=\"" + foreground + "\" BGCOLOR=\"" + background + "\" LINK=\"" + link + "\">\n";
        while (at < source.size()) {
            const unsigned char c = static_cast<unsigned char>(source[at]);
            if (root_closed) { if (!white(c)) fail("Unexpected data after the RTF document."); ++at; continue; }
            if (c == '{') {
                flush_bytes(); skip_fallback = 0;
                if (stack.size() >= max_depth) fail("RTF group nesting is too deep.");
                stack.push_back(state); ++at;
            } else if (c == '}') {
                if (stack.empty()) fail("Unexpected RTF closing brace.");
                flush_bytes(); skip_fallback = 0; state = stack.back(); stack.pop_back(); ++at;
                if (stack.empty()) root_closed = true;
            } else if (c == '\\') control();
            else {
                ++at;
                if (c != '\r' && c != '\n') byte(c);
            }
        }
        if (!header_seen || !root_closed || !stack.empty()) fail("The RTF document has unclosed groups.");
        paragraph_break(false); html += "</BODY></HTML>"; bound();
        RenderedDocument result; result.html = std::move(html); return result;
    }
};
} // namespace

bool rtf_supported(const std::string& filename, const std::string& content_type) {
    auto mime = lower(content_type.substr(0,content_type.find(';')));
    while (!mime.empty() && white(mime.back())) mime.pop_back();
    if (mime == "application/rtf" || mime == "text/rtf" || mime == "application/x-rtf") return true;
    const auto name = lower(filename);
    return name.size() >= 4 && name.compare(name.size()-4,4,".rtf") == 0;
}
RenderedDocument render_rtf(const std::string& raw, const std::string& foreground_hex,
    const std::string& background_hex, const std::string& link_hex) {
    return Parser(raw,foreground_hex,background_hex,link_hex).run();
}
} // namespace cpv::document
