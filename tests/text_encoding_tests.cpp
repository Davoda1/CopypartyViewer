// Copyparty Viewer 2.1.0 — Copyright (C) 2026 davoda1
// SPDX-License-Identifier: MIT
#include "text_encoding.hpp"
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
int checks=0;
void require(bool condition,const char* name) {
    ++checks;if(!condition)throw std::runtime_error(name);
}
std::vector<unsigned char> bytes(const std::string& text) {return {text.begin(),text.end()};}
std::vector<unsigned char> wide_ascii(const std::string& text,unsigned width,bool little) {
    std::vector<unsigned char> out;
    for(unsigned char c:text)for(unsigned i=0;i<width;++i)out.push_back(i==(little?0:width-1)?c:0);
    return out;
}
void decoded(const std::vector<unsigned char>& raw,const std::string& choice,
             const std::string& expected,const std::string& label,const char* name,
             const std::string& mime="") {
    auto d=cpv::decode_text(raw,choice,mime);
    require(d.text==expected,name);require(d.encoding==label,"encoding label");
    require(!d.truncated,"small input is not truncated");
}
}
int main() {
    try {
        const std::string replacement=u8"\ufffd";
        decoded({},"auto","","UTF-8","empty input");
        decoded(bytes("Plain ASCII\r\nUnix\nClassic\rEnd"),"auto","Plain ASCII\r\nUnix\nClassic\rEnd","UTF-8","newlines are preserved for the preparer");
        decoded(bytes(u8"Árvíztűrő tükörfúrógép 😀 日本語"),"auto",u8"Árvíztűrő tükörfúrógép 😀 日本語","UTF-8","valid UTF-8");
        decoded({0xef,0xbb,0xbf,'A'},"auto","A","UTF-8","UTF-8 BOM");
        decoded({0xef,0xbb,0xbf,'A'},"auto","A","UTF-8","BOM overrides declared legacy charset","text/plain; charset=windows-1252");
        decoded({0xef,0xbb,0xbf,'A',0xef,0xbb,0xbf},"utf8",u8"A\ufeff","UTF-8","only the first BOM is removed");
        decoded({0xe2,0x82},"utf8",replacement,"UTF-8","incomplete UTF-8 is one replacement");
        decoded({0xe2,0x82,'A'},"utf8",replacement+"A","UTF-8","broken UTF-8 preserves following ASCII");
        decoded({0xc0,0xaf},"utf8",replacement+replacement,"UTF-8","overlong UTF-8 rejected");
        decoded({0xed,0xa0,0x80},"utf8",replacement+replacement+replacement,"UTF-8","UTF-8 surrogate rejected");
        decoded({0xf4,0x90,0x80,0x80},"utf8",replacement+replacement+replacement+replacement,"UTF-8","out-of-range UTF-8 rejected");
        decoded({'A',0,'B'},"utf8",std::string("A\0B",3),"UTF-8","NUL retained for binary rejection");

        const auto hungarian=bytes("\xc1rv\xedzt\xfbr\xf5 t\xfck\xf6rf\xfar\xf3g\xe9p");
        decoded(hungarian,"cp1250",u8"Árvíztűrő tükörfúrógép","CP1250","Hungarian Windows text");
        decoded(hungarian,"auto",u8"Árvíztűrő tükörfúrógép","CP1250","unlabelled legacy fallback is CP1250");
        decoded({0xd5,0xf5,0xdb,0xfb},"cp1250",u8"ŐőŰű","CP1250","Hungarian double acute vowels");
        decoded({0xd5,0xf5,0xdb,0xfb},"cp1252",u8"ÕõÛû","CP1252","same bytes have a different explicit codepage");
        decoded({0x93,'A',0x94,' ',0x96,' ',0x80,' ',0x85},"cp1252",u8"“A” – € …","CP1252","Western punctuation");
        decoded({0x81},"cp1250",replacement,"CP1250","undefined CP1250 byte");
        decoded({0x81},"cp1252",replacement,"CP1252","undefined CP1252 byte");
        decoded({0xc3,0xa9},"auto",u8"Ã©","CP1252","declared charset precedes valid UTF-8","text/plain; CHARSET = \"Windows-1252\"");
        decoded({0xc3,0xa9},"auto",u8"Ă©","CP1250","declared CP1250 precedes valid UTF-8","text/plain; charset=CP-1250");
        decoded({'A'},"auto","A","CP1252","declared encoding honored for ASCII","text/plain; charset = 'cp1252' ; other=x");
        decoded({0xf5},"cp1250",u8"ő","CP1250","explicit choice overrides MIME","text/plain; charset=cp1252");
        decoded({0xc3,0xa9},"utf8",u8"é","UTF-8","explicit UTF-8 overrides MIME","text/plain; charset=cp1252");
        decoded({0xf5},"auto",u8"ő","CP1250","ignore a charset string inside another quoted parameter","text/plain; note=\"charset=cp1252; x=y\"");
        decoded({0xf5},"auto",u8"õ","CP1252","quoted semicolon before the actual charset","text/plain; note=\"x;y\"; charset=windows-1252");
        decoded(bytes("ABC"),"auto","ABC","UTF-8","unsupported charset falls back safely","text/plain; charset=not-a-charset");
        decoded(bytes(u8"é"),"auto",u8"é","UTF-8","UTF-8 MIME alias","text/plain; charset=unicode-1-1-utf-8");

        const std::string sample=u8"Aő😀";
        const std::vector<unsigned char> le16={0x41,0,0x51,1,0x3d,0xd8,0,0xde};
        const std::vector<unsigned char> be16={0,0x41,1,0x51,0xd8,0x3d,0xde,0};
        const std::vector<unsigned char> le32={0x41,0,0,0,0x51,1,0,0,0,0xf6,1,0};
        const std::vector<unsigned char> be32={0,0,0,0x41,0,0,1,0x51,0,1,0xf6,0};
        decoded(le16,"utf16le",sample,"UTF-16LE","UTF-16LE supplementary pair");
        decoded(be16,"utf16be",sample,"UTF-16BE","UTF-16BE supplementary pair");
        decoded(le32,"utf32le",sample,"UTF-32LE","UTF-32LE supplementary scalar");
        decoded(be32,"utf32be",sample,"UTF-32BE","UTF-32BE supplementary scalar");
        decoded(be16,"auto",sample,"UTF-16BE","generic MIME UTF-16 defaults big endian","text/plain; charset=UTF-16");
        decoded(le16,"auto",sample,"UTF-16LE","explicit MIME UTF-16LE","text/plain; charset=utf-16le");
        decoded(be32,"auto",sample,"UTF-32BE","explicit MIME UTF-32BE","text/plain; charset=\"UTF-32BE\"");
        for(const auto& entry:std::vector<std::pair<std::vector<unsigned char>,std::string>>{
            {{0xff,0xfe},"UTF-16LE"},{{0xfe,0xff},"UTF-16BE"},
            {{0xff,0xfe,0,0},"UTF-32LE"},{{0,0,0xfe,0xff},"UTF-32BE"}}) {
            auto b=entry.first;
            const auto& body=entry.second=="UTF-16LE"?le16:entry.second=="UTF-16BE"?be16:entry.second=="UTF-32LE"?le32:be32;
            b.insert(b.end(),body.begin(),body.end());
            decoded(b,"auto",sample,entry.second,"Unicode BOM and byte order","text/plain; charset=cp1252");
        }
        decoded({0x00,0xd8,0x41,0},"utf16le",replacement+"A","UTF-16LE","unpaired high surrogate preserves next unit");
        decoded({0x00,0xdc},"utf16le",replacement,"UTF-16LE","unpaired low surrogate");
        decoded({0x41,0,0x42},"utf16le","A"+replacement,"UTF-16LE","odd UTF-16 tail");
        decoded({0,0xd8,0,0},"utf32le",replacement,"UTF-32LE","UTF-32 surrogate rejected");
        decoded({0,0,0x11,0},"utf32le",replacement,"UTF-32LE","UTF-32 out of range");
        decoded({0,0,0},"utf32le",replacement,"UTF-32LE","partial UTF-32 unit");
        for(unsigned width:{2u,4u})for(bool little:{true,false}) {
            const std::string label=std::string("UTF-")+(width==2?"16":"32")+(little?"LE":"BE");
            decoded(wide_ascii("Hello file\r\n",width,little),"auto","Hello file\r\n",label,"strong BOM-less byte alignment");
        }
        decoded({'A',0},"auto",std::string("A\0",2),"UTF-8","weak zero-byte evidence is not guessed");
        decoded({0,0,0,0,0,0,0,0},"auto",std::string(8,'\0'),"UTF-8","all-zero binary is not inferred as Unicode text");
        decoded(wide_ascii("Hello file",2,true),"auto",std::string("H\0e\0l\0l\0o\0 \0f\0i\0l\0e\0",20),"CP1252","declared charset overrides byte-pattern inference","text/plain; charset=cp1252");

        constexpr std::size_t cap=8u*1024u*1024u;
        std::vector<unsigned char> bounded(cap-1,'a');
        bounded.insert(bounded.end(),{0xe2,0x82,0xac});
        auto d=cpv::decode_text(bounded);
        require(d.truncated,"over-limit input marked truncated");
        require(d.encoding=="UTF-8","UTF-8 detection tolerates a bounded trailing scalar");
        require(d.text.size()==cap+2 && d.text.compare(cap-1,3,replacement)==0,"UTF-8 boundary produces one valid replacement");
        bounded.assign(cap+1,0x81);
        d=cpv::decode_text(bounded,"cp1252");
        require(d.truncated && d.text.size()==cap*3,"legacy expansion is bounded to three bytes per input byte");
        require(bounded.size()==cap+1 && bounded.front()==0x81,"original bytes remain untouched");
        constexpr std::size_t document_cap=25u*1024u*1024u;
        bounded.assign(document_cap,'a');
        d=cpv::decode_text(bounded,"auto","text/csv",document_cap);
        require(!d.truncated && d.text.size()==document_cap,"formatted document decoding accepts exactly 25 MiB without the plain-text 8 MiB cutoff");
        bounded.push_back('z');
        d=cpv::decode_text(bounded,"utf8","text/markdown",document_cap);
        require(d.truncated && d.text.size()==document_cap && d.text.back()=='a',"formatted document decoding bounds input at 25 MiB");
        require(bounded.back()=='z' && bounded.size()==document_cap+1,"formatted decoding leaves the original download bytes intact");
        std::cout<<"Text encoding tests passed: "<<checks<<" checks\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<"Text encoding tests FAILED: "<<e.what()<<'\n';return 1;}
}
