// Copyright (C) 2026 davoda1 — SPDX-License-Identifier: MIT
#include "rtf.hpp"
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
using namespace cpv::document;
namespace {
int checks = 0;
void require(bool value, const char* message) { ++checks; if (!value) throw std::runtime_error(message); }
void contains(const std::string& text, const std::string& value, const char* message) { require(text.find(value) != std::string::npos,message); }
RenderedDocument render(const std::string& raw) { return render_rtf(raw,"#202020","#ffffff","#165ea8"); }
std::string plain(const std::string& html) {
    std::string out;
    for (std::size_t i = html.find('>') + 1; i < html.size();) {
        if (html[i] == '<') { const auto end = html.find('>',i); if (end == std::string::npos) break; i = end + 1; }
        else out += html[i++];
    }
    for (const auto& pair : std::vector<std::pair<std::string,std::string>>{{"&lt;","<"},{"&gt;",">"},{"&quot;","\""},{"&#39;","'"},{"&nbsp;"," "},{"&amp;","&"}}) {
        std::size_t start = 0;
        while ((start = out.find(pair.first,start)) != std::string::npos) { out.replace(start,pair.first.size(),pair.second); start += pair.second.size(); }
    }
    return out;
}
void rejects(const std::string& raw, const std::string& reason) {
    ++checks;
    try { (void)render(raw); } catch (const std::runtime_error& error) {
        if (std::string(error.what()).find(reason) != std::string::npos) return;
        throw std::runtime_error("Wrong rejection: " + std::string(error.what()));
    }
    throw std::runtime_error("Malformed RTF was accepted: " + reason);
}
void formatting() {
    const auto doc = render(R"({\rtf1\ansi plain {\b bold {\i both} bold} normal\par\qc centered\par\qr right\par\ql left\line next\tab column})");
    contains(doc.html,"<B>bold ","bold text rendered");
    contains(doc.html,"<B><I>both","nested italic and bold rendered");
    contains(doc.html,"</I></B>","nested styles close correctly");
    contains(doc.html,"<P ALIGN=\"CENTER\">","center paragraph alignment");
    contains(doc.html,"<P ALIGN=\"RIGHT\">","right paragraph alignment");
    contains(doc.html,"<P ALIGN=\"LEFT\">","left paragraph alignment");
    contains(doc.html,"<BR>","line break rendered");
    contains(plain(doc.html),"next    column","tab renders four spaces");
    const auto restored = render(R"({\rtf1 {\b bold}plain})").html;
    contains(restored,"<B>bold</B></FONT><FONT FACE=\"sans\" SIZE=\"3\" COLOR=\"#202020\">plain","group close restores normal style");
    const auto styles = render(R"({\rtf1\ul under\ulnone plain\fs48 large\fs16 small\b\i\plain reset})").html;
    contains(styles,"<U>under</U>","underline and underline reset");
    contains(styles,"SIZE=\"6\"","larger font size");
    contains(styles,"SIZE=\"1\"","smaller font size");
    contains(styles,"SIZE=\"3\" COLOR=\"#202020\">reset","plain resets character formatting");
    const auto colors = render(R"({\rtf1{\colortbl;\red255\green0\blue0;\red0\green0\blue255;}\cf1 Red {\cf2 Blue} Red\cf0 Default})").html;
    contains(colors,"COLOR=\"#ff0000\">Red","color table red");
    contains(colors,"COLOR=\"#0000ff\">Blue","color table blue");
    contains(colors,"COLOR=\"#ff0000\"> Red","color restores on group close");
    contains(colors,"COLOR=\"#202020\">Default","automatic color uses host foreground");
    contains(render(R"({\rtf1\fs32767 Huge})").html,"SIZE=\"7\"","native font size remains bounded");
    require(doc.links.empty() && doc.anchors.empty(),"basic RTF does not expose active resources or links");
}
void encodings() {
    contains(plain(render(R"({\rtf1\ansi\ansicpg1252 Caf\'e9 \'80 \'93quote\'94})").html),"Café € “quote”","CP1252 hex bytes decode correctly");
    std::string raw = "{\\rtf1\\ansi "; raw += static_cast<char>(0xe9); raw += '}';
    contains(plain(render(raw).html),"é","unescaped CP1252 bytes are preserved until decoded");
    const auto hu = render(R"({\rtf1\ansi\ansicpg1250 \'e1rv\'edzt\'fbr\'f5 t\'fck\'f6rf\'far\'f3g\'e9p})");
    contains(plain(hu.html),"árvíztűrő tükörfúrógép","Hungarian CP1250 text decodes correctly");
    const std::string unicode_text = "árvíztűrő 世界 😀";
    contains(plain(render("{\\rtf1\\ansi\\ansicpg65001 " + unicode_text + "}").html),unicode_text,"UTF-8 RTF bytes decode correctly");
    contains(plain(render(R"({\rtf1\ansicpg65001 \'c3\'a9})").html),"é","adjacent UTF-8 hex byte escapes combine");
    contains(plain(render(R"({\rtf1\uc1\u945? \u-10179?\u-8704?})").html),"α 😀","signed Unicode escapes and surrogate pair");
    contains(plain(render(R"({\rtf1\uc2\u233ab{\uc0\u945}X\u246??})").html),"éαXö","Unicode fallback count restores with group scope");
    contains(plain(render(R"({\rtf1\uc1\u233\'e9\u123\{})").html),"é{","hex and escaped-symbol Unicode fallbacks count as one byte each");
    contains(plain(render(R"({\rtf1\uc0\u-10179 X\u-8704})").html),"�X�","unpaired surrogates render replacement characters");
    contains(plain(render(R"({\rtf1\uc1{\u-10179?}{\u-8704?}})").html),"😀","surrogate pair survives harmless group boundaries");
    const std::string fallback = "{\\rtf1\\uc1\\u945\\bin4 " + std::string{'{','}','}','\\'} + "\\u946?}";
    contains(plain(render(fallback).html),"αβ","binary Unicode fallback is one token and ignores structural bytes");
    const std::string lf = "{\\rtf1\n\\ansi first\n second\\par\nthird\\line fourth\n}";
    std::string crlf;
    for (char c : lf) { if (c == '\n') crlf += '\r'; crlf += c; }
    require(render(lf).html == render(crlf).html,"CRLF and LF source framing render identically");
    contains(plain(render("\xef\xbb\xbf{\\rtf1 text}").html),"text","optional UTF-8 BOM before RTF header");
    contains(plain(render(R"({\rtf1\ansi \{brace\} \\ \~ \_ \emdash \endash \bullet})").html),R"({brace} \)","escaped braces and backslash");
    contains(plain(render(R"({\rtf1\emdash \endash \bullet})").html),"—–•","typographic control words");
}
void skipped_content() {
    const std::string binary{'{','\\','\0','}','}','x'};
    const auto bin = render("{\\rtf1 before{\\object{\\*\\objdata\\bin6 " + binary + "}}after}");
    contains(plain(bin.html),"beforeafter","ignored binary objects skip exact original bytes including NUL and braces");
    const auto ignored = render(R"({\rtf1{\fonttbl{\f0 Arial;}}{\stylesheet secret-style;}{\info secret-info}{\pict deadbeef}{\*\unknown secret-unknown}{\header secret-header}Visible{\object{\result secret-result}}{\v hidden} End})");
    const auto text = plain(ignored.html);
    contains(text,"Visible End","ordinary text survives ignored destinations");
    require(text.find("secret") == std::string::npos && text.find("Arial") == std::string::npos && text.find("hidden") == std::string::npos,"metadata, images, objects and hidden text never leak into body");
    const auto field = render(R"({\rtf1{\field{\*\fldinst HYPERLINK "file:///etc/passwd"}{\fldrslt\b Visible label}}})");
    contains(plain(field.html),"Visible label","field result text remains visible");
    require(field.html.find("file:") == std::string::npos && field.html.find("HREF") == std::string::npos && field.links.empty(),"field instruction never creates an active link");
    const auto html = render(R"({\rtf1 <script>alert(1)</script> <img src="file:///secret"> & unsafe})").html;
    require(html.find("<script") == std::string::npos && html.find("<img") == std::string::npos,"literal HTML cannot become active markup");
    contains(html,"&lt;script&gt;","literal HTML displays as escaped text");
    contains(html,"&amp; unsafe","literal ampersand remains escaped");
    const auto host = render_rtf(R"({\rtf1 Safe})","\"><IMG SRC=x>","invalid","#123456").html;
    require(host.find("<IMG") == std::string::npos,"host color arguments cannot inject markup");
    contains(plain(render(R"({\rtf1{\*\unknown\ansicpg932\bin2 {}}ok})").html),"ok","ignored destinations remain skipped through encoding declarations and binary data");
}
void malformed() {
    rejects("plain text","valid RTF header");
    rejects("{\\rtf1 unfinished","unclosed groups");
    rejects("{\\rtf1 x}}","Unexpected data");
    rejects("{\\rtf10 x}","header");
    rejects("{\\rtf1\\fs9999999999999999 x}","out of range");
    rejects("{\\rtf1\\" + std::string(33,'a') + " x}","too long");
    rejects("{\\rtf1" + std::string(256,'{') + "x" + std::string(256,'}') + "}","too deep");
    rejects(R"({\rtf1\bin-1 x})","binary data");
    rejects(R"({\rtf1\bin99 short})","binary data");
    rejects(R"({\rtf1\'xz})","hexadecimal escape");
    rejects(R"({\rtf1\uc-1 text})","fallback count");
    rejects(R"({\rtf1\ansicpg932 text})","code page 932");
    rejects(R"({\rtf1\ansicpg65001\'c0\'af})","UTF-8");
    rejects(R"({\rtf1\ansicpg65001\'e2\'82})","UTF-8");
    rejects(R"({\rtf1\ansicpg65001\'ed\'a0\'80})","UTF-8");
    rejects(R"({\rtf1{\colortbl;\red256;}text})","color component");
    rejects("{\\rtf1 " + std::string(1,'\0') + "}","control byte");
    rejects("{\\rtf1 " + std::string(512 * 1024,'a') + "}","too large");
    require(rtf_supported("Document.RTF") && rtf_supported("file","application/rtf; charset=windows-1252") && rtf_supported("file","text/rtf"),"RTF filename and MIME recognition");
    require(!rtf_supported("document.rtf.exe") && !rtf_supported("file","text/plain"),"unrelated formats are not RTF");
}
}
int main() {
    try { formatting(); encodings(); skipped_content(); malformed(); }
    catch (const std::exception& error) { std::cerr << "RTF tests FAILED: " << error.what() << '\n'; return 1; }
    std::cout << "RTF tests passed: " << checks << " checks\n";
    return 0;
}
