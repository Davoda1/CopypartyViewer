// SPDX-License-Identifier: MIT
// Copyright (C) 2026 davoda1
#include "retro.hpp"
#include "preview.hpp"
#include <FL/Fl.H>
#include <FL/Fl_Scrollbar.H>
#include <FL/Fl_Window.H>
#include <FL/fl_draw.H>
#include <algorithm>
#include <cmath>
#include <limits>
#include <string_view>
#include <utility>

namespace cpv {
namespace {
constexpr int font_size = RetroListing::text_size;
constexpr int pad_x = 12, pad_y = 9;
constexpr std::size_t max_header = 64u * 1024u;
constexpr std::size_t cache_limit = 512;

// Suppress only the visual focus marker. Unlike permanently clearing the flag,
// this leaves keyboard focus and keyboard scrolling available to the scrollbar.
class RetroScrollbar final : public Fl_Scrollbar {
public:
    RetroScrollbar(int x,int y,int w,int h) : Fl_Scrollbar(x,y,w,h) {}
protected:
    void draw() override {
        const bool show_focus = visible_focus() != 0;
        clear_visible_focus();
        Fl_Scrollbar::draw();
        if(show_focus)set_visible_focus();
    }
};

// Monospace extent estimation is independent of the display. Exact glyph widths
// are measured only for visible lines, so large directories do not synchronously
// rasterize/measure every filename when a response arrives.
std::size_t columns(std::string_view text) {
    std::size_t result = 0;
    for (std::size_t i = 0; i < text.size();) {
        const auto first = static_cast<unsigned char>(text[i++]);
        std::uint32_t c = first;
        unsigned extra = 0;
        if (first >= 0xc2 && first <= 0xdf) {c = first & 31; extra = 1;}
        else if (first >= 0xe0 && first <= 0xef) {c = first & 15; extra = 2;}
        else if (first >= 0xf0 && first <= 0xf4) {c = first & 7; extra = 3;}
        for (unsigned n = 0; n < extra && i < text.size(); ++n)
            c = (c << 6) | (static_cast<unsigned char>(text[i++]) & 63);
        if ((c >= 0x300 && c <= 0x36f) || (c >= 0x1ab0 && c <= 0x1aff) ||
            (c >= 0x1dc0 && c <= 0x1dff) || (c >= 0x20d0 && c <= 0x20ff) ||
            (c >= 0xfe00 && c <= 0xfe0f) || (c >= 0xfe20 && c <= 0xfe2f) ||
            (c >= 0xe0100 && c <= 0xe01ef) || c == 0x200d) continue;
        const bool wide = (c >= 0x1100 && c <= 0x115f) || c == 0x2329 || c == 0x232a ||
            (c >= 0x2e80 && c <= 0xa4cf && c != 0x303f) ||
            (c >= 0xac00 && c <= 0xd7a3) || (c >= 0xf900 && c <= 0xfaff) ||
            (c >= 0xfe10 && c <= 0xfe19) || (c >= 0xfe30 && c <= 0xfe6f) ||
            (c >= 0xff00 && c <= 0xff60) || (c >= 0xffe0 && c <= 0xffe6) ||
            (c >= 0x1f300 && c <= 0x1faff) || (c >= 0x20000 && c <= 0x3fffd);
        result += wide ? 2 : 1;
    }
    return result;
}
std::size_t style_index(char style) {
    return style >= 'A' && style <= 'R' ? static_cast<std::size_t>(style - 'A') : 0;
}
Fl_Font style_font(std::size_t style) {return style == 1 || style >= 10 ? FL_COURIER_BOLD : FL_COURIER;}

void reverse_sgr(std::string_view args, bool& reverse) {
    std::vector<int> values;
    int value = 0;
    for (char c : args) {
        if (c >= '0' && c <= '9') value = std::min(100000, value * 10 + c - '0');
        else if (c == ';' || c == ':') {values.push_back(value); value = 0;}
        else return;
    }
    values.push_back(value);
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (values[i] == 0 || values[i] == 27) reverse = false;
        else if (values[i] == 7) reverse = true;
        else if ((values[i] == 38 || values[i] == 48) && i + 1 < values.size()) {
            // Color parameters containing 0, 7 or 27 are not SGR operations.
            if (values[i+1] == 5 && i + 2 < values.size()) i += 2;
            else if (values[i+1] == 2 && i + 4 < values.size()) i += 4;
        }
    }
}
// Track reverse-video separately from the common text style table. This keeps
// Copyparty's inverse cyan year intact without changing regular text previews.
std::vector<unsigned char> reverse_flags(const std::string& ansi, std::size_t output_size) {
    std::vector<unsigned char> result;
    result.reserve(output_size);
    bool reverse = false;
    std::size_t i = 0;
    while (i < ansi.size()) {
        if (static_cast<unsigned char>(ansi[i]) != 27) {
            auto end = ansi.find('\x1b', i);
            if (end == std::string::npos) end = ansi.size();
            // Prefix prevents a BOM inside a filename from being treated as a
            // document BOM. The complete common parser already handles its BOM.
            std::string part = "x" + ansi.substr(i, end - i);
            std::vector<unsigned char> bytes(part.begin(), part.end());
            const auto plain = prepare_text(bytes);
            if (!plain.empty()) result.insert(result.end(), plain.size()-1, reverse ? 1 : 0);
            i = end;
            continue;
        }
        if (++i >= ansi.size()) break;
        if (ansi[i] == '[') {
            const auto start = ++i;
            while (i < ansi.size() && !(ansi[i] >= 0x40 && ansi[i] <= 0x7e)) ++i;
            if (i < ansi.size()) {
                if (ansi[i] == 'm' && i-start <= 256) reverse_sgr(std::string_view(ansi.data()+start,i-start),reverse);
                ++i;
            }
        } else if (ansi[i] == ']' || ansi[i] == 'P' || ansi[i] == '_' || ansi[i] == '^') {
            ++i;
            while (i < ansi.size()) {
                if (ansi[i] == 7) {++i; break;}
                if (ansi[i] == 27 && i+1 < ansi.size() && ansi[i+1] == '\\') {i += 2; break;}
                ++i;
            }
        } else {
            while (i < ansi.size() && ansi[i] >= 0x20 && ansi[i] <= 0x2f) ++i;
            if (i < ansi.size()) ++i;
        }
    }
    if (ansi.size() >= 3 && static_cast<unsigned char>(ansi[0]) == 0xef &&
        static_cast<unsigned char>(ansi[1]) == 0xbb && static_cast<unsigned char>(ansi[2]) == 0xbf && result.size() >= 3)
        result.erase(result.begin(),result.begin()+3);
    result.resize(output_size,0);
    return result;
}
}

