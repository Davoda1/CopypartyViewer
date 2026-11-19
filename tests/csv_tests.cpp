// Copyright (C) 2026 davoda1 — SPDX-License-Identifier: MIT
#include "csv.hpp"
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
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
std::vector<std::string> cells(const RenderedDocument& doc,bool keep_bold=false) {
    std::vector<std::string> result;
    std::size_t at=0;
    while((at=doc.html.find("<TD ",at))!=std::string::npos) {
        const auto start=doc.html.find('>',at)+1,end=doc.html.find("</TD>",start);
        if(end==std::string::npos)throw std::runtime_error("Unclosed CSV table cell");
        auto value=doc.html.substr(start,end-start);
        if(!keep_bold&&value.size()>=7&&value.compare(0,3,"<B>")==0&&value.compare(value.size()-4,4,"</B>")==0)
            value=value.substr(3,value.size()-7);
        result.push_back(std::move(value));at=end+5;
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
    require(cells(doc,true)==std::vector<std::string>{"<B>name</B>","<B>value</B>","alpha","42"},"only the first record is bold and all records keep their values");
    require(doc.html.find("<TH")==std::string::npos&&count(doc.html,"<TR>")==2&&count(doc.html,"<TD ALIGN=\"LEFT\"")==4,"bold header keeps left-aligned data cells and final newline adds no row");
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
void comments_and_headers() {
    for(const std::string ending:{"\n","\r\n","\r"}) {
        const auto doc=render("# initial comment"+ending+"A,B"+ending+"1,2"+ending+
            "  # middle comment,;\t\"unclosed"+ending+"3,4"+ending+"# final comment");
        require(cells(doc)==std::vector<std::string>{"A","B","1","2","3","4"},"comments at the start middle and end respect every record ending");
        require(cells(doc,true)==std::vector<std::string>{"<B>A</B>","<B>B</B>","1","2","3","4"},"first actual record after comments is the only bold record");
    }
    require(cells(render("# commas,,,,,,, and an unmatched \"\n# more,,,;\t\"\nA;B\n1;2"))==
        std::vector<std::string>{"A","B","1","2"},"comment punctuation and quotes do not affect delimiter detection");
    std::string many_comments;
    for(int i=0;i<40;++i)many_comments+="# ,,,,,;\"\n";
    require(cells(render(many_comments+"A\tB\n1\t2"))==std::vector<std::string>{"A","B","1","2"},"comments do not exhaust the delimiter sample");
    require(cells(render("\xef\xbb\xbf" " # intro\r\n# \";,,\r\nsep=;\r\n# after directive\r\nA;B\r\n1;2"),true)==
        std::vector<std::string>{"<B>A</B>","<B>B</B>","1","2"},"BOM comments and separator directive precede the bold header");
    require(cells(render("# intro\nsep=\t\n# another\nA\tB\n1\t2"))==std::vector<std::string>{"A","B","1","2"},"tab separator directive works after comments");
    require(cells(render("# comment\n\"#heading\",value\n\"#data\",42"),true)==
        std::vector<std::string>{"<B>#heading</B>","<B>value</B>","#data","42"},"quoted first fields starting with # remain header and data cells");
    require(cells(render("Name,HEX,RGB\nWhite,#FFFFFF,\"rgb(100,100,100)\"\nBlack,#000000,\"rgb(0,0,0)\""))==
        std::vector<std::string>{"Name","HEX","RGB","White","#FFFFFF","rgb(100,100,100)","Black","#000000","rgb(0,0,0)"},"later hexadecimal color fields remain data");
    require(cells(render("Name,Value\nx#y,https://example.test/#fragment\n,#empty-first"))==
        std::vector<std::string>{"Name","Value","x#y","https://example.test/#fragment","&nbsp;","#empty-first"},"# inside unquoted fields and after an empty first field stays data");
    require(cells(render("A\tB\n\t#second field\n \t#after space cell"))==
        std::vector<std::string>{"A","B","&nbsp;","#second field","&nbsp;","#after space cell"},"leading tabs retain empty TSV cells rather than indent comments");
    require(cells(render("# outside\r\nA,B\r\n\"line\r\n#quoted continuation\rnext\",x\r\n# end"))==
        std::vector<std::string>{"A","B","line<BR>#quoted continuation<BR>next","x"},"# on a quoted multiline continuation is preserved");
    require(cells(render("\"#first\n#continued\",B\n1,2"),true)==
        std::vector<std::string>{"<B>#first<BR>#continued</B>","<B>B</B>","1","2"},"quoted multiline first record remains a single bold header");
    require(cells(render("\xef\xbb\xbf" "# one\n  # two,;\"\r# three")).empty(),"comment-only input has no table records");
    require(cells(render("# comment\nsep=;\n# comment")).empty(),"comments around a directive do not create records");
    require(cells(render("# comment\n\nA,B"),true)==
        std::vector<std::string>{"<B>&nbsp;</B>","<B>&nbsp;</B>","A","B"},"explicit blank records retain their existing position and padding");
    require(cells(render("# comment\nA\n1,2"),true)==
        std::vector<std::string>{"<B>A</B>","<B>&nbsp;</B>","1","2"},"ragged bold header retains padded empty cells");
    require(cells(render(" # comment\n  name,value"))==std::vector<std::string>{"&nbsp;&nbsp;name","value"},"leading spaces on data remain intact");
    rejects([]{render("# ignored \"\nA,B\n1,\"unclosed\n# still quoted");},"comments do not hide unclosed quoted data");
    rejects([]{render("A,B\n1,\"closed\"#not-a-comment");},"# after a closing quote is still malformed data");
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
    rejects([]{render(std::string(25u*1024u*1024u+1,'a'));},"25 MiB input limit enforced");
    require(cells(render("#"+std::string(25u*1024u*1024u-9,'x')+"\nA,B\n1,2"))==std::vector<std::string>{"A","B","1","2"},"exactly 25 MiB can render within table limits");
    rejects([]{render("#"+std::string(25u*1024u*1024u,'x'));},"input limit includes ignored comment bytes");
    rejects([]{render(std::string(16u*1024u+1,'a'));},"field limit enforced");
    rejects([]{render(std::string(64,','));},"column limit enforced");
    rejects([]{std::string input;for(int i=0;i<501;++i)input+="x\n";render(input);},"row limit enforced");
    std::string maximum_rows;
    for(int i=0;i<500;++i)maximum_rows+="# comment\nx\n";
    require(count(render(maximum_rows).html,"<TR>")==500,"comments do not consume the 500 data-row limit");
    rejects([]{std::string input;for(int i=0;i<201;++i)input+=std::string(49,',')+"x\n";render(input);},"cell limit enforced");
    rejects([]{std::string input=std::string(63,',')+"x\n";for(int i=0;i<160;++i)input+="x\n";render(input);},"padded rectangular cell count is bounded");
    rejects([]{std::string input;for(int i=0;i<30;++i)input+=std::string(16000,'&')+"\n";render(input);},"expanded HTML output is bounded");
}
} // namespace
int main() {
    try {formats();comments_and_headers();safety_and_limits();std::cout<<"PASS: "<<checks<<" CSV checks\n";return 0;}
    catch(const std::exception& error){std::cerr<<"FAIL after "<<checks<<" CSV checks: "<<error.what()<<'\n';return 1;}
}
