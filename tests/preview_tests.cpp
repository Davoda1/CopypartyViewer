// Copyright (C) 2026 davoda1 — SPDX-License-Identifier: MIT
#include "preview.hpp"
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
using namespace cpv;
namespace {
unsigned checks=0;
void check(bool condition,const std::string& what){++checks;if(!condition)throw std::runtime_error(what);}
std::vector<unsigned char> read(const std::filesystem::path& p){std::ifstream f(p,std::ios::binary);if(!f)throw std::runtime_error("Missing fixture: "+p.string());return {std::istreambuf_iterator<char>(f),{}};}
std::vector<unsigned char> bytes(const std::string& s){return {s.begin(),s.end()};}
bool rejects(const std::vector<unsigned char>& b,const std::string& name){try{(void)decode_image(b,name,"");return false;}catch(const std::runtime_error&){return true;}}
void red(const ImageData& image,int tolerance=0){check(image.channels==4,"RGBA output");check(image.pixels.size()==static_cast<std::size_t>(image.width)*image.height*4,"pixel extent");check(std::abs(int(image.pixels[0])-230)<=tolerance && std::abs(int(image.pixels[1])-30)<=tolerance && std::abs(int(image.pixels[2])-20)<=tolerance,"red first pixel");}
}
int main(int argc,char** argv){
 try {
  const std::filesystem::path dir=argc>1?argv[1]:"tests/fixtures";
  const char* normal[]={"rgba.png","rgb.jpg","rgb.bmp","rgba.webp","cmyk.jpg","palette.gif","animated.webp","animated.gif","shapes.svg"};
  for(const char* name:normal){auto image=decode_image(read(dir/name),name,"");check(image.width==32&&image.height==24,std::string(name)+" dimensions");red(image,std::string(name).find("jpg")!=std::string::npos?3:0);}
  auto png=decode_image(read(dir/"rgba.png"),"unknown.bin","image/png");check(png.pixels.back()==0,"PNG alpha");
  auto webp=decode_image(read(dir/"rgba.webp"),"a.webp","");check(webp.pixels.back()==0,"WebP alpha");
  auto ico=decode_image(read(dir/"png.ico"),"a.ico","");check(ico.width==32&&ico.height==32,"highest resolution ICO");red(ico);
  auto dib=decode_image(read(dir/"dib.ico"),"a.ico","");check(dib.width==16&&dib.height==16,"DIB ICO dimensions");check(dib.pixels[3]==0&&dib.pixels[7]==255,"DIB ICO AND mask");
  auto bmp16=decode_image(read(dir/"topdown16.bmp"),"a.bmp","");check(bmp16.width==4&&bmp16.height==3&&bmp16.pixels[0]==255&&bmp16.pixels[1]==0&&bmp16.pixels[2]==0,"RGB565 top-down bitmap");
  auto bmp32=decode_image(read(dir/"topdown32.bmp"),"a.bmp","");red(bmp32);
  for(const char* name:{"rle4.bmp","rle8.bmp","palette.bmp","palette4.bmp"}){auto image=decode_image(read(dir/name),name,"");check(image.width==4&&image.height==3,"RLE dimensions");red(image);}
  auto mono=decode_image(read(dir/"mono.bmp"),"a.bmp","");check(mono.width==9&&mono.height==3&&mono.pixels[0]==0&&mono.pixels[4]==255&&mono.pixels[(2*9+8)*4]==0,"1-bit BMP row padding and orientation");
  auto dictionary=decode_image(read(dir/"dictionary.gif"),"a.gif","");check(dictionary.pixels==read(dir/"dictionary.rgba"),"GIF LZW dictionary, interlace and all palette entries");
  check(rejects({},"empty.png"),"empty image rejected");
  check(rejects(read(dir/"oversized.png"),"a.png"),"oversized image rejected");
  check(rejects(bytes("<svg width='0' height='0'/ >"),"a.svg"),"zero SVG rejected");
  check(rejects(bytes("this is not SVG"),"a.svg"),"non-SVG rejected");
  check(rejects(bytes("BM"),"a.bmp"),"short BMP rejected");
  for(const char* name:normal){auto data=read(dir/name);if(std::string(name).find("svg")!=std::string::npos)continue;data.resize(std::min<std::size_t>(data.size(),16));check(rejects(data,name),std::string(name)+" truncated header rejected");}
  auto jpeg=read(dir/"rgb.jpg");jpeg.resize(jpeg.size()-30);check(rejects(jpeg,"a.jpg"),"truncated JPEG rejected");
  auto largeico=read(dir/"png.ico");largeico[34]=0xff;largeico[35]=0xff;largeico[36]=0xff;largeico[37]=0xff;check(rejects(largeico,"a.ico"),"ICO range overflow rejected");
  check(image_supported("PHOTO.JPEG",""),"case insensitive extension");check(image_supported("untitled","image/svg+xml; charset=utf-8"),"image MIME");check(!image_supported("a.exe","application/octet-stream"),"binary not image");
  const auto colored=prepare_styled_text(bytes("a\x1b[1;31mred\x1b[0mz\r\n"));
  check(colored.text=="aredz\n","ANSI and CRLF text");check(colored.styles=="ALLLAA","ANSI styles");
  auto extended=prepare_styled_text(bytes("\x1b[38;2;255;0;0mR\x1b[0mN"));check(extended.styles=="DA","true-color ANSI nearest palette");
  auto control=prepare_text(bytes("a\x1b]8;;https://example.test\x1b\\b\x1b]8;;\x07" "c\x1b[2Jd"));check(control=="abcd","OSC and cursor controls removed");
  auto invalid_utf8=prepare_styled_text({0xc0,0xaf,'A',0xed,0xa0,0x80});check(invalid_utf8.text.find('A')!=std::string::npos&&invalid_utf8.text.size()==invalid_utf8.styles.size(),"invalid UTF8 safely replaced and styles aligned");
  auto utf8=prepare_styled_text(bytes("\xef\xbb\xbfGr\xc3\xbc\xc3\x9f"));check(utf8.text=="Gr\xc3\xbc\xc3\x9f"&&utf8.text.size()==utf8.styles.size(),"UTF8 BOM removal and style alignment");
  auto oversize=std::vector<unsigned char>(8u*1024u*1024u+1,'x');auto truncated=prepare_text(oversize);check(truncated.find("Preview truncated at 8 MiB")!=std::string::npos,"text preview truncation notice");
  check(text_supported("a.json","application/json",bytes("{\"x\": 1}")),"JSON text");check(text_supported("unknown","application/octet-stream",bytes("plain text\n")),"unknown UTF8 text");check(!text_supported("a.txt","text/plain",{'a',0,'b'}),"binary NUL rejection");
  // Workers decode independently; no FLTK initialization or display server needed.
  std::atomic<bool> concurrent_ok{true};std::vector<std::thread> workers;
  for(unsigned t=0;t<8;++t) workers.emplace_back([&]{try{for(unsigned n=0;n<12;++n){auto image=decode_image(read(dir/(n%2?"shapes.svg":"rgba.png")),n%2?"a.svg":"a.png","");if(image.width!=32||image.pixels[0]!=230)concurrent_ok=false;}}catch(...){concurrent_ok=false;}});
  for(auto& worker:workers)worker.join();
  check(concurrent_ok,"concurrent SVG and PNG decoding without GUI");
  std::cout<<"Preview tests passed ("<<checks<<" checks).\n";return EXIT_SUCCESS;
 } catch(const std::exception& e){std::cerr<<"Preview test failed: "<<e.what()<<"\n";return EXIT_FAILURE;}
}