RetroListing::Line RetroListing::parse_line(const std::string& ansi) {
    std::vector<unsigned char> bytes(ansi.begin(),ansi.end());
    auto prepared = prepare_styled_text(bytes);
    Line line;
    line.text = std::move(prepared.text);
    line.styles = std::move(prepared.styles);
    line.reversed = reverse_flags(ansi,line.text.size());
    return line;
}
RetroListing::RetroListing(int X,int Y,int W,int H) : Fl_Group(X,Y,W,H) {
    box(FL_BORDER_BOX);
    begin();
    vertical_ = new RetroScrollbar(X+W-17,Y+1,16,H-2);
    horizontal_ = new RetroScrollbar(X+1,Y+H-17,W-2,16);
    horizontal_->type(FL_HORIZONTAL);
    auto moved = [](Fl_Widget*,void* data) {
        auto* self = static_cast<RetroListing*>(data);
        self->hover(-1);
        self->redraw();
    };
    vertical_->callback(moved,this); horizontal_->callback(moved,this);
    end();
    vertical_->hide(); horizontal_->hide();
    tooltip("Click a name to open it. Arrow keys select; Enter opens. Ctrl+C copies the selected path.");
    palette(false);
    metrics();
}
RetroListing::~RetroListing() {Fl::remove_timeout(open_timeout,this);}

