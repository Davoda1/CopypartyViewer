// Copyright (C) 2026 davoda1 — SPDX-License-Identifier: MIT
#include "document.hpp"
#include "../vendor/md4c/md4c.h"
extern "C" {
#include "../vendor/md4c/entity.h"
}
#include <algorithm>
#include <cctype>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace cpv::document {
namespace {
struct ThemeData { Theme theme; Palette light, dark; };
// Editor-inspired palettes with separate light and dark syntax colors.
const std::vector<ThemeData> theme_data = {
    {{"default", "Default"},
     {"#f6f8fa", "#383a42", "#eaf0f6", "#47596e", "#dce1e8", "#a0a1a7", "#a626a4", "#50a14f", "#986801", "#4078f2", "#c18401", "#986801", "#986801", "#4078f2", "#383a42", "#986801", "#50a14f", "#f6f8fa", "#e45649", "#f6f8fa"},
     {"#111a27", "#abb2bf", "#1c293c", "#afc2d7", "#333e4c", "#5c6370", "#c678dd", "#98c379", "#d19a66", "#61aeee", "#e6c07b", "#d19a66", "#d19a66", "#61aeee", "#abb2bf", "#d19a66", "#98c379", "#111a27", "#e06c75", "#111a27"}},
    {{"vscode", "VS Code"},
     {"#ffffff", "#000000", "#f3f3f3", "#444444", "#d4d4d4", "#008000", "#0000ff", "#a31515", "#098658", "#795e26", "#267f99", "#d00000", "#001080", "#707070", "#393a34", "#800000", "#207d36", "#e6ffed", "#b31d28", "#ffeef0"},
     {"#1e1e1e", "#d4d4d4", "#252526", "#cccccc", "#454545", "#6a9955", "#569cd6", "#ce9178", "#b5cea8", "#dcdcaa", "#4ec9b0", "#9cdcfe", "#9cdcfe", "#c586c0", "#d4d4d4", "#d7ba7d", "#b5cea8", "#203622", "#f0a0a0", "#4d2323"}},
    {{"github", "GitHub"},
     {"#ffffff", "#24292f", "#f6f8fa", "#57606a", "#d0d7de", "#57606a", "#cf222e", "#0a3069", "#0550ae", "#8250df", "#953800", "#0550ae", "#953800", "#0550ae", "#24292f", "#116329", "#116329", "#dafbe1", "#82071e", "#ffebe9"},
     {"#0d1117", "#c9d1d9", "#161b22", "#b1bac4", "#30363d", "#8b949e", "#ff7b72", "#a5d6ff", "#79c0ff", "#d2a8ff", "#ffa657", "#79c0ff", "#ffa657", "#79c0ff", "#c9d1d9", "#7ee787", "#aff5b4", "#033a16", "#ffdcd7", "#67060c"}},
    {{"monokai", "Monokai"},
     {"#fbfaf1", "#37352d", "#efeddf", "#625e50", "#d8d4c1", "#706d5c", "#a9004a", "#526c16", "#7045a0", "#526c16", "#197183", "#773c91", "#9b480e", "#5d557d", "#454237", "#a9004a", "#316729", "#e7eed9", "#a3293a", "#f7e3e5"},
     {"#272822", "#f8f8f2", "#34352d", "#d3d0bc", "#555747", "#a7a48e", "#ff5c8d", "#e6db74", "#ae81ff", "#a6e22e", "#66d9ef", "#c49ae4", "#fd971f", "#e6db74", "#f8f8f2", "#a6e22e", "#c5eb89", "#334224", "#ffa8b6", "#522b34"}},
    {{"nord", "Nord"},
     {"#eceff4", "#2e3440", "#e2e7ef", "#4c566a", "#bfc9d8", "#59677f", "#4c6290", "#416f37", "#85518c", "#2e6c84", "#386f77", "#7d4c23", "#8b4350", "#57627f", "#3b4252", "#486b36", "#416f37", "#dce9d8", "#8b4350", "#f0dfe2"},
     {"#2e3440", "#eceff4", "#3b4252", "#d8dee9", "#56627b", "#9aa8bf", "#81a1c1", "#a3be8c", "#c69abd", "#88c0d0", "#8fbcbb", "#dda482", "#d8989f", "#c69abd", "#eceff4", "#ebcb8b", "#c3dba9", "#374538", "#f0afb5", "#50353f"}},
    {{"dracula", "Dracula"},
     {"#faf7ff", "#332d43", "#eee8f7", "#665978", "#d8cbe7", "#6f6786", "#963b84", "#456822", "#6f45a7", "#256d44", "#1d6c82", "#7c510c", "#843a5d", "#664e98", "#463c61", "#843a5d", "#256d44", "#e2f3e8", "#a13164", "#f8e3ef"},
     {"#282a36", "#f8f8f2", "#343746", "#d0cce7", "#575974", "#9c9ec5", "#ff79c6", "#f1fa8c", "#bd93f9", "#50fa7b", "#8be9fd", "#f1fa8c", "#ffb86c", "#bd93f9", "#ff79c6", "#50fa7b", "#9bffc0", "#234d36", "#ffb4d4", "#59293f"}},
    {{"solarized", "Solarized"},
     {"#fdf6e3", "#586e75", "#eee8d5", "#52686f", "#cfc8b4", "#5c727a", "#637200", "#187d76", "#996600", "#2075a0", "#526bb0", "#9d480c", "#a73731", "#7a548b", "#586e75", "#6c7100", "#456c18", "#e5edc8", "#a73731", "#f6e2d7"},
     {"#002b36", "#a1b1b1", "#073642", "#b1bfbf", "#315764", "#839496", "#a6ad35", "#2fb9ab", "#d9a431", "#57aade", "#9095e5", "#ee9250", "#f77a6f", "#cd86aa", "#a1b1b1", "#a6ad35", "#c3cf68", "#28422c", "#ffaaa2", "#573b3b"}},
    {{"codex", "Codex"},
     {"#f5f4ef", "#252922", "#e9ebe2", "#56614f", "#cbd2c2", "#647161", "#2c7165", "#826027", "#835f9d", "#326d91", "#61712c", "#9b4d36", "#445c73", "#745a6f", "#484f43", "#657331", "#386b3e", "#e0eddd", "#a23e3a", "#f1e2dd"},
     {"#151917", "#e3e8dd", "#202821", "#b7c4af", "#39473b", "#98a591", "#85c8b6", "#d9c28a", "#b8a3d5", "#94bfd8", "#b5c684", "#e7a68b", "#a7bbce", "#c2a6bc", "#c3cfbd", "#adc48b", "#b8d5a2", "#283c28", "#edb1a5", "#452d28"}},
    {{"xcode", "Xcode Classic"},
     {"#ffffff", "#262626", "#f1f2f5", "#5f6570", "#d3d7de", "#5d6c79", "#aa0d91", "#c41a16", "#1c00cf", "#326d74", "#5c2699", "#765d20", "#3f6e74", "#643820", "#454555", "#aa0d91", "#226535", "#e5f1e8", "#aa2638", "#f9e5e6"},
     {"#1f1f24", "#dfdfe0", "#29292f", "#a6a6b3", "#414149", "#9193a4", "#fc5fa3", "#fc6a5d", "#d0bf69", "#67b7a4", "#dabafa", "#a9a1f0", "#78c2b3", "#fd8f3f", "#d4d4e0", "#fc5fa3", "#a3d491", "#27372c", "#ffb0b0", "#432a2e"}},
    {{"ayu", "Ayu"},
     {"#fcfcfc", "#5c6166", "#f3f4f5", "#60696f", "#d7dce1", "#657484", "#995000", "#547716", "#8b4ba8", "#8a6307", "#196d81", "#9c4d34", "#385d85", "#92551c", "#785213", "#7b3d70", "#47720d", "#e8f1dc", "#a93832", "#f9e4df"},
     {"#0d1017", "#bfbdb6", "#151a23", "#aab3c1", "#303947", "#8494a6", "#ff8f40", "#aad94c", "#d2a6ff", "#ffb454", "#59c2ff", "#f29e74", "#c7dcf2", "#ffb454", "#f29668", "#d2a6ff", "#c0db75", "#263520", "#ffa08c", "#432921"}},
};

std::string lower(std::string value) {
    for (auto& c : value) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
    return value;
}
bool alpha(unsigned char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
bool digit(unsigned char c) { return c >= '0' && c <= '9'; }
bool word(unsigned char c) { return alpha(c) || digit(c) || c >= 128; }
bool space(unsigned char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
std::string normalize_language(std::string value) {
    value = lower(value);
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "plaintext";
    value.erase(0, first);
    value.resize(value.find_first_of(" \t\r\n") == std::string::npos ? value.size() : value.find_first_of(" \t\r\n"));
    if (!value.empty() && value.front() == '{') value.erase(0, 1);
    if (!value.empty() && value.back() == '}') value.pop_back();
    if (!value.empty() && value.front() == '.') value.erase(0, 1);
    if (value.rfind("language-", 0) == 0) value.erase(0, 9);
    if (value.rfind("lang-", 0) == 0) value.erase(0, 5);
    static const std::unordered_map<std::string, std::string> aliases = {
        {"js","javascript"},{"jsx","javascript"},{"mjs","javascript"},{"cjs","javascript"},
        {"ts","typescript"},{"tsx","typescript"},{"py","python"},{"py3","python"},{"python3","python"},
        {"c++","cpp"},{"cc","cpp"},{"cxx","cpp"},{"hpp","cpp"},{"hxx","cpp"},{"h","cpp"},
        {"cs","csharp"},{"c#","csharp"},{"rs","rust"},{"golang","go"},
        {"sh","bash"},{"shell","bash"},{"zsh","bash"},{"posix","bash"},{"ksh","bash"},{"dash","bash"},
        {"ps","powershell"},{"ps1","powershell"},{"pwsh","powershell"},
        {"yml","yaml"},{"htm","xml"},{"html","xml"},{"svg","xml"},{"xhtml","xml"},
        {"jsonc","json"},{"json5","json"},{"toml","ini"},{"conf","ini"},
        {"text","plaintext"},{"txt","plaintext"},{"plain","plaintext"},{"no-highlight","plaintext"},
        {"nohighlight","plaintext"},{"output","plaintext"},{"log","plaintext"}
    };
    const auto it = aliases.find(value);
    return it == aliases.end() ? value : it->second;
}
bool has_word(std::string_view list, std::string_view token) {
    if (token.empty() || token.size() > 40) return false;
    const std::string needle = " " + std::string(token) + " ";
    return list.find(needle) != std::string_view::npos;
}
void mark(std::string& styles, std::size_t a, std::size_t b, char style) {
    std::fill(styles.begin() + static_cast<std::ptrdiff_t>(a), styles.begin() + static_cast<std::ptrdiff_t>(b), style);
}
std::size_t line_end(const std::string& text, std::size_t at) {
    const auto found = text.find('\n', at);
    return found == std::string::npos ? text.size() : found;
}
std::size_t through(const std::string& text, std::size_t at, std::string_view end) {
    const auto found = text.find(end, at);
    return found == std::string::npos ? text.size() : found + end.size();
}
std::size_t quoted_end(const std::string& text, std::size_t at, bool escapes) {
    const char quote = text[at++];
    while (at < text.size()) {
        if (escapes && text[at] == '\\' && at + 1 < text.size()) at += 2;
        else if (text[at++] == quote) break;
    }
    return at;
}
std::string markup_styles(const std::string& text) {
    std::string styles(text.size(), 'A');
    for (std::size_t i = 0; i < text.size();) {
        if (text.compare(i, 4, "<!--") == 0) {
            const auto end = through(text, i + 4, "-->"); mark(styles, i, end, 'B'); i = end;
        } else if (text[i] == '<' && i + 1 < text.size() &&
                   (alpha(text[i+1]) || text[i+1] == '/' || text[i+1] == '?' || text[i+1] == '!')) {
            styles[i++] = 'K';
            while (i < text.size() && (text[i] == '/' || text[i] == '?' || text[i] == '!')) styles[i++] = 'K';
            const auto name = i;
            while (i < text.size() && (word(text[i]) || text[i] == '-' || text[i] == ':')) ++i;
            mark(styles, name, i, 'C');
            while (i < text.size() && text[i] != '>') {
                if (text[i] == '\'' || text[i] == '"') {
                    const auto end = quoted_end(text, i, false); mark(styles, i, end, 'D'); i = end;
                } else if (word(text[i])) {
                    const auto start = i++;
                    while (i < text.size() && (word(text[i]) || text[i] == '-' || text[i] == ':')) ++i;
                    mark(styles, start, i, 'H');
                } else { if (!space(text[i])) styles[i] = 'K'; ++i; }
            }
            if (i < text.size()) styles[i++] = 'K';
        } else if (text[i] == '&') {
            auto end = i + 1;
            while (end < text.size() && end - i < 50 && text[end] != ';' && text[end] != '&') ++end;
            if (end < text.size() && text[end] == ';' && end - i < 50) { mark(styles, i, end + 1, 'J'); i = end + 1; }
            else ++i;
        } else ++i;
    }
    return styles;
}
std::string escape(std::string_view text) {
    std::string out; out.reserve(text.size());
    for (const char c : text) {
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        case '\'': out += "&#39;"; break;
        case '\0': out += "\xef\xbf\xbd"; break;
        default: out += c;
        }
    }
    return out;
}
void append_utf8(std::string& out, unsigned c) {
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
int hex(unsigned char c) {
    if (digit(c)) return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
std::string decode_entity(std::string_view value) {
    std::string out;
    if (value.size() >= 4 && value[0] == '&' && value[1] == '#' && value.back() == ';') {
        std::size_t i = 2; unsigned cp = 0, base = 10;
        if (value[i] == 'x' || value[i] == 'X') { ++i; base = 16; }
        for (; i + 1 < value.size(); ++i) {
            const int n = hex(value[i]);
            if (n < 0 || static_cast<unsigned>(n) >= base || cp > 0x10ffff) return "\xef\xbf\xbd";
            cp = cp * base + static_cast<unsigned>(n);
        }
        append_utf8(out, cp); return out;
    }
    if (const ENTITY* entity = entity_lookup(value.data(), value.size())) {
        append_utf8(out, entity->codepoints[0]);
        if (entity->codepoints[1]) append_utf8(out, entity->codepoints[1]);
        return out;
    }
    return std::string(value);
}
std::string attribute(const MD_ATTRIBUTE& attr) {
    if (!attr.text || !attr.size) return {};
    if (!attr.substr_types || !attr.substr_offsets) return std::string(attr.text, attr.size);
    std::string out;
    for (unsigned i = 0; attr.substr_offsets[i] < attr.size; ++i) {
        const std::string_view piece(attr.text + attr.substr_offsets[i], attr.substr_offsets[i+1] - attr.substr_offsets[i]);
        if (attr.substr_types[i] == MD_TEXT_ENTITY) out += decode_entity(piece);
        else if (attr.substr_types[i] == MD_TEXT_NULLCHAR) out += "\xef\xbf\xbd";
        else out.append(piece);
    }
    return out;
}
bool safe_target(const std::string& original) {
    if (original.empty() || original.size() > 8192) return false;
    // Check the decoded spelling too, so encoded controls, drive paths and
    // schemes cannot become active after the host resolves a relative path.
    std::string target;
    for (std::size_t i = 0; i < original.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(original[i]);
        if (c == '%' && i + 2 < original.size() && hex(original[i+1]) >= 0 && hex(original[i+2]) >= 0) {
            c = static_cast<unsigned char>(hex(original[i+1]) * 16 + hex(original[i+2])); i += 2;
        }
        if (c < 32 || c == 127 || c == '\\') return false;
        target += static_cast<char>(c);
    }
    if (target.rfind("//", 0) == 0) return false;
    const auto colon = target.find(':');
    const auto separator = target.find_first_of("/?#");
    if (colon != std::string::npos && (separator == std::string::npos || colon < separator)) {
        const auto scheme = lower(target.substr(0, colon));
        if ((scheme != "https" && scheme != "http") || target.compare(colon, 3, "://") != 0) return false;
        const auto start = colon + 3, end = target.find_first_of("/?#", start);
        const auto authority = target.substr(start, end == std::string::npos ? end : end - start);
        if (authority.empty() || authority.find_first_of(" @\t\r\n") != std::string::npos) return false;
        // An encoded scheme is unnecessary and has ambiguous handling in URI
        // launchers. Only an explicit HTTP(S) spelling is accepted.
        const auto actual_scheme = lower(original.substr(0, original.find(':')));
        return actual_scheme == "http" || actual_scheme == "https";
    }
    return true;
}
std::string color(const std::string& value, const char* fallback) {
    if (value.size() != 7 || value[0] != '#') return fallback;
    for (std::size_t i = 1; i < value.size(); ++i) if (hex(value[i]) < 0) return fallback;
    return value;
}
const std::string& style_color(const Palette& p, char style) {
    switch (style) {
    case 'B': return p.comment; case 'C': return p.keyword; case 'D': return p.string;
    case 'E': return p.number; case 'F': return p.title; case 'G': return p.type;
    case 'H': return p.attribute; case 'I': return p.variable; case 'J': return p.meta;
    case 'K': return p.op; default: return p.foreground;
    }
}
std::string code_html(const std::string& text, const std::string& language, const Palette& p) {
    const auto styles = syntax_styles(text, language);
    std::string html = "<TABLE WIDTH=\"100%\" BORDER=\"0\" BGCOLOR=\"" + p.background + "\"><TR><TD BGCOLOR=\"" + p.header + "\"><FONT FACE=\"courier\" COLOR=\"" + p.label + "\">";
    html += escape(language.empty() ? "Text" : language);
    html += "</FONT></TD></TR><TR><TD BGCOLOR=\"" + p.background + "\"><PRE><FONT FACE=\"courier\" COLOR=\"" + p.foreground + "\">";
    for (std::size_t i = 0; i < text.size();) {
        std::size_t end = i + 1;
        while (end < text.size() && styles[end] == styles[i]) ++end;
        if (styles[i] != 'A') html += "<FONT COLOR=\"" + style_color(p, styles[i]) + "\">";
        html += escape(std::string_view(text).substr(i, end - i));
        if (styles[i] != 'A') html += "</FONT>";
        i = end;
    }
    html += "</FONT></PRE></TD></TR></TABLE>\n";
    return html;
}

struct Renderer {
    RenderedDocument result;
    Palette colors;
    std::string foreground, background, link_color, code, language, heading_text;
    bool in_code = false, in_heading = false;
    std::size_t heading_position = 0;
    unsigned link_depth = 0;
    std::vector<std::string> span_ends;
    std::unordered_map<std::string, unsigned> heading_counts;
    std::string open_link(const std::string& target) {
        if (link_depth || !safe_target(target)) return {};
        const auto index = result.links.size(); result.links.push_back(target); ++link_depth;
        result.html += "<A HREF=\"cpv:" + std::to_string(index) + "\">";
        return "</A>";
    }
    std::string heading_target() {
        std::string slug;
        for (unsigned char c : heading_text) {
            if (alpha(c) || digit(c) || c >= 128 || c == '-') slug += c >= 'A' && c <= 'Z' ? static_cast<char>(c + 32) : static_cast<char>(c);
            else if (space(c) && !slug.empty() && slug.back() != '-') slug += '-';
        }
        while (!slug.empty() && slug.back() == '-') slug.pop_back();
        if (slug.empty()) slug = "section";
        const auto base = slug;
        auto& suffix = heading_counts[base];
        // Start duplicate suffixes at -1. Consult every previously generated
        // slug, including natural suffix headings, so none can shadow another.
        while (result.anchors.find(slug) != result.anchors.end())
            slug = base + "-" + std::to_string(++suffix);
        const auto native_target = "cpv-h" + std::to_string(result.anchors.size());
        result.anchors.emplace(std::move(slug), native_target);
        return native_target;
    }
};
int enter_block(MD_BLOCKTYPE type, void* detail, void* data) {
    auto& r = *static_cast<Renderer*>(data); auto& out = r.result.html;
    switch (type) {
    case MD_BLOCK_DOC: out += "<HTML><BODY TEXT=\"" + r.foreground + "\" BGCOLOR=\"" + r.background + "\" LINK=\"" + r.link_color + "\">\n"; break;
    case MD_BLOCK_QUOTE: out += "<DL><DD>"; break;
    case MD_BLOCK_UL: out += "<UL>\n"; break;
    case MD_BLOCK_OL: out += "<OL START=\"" + std::to_string(static_cast<MD_BLOCK_OL_DETAIL*>(detail)->start) + "\">\n"; break;
    case MD_BLOCK_LI: {
        out += "<LI>";
        const auto* li = static_cast<MD_BLOCK_LI_DETAIL*>(detail);
        if (li->is_task) out += li->task_mark == ' ' ? "[ ] " : "[x] ";
        break;
    }
    case MD_BLOCK_HR: out += "<HR>\n"; break;
    case MD_BLOCK_H:
        r.in_heading = true; r.heading_text.clear(); r.heading_position = out.size();
        out += "<H" + std::to_string(static_cast<MD_BLOCK_H_DETAIL*>(detail)->level) + ">"; break;
    case MD_BLOCK_CODE:
        r.in_code = true; r.code.clear(); r.language = attribute(static_cast<MD_BLOCK_CODE_DETAIL*>(detail)->lang); break;
    case MD_BLOCK_HTML: out += "<PRE>"; break;
    case MD_BLOCK_P: out += "<P>"; break;
    case MD_BLOCK_TABLE: out += "<TABLE BORDER=\"1\" WIDTH=\"100%\">\n"; break;
    case MD_BLOCK_TR: out += "<TR>"; break;
    case MD_BLOCK_TH: case MD_BLOCK_TD: {
        const auto align = static_cast<MD_BLOCK_TD_DETAIL*>(detail)->align;
        out += type == MD_BLOCK_TH ? "<TH BGCOLOR=\"" + r.colors.header + "\"" : "<TD";
        out += " ALIGN=\"" + std::string(align == MD_ALIGN_RIGHT ? "RIGHT" : align == MD_ALIGN_CENTER ? "CENTER" : "LEFT") + "\">";
        break;
    }
    default: break;
    }
    return 0;
}
int leave_block(MD_BLOCKTYPE type, void* detail, void* data) {
    auto& r = *static_cast<Renderer*>(data); auto& out = r.result.html;
    switch (type) {
    case MD_BLOCK_DOC: out += "</BODY></HTML>"; break;
    case MD_BLOCK_QUOTE: out += "</DD></DL>\n"; break;
    case MD_BLOCK_UL: out += "</UL>\n"; break;
    case MD_BLOCK_OL: out += "</OL>\n"; break;
    case MD_BLOCK_LI: out += "</LI>\n"; break;
    case MD_BLOCK_H:
        out += "</H" + std::to_string(static_cast<MD_BLOCK_H_DETAIL*>(detail)->level) + ">\n";
        out.insert(r.heading_position, "<A NAME=\"" + r.heading_target() + "\"></A>"); r.in_heading = false; break;
    case MD_BLOCK_CODE: out += code_html(r.code, r.language, r.colors); r.in_code = false; break;
    case MD_BLOCK_HTML: out += "</PRE>\n"; break;
    case MD_BLOCK_P: out += "</P>\n"; break;
    case MD_BLOCK_TABLE: out += "</TABLE>\n"; break;
    case MD_BLOCK_TR: out += "</TR>\n"; break;
    case MD_BLOCK_TH: out += "</TH>"; break;
    case MD_BLOCK_TD: out += "</TD>"; break;
    default: break;
    }
    return 0;
}
int enter_span(MD_SPANTYPE type, void* detail, void* data) {
    auto& r = *static_cast<Renderer*>(data); auto& out = r.result.html;
    std::string close;
    switch (type) {
    case MD_SPAN_EM: out += "<I>"; close = "</I>"; break;
    case MD_SPAN_STRONG: out += "<B>"; close = "</B>"; break;
    case MD_SPAN_CODE: out += "<FONT FACE=\"courier\">"; close = "</FONT>"; break;
    case MD_SPAN_A: close = r.open_link(attribute(static_cast<MD_SPAN_A_DETAIL*>(detail)->href)); break;
    case MD_SPAN_IMG:
        close = r.open_link(attribute(static_cast<MD_SPAN_IMG_DETAIL*>(detail)->src));
        out += "[Image: "; close = "]" + close; break;
    default: break;
    }
    r.span_ends.push_back(std::move(close));
    return 0;
}
int leave_span(MD_SPANTYPE, void*, void* data) {
    auto& r = *static_cast<Renderer*>(data);
    if (!r.span_ends.empty()) {
        const auto& close = r.span_ends.back(); r.result.html += close;
        if (close.find("</A>") != std::string::npos) --r.link_depth;
        r.span_ends.pop_back();
    }
    return 0;
}
int text_callback(MD_TEXTTYPE type, const MD_CHAR* text, MD_SIZE size, void* data) {
    auto& r = *static_cast<Renderer*>(data);
    std::string value;
    if (type == MD_TEXT_NULLCHAR) value = "\xef\xbf\xbd";
    else if (type == MD_TEXT_ENTITY) value = decode_entity(std::string_view(text, size));
    else if (type == MD_TEXT_BR || type == MD_TEXT_SOFTBR) value = "\n";
    else value.assign(text, size);
    if (r.in_heading) r.heading_text += value;
    if (r.in_code) r.code += value;
    else if (type == MD_TEXT_BR) r.result.html += "<BR>\n";
    else r.result.html += escape(value);
    return 0;
}
} // namespace

const std::vector<Theme>& themes() {
    static const auto result = [] { std::vector<Theme> t; for (const auto& row : theme_data) t.push_back(row.theme); return t; }();
    return result;
}
Palette palette(const std::string& theme_id, bool dark) {
    for (const auto& row : theme_data) if (theme_id == row.theme.id) return dark ? row.dark : row.light;
    return dark ? theme_data.front().dark : theme_data.front().light;
}
std::string syntax_styles(const std::string& text, const std::string& requested_language) {
    const auto language = normalize_language(requested_language);
    if (language == "xml") return markup_styles(text);
    std::string styles(text.size(), 'A');
    if (!has_word(" c cpp javascript typescript python bash powershell json yaml css sql rust go java csharp php ini ", language)) return styles;
    const bool py = language == "python", sh = language == "bash", ps = language == "powershell";
    const bool yaml = language == "yaml", ini = language == "ini", css = language == "css", sql = language == "sql";
    const bool json = language == "json", rust = language == "rust";
    const bool hash_comments = py || sh || ps || yaml || ini || language == "php";
    const bool slash_comments = !py && !sh && !ps && !yaml && !ini && !sql;
    constexpr std::string_view keywords = " abstract and as assert async await auto base begin bool boolean break by byte case catch char class const constexpr continue crate def default defer del delete do double dynamic elif else elseif end enum except explicit export extends extern false final finally float fn for foreach friend from func function global goto if implements import in inline instanceof int interface internal into is lambda let lock long match mod module mut namespace native new nil none not null nullptr object of on operator or order out override package param pass private protected public raise readonly ref register repeat require return sbyte sealed select self short signed sizeof static strictfp string struct super switch sync synchronized template then this throw throws trait transient true try type typedef typename typeof union unless unsafe unsigned until use using var virtual void volatile when where while with yield ";
    constexpr std::string_view types = " Array Boolean Buffer Date Error File HashMap Int32 Int64 List Map Number Object Option Result Set String Task Tuple UInt32 UInt64 Vec bool boolean byte char decimal double float int int8 int16 int32 int64 long object short size_t string uint uint8 uint16 uint32 uint64 usize void wchar_t ";
    constexpr std::string_view sql_keywords = " add all alter and any as asc backup between by case check column constraint create database default delete desc distinct drop else end exists foreign from full group having in index inner insert into is join key left like limit not null offset on or order outer primary procedure references right select set table top truncate union unique update values view when where with ";
    constexpr std::string_view shell_keywords = " alias bg break case cd command continue declare do done echo elif else esac eval exec exit export false fi for function getopts if in let local printf read readonly return select set shift source test then time trap true type typeset ulimit umask unalias unset until wait while ";
    for (std::size_t i = 0; i < text.size();) {
        const auto start = i; const char c = text[i];
        if (ps && text.compare(i, 2, "<#") == 0) {
            i = through(text, i + 2, "#>"); mark(styles, start, i, 'B');
        } else if ((slash_comments || sql) && text.compare(i, 2, "/*") == 0) {
            i = through(text, i + 2, "*/"); mark(styles, start, i, 'B');
        } else if ((slash_comments && !css && text.compare(i, 2, "//") == 0) ||
                   (sql && text.compare(i, 2, "--") == 0) ||
                   (hash_comments && c == '#' && (!yaml || i == 0 || space(text[i-1])) &&
                    (!sh || i == 0 || space(text[i-1]) || std::string_view(";|&()").find(text[i-1]) != std::string_view::npos)) || (ini && c == ';')) {
            i = line_end(text, i); mark(styles, start, i, 'B');
        } else if ((language == "c" || language == "cpp") && c == '#') {
            i = line_end(text, i); mark(styles, start, i, 'J');
        } else if (ps && c == '@' && i + 1 < text.size() && (text[i+1] == '\'' || text[i+1] == '"') &&
                   (i + 2 == text.size() || text[i+2] == '\r' || text[i+2] == '\n')) {
            const std::string end = std::string("\n") + text[i+1] + '@';
            i = through(text, i + 2, end); mark(styles, start, i, 'D');
        } else if (rust && c == '\'' && i + 1 < text.size() && alpha(text[i+1]) &&
                   !(i + 2 < text.size() && text[i+2] == '\'')) {
            ++i; while (i < text.size() && word(text[i])) ++i; mark(styles, start, i, 'I');
        } else if (c == '\'' || c == '"' || (c == '`' && (language == "javascript" || language == "typescript" || language == "go"))) {
            if (py && text.compare(i, 3, std::string(3, c)) == 0) i = through(text, i + 3, std::string(3, c));
            else i = quoted_end(text, i, !(sh && c == '\'') && !(language == "go" && c == '`'));
            char style = 'D';
            if (json) { auto next = i; while (next < text.size() && space(text[next])) ++next; if (next < text.size() && text[next] == ':') style = 'H'; }
            mark(styles, start, i, style);
        } else if (c == '$' && (sh || ps || language == "php")) {
            ++i;
            if (i < text.size() && text[i] == '{') { ++i; while (i < text.size() && text[i] != '}' && text[i] != '\n') ++i; if (i < text.size() && text[i] == '}') ++i; }
            else { while (i < text.size() && (word(text[i]) || text[i] == ':' || text[i] == '?' || text[i] == '@')) ++i; }
            mark(styles, start, i, 'I');
        } else if (py && c == '@') {
            ++i; while (i < text.size() && (word(text[i]) || text[i] == '.')) ++i; mark(styles, start, i, 'J');
        } else if (digit(c) || (c == '.' && i + 1 < text.size() && digit(text[i+1]))) {
            ++i;
            while (i < text.size()) {
                const char n = text[i];
                if (digit(n) || n == '.' || n == '_' || (n >= 'a' && n <= 'f') || (n >= 'A' && n <= 'F') || n == 'x' || n == 'X' || n == 'o' || n == 'O' || n == 'u' || n == 'U' || n == 'l' || n == 'L') ++i;
                else if ((n == '+' || n == '-') && (text[i-1] == 'e' || text[i-1] == 'E')) ++i;
                else break;
            }
            mark(styles, start, i, 'E');
        } else if (alpha(c) || static_cast<unsigned char>(c) >= 128) {
            ++i; while (i < text.size() && (word(text[i]) || ((css || yaml) && text[i] == '-'))) ++i;
            const auto token = lower(text.substr(start, i - start));
            auto next = i; while (next < text.size() && (text[next] == ' ' || text[next] == '\t')) ++next;
            char style = 'A';
            if ((json || yaml) && (token == "true" || token == "false" || token == "null")) style = 'E';
            else if ((yaml || css) && next < text.size() && text[next] == ':') style = 'H';
            else if (ini && next < text.size() && text[next] == '=') style = 'H';
            else if (has_word(sql ? sql_keywords : sh ? shell_keywords : keywords, token)) style = 'C';
            else if (has_word(types, text.substr(start, i - start))) style = 'G';
            else if (next < text.size() && text[next] == '(' && !yaml && !ini && !json) style = 'F';
            mark(styles, start, i, style);
        } else if (ini && c == '[') {
            i = through(text, i + 1, "]"); mark(styles, start, i, 'J');
        } else {
            if (std::string_view("+-*/%=<>!&|^~?:.,;(){}[]").find(c) != std::string_view::npos) styles[i] = 'K';
            ++i;
        }
    }
    return styles;
}
std::string language_for_filename(const std::string& filename) {
    auto name = lower(filename.substr(filename.find_last_of("/\\") == std::string::npos ? 0 : filename.find_last_of("/\\") + 1));
    if (name == "dockerfile" || name == "makefile" || name == ".bashrc" || name == ".zshrc" || name == ".profile" || name == ".env") return "bash";
    const auto dot = name.find_last_of('.');
    if (dot == std::string::npos) return "plaintext";
    const auto ext = name.substr(dot + 1);
    static const std::unordered_map<std::string, std::string> extensions = {
        {"c","c"},{"h","cpp"},{"cc","cpp"},{"cpp","cpp"},{"cxx","cpp"},{"hpp","cpp"},{"hxx","cpp"},
        {"js","javascript"},{"jsx","javascript"},{"mjs","javascript"},{"cjs","javascript"},{"ts","typescript"},{"tsx","typescript"},
        {"py","python"},{"pyw","python"},{"sh","bash"},{"bash","bash"},{"zsh","bash"},{"ps1","powershell"},{"psm1","powershell"},{"psd1","powershell"},
        {"json","json"},{"jsonc","json"},{"json5","json"},{"yaml","yaml"},{"yml","yaml"},
        {"html","xml"},{"htm","xml"},{"xml","xml"},{"xhtml","xml"},{"svg","xml"},{"css","css"},{"scss","css"},
        {"sql","sql"},{"rs","rust"},{"go","go"},{"java","java"},{"cs","csharp"},{"php","php"},
        {"ini","ini"},{"toml","ini"},{"cfg","ini"},{"conf","ini"},{"desktop","ini"}
    };
    const auto it = extensions.find(ext);
    return it == extensions.end() ? "plaintext" : it->second;
}
bool markdown_supported(const std::string& filename, const std::string& content_type) {
    const auto mime = lower(content_type.substr(0, content_type.find(';')));
    if (mime == "text/markdown" || mime == "text/x-markdown") return true;
    const auto name = lower(filename);
    const auto dot = name.find_last_of('.');
    return dot != std::string::npos && has_word(" md markdown mdown mkd mkdn mdtext mdtxt ", name.substr(dot + 1));
}
RenderedDocument render_document(const std::string& text, const std::string& theme_id,
    bool dark, const std::string& foreground_hex, const std::string& background_hex, const std::string& link_hex) {
    if (text.size() > std::numeric_limits<MD_SIZE>::max()) throw std::length_error("Markdown input is too large.");
    Renderer r;
    r.colors = palette(theme_id, dark);
    r.foreground = color(foreground_hex, dark ? "#eeeeee" : "#202020");
    r.background = color(background_hex, dark ? "#202020" : "#ffffff");
    r.link_color = color(link_hex, dark ? "#80bfff" : "#165ea8");
    MD_PARSER parser{};
    parser.flags = MD_FLAG_NOHTML | MD_FLAG_TABLES | MD_FLAG_TASKLISTS | MD_FLAG_PERMISSIVEURLAUTOLINKS;
    parser.enter_block = enter_block; parser.leave_block = leave_block;
    parser.enter_span = enter_span; parser.leave_span = leave_span; parser.text = text_callback;
    if (md_parse(text.data(), static_cast<MD_SIZE>(text.size()), &parser, &r) != 0) throw std::runtime_error("Markdown could not be rendered.");
    return std::move(r.result);
}
std::string render_markdown(const std::string& text, const std::string& theme_id,
    bool dark, const std::string& foreground_hex, const std::string& background_hex, const std::string& link_hex) {
    return render_document(text, theme_id, dark, foreground_hex, background_hex, link_hex).html;
}
} // namespace cpv::document
