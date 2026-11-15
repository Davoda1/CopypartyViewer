// Copyright (C) 2026 davoda1 — SPDX-License-Identifier: MIT
#include "document.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace cpv::document;
namespace {
int checks = 0;
void require(bool value, const char* message) {
    ++checks; if (!value) throw std::runtime_error(message);
}
RenderedDocument render(const std::string& text) {
    return render_document(text, "vscode", false, "#202020", "#ffffff", "#165ea8");
}
void contains(const std::string& text, const std::string& value, const char* message) {
    require(text.find(value) != std::string::npos, message);
}
std::string strip_font_tags(std::string value) {
    std::string result;
    for (std::size_t i = 0; i < value.size();) {
        if (value[i] == '<') { const auto end = value.find('>', i); if (end == std::string::npos) break; i = end + 1; }
        else result += value[i++];
    }
    for (const auto& pair : std::vector<std::pair<std::string, std::string>>{{"&lt;","<"},{"&gt;",">"},{"&quot;","\""},{"&#39;","'"},{"&amp;","&"}}) {
        std::size_t start = 0;
        while ((start = result.find(pair.first, start)) != std::string::npos) {
            result.replace(start, pair.first.size(), pair.second); start += pair.second.size();
        }
    }
    return result;
}
void palettes() {
    const std::vector<std::string> ids = {"default","vscode","github","monokai","nord","dracula","solarized","codex","xcode","ayu"};
    const std::vector<std::string> names = {"Default","VS Code","GitHub","Monokai","Nord","Dracula","Solarized","Codex","Xcode Classic","Ayu"};
    require(themes().size() == 10, "exact ten-theme catalog");
    for (std::size_t i = 0; i < ids.size(); ++i) {
        require(themes()[i].id == ids[i] && themes()[i].label == names[i], "catalog labels and ordering");
        const auto light = palette(ids[i], false), dark = palette(ids[i], true);
        require(light.background != dark.background, "every palette has light and dark variants");
        for (const auto& p : {light, dark}) {
            for (const auto* v : {&p.background,&p.foreground,&p.header,&p.label,&p.border,&p.comment,&p.keyword,&p.string,&p.number,&p.title,&p.type,&p.attribute,&p.variable,&p.meta,&p.op,&p.selector,&p.addition,&p.addition_background,&p.deletion,&p.deletion_background}) {
                require(v->size() == 7 && (*v)[0] == '#' && v->find_first_not_of("0123456789abcdefABCDEF",1) == std::string::npos, "all palette tokens are six-digit hex colors");
            }
        }
    }
    require(palette("vscode", true).keyword == "#569cd6", "VS Code dark keyword palette");
    require(palette("ayu", false).background == "#fcfcfc", "Ayu light background palette");
    require(palette("unknown", true).foreground == palette("default", true).foreground, "unknown theme falls back to Default");
}
void highlighting() {
    const std::string json = "{\"café\": \"世界\", \"n\": 42, \"ok\": true}";
    const auto js = syntax_styles(json, "JSON");
    require(js.size() == json.size(), "JSON style bytes match UTF-8 source bytes");
    require(js[json.find("café")] == 'H' && js[json.find("世界")] == 'D', "JSON keys and string values differ");
    require(js[json.find("42")] == 'E' && js[json.find("true")] == 'E', "JSON numbers and literals");
    const auto unicode = json.find("世界");
    require(js.substr(unicode, std::string("世界").size()) == std::string(std::string("世界").size(), 'D'), "UTF-8 multibyte characters retain a uniform style");
    const std::string cpp = "// comment\nint main() { return \"<script>&\"; }";
    const auto cs = syntax_styles(cpp, "{.C++}");
    require(cs.substr(0,10) == std::string(10,'B'), "C++ line comment");
    require(cs[cpp.find("return")] == 'C' && cs[cpp.find("main")] == 'F', "C++ keyword and function");
    require(cs[cpp.find("<script>")] == 'D', "code string is colored as source");
    require(syntax_styles(cpp, "not-a-language") == std::string(cpp.size(),'A'), "unknown language stays plain text");
    require(syntax_styles(cpp, "text") == std::string(cpp.size(),'A'), "explicit plain text stays plain");
    require(syntax_styles("", "cpp").empty(), "empty source");
    require(syntax_styles("x='''line\n世界'''", "language-python")[6] == 'D', "Python multiline string");
    require(syntax_styles("value=foo#bar\n# comment", "sh")[9] != 'B', "shell hash within a word is not a comment");
    const auto xml = syntax_styles("<a href=\"x\"><!-- note --></a>", "html");
    require(xml[1] == 'C' && xml[3] == 'H' && xml[8] == 'D' && xml[12] == 'B', "markup tag, attribute, string and comment");
    const std::vector<std::string> languages = {"c","cpp","javascript","typescript","python","bash","powershell","json","yaml","css","sql","rust","go","java","csharp","php","ini"};
    for (const auto& language : languages) {
        const auto styles = syntax_styles("\"Hello 世界\"", language);
        require(styles == std::string(std::string("\"Hello 世界\"").size(),'D'), "common language string coloring preserves every source byte");
    }
    const std::string adversarial(262144, '&');
    require(syntax_styles(adversarial, "html").size() == adversarial.size(), "markup entity scan is bounded for an unterminated sequence");
}
void markdown() {
    const auto doc = render("# Heading\n\nSetext\n------\n\n**bold** and *italic* and `code`.\n\n- one\n  - nested\n\n3. ordered\n\n> quoted\n\nA | B\n--- | ---:\n1 | 2\n\n- [x] done\n- [ ] todo\n");
    contains(doc.html,"<H1>Heading</H1>","ATX heading rendered");
    contains(doc.html,"<H2>Setext</H2>","setext heading rendered");
    contains(doc.html,"<B>bold</B>","strong emphasis rendered");
    contains(doc.html,"<I>italic</I>","emphasis rendered");
    contains(doc.html,"<FONT FACE=\"courier\">code</FONT>","inline code rendered");
    contains(doc.html,"<UL>","unordered list rendered");
    contains(doc.html,"<OL START=\"3\">","ordered list start preserved");
    contains(doc.html,"<DL><DD>","blockquote rendered with supported indented tags");
    contains(doc.html,"<TABLE BORDER=\"1\"","Markdown table rendered");
    contains(doc.html,"ALIGN=\"RIGHT\"","table alignment retained");
    contains(doc.html,"[x] done","completed task rendered");
    contains(doc.html,"[ ] todo","unchecked task rendered");
    const std::string original = "  if (x < 3) {\n    print(\"café & <tag>\");\n  }\n";
    const auto code = render("```cpp\n" + original + "```\n").html;
    const auto start = code.find("<PRE>") + 5, end = code.find("</PRE>",start);
    require(strip_font_tags(code.substr(start,end-start)) == original, "fenced code keeps whitespace and source text after presentation markup is removed");
    const auto tabs = render("```text\n\tindented\n```\n").html;
    contains(tabs, ">    indented\n</FONT>", "CommonMark leading tabs are rendered at four-column stops");
    contains(code,"BGCOLOR=\"#ffffff\"","code box background from selected theme");
    contains(code,"COLOR=\"#0000ff\"","code keyword uses selected theme");
    const auto entities = render("&copy; &#x1f600; &NotEqualTilde; &amp;\n").html;
    contains(entities,"©","named entity decoded");
    contains(entities,"😀","numeric entity decoded to UTF-8");
    contains(entities,"&amp;","ampersand entity remains safe text");
    const auto headers = render("# Title\n\n# Title\n\n[Jump](#title-1)\n");
    contains(headers.html,"NAME=\"cpv-h0\"","short native heading anchor generated");
    contains(headers.html,"NAME=\"cpv-h1\"","duplicate has a distinct native target");
    require(headers.anchors.at("title") == "cpv-h0" && headers.anchors.at("title-1") == "cpv-h1", "duplicate heading suffix starts at one");
    require(headers.links == std::vector<std::string>{"#title-1"}, "fragment target kept in inert registry");

    const std::string long_heading = "a heading with a very long name that exceeds the native target limit by a substantial margin";
    std::string long_slug = long_heading;
    std::replace(long_slug.begin(),long_slug.end(),' ','-');
    const std::string unicode_heading = "árvíztűrő tükörfúrógép 世界 hosszú címsor";
    std::string unicode_slug = unicode_heading;
    std::replace(unicode_slug.begin(),unicode_slug.end(),' ','-');
    const auto long_headers = render("# " + long_heading + "\n\n# " + unicode_heading + "\n\n[Long](#" + long_slug + ") [UTF8](#" + unicode_slug + ")\n");
    require(long_headers.anchors.at(long_slug) == "cpv-h0", "full long fragment maps to a short native target without truncation");
    require(long_headers.anchors.at(unicode_slug) == "cpv-h1", "full UTF-8 heading slug is preserved without cutting codepoints");
    require(long_headers.links == std::vector<std::string>{"#" + long_slug,"#" + unicode_slug}, "long and UTF-8 fragment links remain intact");
    for (const auto& item : long_headers.anchors) {
        require(item.second.size() < 32, "native target fits the FLTK name buffer");
        contains(long_headers.html,"NAME=\"" + item.second + "\"","mapped native target exists in rendered HTML");
    }
    const auto collisions = render("# Foo\n\n# Foo\n\n# Foo-1\n\n# Foo\n\n# Foo-2\n");
    require(collisions.anchors.size() == 5, "duplicate and natural suffix headings never overwrite each other");
    require(collisions.anchors.at("foo") == "cpv-h0" && collisions.anchors.at("foo-1") == "cpv-h1" && collisions.anchors.at("foo-1-1") == "cpv-h2" && collisions.anchors.at("foo-2") == "cpv-h3" && collisions.anchors.at("foo-2-1") == "cpv-h4", "duplicate suffix collisions follow document order");
    const auto natural_first = render("# Foo\n\n# Foo-1\n\n# Foo\n");
    require(natural_first.anchors.at("foo-2") == "cpv-h2", "duplicate skips a previously reserved natural suffix");
}
void safety_and_links() {
    const auto raw = render("<script>alert(1)</script>\n\n<img src=\"file:///secret\">\n\n<iframe src=\"https://example.test\"></iframe>\n");
    require(raw.html.find("<script") == std::string::npos && raw.html.find("<img") == std::string::npos && raw.html.find("<iframe") == std::string::npos, "raw active HTML is inert");
    contains(raw.html,"&lt;script&gt;","raw HTML is displayed as text");
    const auto hostile = render("[a](javascript:alert(1)) [b](jav&#97;script:alert(1)) [c](javascript&colon;alert(1)) [d](data:text/html,foo) [e](file:///etc/passwd) [f](//evil.test/) [g](%2f%2fevil.test/) [h](C:%5csecret) [i](https://user:password@example.test/)\n");
    require(hostile.links.empty(), "dangerous schemes, encoded schemes, UNC paths and URL credentials do not become links");
    require(hostile.html.find("HREF=") == std::string::npos, "rejected targets have no active href");
    const std::string long_url = "https://example.test/" + std::string(400,'a') + "?a=1&b=2#tail";
    const auto safe = render("[Long](" + long_url + ") [Relative](../folder/readme.md) ![Picture](image.png) [Email](mailto:a@example.test)\n");
    require(safe.links == std::vector<std::string>{long_url,"../folder/readme.md","image.png"}, "long HTTP and relative link targets are preserved without FLTK truncation");
    contains(safe.html,"HREF=\"cpv:0\"","inert indexed href emitted");
    require(safe.html.find("HREF=\"https:") == std::string::npos, "FLTK never sees raw browser-launch hrefs");
    require(safe.html.find("<IMG") == std::string::npos && safe.html.find("<img") == std::string::npos, "images never trigger implicit resource loads");
    contains(safe.html,"[Image: Picture]","image alt text is retained");
    const auto colors = render_document("text", "vscode", false, "\"><IMG SRC=x>", "bad", "#123456");
    require(colors.html.find("<IMG") == std::string::npos, "host color parameters are restricted to hex");
    require(markdown_supported("README.MD") && markdown_supported("notes.markdown") && markdown_supported("unknown","text/markdown; charset=utf-8"), "Markdown extension and MIME detection");
    require(!markdown_supported("source.cpp") && !markdown_supported("notes.md.exe"), "non-Markdown filenames rejected");
    require(language_for_filename("/folder/file.PS1") == "powershell" && language_for_filename("source.ts") == "typescript" && language_for_filename("page.html") == "xml", "source extension language selection");
    require(language_for_filename("unknown.bin") == "plaintext", "unknown extension plain fallback");
}
}
int main() {
    try { palettes(); highlighting(); markdown(); safety_and_links(); }
    catch (const std::exception& error) { std::cerr << "Document tests FAILED: " << error.what() << '\n'; return 1; }
    std::cout << "Document tests passed: " << checks << " checks\n";
    return 0;
}