void RetroListing::clear() {
    Fl::remove_timeout(open_timeout,this); pending_open_.reset();
    entries_.clear(); header_.clear(); row_cache_.clear(); model_path_.clear();
    selected_ = hovered_ = pressed_ = -1;
    longest_name_columns_ = 0; prefix_columns_ = 25;
    vertical_->value(0); horizontal_->value(0);
    metrics(); redraw();
}
void RetroListing::set_header_ansi(const std::string& header) {
    auto parsed = parse_line(header.substr(0,max_header));
    header_.clear();
    std::size_t start = 0;
    while (start < parsed.text.size()) {
        auto end = parsed.text.find('\n',start);
        if (end == std::string::npos) end = parsed.text.size();
        Line line;
        line.text = parsed.text.substr(start,end-start);
        line.styles = parsed.styles.substr(start,end-start);
        line.reversed.assign(parsed.reversed.begin()+static_cast<std::ptrdiff_t>(start),parsed.reversed.begin()+static_cast<std::ptrdiff_t>(end));
        header_.push_back(std::move(line));
        start = end+1;
    }
    metrics(); redraw();
}
void RetroListing::set_listing(const Listing& listing,const std::vector<Entry>& visible_entries,bool show_sizes) {
    Fl::remove_timeout(open_timeout,this); pending_open_.reset();
    const bool same_path = model_path_ == listing.path;
    const std::string selection = selected_entry() ? selected_entry()->path : std::string{};
    entries_ = visible_entries;
    model_path_ = listing.path;
    show_sizes_ = show_sizes;
    row_cache_.clear(); selected_ = hovered_ = pressed_ = -1;
    longest_name_columns_ = 0;
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        const auto& entry = entries_[i];
        longest_name_columns_ = std::max(longest_name_columns_,columns(entry.name)+(entry.directory?1:0));
        if (same_path && !selection.empty() && entry.path == selection) selected_ = static_cast<int>(i);
    }
    prefix_columns_ = 25;
    if (!entries_.empty()) {
        auto& first = row_line(0);
        prefix_columns_ = columns(std::string_view(first.text.data(),first.name_begin));
    }
    if (!same_path) {vertical_->value(0); horizontal_->value(0);}
    set_header_ansi(listing.header_ansi);
}

