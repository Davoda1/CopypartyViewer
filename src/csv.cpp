// Copyright (C) 2026 davoda1 — SPDX-License-Identifier: MIT
#include "csv.hpp"
#include <algorithm>
#include <array>
#include <map>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace cpv::document {
namespace {
constexpr std::size_t max_input=25u*1024u*1024u, max_output=2u*1024u*1024u;
constexpr std::size_t max_rows=500, max_columns=64, max_cells=10000, max_field=16u*1024u;
constexpr std::array<char,3> delimiters={',',';','\t'};
std::string lower(std::string text) {
    for(char& c:text)if(c>='A'&&c<='Z')c=static_cast<char>(c+'a'-'A');
    return text;
}
bool hex(char c) {return (c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F');}
std::string color(const std::string& value,const char* fallback) {
    if(value.size()==7&&value[0]=='#'&&std::all_of(value.begin()+1,value.end(),hex))return value;
    return fallback;
}
void append_escaped(std::string& out,const std::string& value) {
    bool leading=true,previous_space=false;
    for(unsigned char c:value) {
        if(c==' ') {out+=leading||previous_space?"&nbsp;":" ";previous_space=true;}
        else {
            previous_space=false;
            switch(c) {
            case '&':out+="&amp;";break;
            case '<':out+="&lt;";break;
            case '>':out+="&gt;";break;
            case '"':out+="&quot;";break;
            case '\'':out+="&#39;";break;
            case '\n':out+="<BR>";leading=true;continue;
            case '\t':out+="&nbsp;&nbsp;&nbsp;&nbsp;";break;
            default:out+=static_cast<char>(c);
            }
        }
        leading=false;
    }
}
std::size_t comment_end(std::string_view text,std::size_t start) {
    // Call only at a record boundary. Tabs cannot be skipped here: in TSV,
    // a leading tab is an empty first cell, even when the next cell starts #.
    auto first=start;
    while(first<text.size()&&text[first]==' ')++first;
    if(first==text.size()||text[first]!='#')return start;
    auto end=text.find_first_of("\r\n",first+1);
    if(end==std::string_view::npos)return text.size();
    if(text[end]=='\r'&&end+1<text.size()&&text[end+1]=='\n')++end;
    return end+1;
}
char detect_delimiter(std::string_view text) {
    // A shared quote-aware sample keeps delimiter-like text inside quoted
    // fields out of the score. Consistent logical records outrank raw counts.
    std::vector<std::array<std::size_t,3>> sample;
    std::array<std::size_t,3> counts{};
    bool quoted=false,nonempty=false;
    for(std::size_t i=0;i<text.size()&&sample.size()<32;++i) {
        if(!quoted&&!nonempty) {
            const auto next=comment_end(text,i);
            if(next!=i){i=next-1;continue;}
        }
        const char c=text[i];
        if(c=='"') {
            nonempty=true;
            if(quoted&&i+1<text.size()&&text[i+1]=='"'){++i;continue;}
            quoted=!quoted;continue;
        }
        if(!quoted&&(c=='\r'||c=='\n')) {
            if(nonempty)sample.push_back(counts);
            counts={};nonempty=false;
            if(c=='\r'&&i+1<text.size()&&text[i+1]=='\n')++i;
            continue;
        }
        nonempty=true;
        if(!quoted)for(std::size_t d=0;d<delimiters.size();++d)if(c==delimiters[d])++counts[d];
    }
    if(nonempty&&sample.size()<32)sample.push_back(counts);
    std::array<std::size_t,3> best{};
    char selected=',';
    for(std::size_t d=0;d<delimiters.size();++d) {
        std::map<std::size_t,std::size_t> frequencies;
        std::size_t support=0,consistency=0,width=0;
        for(const auto& row:sample)if(row[d]){++frequencies[row[d]];++support;}
        for(const auto& item:frequencies)if(item.second>consistency||(item.second==consistency&&item.first>width)) {
            consistency=item.second;width=item.first;
        }
        const std::array<std::size_t,3> score={consistency,support,width};
        if(score>best){best=score;selected=delimiters[d];}
    }
    return selected;
}
using Row=std::vector<std::string>;
std::vector<Row> parse(std::string_view input,char delimiter) {
    enum class Field { start,unquoted,quoted,closed } state=Field::start;
    std::vector<Row> rows;
    Row row;
    std::string value;
    std::size_t cells=0;
    auto finish_field=[&] {
        if(row.size()>=max_columns)throw std::runtime_error("CSV preview supports at most 64 columns. Use Source.");
        if(++cells>max_cells)throw std::runtime_error("CSV preview supports at most 10,000 cells. Use Source.");
        row.push_back(std::move(value));value.clear();state=Field::start;
    };
    auto finish_row=[&] {
        if(rows.size()>=max_rows)throw std::runtime_error("CSV preview supports at most 500 rows. Use Source.");
        rows.push_back(std::move(row));row.clear();
    };
    auto append=[&](char c) {
        if(value.size()>=max_field)throw std::runtime_error("A CSV field exceeds the 16 KiB preview limit. Use Source.");
        const auto byte=static_cast<unsigned char>(c);
        if((byte<32&&c!='\n'&&c!='\t')||byte==127)throw std::runtime_error("CSV contains unsupported control characters. Use Source.");
        value+=c;
    };
    for(std::size_t i=0;i<input.size();++i) {
        if(state==Field::start&&row.empty()) {
            const auto next=comment_end(input,i);
            if(next!=i){i=next-1;continue;}
        }
        const char c=input[i];
        if(state==Field::quoted) {
            if(c=='"') {
                if(i+1<input.size()&&input[i+1]=='"'){append('"');++i;}
                else state=Field::closed;
            } else if(c=='\r'||c=='\n') {
                append('\n');if(c=='\r'&&i+1<input.size()&&input[i+1]=='\n')++i;
            } else append(c);
        } else if(c==delimiter)finish_field();
        else if(c=='\r'||c=='\n') {
            finish_field();finish_row();
            if(c=='\r'&&i+1<input.size()&&input[i+1]=='\n')++i;
        } else if(state==Field::closed)throw std::runtime_error("Unexpected text after a quoted CSV field. Use Source.");
        else if(c=='"') {
            if(state!=Field::start)throw std::runtime_error("An unquoted CSV field contains a quote. Use Source.");
            state=Field::quoted;
        } else {append(c);state=Field::unquoted;}
    }
    if(state==Field::quoted)throw std::runtime_error("CSV has an unclosed quoted field. Use Source.");
    if(state!=Field::start||!row.empty()){finish_field();finish_row();}
    return rows;
}
} // namespace

bool csv_supported(const std::string& filename,const std::string& content_type) {
    auto mime=lower(content_type.substr(0,content_type.find(';')));
    const auto begin=mime.find_first_not_of(" \t\r\n");
    if(begin!=std::string::npos)mime=mime.substr(begin,mime.find_last_not_of(" \t\r\n")-begin+1);
    if(mime=="text/csv"||mime=="application/csv"||mime=="application/x-csv"||mime=="text/comma-separated-values"||mime=="text/tab-separated-values"||mime=="text/tsv")return true;
    const auto name=lower(filename);
    return name.size()>=4&&(name.compare(name.size()-4,4,".csv")==0||name.compare(name.size()-4,4,".tsv")==0);
}
RenderedDocument render_csv(const std::string& decoded_utf8,const std::string& foreground_hex,
                           const std::string& background_hex,const std::string& border_hex) {
    if(decoded_utf8.size()>max_input)throw std::runtime_error("CSV exceeds the 25 MiB formatted-preview limit. Use Source.");
    std::string_view input=decoded_utf8;
    if(input.substr(0,3)=="\xef\xbb\xbf")input.remove_prefix(3);
    while(!input.empty()) {
        const auto next=comment_end(input,0);
        if(!next)break;
        input.remove_prefix(next);
    }
    char delimiter=',';
    const auto line_end=input.find_first_of("\r\n");
    const auto first=input.substr(0,line_end);
    if(first.size()==5&&lower(std::string(first.substr(0,4)))=="sep="&&std::find(delimiters.begin(),delimiters.end(),first[4])!=delimiters.end()) {
        delimiter=first[4];
        if(line_end==std::string_view::npos)input={};
        else {
            std::size_t next=line_end+1;
            if(input[line_end]=='\r'&&next<input.size()&&input[next]=='\n')++next;
            input.remove_prefix(next);
        }
    } else delimiter=detect_delimiter(input);
    const auto rows=parse(input,delimiter);
    std::size_t columns=0;for(const auto& row:rows)columns=std::max(columns,row.size());
    if(columns&&rows.size()>max_cells/columns)throw std::runtime_error("CSV preview supports at most 10,000 displayed cells. Use Source.");
    const auto fg=color(foreground_hex,"#202020"),bg=color(background_hex,"#ffffff"),border=color(border_hex,"#b8bec5");
    RenderedDocument result;
    auto& html=result.html;
    html="<HTML><BODY TEXT=\""+fg+"\" BGCOLOR=\""+bg+"\">\n";
    if(rows.empty())html+="<P>This file contains no rows.</P>\n";
    else {
        // Preserve CSV's content-derived column widths; wide tables scroll
        // horizontally within the native preview viewport.
        html+="<TABLE BORDER=\"1\" BGCOLOR=\""+border+"\">\n";
        for(std::size_t index=0;index<rows.size();++index) {
            const auto& row=rows[index];
            html+="<TR>";
            for(std::size_t column=0;column<columns;++column) {
                html+="<TD ALIGN=\"LEFT\" BGCOLOR=\""+bg+"\">";
                if(index==0)html+="<B>";
                if(column<row.size()&&!row[column].empty())append_escaped(html,row[column]);
                else html+="&nbsp;";
                if(index==0)html+="</B>";
                html+="</TD>";
                if(html.size()>max_output)throw std::runtime_error("CSV formatted output exceeds 2 MiB. Use Source.");
            }
            html+="</TR>\n";
        }
        html+="</TABLE>\n";
    }
    html+="</BODY></HTML>";
    if(html.size()>max_output)throw std::runtime_error("CSV formatted output exceeds 2 MiB. Use Source.");
    return result;
}
} // namespace cpv::document
