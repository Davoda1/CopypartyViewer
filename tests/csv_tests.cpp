// Copyright (C) 2026 davoda1 — SPDX-License-Identifier: MIT
#include "csv.hpp"
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace cpv::document;
namespace {
int checks=0;
void require(bool ok,const char* message){++checks;if(!ok)throw std::runtime_error(message);}
template<class F> void rejects(F operation,const char* message) {
    ++checks;try{operation();}catch(const std::runtime_error& error){
        if(std::string(error.what()).find("Source")==std::string::npos)throw std::runtime_error("CSV fallback error must explain Source availability");
        return;
    }
    throw std::runtime_error(message);
}
RenderedDocument render(const std::string& input){return render_csv(input,"#202020","#ffffff","#b8bec5");}
std::vector<std::string> cells(const RenderedDocument& doc) {
    std::vector<std::string> result;
    std::size_t at=0;
    while((at=doc.html.find("<TD ",at))!=std::string::npos) {
        const auto start=doc.html.find('>',at)+1,end=doc.html.find("</TD>",start);
        if(end==std::string::npos)throw std::runtime_error("Unclosed CSV table cell");
        result.push_back(doc.html.substr(start,end-start));at=end+5;
    }
    return result;
}
std::size_t count(const std::string& text,const std::string& token) {
    std::size_t total=0,at=0;
    while((at=text.find(token,at))!=std::string::npos){++total;at+=token.size();}
    return total;
}
void formats() {
    require(csv_supported("sheet.CSV"),"CSV extension is case insensitive");
    require(csv_supported("sheet.tsv"),"TSV extension supported");
    require(csv_supported("file","text/csv; charset=utf-8"),"CSV MIME with charset supported");
    require(csv_supported("file","application/csv")&&csv_supported("file","application/x-csv"),"common application CSV MIME aliases supported");
    require(csv_supported("file","text/tab-separated-values"),"TSV MIME supported");
    require(!csv_supported("file.csv.exe","application/octet-stream"),"unrelated extensions are not tables");
    auto doc=render("name,value\nalpha,42\n");
    require(cells(doc)==std::vector<std::string>{"name","value","alpha","42"},"ordinary CSV rows and first-row data retained");
    require(doc.html.find("<TH")==std::string::npos&&count(doc.html,"<TR>")==2,"first row is not assumed to be a heading and final newline adds no row");
    require(doc.links.empty(),"CSV preview has no active link registry");
    require(cells(render("a,\"b,b\",\"say \"\"hi\"\"\",\n"))==std::vector<std::string>{"a","b,b","say &quot;hi&quot;","&nbsp;"},"quoted commas doubled quotes and trailing empty field");
    require(cells(render("\"a\nb\",c\r\n\"d\r\ne\",f"))==std::vector<std::string>{"a<BR>b","c","d<BR>e","f"},"newlines inside quotes stay within their cells");
    require(cells(render("a,b\r\nc,d\ne,f\r"))==std::vector<std::string>{"a","b","c","d","e","f"},"CRLF LF and CR record endings supported");
    require(cells(render(",,\n\"\",,x"))==std::vector<std::string>{"&nbsp;","&nbsp;","&nbsp;","&nbsp;","&nbsp;","x"},"leading empty quoted empty and repeated empty fields retained");
    require(cells(render("a,b\nc"))==std::vector<std::string>{"a","b","c","&nbsp;"},"ragged records are padded without discarding values");
    require(cells(render("\xef\xbb\xbf" "Árvíztűrő,tükörfúrógép"))==std::vector<std::string>{"Árvíztűrő","tükörfúrógép"},"UTF-8 BOM removed and Unicode text preserved");
    require(cells(render("A;B\n1,5;2,5\n3,5;4,5"))==std::vector<std::string>{"A","B","1,5","2,5","3,5","4,5"},"consistent semicolon rows outrank decimal commas");
    require(cells(render("sep=;\r\n\"a;b\";c\n1;2"))==std::vector<std::string>{"a;b","c","1","2"},"Excel separator directive selects delimiter and is not a data row");
    require(cells(render("\xef\xbb\xbf" "sep=,\na,b"))==std::vector<std::string>{"a","b"},"BOM and Excel directive can coexist");
    require(cells(render("a\tb\n\"c\td\"\te"))==std::vector<std::string>{"a","b","c&nbsp;&nbsp;&nbsp;&nbsp;d","e"},"TSV delimiter and quoted tab field handled");
    require(cells(render("\"a;b\",c\n\"d;e\",f"))==std::vector<std::string>{"a;b","c","d;e","f"},"delimiter detection ignores quoted semicolons");
    require(cells(render("\"one value\""))==std::vector<std::string>{"one value"},"single quoted field supported");
    require(cells(render("")).empty()&&render("").html.find("contains no rows")!=std::string::npos,"empty input renders an empty-file message");
    require(cells(render("sep=;")).empty(),"separator-only input has no data rows");
    require(cells(render("\n\n"))==std::vector<std::string>{"&nbsp;","&nbsp;"},"blank records remain explicit rows");
}
void safety_and_limits() {
    auto doc=render("<script>alert(1)</script>,https://example.test/x\n=1+2,@cmd");
    require(doc.html.find("<script>")==std::string::npos&&doc.html.find("&lt;script&gt;alert(1)&lt;/script&gt;")!=std::string::npos,"cell HTML is escaped");
    require(doc.html.find("<A ")==std::string::npos&&doc.html.find("<IMG")==std::string::npos&&doc.links.empty(),"URLs and markup create no active resources");
    require(cells(doc)==std::vector<std::string>{"&lt;script&gt;alert(1)&lt;/script&gt;","https://example.test/x","=1+2","@cmd"},"formula-like text remains literal data");
    doc=render_csv("x","\" onclick=bad","<bad>","javascript:bad");
    require(doc.html.find("onclick")==std::string::npos&&doc.html.find("javascript")==std::string::npos,"invalid theme colors cannot become HTML attributes");
    rejects([]{render("a,\"unclosed");},"unclosed quote rejected");
    rejects([]{render("a,\"quoted\"oops");},"text after closing quote rejected");
    rejects([]{render("a,b\"c");},"quote in unquoted field rejected");
    rejects([]{render(std::string("a,\0b",4));},"NUL input rejected");
    rejects([]{render(std::string(512u*1024u+1,'a'));},"input limit enforced");
    rejects([]{render(std::string(16u*1024u+1,'a'));},"field limit enforced");
    rejects([]{render(std::string(64,','));},"column limit enforced");
    rejects([]{std::string input;for(int i=0;i<501;++i)input+="x\n";render(input);},"row limit enforced");
    rejects([]{std::string input;for(int i=0;i<201;++i)input+=std::string(49,',')+"x\n";render(input);},"cell limit enforced");
    rejects([]{std::string input=std::string(63,',')+"x\n";for(int i=0;i<160;++i)input+="x\n";render(input);},"padded rectangular cell count is bounded");
    rejects([]{std::string input;for(int i=0;i<30;++i)input+=std::string(16000,'&')+"\n";render(input);},"expanded HTML output is bounded");
}
} // namespace
int main() {
    try {formats();safety_and_limits();std::cout<<"PASS: "<<checks<<" CSV checks\n";return 0;}
    catch(const std::exception& error){std::cerr<<"FAIL after "<<checks<<" CSV checks: "<<error.what()<<'\n';return 1;}
}