RetroListing::Line& RetroListing::row_line(std::size_t row) {
    auto found = row_cache_.find(row);
    if (found != row_cache_.end()) return found->second;
    if (row_cache_.size() >= cache_limit) row_cache_.clear();
    const auto& entry = entries_[row];
    auto line = parse_line(terminal_entry_ansi(entry,show_sizes_));
    while (!line.text.empty() && line.text.back() == '\n') {
        line.text.pop_back(); line.styles.pop_back(); line.reversed.pop_back();
    }
    // Match the displayed name through the same sanitizer as the complete row.
    // The dummy prefix keeps a legitimate interior UTF-8 BOM in a filename.
    const auto raw_name = "x" + entry.name + (entry.directory ? "/" : "");
    const std::vector<unsigned char> name_bytes(raw_name.begin(),raw_name.end());
    auto name = prepare_text(name_bytes);
    if (!name.empty()) name.erase(0,1);
    if (!name.empty() && line.text.size() >= name.size() && line.text.compare(line.text.size()-name.size(),name.size(),name) == 0) {
        line.name_begin = line.text.size()-name.size();
        line.name_end = line.text.size();
    }
    return row_cache_.emplace(row,std::move(line)).first->second;
}
void RetroListing::palette(bool dark) {
    background_ = dark ? fl_rgb_color(23,25,28) : FL_WHITE;
    const std::array<Fl_Color,8> basic = dark ?
        std::array<Fl_Color,8>{fl_rgb_color(186,192,200),fl_rgb_color(255,133,133),fl_rgb_color(126,222,149),fl_rgb_color(245,215,113),fl_rgb_color(125,183,255),fl_rgb_color(222,150,246),fl_rgb_color(111,223,231),FL_WHITE} :
        std::array<Fl_Color,8>{fl_rgb_color(28,34,40),fl_rgb_color(174,32,39),fl_rgb_color(18,113,45),fl_rgb_color(137,97,0),fl_rgb_color(30,80,171),fl_rgb_color(137,42,151),fl_rgb_color(0,111,126),fl_rgb_color(98,105,115)};
    colors_[0] = colors_[1] = dark ? fl_rgb_color(233,236,240) : fl_rgb_color(27,33,40);
    for (std::size_t i=0;i<basic.size();++i) colors_[i+2]=colors_[i+10]=basic[i];
    color(background_); redraw();
}
void RetroListing::metrics() {
    fl_font(FL_COURIER,font_size);
    cell_width_ = std::max(1.0,fl_width("M"));
    line_height_ = std::max(font_size+6,fl_height()+4);
    baseline_ = (line_height_-fl_height())/2 + fl_height()-fl_descent();
    std::size_t max_columns = entries_.empty() ? 0 : prefix_columns_+longest_name_columns_;
    for (auto& line:header_) {
        measure(line);
        max_columns = std::max(max_columns,columns(line.text));
    }
    content_width_ = static_cast<int>(std::min<double>(1000000, std::ceil(max_columns*cell_width_)))+2*pad_x;
    for (const auto& line:header_) content_width_=std::max(content_width_,static_cast<int>(std::ceil(line.width))+2*pad_x);
    content_height_ = static_cast<int>((header_.size()+entries_.size())*static_cast<std::size_t>(line_height_))+2*pad_y;
    vertical_->linesize(line_height_); horizontal_->linesize(static_cast<int>(cell_width_*3));
    scrollbars();
}
void RetroListing::scrollbars() {
    const int thickness = std::max(12,Fl::scrollbar_size());
    bool vertical=false,horizontal=false;
    for (int i=0;i<3;++i) {
        horizontal = content_width_ > std::max(1,w()-2-(vertical?thickness:0));
        vertical = content_height_ > std::max(1,h()-2-(horizontal?thickness:0));
    }
    viewport_w_=std::max(1,w()-2-(vertical?thickness:0));
    viewport_h_=std::max(1,h()-2-(horizontal?thickness:0));
    vertical_->resize(x()+w()-1-thickness,y()+1,thickness,viewport_h_);
    horizontal_->resize(x()+1,y()+h()-1-thickness,viewport_w_,thickness);
    vertical ? vertical_->show() : vertical_->hide();
    horizontal ? horizontal_->show() : horizontal_->hide();
    const int vx=std::clamp(horizontal_->value(),0,std::max(0,content_width_-viewport_w_));
    const int vy=std::clamp(vertical_->value(),0,std::max(0,content_height_-viewport_h_));
    horizontal_->value(vx,viewport_w_,0,std::max(content_width_,viewport_w_));
    vertical_->value(vy,viewport_h_,0,std::max(content_height_,viewport_h_));
}
void RetroListing::resize(int X,int Y,int W,int H) {
    Fl_Group::resize(X,Y,W,H); scrollbars(); redraw();
}
int RetroListing::scroll_position() const {
    if (entries_.empty()) return 0;
    const int top=vertical_->value()-pad_y-static_cast<int>(header_.size())*line_height_;
    return std::clamp(top/line_height_,0,static_cast<int>(entries_.size())-1);
}
void RetroListing::scroll_position(int row) {
    row=std::clamp(row,0,std::max(0,static_cast<int>(entries_.size())-1));
    int offset=row?pad_y+(static_cast<int>(header_.size())+row)*line_height_:0;
    move_scroll(horizontal_->value(),offset);
}
const Entry* RetroListing::selected_entry() const {
    return selected_>=0&&selected_<static_cast<int>(entries_.size())?&entries_[static_cast<std::size_t>(selected_)]:nullptr;
}
void RetroListing::move_scroll(int X,int Y) {
    horizontal_->value(std::clamp(X,0,std::max(0,content_width_-viewport_w_)));
    vertical_->value(std::clamp(Y,0,std::max(0,content_height_-viewport_h_)));
    hover(-1); redraw();
}
void RetroListing::ensure_visible(int row) {
    const int top=pad_y+(static_cast<int>(header_.size())+row)*line_height_;
    int offset=vertical_->value();
    if(top<offset)offset=top;
    else if(top+line_height_>offset+viewport_h_)offset=top+line_height_-viewport_h_;
    move_scroll(horizontal_->value(),offset);
}
void RetroListing::measure(Line& line) {
    if(line.width>=0)return;
    double xpos=0;
    for(std::size_t start=0;start<line.text.size();) {
        auto style=style_index(line.styles[start]);
        auto end=start+1;
        while(end<line.text.size()&&line.styles[end]==line.styles[start])++end;
        fl_font(style_font(style),font_size);
        if(line.name_begin>=start&&line.name_begin<end)
            line.name_x=xpos+fl_width(line.text.data()+start,static_cast<int>(line.name_begin-start));
        xpos+=fl_width(line.text.data()+start,static_cast<int>(end-start));
        start=end;
    }
    line.width=xpos;
    if(line.name_end>line.name_begin) {
        if(line.name_x<0)line.name_x=0;
        line.name_width=std::max(0.0,line.width-line.name_x);
    }
}
void RetroListing::draw_line(Line& line,int sy,bool selected,bool is_hover) {
    measure(line);
    double xpos=x()+1+pad_x-horizontal_->value();
    if(selected) {fl_color(FL_SELECTION_COLOR);fl_rectf(x()+1,sy,viewport_w_,line_height_);}
    for(std::size_t start=0;start<line.text.size();) {
        const auto style=style_index(line.styles[start]);
        const bool reversed=line.reversed[start]!=0;
        auto end=start+1;
        while(end<line.text.size()&&line.styles[end]==line.styles[start]&&(line.reversed[end]!=0)==reversed)++end;
        fl_font(style_font(style),font_size);
        const double width=fl_width(line.text.data()+start,static_cast<int>(end-start));
        Fl_Color foreground=selected?FL_WHITE:colors_[style];
        if(reversed) {
            fl_color(colors_[style]);fl_rectf(static_cast<int>(std::floor(xpos)),sy,static_cast<int>(std::ceil(width)),line_height_);
            foreground=background_;
        }
        fl_color(foreground);
        fl_draw(line.text.data()+start,static_cast<int>(end-start),static_cast<int>(std::lround(xpos)),sy+baseline_);
        xpos+=width;start=end;
    }
    if(is_hover&&line.name_width>0) {
        auto style=line.name_begin<line.styles.size()?style_index(line.styles[line.name_begin]):0;
        fl_color(selected?FL_WHITE:colors_[style]);
        int left=static_cast<int>(std::lround(x()+1+pad_x-horizontal_->value()+line.name_x));
        fl_line(left,sy+baseline_+2,left+static_cast<int>(std::ceil(line.name_width)),sy+baseline_+2);
    }
}
void RetroListing::draw() {
    draw_box();
    fl_push_clip(x()+1,y()+1,viewport_w_,viewport_h_);
    fl_color(background_);fl_rectf(x()+1,y()+1,viewport_w_,viewport_h_);
    const int top=vertical_->value();
    const auto lines=header_.size()+entries_.size();
    int first=std::max(0,(top-pad_y)/line_height_);
    int last=std::min(static_cast<int>(lines),first+viewport_h_/line_height_+3);
    int measured_width=content_width_;
    for(int i=first;i<last;++i) {
        int sy=y()+1+pad_y+i*line_height_-top;
        if(i<static_cast<int>(header_.size())) {
            auto& line=header_[static_cast<std::size_t>(i)];
            draw_line(line,sy,false,false);
            measured_width=std::max(measured_width,static_cast<int>(std::ceil(line.width))+2*pad_x);
        } else {
            int row=i-static_cast<int>(header_.size());auto& line=row_line(static_cast<std::size_t>(row));
            draw_line(line,sy,row==selected_,row==hovered_);
            measured_width=std::max(measured_width,static_cast<int>(std::ceil(line.width))+2*pad_x);
        }
    }
    fl_pop_clip();
    if(measured_width>content_width_) {content_width_=measured_width;scrollbars();redraw();}
    draw_children();
    // Keyboard focus remains active; the terminal panel has no dotted outline.
}
int RetroListing::hit_name(int X,int Y) {
    if(X<x()+1||X>=x()+1+viewport_w_||Y<y()+1||Y>=y()+1+viewport_h_)return -1;
    const int offset=Y-y()-1+vertical_->value()-pad_y;
    if(offset<0)return -1;
    int row=offset/line_height_-static_cast<int>(header_.size());
    if(row<0||row>=static_cast<int>(entries_.size()))return -1;
    auto& line=row_line(static_cast<std::size_t>(row));measure(line);
    const double local=X-x()-1-pad_x+horizontal_->value();
    return line.name_width>0&&local>=line.name_x&&local<line.name_x+line.name_width?row:-1;
}
void RetroListing::hover(int row) {
    if(hovered_==row)return;
    hovered_=row;
    if(window())window()->cursor(row>=0?FL_CURSOR_HAND:FL_CURSOR_DEFAULT);
    redraw();
}
void RetroListing::queue_open(int row) {
    if(row<0||row>=static_cast<int>(entries_.size())||!open)return;
    pending_open_=entries_[static_cast<std::size_t>(row)];
    Fl::remove_timeout(open_timeout,this);
    Fl::add_timeout(0,open_timeout,this);
}
void RetroListing::open_timeout(void* value) {
    auto* self=static_cast<RetroListing*>(value);
    if(!self->pending_open_)return;
    auto entry=std::move(*self->pending_open_);self->pending_open_.reset();
    auto callback=self->open;
    if(callback)callback(entry);
}
int RetroListing::handle(int event) {
    switch(event) {
    case FL_FOCUS: case FL_UNFOCUS:
        redraw();return 1;
    case FL_ENTER: case FL_MOVE:
        hover(hit_name(Fl::event_x(),Fl::event_y()));
        return Fl_Group::handle(event);
    case FL_LEAVE:
        hover(-1);return 1;
    case FL_HIDE:
        hover(-1);pressed_=-1;
        Fl::remove_timeout(open_timeout,this);pending_open_.reset();
        return Fl_Group::handle(event);
    case FL_MOUSEWHEEL:
        if(Fl::event_x()>=x()+1&&Fl::event_x()<x()+1+viewport_w_&&Fl::event_y()>=y()+1&&Fl::event_y()<y()+1+viewport_h_) {
            const int dx=Fl::event_dx(),dy=Fl::event_dy();
            if(Fl::event_state(FL_SHIFT))move_scroll(horizontal_->value()+(dx+dy)*static_cast<int>(cell_width_*3),vertical_->value());
            else move_scroll(horizontal_->value()+dx*static_cast<int>(cell_width_*3),vertical_->value()+dy*line_height_*3);
            return 1;
        }
        break;
    case FL_PUSH:
        if(Fl::event_button()==FL_LEFT_MOUSE&&Fl::event_x()<x()+1+viewport_w_&&Fl::event_y()<y()+1+viewport_h_) {
            take_focus();pressed_=hit_name(Fl::event_x(),Fl::event_y());
            if(pressed_>=0)selected_=pressed_;
            redraw();return 1;
        }
        break;
    case FL_DRAG:
        if(pressed_>=0){hover(hit_name(Fl::event_x(),Fl::event_y()));return 1;}
        break;
    case FL_RELEASE:
        if(Fl::event_button()==FL_LEFT_MOUSE&&pressed_>=0) {
            int row=pressed_;pressed_=-1;
            if(Fl::event_is_click()&&hit_name(Fl::event_x(),Fl::event_y())==row)queue_open(row);
            return 1;
        }
        break;
    case FL_KEYDOWN:
        if((Fl::event_state()&FL_CTRL)&&Fl::event_key()=='c') {
            if(auto* entry=selected_entry()) {
                // Escape URL-significant characters so pasting into the path
                // field preserves literal percent signs, question marks and '#'.
                std::string path;
                for (char c:entry->path) {
                    if(c=='%')path+="%25";
                    else if(c=='?')path+="%3F";
                    else if(c=='#')path+="%23";
                    else path+=c;
                }
                Fl::copy(path.c_str(),static_cast<int>(path.size()),1);
            }
            return 1;
        }
        if(Fl::event_state()&(FL_CTRL|FL_ALT|FL_META))return 0;
        if(Fl::event_key()==FL_Enter||Fl::event_key()==FL_KP_Enter){queue_open(selected_);return 1;}
        if(!entries_.empty()) {
            int next=selected_<0?0:selected_;
            switch(Fl::event_key()) {
            case FL_Up: if(selected_>=0)--next;break;
            case FL_Down: if(selected_>=0)++next;break;
            case FL_Home: next=0;break;
            case FL_End: next=static_cast<int>(entries_.size())-1;break;
            case FL_Page_Up: next-=std::max(1,viewport_h_/line_height_-1);break;
            case FL_Page_Down: next+=std::max(1,viewport_h_/line_height_-1);break;
            default: return Fl_Group::handle(event);
            }
            selected_=std::clamp(next,0,static_cast<int>(entries_.size())-1);
            ensure_visible(selected_);redraw();return 1;
        }
        break;
    }
    return Fl_Group::handle(event);
}
} // namespace cpv
