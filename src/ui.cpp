// SPDX-License-Identifier: MIT
// Copyright (C) 2026 davoda1
#include "ui.hpp"
#include "core.hpp"
#include "preview.hpp"
#include "document.hpp"
#include "rtf.hpp"
#include "csv.hpp"
#include "text_encoding.hpp"
#include "retro.hpp"
#include "platform.hpp"
#include "assets.hpp"
#include <FL/Fl.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Return_Button.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Secret_Input.H>
#include <FL/Fl_Table_Row.H>
#include <FL/Fl_Scrollbar.H>
#include <FL/Fl_Text_Display.H>
#include <FL/Fl_Text_Buffer.H>
#include <FL/Fl_Help_View.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Native_File_Chooser.H>
#include <FL/Fl_PNG_Image.H>
#include <FL/Fl_SVG_Image.H>
#include <FL/Fl_Tooltip.H>
#include <FL/fl_draw.H>
#include <FL/fl_ask.H>
#include <FL/filename.H>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <sstream>
#include <thread>
#include <utility>
#ifdef _WIN32
# ifndef NOMINMAX
#  define NOMINMAX
# endif
# include <windows.h>
#else
# include <dlfcn.h>
#endif

namespace cpv {
namespace {
Fl_Color accent = fl_rgb_color(35, 105, 168);
Fl_Color muted = fl_rgb_color(100, 107, 115);
Fl_Color surface = FL_WHITE;
bool dark_mode = false;
constexpr std::size_t formatted_render_limit = 25u * 1024u * 1024u;
constexpr std::size_t text_input_limit = 8u * 1024u * 1024u;
const std::array<const char*,8> encoding_ids={"auto","utf8","utf16le","utf16be","utf32le","utf32be","cp1250","cp1252"};

struct DecodedPreview { PreparedText source; std::string encoding; bool supported=false,truncated=false; };
std::size_t preview_input_limit(const std::string& name,const std::string& content_type) {
    return document::markdown_supported(name,content_type)||document::csv_supported(name,content_type)||
        document::rtf_supported(name,content_type)?formatted_render_limit:text_input_limit;
}
DecodedPreview decode_preview(const std::vector<unsigned char>& bytes,const std::string& name,
        const std::string& content_type,const std::string& choice="auto") {
    const auto byte_limit=preview_input_limit(name,content_type);
    auto decoded=decode_text(bytes,choice,content_type,byte_limit);
    std::vector<unsigned char> utf8(decoded.text.begin(),decoded.text.end());
    DecodedPreview result;result.encoding=std::move(decoded.encoding);
    result.truncated=decoded.truncated||decoded.text.size()>byte_limit;
    result.supported=text_supported(name,content_type,utf8,true);
    result.source=prepare_styled_text(utf8,byte_limit);
    const std::string truncation="[Preview truncated at "+std::to_string(byte_limit/(1024u*1024u))+" MiB.";
    if(decoded.truncated&&result.source.text.find(truncation)==std::string::npos) {
        const std::string note="\n\n"+truncation+" Download the file to read it in full.]\n";
        result.source.text+=note;result.source.styles.append(note.size(),'A');
    }
    return result;
}

Fl_Color document_color(const std::string& hex) {
    unsigned r=0,g=0,b=0;
    if(hex.size()==7 && hex[0]=='#' && std::sscanf(hex.c_str()+1,"%2x%2x%2x",&r,&g,&b)==3)
        return fl_rgb_color(static_cast<uchar>(r),static_cast<uchar>(g),static_cast<uchar>(b));
    return FL_FOREGROUND_COLOR;
}

document::RenderedDocument make_markdown(const std::string& text,const std::string& theme,bool dark) {
    return document::render_document(text,theme,dark,dark?"#e9ebee":"#1f252c",
        dark?"#1e2023":"#ffffff",dark?"#6eb1ec":"#2369a8");
}

document::RenderedDocument make_csv(const std::string& text,bool dark) {
    return document::render_csv(text,dark?"#e9ebee":"#1f252c",
        dark?"#1e2023":"#ffffff",dark?"#626972":"#b9c0c8");
}

void no_focus_outline(Fl_Boxtype,int,int,int,int,Fl_Color,Fl_Color) {}

void configure_focus_drawing() {
    // Keep FLTK's keyboard-focus behavior enabled. Disabling visible_focus()
    // also makes standard buttons reject focus; change only their painting.
    for(Fl_Boxtype box : {FL_NO_BOX,FL_FLAT_BOX,FL_UP_BOX,FL_DOWN_BOX,
            FL_UP_FRAME,FL_DOWN_FRAME,FL_THIN_UP_BOX,FL_THIN_DOWN_BOX,
            FL_THIN_UP_FRAME,FL_THIN_DOWN_FRAME,FL_BORDER_BOX,FL_BORDER_FRAME}) {
        Fl::set_boxtype(box,Fl::get_boxtype(box),
            static_cast<uchar>(Fl::box_dx(box)),static_cast<uchar>(Fl::box_dy(box)),
            static_cast<uchar>(Fl::box_dw(box)),static_cast<uchar>(Fl::box_dh(box)),
            no_focus_outline);
    }
}

std::string lower_ascii(std::string s) {
    for (auto& c : s) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 'a' - 'A');
    return s;
}
std::string display_path(const std::string& s) {
    std::string out;out.reserve(s.size());
    for(char c:s){if(c=='%')out+="%25";else if(c=='?')out+="%3F";else if(c=='#')out+="%23";else out+=c;}
    return out;
}

// Optional native desktop setting; there is no shell invocation or launcher.
bool desktop_dark() {
#ifdef _WIN32
    DWORD value = 1, size = sizeof(value);
    return RegGetValueW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size) == ERROR_SUCCESS && !value;
#else
    static void* gio = dlopen("libgio-2.0.so.0", RTLD_LAZY | RTLD_LOCAL);
    static void* glib = dlopen("libglib-2.0.so.0", RTLD_LAZY | RTLD_LOCAL);
    static void* obj = dlopen("libgobject-2.0.so.0", RTLD_LAZY | RTLD_LOCAL);
    if (gio && glib && obj) {
        using Ptr0 = void* (*)(); using Lookup = void* (*)(void*, const char*, int);
        using HasKey = int (*)(void*, const char*); using New = void* (*)(void*, void*, const char*);
        using Get = char* (*)(void*, const char*); using Free = void (*)(void*);
        auto source = reinterpret_cast<Ptr0>(dlsym(gio, "g_settings_schema_source_get_default"));
        auto lookup = reinterpret_cast<Lookup>(dlsym(gio, "g_settings_schema_source_lookup"));
        auto has_key = reinterpret_cast<HasKey>(dlsym(gio, "g_settings_schema_has_key"));
        auto create = reinterpret_cast<New>(dlsym(gio, "g_settings_new_full"));
        auto get = reinterpret_cast<Get>(dlsym(gio, "g_settings_get_string"));
        auto free_mem = reinterpret_cast<Free>(dlsym(glib, "g_free"));
        auto unref = reinterpret_cast<Free>(dlsym(obj, "g_object_unref"));
        auto unref_schema = reinterpret_cast<Free>(dlsym(gio, "g_settings_schema_unref"));
        if (source && lookup && has_key && create && get && free_mem && unref && unref_schema) {
            void* src = source();
            void* schema = src ? lookup(src, "org.gnome.desktop.interface", 1) : nullptr;
            if (schema) {
                void* settings = create(schema, nullptr, nullptr);
                bool found = false;
                if (settings) {
                    for (const char* key : {"color-scheme", "gtk-theme"}) {
                        if (!has_key(schema, key)) continue;
                        char* value = get(settings, key);
                        if (value) { found = found || lower_ascii(value).find("dark") != std::string::npos; free_mem(value); }
                    }
                    unref(settings);
                }
                unref_schema(schema);
                if (found) return true;
            }
        }
    }
    if (const char* theme = std::getenv("GTK_THEME"))
        if (lower_ascii(theme).find("dark") != std::string::npos) return true;
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    const char* home_dir = std::getenv("HOME");
    std::filesystem::path dir = xdg && *xdg ? std::filesystem::path(xdg) :
        std::filesystem::path(home_dir ? home_dir : ".") / ".config";
    std::ifstream in(dir / "gtk-3.0/settings.ini");
    std::string line;
    while (std::getline(in, line)) {
        line = lower_ascii(line);
        if (line.find("gtk-application-prefer-dark-theme") != std::string::npos &&
            (line.find("true") != std::string::npos || line.find("=1") != std::string::npos || line.find("= 1") != std::string::npos)) return true;
        if (line.find("gtk-theme-name") != std::string::npos && line.find("dark") != std::string::npos) return true;
    }
    return false;
#endif
}

void set_palette(bool dark) {
    dark_mode = dark;
    if (dark) {
        Fl::background(42, 44, 48); Fl::background2(30, 32, 35); Fl::foreground(233, 235, 238);
        accent = fl_rgb_color(110, 177, 236); muted = fl_rgb_color(168, 174, 182);
        surface = fl_rgb_color(30, 32, 35);
    } else {
        Fl::background(239, 241, 244); Fl::background2(255, 255, 255); Fl::foreground(31, 37, 44);
        accent = fl_rgb_color(35, 105, 168); muted = fl_rgb_color(100, 107, 115); surface = FL_WHITE;
    }
    Fl::set_color(FL_SELECTION_COLOR, 47, 113, 171);
    Fl_Tooltip::color(dark ? fl_rgb_color(64, 66, 70) : fl_rgb_color(255, 255, 229));
    Fl_Tooltip::textcolor(FL_FOREGROUND_COLOR);
}

Fl_Color listing_size_color(std::uint64_t size) {
    switch(size_color_index(size)) {
        case 6:return dark_mode?fl_rgb_color(122,214,219):fl_rgb_color(15,117,128);
        case 5:return dark_mode?fl_rgb_color(221,151,241):fl_rgb_color(142,51,155);
        case 1:return dark_mode?fl_rgb_color(255,134,134):fl_rgb_color(170,39,39);
        case 3:return dark_mode?fl_rgb_color(242,214,121):fl_rgb_color(139,102,8);
        default:return FL_FOREGROUND_COLOR;
    }
}

Geometry on_screen(Geometry g, int min_w, int min_h) {
    int sx = 0, sy = 0, sw = 1280, sh = 800;
    int screen = (g.x != -1 || g.y != -1) ? Fl::screen_num(g.x + g.w / 2, g.y + g.h / 2) : 0;
    Fl::screen_work_area(sx, sy, sw, sh, screen);
    g.w = std::max(std::min(min_w, sw), std::min(g.w, sw));
    g.h = std::max(std::min(min_h, sh), std::min(g.h, sh));
    if (g.x == -1 && g.y == -1) { g.x = sx + (sw - g.w) / 2; g.y = sy + (sh - g.h) / 2; }
    g.x = std::max(sx, std::min(g.x, sx + sw - g.w));
    g.y = std::max(sy, std::min(g.y, sy + sh - g.h));
    return g;
}

class ProgressIndicator : public Fl_Widget {
public:
    bool running = false;
    int phase = 0;
    ProgressIndicator(int x, int y, int w, int h) : Fl_Widget(x,y,w,h) {}
    void draw() override {
        fl_color(FL_BACKGROUND_COLOR); fl_rectf(x(), y(), w(), h());
        if (!running) return;
        const double pi = 3.141592653589793;
        const double cx = x() + w()/2.0, cy = y() + h()/2.0;
        fl_line_style(FL_SOLID, 2);
        for (int i=0; i<12; ++i) {
            const int age = (i - phase + 12) % 12;
            fl_color(fl_color_average(accent, FL_BACKGROUND_COLOR, 0.18f + 0.82f * float(age)/11.f));
            double a = 2*pi*i/12;
            fl_line(int(cx+5*std::cos(a)), int(cy+5*std::sin(a)), int(cx+9*std::cos(a)), int(cy+9*std::sin(a)));
        }
        fl_line_style(0);
    }
};

class TransferBar : public Fl_Widget {
public:
    std::uint64_t received = 0, total = 0;
    TransferBar(int x, int y, int w, int h) : Fl_Widget(x,y,w,h) {}
    void draw() override {
        fl_color(FL_BACKGROUND_COLOR); fl_rectf(x(),y(),w(),h());
        if (!total) return;
        fl_color(fl_color_average(muted, FL_BACKGROUND_COLOR, .25f)); fl_rectf(x(),y()+h()/2-2,w(),4);
        double ratio = std::min(1.0, double(received)/double(total));
        fl_color(accent); fl_rectf(x(),y()+h()/2-2,int(w()*ratio),4);
    }
};

class AnimatedToggle : public Fl_Widget {
    bool value_ = false;
    double position_ = 0, start_position_ = 0;
    std::chrono::steady_clock::time_point started_;
    std::string off_label_, on_label_;
    static void animate(void* data) {
        auto* self=static_cast<AnimatedToggle*>(data);
        const double elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-self->started_).count();
        const double t=std::min(1.0,elapsed/.16), eased=t*t*(3-2*t);
        self->position_=self->start_position_+((self->value_?1.0:0.0)-self->start_position_)*eased;
        self->redraw();
        if(t<1.0)Fl::repeat_timeout(1.0/60.0,animate,self);
    }
    void change(bool value) {
        if(value==value_)return;
        Fl::remove_timeout(animate,this);value_=value;start_position_=position_;
        started_=std::chrono::steady_clock::now();Fl::add_timeout(0,animate,this);do_callback();
    }
public:
    AnimatedToggle(int x,int y,int w,int h,const char* off,const char* on)
        : Fl_Widget(x,y,w,h),off_label_(off),on_label_(on) {visible_focus(1);}
    ~AnimatedToggle() override {Fl::remove_timeout(animate,this);}
    bool value() const {return value_;}
    void value(bool value) {Fl::remove_timeout(animate,this);value_=value;position_=value?1:0;redraw();}
    int handle(int event) override {
        if(event==FL_FOCUS||event==FL_UNFOCUS){redraw();return 1;}
        if(event==FL_PUSH&&Fl::event_button()==FL_LEFT_MOUSE){take_focus();return 1;}
        if(event==FL_RELEASE&&Fl::event_button()==FL_LEFT_MOUSE){if(Fl::event_inside(this))change(!value_);return 1;}
        if(event==FL_KEYDOWN){
            int key=Fl::event_key();
            if(key==' '||key==FL_Enter||key==FL_KP_Enter){change(!value_);return 1;}
            if(key==FL_Left||key==FL_Right){change(key==FL_Right);return 1;}
        }
        return Fl_Widget::handle(event);
    }
    void draw() override {
        fl_push_clip(x(),y(),w(),h());fl_color(FL_BACKGROUND_COLOR);fl_rectf(x(),y(),w(),h());
        int tx=x()+78,ty=y()+(h()-24)/2,tw=54;
        fl_color(fl_color_average(accent,fl_color_average(muted,FL_BACKGROUND_COLOR,.55f),float(position_)));
        fl_pie(tx,ty,24,24,0,360);fl_rectf(tx+12,ty,tw-24,24);fl_pie(tx+tw-24,ty,24,24,0,360);
        fl_color(FL_WHITE);fl_pie(tx+3+int(position_*(tw-24)),ty+3,18,18,0,360);
        fl_color(value_?muted:FL_FOREGROUND_COLOR);fl_font(value_?FL_HELVETICA:FL_HELVETICA_BOLD,14);
        fl_draw(off_label_.c_str(),x()+4,y(),66,h(),FL_ALIGN_RIGHT);
        fl_color(value_?FL_FOREGROUND_COLOR:muted);fl_font(value_?FL_HELVETICA_BOLD:FL_HELVETICA,14);
        fl_draw(on_label_.c_str(),tx+tw+10,y(),w()-142,h(),FL_ALIGN_LEFT);
        fl_pop_clip();
    }
};

class ImageCanvas : public Fl_Widget {
    ImageData data_;
    std::unique_ptr<Fl_RGB_Image> image_;
public:
    ImageCanvas(int x,int y,int w,int h) : Fl_Widget(x,y,w,h) {}
    void clear() { image_.reset(); data_ = {}; redraw(); }
    void set(ImageData data) {
        clear(); data_ = std::move(data);
        image_ = std::make_unique<Fl_RGB_Image>(data_.pixels.data(),data_.width,data_.height,data_.channels);
        redraw();
    }
    void draw() override {
        fl_push_clip(x(),y(),w(),h());
        fl_color(surface); fl_rectf(x(),y(),w(),h());
        if (image_) {
            double scale = std::min(1.0,std::min(double(w()-24)/data_.width,double(h()-24)/data_.height));
            int iw = std::max(1,int(data_.width*scale)), ih = std::max(1,int(data_.height*scale));
            int ix = x()+(w()-iw)/2, iy=y()+(h()-ih)/2;
            if (data_.channels == 4 || data_.channels == 2) {
                fl_push_clip(ix,iy,iw,ih);
                for(int yy=iy; yy<iy+ih; yy+=12) for(int xx=ix; xx<ix+iw; xx+=12) {
                    fl_color(((xx-ix)/12+(yy-iy)/12)%2 ? fl_rgb_color(216,218,220) : fl_rgb_color(244,245,246));
                    fl_rectf(xx,yy,12,12);
                }
                fl_pop_clip();
            }
            image_->scale(iw,ih,0,1); image_->draw(ix,iy);
        }
        fl_pop_clip();
    }
};

class FileTable : public Fl_Table_Row {
public:
    static constexpr int row_font_size=17;
    static constexpr int header_font_size=16;
    static constexpr int row_height_pixels=34;
    std::vector<Entry> entries;
    bool show_directory_sizes=true;
    int sort_column = 0;
    bool descending = false;
    std::function<void(int)> open;
    std::function<void(int)> sort;
    FileTable(int x,int y,int w,int h) : Fl_Table_Row(x,y,w,h) {
        cols(3); col_header(1); col_header_height(32); col_resize(0); row_header(0);
        row_height_all(row_height_pixels); type(SELECT_SINGLE); when(FL_WHEN_RELEASE); box(FL_BORDER_FRAME);
        callback([](Fl_Widget* widget, void*) {
            auto* t = static_cast<FileTable*>(widget);
            if (Fl::event() != FL_RELEASE || Fl::event_button() != FL_LEFT_MOUSE || Fl::event_is_click()==0) return;
            if (t->callback_context()==CONTEXT_COL_HEADER) { if(t->sort) t->sort(t->callback_col()); }
            else if(t->callback_context()==CONTEXT_CELL && t->callback_col()==0 && t->callback_row()>=0) {
                int row=t->callback_row();
                // Defer mutation until FLTK finishes dispatching this table event.
                auto* call=new std::pair<FileTable*,int>(t,row);
                Fl::add_timeout(0,[](void* v) {
                    std::unique_ptr<std::pair<FileTable*,int>> p(static_cast<std::pair<FileTable*,int>*>(v));
                    if(p->first->open) p->first->open(p->second);
                },call);
            }
        });
        end(); adjust_columns();
    }
    void adjust_columns() {
        // Account for an actual scrollbar, not a permanently reserved strip.
        int available=w()-Fl::box_dw(box())-Fl::box_dw(table_box());
        int viewport_h=h()-Fl::box_dh(box())-Fl::box_dh(table_box())-col_header_height();
        if(table_h>viewport_h)available-=scrollbar_size()?scrollbar_size():Fl::scrollbar_size();
        const int size_width=112,modified_width=204,name_width=std::max(1,available-size_width-modified_width);
        if(col_width(0)!=name_width)col_width(0,name_width);
        if(col_width(1)!=size_width)col_width(1,size_width);
        if(col_width(2)!=modified_width)col_width(2,modified_width);
        if(hscrollbar->value()!=0){hscrollbar->value(0);table_scrolled();}
    }
    void resize(int x,int y,int w,int h) override { Fl_Table_Row::resize(x,y,w,h); adjust_columns(); }
    void draw() override {adjust_columns();Fl_Table_Row::draw();}
    int selected() { for(int i=0;i<rows();++i) if(row_selected(i)) return i; return -1; }
    int handle(int event) override {
        if(event==FL_KEYDOWN) {
            if(Fl::event_key()==FL_Enter || Fl::event_key()==FL_KP_Enter) { int r=selected(); if(r>=0&&open)open(r); return 1; }
            if(Fl::event_key()==FL_Up || Fl::event_key()==FL_Down || Fl::event_key()==FL_Home || Fl::event_key()==FL_End) {
                int r=selected(); if(Fl::event_key()==FL_Home)r=0; else if(Fl::event_key()==FL_End)r=rows()-1;
                else r+=Fl::event_key()==FL_Up?-1:1;
                r=std::max(0,std::min(r,rows()-1)); select_all_rows(0);
                if(rows()) {select_row(r); if(r<row_position()||r>row_position()+std::max(1,(h()-col_header_height())/row_height_pixels-1))row_position(r);}
                redraw(); return 1;
            }
        }
        return Fl_Table_Row::handle(event);
    }
    void draw_cell(TableContext context,int row,int col,int X,int Y,int W,int H) override {
        if(context==CONTEXT_STARTPAGE) { fl_font(FL_HELVETICA,row_font_size); return; }
        if(context==CONTEXT_COL_HEADER) {
            fl_push_clip(X,Y,W,H); fl_color(FL_BACKGROUND_COLOR); fl_rectf(X,Y,W,H);
            fl_color(fl_color_average(muted,FL_BACKGROUND_COLOR,.35f)); fl_line(X,Y+H-1,X+W,Y+H-1);
            const char* labels[]={"Name","Size","Modified"};
            std::string label=labels[col]; if(col==sort_column)label+=descending?"  v":"  ^";
            fl_color(FL_FOREGROUND_COLOR); fl_font(FL_HELVETICA_BOLD,header_font_size);
            fl_draw(label.c_str(),X+10,Y,W-20,H,FL_ALIGN_LEFT); fl_pop_clip(); return;
        }
        if(context!=CONTEXT_CELL || row<0 || row>=int(entries.size()))return;
        const auto& e=entries[std::size_t(row)]; bool selected_row=row_selected(row)!=0;
        fl_push_clip(X,Y,W,H);
        Fl_Color bg=selected_row?FL_SELECTION_COLOR:surface;
        if(!selected_row && row%2)bg=fl_color_average(FL_BACKGROUND_COLOR,surface,.22f);
        fl_color(bg); fl_rectf(X,Y,W,H);
        Fl_Color fg=selected_row?FL_WHITE:(col==0?accent:FL_FOREGROUND_COLOR);
        fl_color(fg); fl_font(e.directory && col==0?FL_HELVETICA_BOLD:FL_HELVETICA,row_font_size);
        std::string value;
        if(col==0) {
            const int icon_y=Y+(H-16)/2;
            if(e.directory) {
                fl_color(selected_row?FL_WHITE:fl_rgb_color(190,143,36));
                fl_rectf(X+12,icon_y+2,7,3); fl_rectf(X+10,icon_y+4,16,10);
            } else {
                fl_color(selected_row?FL_WHITE:muted); fl_rect(X+13,icon_y,11,15); fl_line(X+16,icon_y+4,X+21,icon_y+4); fl_line(X+16,icon_y+8,X+21,icon_y+8);
            }
            fl_color(fg); value=e.name; fl_draw(value.c_str(),X+35,Y,W-44,H,FL_ALIGN_LEFT);
        } else {
            bool size_visible=e.size_known&&(!e.directory||show_directory_sizes);
            if(col==1&&size_visible&&!selected_row)fl_color(listing_size_color(e.size));
            value=col==1?(size_visible?human_size(e.size):"—"):format_time(e.modified);
            fl_draw(value.c_str(),X+10,Y,W-20,H,col==1?FL_ALIGN_RIGHT:FL_ALIGN_LEFT);
        }
        fl_pop_clip();
    }
};

class App;
class AppWindow : public Fl_Double_Window {
public:
    App* app = nullptr;
    AppWindow(int x,int y,int w,int h,const char* title) : Fl_Double_Window(x,y,w,h,title) {}
    void resize(int x,int y,int w,int h) override;
    int handle(int event) override;
};
class SettingsWindow : public Fl_Double_Window {
public:
    std::function<void()> layout;
    SettingsWindow(int x,int y,int w,int h,const char* title):Fl_Double_Window(x,y,w,h,title){}
    void resize(int x,int y,int w,int h) override {Fl_Double_Window::resize(x,y,w,h);if(layout)layout();}
};

struct Location { std::string path; FetchKind kind = FetchKind::Auto; int scroll=0; std::string filter; };
struct Job {
    enum class Type { Fetch, Download } type=Type::Fetch;
    Config snapshot;
    Location location;
    std::filesystem::path destination;
    unsigned generation=0;
    TransferControl progress;
    std::atomic<bool> done{false};
    std::thread worker;
    std::string error;
    bool cancelled=false, is_listing=false, is_image=false, is_text=false;
    bool is_markdown=false, is_rtf=false, is_csv=false, renderable_document=false, dark=false;
    Listing listing;
    ImageData image;
    std::string text, styles, detail, language, raw_rtf, text_encoding, content_type;
    std::vector<unsigned char> raw_text;
    std::size_t text_size=0;
    document::RenderedDocument rendered_document;
    ~Job() { if(worker.joinable())worker.join(); }
};

class App {
public:
    Config config;
    std::filesystem::path config_path;
    std::unique_ptr<AppWindow> window;
    Fl_Input *path=nullptr, *username=nullptr, *filter=nullptr;
    Fl_Secret_Input* password=nullptr;
    Fl_Button *back=nullptr,*forward=nullptr,*up=nullptr,*refresh=nullptr,*fetch_button=nullptr,*settings_button=nullptr;
    Fl_Button *save_button=nullptr,*browser_button=nullptr,*cancel_button=nullptr,*source_button=nullptr;
    Fl_Box *path_label=nullptr,*user_label=nullptr,*pass_label=nullptr,*filter_label=nullptr,*status=nullptr,*file_title=nullptr,*message=nullptr;
    FileTable* table=nullptr;
    RetroListing* retro=nullptr;
    ImageCanvas* image_canvas=nullptr;
    Fl_Text_Display* text_display=nullptr;
    Fl_Help_View* document_view=nullptr;
    Fl_Text_Buffer text_buffer,style_buffer;
    std::array<Fl_Text_Display::Style_Table_Entry,18> text_styles{};
    std::array<Fl_Text_Display::Style_Table_Entry,11> code_styles{};
    ProgressIndicator* spinner=nullptr;
    TransferBar* transfer_bar=nullptr;
    std::unique_ptr<Fl_PNG_Image> app_icon;
    std::unique_ptr<Fl_SVG_Image> gear;
    std::unique_ptr<SettingsWindow> settings;
    Fl_Input *settings_url=nullptr,*settings_user=nullptr;
    Fl_Secret_Input* settings_pass=nullptr;
    Fl_Box *settings_error=nullptr,*settings_note=nullptr;
    Fl_Button *settings_save=nullptr,*settings_cancel=nullptr;
    AnimatedToggle *settings_modern=nullptr,*settings_sizes=nullptr;
    Fl_Choice* settings_code_theme=nullptr;
    Fl_Choice* encoding_choice=nullptr;
    std::vector<std::shared_ptr<Job>> jobs;
    std::shared_ptr<Job> pending_fetch;
    std::vector<Location> history;
    int history_index=-1;
    unsigned generation=0;
    bool closing=false, file_view=false, directory_view=false;
    bool preview_text_available=false, preview_rtf=false, preview_csv=false, preview_markdown=false, preview_renderable=false, source_mode=false;
    std::string preview_language, preview_text, preview_styles, preview_raw_rtf, preview_encoding, preview_content_type, queued_link, pending_anchor;
    std::vector<unsigned char> preview_raw_text;
    std::size_t preview_size=0;
    int preview_encoding_index=0;
    document::RenderedDocument preview_document;
    std::string preview_document_theme;
    bool preview_document_dark=false;
    Listing directory;
    std::string current_path="/", idle_status="Ready", startup_warning;
    int tick_count=0;
    std::chrono::steady_clock::time_point geometry_changed=std::chrono::steady_clock::now();

    App() {
        config_path=settings_path(); config=load_config(config_path,startup_warning);
        Fl::scheme("gtk+"); Fl::visible_focus(1); Fl::option(Fl::OPTION_ARROW_FOCUS,1);
        Fl::option(Fl::OPTION_FNFC_USES_ZENITY,0); Fl::option(Fl::OPTION_FNFC_USES_KDIALOG,0);
        Fl::option(Fl::OPTION_FNFC_USES_GTK,0);
        Fl_Tooltip::delay(.5f); Fl_Tooltip::size(13); Fl_Image::RGB_scaling(FL_RGB_SCALING_BILINEAR);
        set_palette(desktop_dark());
        config.main_window=on_screen(config.main_window,880,420);
        auto g=config.main_window;
        window=std::make_unique<AppWindow>(g.x,g.y,g.w,g.h,"Copyparty Viewer");
        window->app=this; window->size_range(880,420); window->xclass("CopypartyViewer");
        app_icon=std::make_unique<Fl_PNG_Image>(nullptr,app_png,int(app_png_size));
        if(app_icon->w()>0)window->icon(app_icon.get());
        window->begin();
        path_label=new Fl_Box(12,12,36,30,"Path");
        path=new Fl_Input(52,12,350,30); path->value(display_path(config.last_path).c_str()); path->tooltip("Server path or a full URL on the configured server · Ctrl+L");
        user_label=new Fl_Box(410,12,38,30,"User"); username=new Fl_Input(451,12,130,30); username->value(config.username.c_str());
        pass_label=new Fl_Box(590,12,65,30,"Password"); password=new Fl_Secret_Input(658,12,130,30); password->value(config.password.c_str());
        username->tooltip("Session username · saved credentials can be changed in Settings");
        password->tooltip("Session password · kept in memory unless entered and saved in Settings");
        fetch_button=new Fl_Button(800,12,76,30,"Fetch");
        back=new Fl_Button(12,54,66,28,"Back"); forward=new Fl_Button(84,54,76,28,"Forward");
        up=new Fl_Button(166,54,52,28,"Up"); refresh=new Fl_Button(224,54,74,28,"Refresh");
        back->tooltip("Back · Alt+Left"); forward->tooltip("Forward · Alt+Right"); up->tooltip("Parent directory · Alt+Up"); refresh->tooltip("Refresh · F5");
        filter_label=new Fl_Box(480,54,80,28,"Filter files"); filter=new Fl_Input(562,54,245,28); filter->tooltip("Filter the current directory · Ctrl+F");
        settings_button=new Fl_Button(837,51,36,34);
        gear=std::make_unique<Fl_SVG_Image>(nullptr,gear_svg,gear_svg_size); gear->resize(22,22); settings_button->image(gear.get()); settings_button->tooltip("Settings");
        table=new FileTable(12,94,856,540);
        retro=new RetroListing(12,94,856,540);
        file_title=new Fl_Box(24,98,500,34); file_title->align(FL_ALIGN_LEFT|FL_ALIGN_INSIDE); file_title->labelfont(FL_HELVETICA_BOLD);
        save_button=new Fl_Button(630,100,100,28,"Save As…"); browser_button=new Fl_Button(738,100,118,28,"Open in browser");
        source_button=new Fl_Button(534,100,88,28,"Source");
        source_button->tooltip("Switch between the rendered document and its source text");
        encoding_choice=new Fl_Choice(366,100,160,28);
        encoding_choice->add("Auto encoding|UTF-8|UTF-16 LE|UTF-16 BE|UTF-32 LE|UTF-32 BE|CP1250|CP1252");
        encoding_choice->value(0);encoding_choice->textsize(13);
        encoding_choice->tooltip("Text encoding. Auto checks Unicode and the server's charset, then uses CP1250. Select CP1252 when needed.");
        save_button->tooltip("Download this file · Ctrl+S"); browser_button->tooltip("Open the current server URL in your default browser");
        text_display=new Fl_Text_Display(12,142,856,492); text_display->buffer(text_buffer); text_display->textfont(FL_COURIER); text_display->textsize(14); text_display->box(FL_BORDER_BOX);
        text_display->highlight_data(&style_buffer,text_styles.data(),int(text_styles.size()),0,nullptr,nullptr);
        document_view=new Fl_Help_View(12,142,856,492);document_view->box(FL_BORDER_BOX);document_view->textsize(15);
        document_view->user_data(this);
        // Generated links are opaque indices. Never let FLTK load a URL or a
        // local file directly, and defer navigation until its event returns.
        document_view->link([](Fl_Widget*w,const char* target)->const char*{
            static_cast<App*>(w->user_data())->queue_document_link(target?target:"");return nullptr;
        });
        image_canvas=new ImageCanvas(12,142,856,492);
        message=new Fl_Box(32,150,816,450); message->align(FL_ALIGN_CENTER|FL_ALIGN_INSIDE|FL_ALIGN_WRAP); message->labelsize(15);
        spinner=new ProgressIndicator(10,650,26,28); status=new Fl_Box(42,650,560,28); status->align(FL_ALIGN_LEFT|FL_ALIGN_INSIDE); status->labelsize(13);
        transfer_bar=new TransferBar(615,650,140,28); cancel_button=new Fl_Button(776,650,92,28,"Cancel");
        window->end(); window->resizable(table);
        for(Fl_Widget* w : std::vector<Fl_Widget*>{path_label,user_label,pass_label,filter_label}) { w->align(FL_ALIGN_LEFT|FL_ALIGN_INSIDE); w->labelsize(13); }
        for(Fl_Input* i : std::vector<Fl_Input*>{path,username,password,filter}) {i->textsize(14); i->labelsize(13);}
        for(Fl_Widget* w : std::vector<Fl_Widget*>{back,forward,up,refresh,fetch_button,save_button,browser_button,cancel_button,source_button})w->labelsize(13);
        path->when(FL_WHEN_ENTER_KEY_ALWAYS); username->when(FL_WHEN_ENTER_KEY_ALWAYS); password->when(FL_WHEN_ENTER_KEY_ALWAYS);
        auto fetch_cb=[](Fl_Widget*,void* v){static_cast<App*>(v)->fetch_from_path();};
        path->callback(fetch_cb,this); username->callback(fetch_cb,this); password->callback(fetch_cb,this); fetch_button->callback(fetch_cb,this);
        back->callback([](Fl_Widget*,void* v){static_cast<App*>(v)->travel(-1);},this);
        forward->callback([](Fl_Widget*,void* v){static_cast<App*>(v)->travel(1);},this);
        up->callback([](Fl_Widget*,void* v){auto*a=static_cast<App*>(v); a->navigate(parent_path(a->current_path),FetchKind::Listing);},this);
        refresh->callback([](Fl_Widget*,void* v){static_cast<App*>(v)->reload();},this);
        settings_button->callback([](Fl_Widget*,void* v){static_cast<App*>(v)->open_settings();},this);
        save_button->callback([](Fl_Widget*,void* v){static_cast<App*>(v)->save_as();},this);
        browser_button->callback([](Fl_Widget*,void* v){static_cast<App*>(v)->open_browser();},this);
        source_button->callback([](Fl_Widget*,void*v){auto*a=static_cast<App*>(v);a->source_mode=!a->source_mode;a->show_text_preview();},this);
        encoding_choice->callback([](Fl_Widget*,void*v){static_cast<App*>(v)->change_encoding();},this);
        cancel_button->callback([](Fl_Widget*,void* v){static_cast<App*>(v)->cancel_current();},this);
        filter->when(FL_WHEN_CHANGED); filter->callback([](Fl_Widget*,void* v){static_cast<App*>(v)->filter_table();},this);
        window->callback([](Fl_Widget*,void* v){static_cast<App*>(v)->close();},this);
        table->open=[this](int r){if(r>=0&&r<int(table->entries.size())){auto e=table->entries[std::size_t(r)]; navigate(e.path,e.directory?FetchKind::Listing:FetchKind::File);}};
        retro->open=[this](const Entry&e){navigate(e.path,e.directory?FetchKind::Listing:FetchKind::File);};
        table->sort=[this](int col){if(table->sort_column==col)table->descending=!table->descending;else{table->sort_column=col;table->descending=false;}filter_table();};
        hide_content(); cancel_button->hide(); palette_widgets(); layout(); update_navigation();
    }
    ~App() {
        Fl::remove_timeout(timer_cb,this);
        Fl::remove_timeout(document_link_cb,this);
        for(auto&j:jobs)j->progress.cancel=true;
        for(auto&j:jobs)if(j->worker.joinable())j->worker.join();
        // Widgets referencing images/buffers must be destroyed before their owners.
        settings.reset(); window.reset();
    }
    void palette_widgets() {
        std::array<Fl_Color,8> colors=dark_mode?
            std::array<Fl_Color,8>{fl_rgb_color(187,193,199),fl_rgb_color(255,134,134),fl_rgb_color(133,212,153),fl_rgb_color(242,214,121),fl_rgb_color(125,180,247),fl_rgb_color(221,151,241),fl_rgb_color(122,214,219),FL_WHITE}:
            std::array<Fl_Color,8>{fl_rgb_color(35,40,46),fl_rgb_color(170,39,39),fl_rgb_color(24,120,55),fl_rgb_color(139,102,8),fl_rgb_color(35,87,173),fl_rgb_color(142,51,155),fl_rgb_color(15,117,128),fl_rgb_color(105,111,119)};
        text_styles[0]={FL_FOREGROUND_COLOR,FL_COURIER,14,0,0}; text_styles[1]={FL_FOREGROUND_COLOR,FL_COURIER_BOLD,14,0,0};
        for(int i=0;i<8;++i){text_styles[std::size_t(i+2)]={colors[std::size_t(i)],FL_COURIER,14,0,0};text_styles[std::size_t(i+10)]={colors[std::size_t(i)],FL_COURIER_BOLD,14,0,0};}
        status->labelcolor(muted); filter_label->labelcolor(muted); text_display->color(surface); text_display->textcolor(FL_FOREGROUND_COLOR); text_display->cursor_color(accent);
        document_view->color(surface);document_view->textcolor(FL_FOREGROUND_COLOR);
        const auto p=document::palette(config.code_theme,dark_mode);
        const std::array<std::string,11> tokens={p.foreground,p.comment,p.keyword,p.string,p.number,p.title,p.type,p.attribute,p.variable,p.meta,p.op};
        for(std::size_t i=0;i<tokens.size();++i)code_styles[i]={document_color(tokens[i]),i==1?FL_COURIER_ITALIC:FL_COURIER,14,0,0};
        if(preview_text_available)show_text_preview();
        table->color(surface);retro->palette(dark_mode);
        apply_titlebar_theme(*window,dark_mode);window->redraw();
        if(settings){apply_titlebar_theme(*settings,dark_mode);settings_note->labelcolor(muted);settings->redraw();}
    }
    void layout() {
        if(!path)return;
        int W=window->w(),H=window->h(), path_width=W-543;
        path_label->resize(12,12,36,30);path->resize(52,12,path_width,30);
        user_label->resize(W-480,12,37,30);username->resize(W-441,12,114,30);
        pass_label->resize(W-316,12,65,30);password->resize(W-249,12,143,30);fetch_button->resize(W-94,12,82,30);
        filter_label->resize(W-393,54,77,28);filter->resize(W-315,54,255,28);settings_button->resize(W-48,51,36,34);
        table->resize(12,94,W-24,H-144);retro->resize(12,94,W-24,H-144);layout_file_toolbar();
        source_button->resize(W-346,100,88,28);
        save_button->resize(W-250,100,104,28);browser_button->resize(W-138,100,126,28);
        text_display->resize(12,142,W-24,H-192);document_view->resize(12,142,W-24,H-192);image_canvas->resize(12,142,W-24,H-192);message->resize(32,146,W-64,H-200);
        spinner->resize(10,H-38,26,28);status->resize(42,H-38,W-340,28);transfer_bar->resize(W-286,H-38,166,28);cancel_button->resize(W-106,H-38,94,28);
        // Window managers may send the maximized size before the state event.
        // Debounce geometry in tick() so this never overwrites the normal size.
        geometry_changed=std::chrono::steady_clock::now();
        window->redraw();
    }
    void layout_file_toolbar() {
        const bool encoding=preview_text_available&&!preview_rtf;
        const int occupied=preview_renderable?398:298;
        file_title->resize(24,98,window->w()-occupied-(encoding?168:0),34);
        encoding_choice->resize(window->w()-(preview_renderable?514:418),100,160,28);
    }
    void hide_content() {table->hide();retro->hide();file_title->hide();save_button->hide();browser_button->hide();source_button->hide();encoding_choice->hide();text_display->hide();document_view->hide();image_canvas->hide();message->hide();}
    std::string preview_format_name() const {return preview_rtf?"RTF":preview_csv?"CSV":"Markdown";}
    void change_encoding() {
        if(!preview_text_available||preview_rtf||closing)return;
        const int index=encoding_choice->value();
        if(index<0||index>=int(encoding_ids.size()))return;
        try {
            auto decoded=decode_preview(preview_raw_text,basename(current_path),preview_content_type,encoding_ids[std::size_t(index)]);
            preview_text=std::move(decoded.source.text);preview_styles=std::move(decoded.source.styles);preview_encoding=std::move(decoded.encoding);
            if(!preview_language.empty())preview_styles=document::syntax_styles(preview_text,preview_language);
            const std::string prefix=human_size(preview_size)+" · "+preview_encoding+" · ";
            idle_status=prefix+(preview_language.empty()?"Read-only text":preview_language+" · Read-only code");
            preview_renderable=false;preview_document={};
            if(preview_markdown||preview_csv) {
                idle_status=prefix+preview_format_name();
                if(!decoded.truncated&&preview_size<=formatted_render_limit&&preview_text.size()<=formatted_render_limit) {
                    try {
                        preview_document=preview_csv?make_csv(preview_text,dark_mode):make_markdown(preview_text,config.code_theme,dark_mode);
                        preview_renderable=true;
                    }catch(const std::exception&e){idle_status+=" source · "+std::string(e.what());}
                }else idle_status+=" source · Rendered preview limit: 25 MiB";
            }
            preview_document_theme=config.code_theme;preview_document_dark=dark_mode;
            text_buffer.text(preview_text.c_str());style_buffer.text(preview_styles.c_str());text_display->scroll(1,0);
            document_view->value(preview_document.html.c_str());document_view->topline(0);document_view->leftline(0);
            preview_encoding_index=index;
            show_text_preview();status_text(idle_status);
        }catch(const std::exception&e){
            encoding_choice->value(preview_encoding_index);
            idle_status=std::string("Could not decode text: ")+e.what();status_text(idle_status);
        }
    }
    void show_text_preview() {
        if(!preview_text_available)return;
        const bool code=!preview_language.empty();
        const auto p=document::palette(config.code_theme,dark_mode);
        text_display->highlight_data(&style_buffer,code?code_styles.data():text_styles.data(),
            code?int(code_styles.size()):int(text_styles.size()),0,nullptr,nullptr);
        text_display->color(code?document_color(p.background):surface);
        text_display->textcolor(code?document_color(p.foreground):FL_FOREGROUND_COLOR);
        if(preview_renderable && (preview_document_theme!=config.code_theme || preview_document_dark!=dark_mode)) {
            try {
                const int top=document_view->topline(),left=document_view->leftline();
                preview_document=preview_rtf?document::render_rtf(preview_raw_rtf,"#1f252c","#ffffff","#2369a8"):
                    preview_csv?make_csv(preview_text,dark_mode):make_markdown(preview_text,config.code_theme,dark_mode);
                preview_document_theme=config.code_theme;preview_document_dark=dark_mode;
                document_view->value(preview_document.html.c_str());document_view->topline(top);document_view->leftline(left);
            }catch(const std::exception&){
                preview_renderable=false;source_mode=true;
                idle_status=preview_format_name()+" source · Rendered preview unavailable";status_text(idle_status);
            }
        }
        if(preview_renderable) {
            source_button->copy_label(source_mode?"Rendered":"Source");source_button->show();
        }else source_button->hide();
        if(preview_rtf)encoding_choice->hide();else encoding_choice->show();
        if(preview_renderable&&!source_mode){text_display->hide();document_view->show();}
        else{document_view->hide();text_display->show();}
        layout_file_toolbar();
        text_display->redraw();document_view->redraw();window->redraw();
    }
    void jump_to_document_anchor(const std::string& anchor) {
        if(anchor.empty()){document_view->topline(0);return;}
        const auto target=preview_document.anchors.find(anchor);
        if(target!=preview_document.anchors.end())document_view->topline(target->second.c_str());
        else status_text("Heading not found in this document");
    }
    static void document_link_cb(void*v) {
        auto*a=static_cast<App*>(v);
        if(a->closing)return;
        auto target=std::move(a->queued_link);a->queued_link.clear();
        try {
            const auto link=resolve_document_link(target,a->current_path,a->config.server);
            if(link.kind==DocumentLink::Kind::Anchor) {
                a->jump_to_document_anchor(link.anchor);
            }else if(link.kind==DocumentLink::Kind::ServerPath) {
                a->navigate(link.target,!link.target.empty()&&link.target.back()=='/'?FetchKind::Listing:FetchKind::Auto);
                a->pending_anchor=link.anchor;
            }else {
                char error[1024]={};
                if(!fl_open_uri(link.target.c_str(),error,sizeof(error)))a->status_text(std::string("Could not open browser: ")+error);
            }
        }catch(const std::exception&e){a->status_text(safe_error(e.what(),a->request_config()));}
    }
    void queue_document_link(const std::string& token) {
        if(closing||token.compare(0,4,"cpv:")!=0||token.size()<5||token.size()>24)return;
        std::size_t index=0;
        for(std::size_t i=4;i<token.size();++i) {
            if(token[i]<'0'||token[i]>'9'||index>preview_document.links.size())return;
            index=index*10+std::size_t(token[i]-'0');
        }
        if(index>=preview_document.links.size())return;
        queued_link=preview_document.links[index];
        Fl::remove_timeout(document_link_cb,this);Fl::add_timeout(0,document_link_cb,this);
    }
    void show_message(const std::string& value,bool file=false) {
        hide_content(); message->copy_label(value.c_str()); message->show();
        if(file){file_title->show();save_button->show();browser_button->show();}
        window->redraw();
    }
    void status_text(const std::string& s) {status->copy_label(s.c_str());status->tooltip(status->label());status->redraw();}
    void remember_location() {
        if(history_index>=0&&history_index<int(history.size())) {history[std::size_t(history_index)].scroll=config.modern_ui?table->row_position():retro->scroll_position();history[std::size_t(history_index)].filter=filter->value();}
    }
    Config request_config() const {
        // Main-window edits are copied into an individual worker's memory only.
        // Config's saved-credential policy also keeps build defaults out of JSON
        // until the user explicitly saves them in Settings.
        Config snapshot=config;snapshot.username=username->value();snapshot.password=password->value();return snapshot;
    }
    void remember_geometry(bool immediate=false) {
        if(!window->maximize_active()&&window->shown()&&
           (immediate||std::chrono::steady_clock::now()-geometry_changed>std::chrono::milliseconds(180)))
            config.main_window={window->x(),window->y(),window->w(),window->h(),false};
    }
    void persist() {
        remember_geometry();
        config.main_window.maximized=window->maximize_active()!=0;
        if(settings&&settings->shown())config.settings_window={settings->x(),settings->y(),settings->w(),settings->h(),false};
        std::string error;
        if(!save_config(config_path,config,error)){idle_status="Settings could not be saved: "+error;status_text(idle_status);}
    }
    void update_navigation() {
        history_index>0?back->activate():back->deactivate();
        history_index+1<int(history.size())?forward->activate():forward->deactivate();
        current_path!="/"?up->activate():up->deactivate();
        directory_view?filter->activate():filter->deactivate();
    }
    void fetch_from_path() {
        if(closing)return;
        try {
            std::string base=directory_view?current_path:parent_path(current_path);
            auto p=resolve_path(path->value(),base,config.server);
            navigate(p,(!p.empty()&&p.back()=='/')?FetchKind::Listing:FetchKind::Auto);
        }catch(const std::exception&e){idle_status=safe_error(e.what(),request_config());status_text(idle_status);}
    }
    void navigate(const std::string& p,FetchKind kind,bool add=true) {
        if(closing)return;
        remember_location();
        if(add) {
            if(history_index>=0&&history[std::size_t(history_index)].path==p) {reload();return;}
            history.resize(std::size_t(history_index+1));history.push_back({p,kind,0,""});history_index=int(history.size())-1;
            if(history.size()>100){history.erase(history.begin());--history_index;}
        }
        Location loc=history_index>=0?history[std::size_t(history_index)]:Location{p,kind,0,""};
        loc.path=p;loc.kind=kind;start_fetch(loc);
    }
    void reload() {
        if(closing)return;
        remember_location();
        if(history_index>=0)start_fetch(history[std::size_t(history_index)]);
        else navigate(current_path,FetchKind::Auto);
    }
    void travel(int delta) {
        int next=history_index+delta;if(next<0||next>=int(history.size())||closing)return;
        remember_location();history_index=next;start_fetch(history[std::size_t(next)]);
    }
    void start_fetch(Location loc) {
        ++generation;for(auto&j:jobs)if(j->type==Job::Type::Fetch)j->progress.cancel=true;
        current_path=loc.path;path->value(display_path(loc.path).c_str());config.last_path=loc.path;
        filter->value(loc.filter.c_str()); directory_view=false; file_view=false;update_navigation();
        Fl::remove_timeout(document_link_cb,this);queued_link.clear();pending_anchor.clear();
        preview_text_available=false;preview_rtf=false;preview_csv=false;preview_markdown=false;preview_renderable=false;source_mode=false;
        preview_language.clear();preview_text.clear();preview_styles.clear();preview_raw_rtf.clear();preview_raw_text.clear();preview_encoding.clear();preview_content_type.clear();preview_document={};preview_document_theme.clear();
        encoding_choice->value(0);preview_encoding_index=0;layout_file_toolbar();
        image_canvas->clear();text_buffer.text("");style_buffer.text("");document_view->value("");
        show_message("Fetching…");idle_status="Fetching "+loc.path;status_text(idle_status);persist();
        auto job=std::make_shared<Job>();job->snapshot=request_config();job->location=loc;job->generation=generation;job->dark=dark_mode;
        pending_fetch=job;launch_pending();
    }
    void launch_pending() {
        if(!pending_fetch||closing)return;
        for(const auto& existing:jobs)if(existing->type==Job::Type::Fetch)return;
        auto job=std::move(pending_fetch);
        jobs.push_back(job);
        try { job->worker=std::thread([job]{
            try {
                Response response=fetch(job->snapshot,job->location.path,job->location.kind,job->progress);
                if(job->progress.cancel)throw std::runtime_error("Cancelled");
                if(job->location.kind!=FetchKind::File) {
                    try {
                        job->listing=parse_listing(std::string(response.bytes.begin(),response.bytes.end()),job->location.path);
                        job->is_listing=true;
                    }catch(const std::exception&) {if(job->location.kind==FetchKind::Listing)throw;}
                }
                if(job->is_listing&&!job->snapshot.modern_ui) {
                    // Keep genuine Copyparty terminal rows when supported. The
                    // verified JSON entries remain the only navigation targets.
                    try {
                        auto terminal=fetch(job->snapshot,job->listing.path,FetchKind::Terminal,job->progress);
                        auto matched=apply_terminal_listing(job->listing,std::string(terminal.bytes.begin(),terminal.bytes.end()));
                        if(!matched&&!job->listing.entries.empty())job->detail="Using compatible Retro formatting";
                    }catch(const std::exception&) {
                        if(job->progress.cancel)throw;
                        job->detail="Server terminal view unavailable; using compatible formatting";
                    }
                }
                if(!job->is_listing) {
                    std::string name=basename(job->location.path);
                    if(image_supported(name,response.content_type)) {job->image=decode_image(response.bytes,name,response.content_type);job->is_image=true;job->detail=job->image.description;}
                    else {
                        job->is_rtf=document::rtf_supported(name,response.content_type);
                        // RTF owns its in-band encoding and can contain arbitrary
                        // binary object bytes. Source decoding must never gate its parser.
                        DecodedPreview prepared;
                        if(job->is_rtf) {
                            try {prepared=decode_preview(response.bytes,name,"application/rtf");}
                            catch(const std::exception&) {prepared.source=prepare_styled_text(response.bytes,formatted_render_limit);}
                        }else prepared=decode_preview(response.bytes,name,response.content_type);
                        if(job->is_rtf||prepared.supported) {
                            job->text=std::move(prepared.source.text);job->styles=std::move(prepared.source.styles);job->text_encoding=std::move(prepared.encoding);job->is_text=true;
                            job->text_size=response.bytes.size();job->content_type=response.content_type;
                            if(!job->is_rtf)job->raw_text.assign(response.bytes.begin(),response.bytes.begin()+std::min(response.bytes.size(),preview_input_limit(name,response.content_type)+1));
                            const std::string description=human_size(response.bytes.size())+" · "+(job->is_rtf?"":job->text_encoding+" · ");
                            job->detail=description+"Read-only text";
                            job->is_markdown=!job->is_rtf&&document::markdown_supported(name,response.content_type);
                            job->is_csv=!job->is_rtf&&!job->is_markdown&&document::csv_supported(name,response.content_type);
                            job->language=job->is_markdown||job->is_rtf||job->is_csv?"":document::language_for_filename(name);
                            if(job->language=="plaintext")job->language.clear();
                            if(!job->language.empty()) {
                                job->styles=document::syntax_styles(job->text,job->language);
                                job->detail=description+job->language+" · Read-only code";
                            }
                            if(job->is_markdown||job->is_rtf||job->is_csv) {
                                const std::string format=job->is_rtf?"RTF":job->is_csv?"CSV":"Markdown";
                                const std::string prefix=description+format;
                                const auto render_size=job->is_rtf?response.bytes.size():job->text.size();
                                if((job->is_rtf||!prepared.truncated)&&response.bytes.size()<=formatted_render_limit&&render_size<=formatted_render_limit) {
                                    try {
                                        if(job->is_rtf) {
                                            job->raw_rtf.assign(response.bytes.begin(),response.bytes.end());
                                            // RTF is a document with explicit text colors. A white
                                            // paper surface keeps those colors readable in either UI mode.
                                            job->rendered_document=document::render_rtf(job->raw_rtf,"#1f252c","#ffffff","#2369a8");
                                        }else job->rendered_document=job->is_csv?make_csv(job->text,job->dark):make_markdown(job->text,job->snapshot.code_theme,job->dark);
                                        job->renderable_document=true;job->detail=prefix;
                                    }catch(const std::exception&e){job->detail=prefix+" source · "+std::string(e.what());}
                                }else job->detail=prefix+" source · Rendered preview limit: 25 MiB";
                            }
                        }else job->detail="Preview is not available for this file.\n\nUse Save As to download it, or open it in your browser.";
                    }
                }
            }catch(const std::exception&e){job->cancelled=job->progress.cancel.load();job->error=safe_error(e.what(),job->snapshot);}
            if(job->progress.cancel.load())job->cancelled=true;
            job->done.store(true,std::memory_order_release);
        }); } catch(const std::exception&) {
            job->error="A background worker could not be started. Close other applications and try again.";
            job->done.store(true,std::memory_order_release);
        }
    }
    void filter_table() {
        if(!directory_view)return;
        std::string needle=lower_ascii(filter->value());table->entries.clear();
        for(const auto&e:directory.entries)if(needle.empty()||lower_ascii(e.name).find(needle)!=std::string::npos)table->entries.push_back(e);
        int col=table->sort_column;bool desc=table->descending,show_sizes=config.show_directory_sizes;
        std::stable_sort(table->entries.begin(),table->entries.end(),[col,desc,show_sizes](const Entry&a,const Entry&b){
            return entry_less(a,b,col,desc,show_sizes);
        });
        table->show_directory_sizes=config.show_directory_sizes;
        table->rows(int(table->entries.size()));table->row_height_all(FileTable::row_height_pixels);table->select_all_rows(0);table->adjust_columns();table->redraw();
        retro->set_listing(directory,table->entries,config.show_directory_sizes);
        if(config.modern_ui){retro->hide();table->show();}else{table->hide();retro->show();}
        auto folders=std::count_if(table->entries.begin(),table->entries.end(),[](const Entry&e){return e.directory;});
        const auto files=table->entries.size()-std::size_t(folders);
        idle_status=std::to_string(folders)+(folders==1?" folder · ":" folders · ")+std::to_string(files)+(files==1?" file":" files");
        if(!needle.empty())idle_status+=" shown";
        status_text(idle_status);
    }
    void complete(const std::shared_ptr<Job>& j) {
        if(j->type==Job::Type::Download) {
            if(j->cancelled)idle_status="Download cancelled";
            else if(!j->error.empty()) {
                idle_status="Download failed: "+j->error;
                if(!closing)fl_alert("Download failed:\n%s",j->error.c_str());
            }
            else idle_status="Saved "+j->destination.filename().u8string();
            status_text(idle_status);return;
        }
        if(j->generation!=generation||closing)return;
        if(j->cancelled){idle_status="Fetch cancelled";show_message("Fetch cancelled.\nUse Fetch or Refresh to try again.");status_text(idle_status);return;}
        if(!j->error.empty()) {
            idle_status=j->error;file_view=j->location.kind!=FetchKind::Listing;
            file_title->copy_label(basename(current_path).c_str());
            show_message(j->error+(file_view?"\n\nYou can still download the file or open it in your browser.":"\n\nCheck the server, path and credentials, then try again."),file_view);
            status_text(idle_status);return;
        }
        hide_content();
        if(j->is_listing) {
            directory_view=true;directory=std::move(j->listing);
            if(!directory.path.empty())current_path=directory.path;
            if(current_path.back()!='/')current_path+='/';
            path->value(display_path(current_path).c_str());config.last_path=current_path;
            if(history_index>=0){history[std::size_t(history_index)].path=current_path;history[std::size_t(history_index)].kind=FetchKind::Listing;}
            filter_table();table->row_position(std::min(j->location.scroll,std::max(0,table->rows()-1)));retro->scroll_position(j->location.scroll);
            if(table->rows()==0){idle_status=*filter->value()?"No matching files":"This directory is empty";status_text(idle_status);}
            if(!j->detail.empty()){idle_status+=" · "+j->detail;status_text(idle_status);}
        }else {
            file_view=true;file_title->copy_label(basename(current_path).c_str());file_title->copy_tooltip(current_path.c_str());file_title->show();save_button->show();browser_button->show();
            if(j->is_image){image_canvas->set(std::move(j->image));image_canvas->show();}
            else if(j->is_text){
                preview_text_available=true;preview_rtf=j->is_rtf;preview_csv=j->is_csv;preview_markdown=j->is_markdown;preview_renderable=j->renderable_document;
                preview_text=std::move(j->text);preview_styles=std::move(j->styles);preview_language=std::move(j->language);preview_raw_rtf=std::move(j->raw_rtf);
                preview_raw_text=std::move(j->raw_text);preview_encoding=std::move(j->text_encoding);preview_content_type=std::move(j->content_type);preview_size=j->text_size;
                preview_document=std::move(j->rendered_document);preview_document_theme=j->snapshot.code_theme;preview_document_dark=j->dark;
                text_buffer.text(preview_text.c_str());style_buffer.text(preview_styles.c_str());text_display->scroll(1,0);
                document_view->value(preview_document.html.c_str());document_view->topline(0);document_view->leftline(0);
                show_text_preview();
                if(preview_renderable&&!pending_anchor.empty())jump_to_document_anchor(pending_anchor);
                pending_anchor.clear();
            }
            else {message->copy_label(j->detail.c_str());message->show();}
            idle_status=j->is_image||j->is_text?j->detail:"Ready · Download available";status_text(idle_status);
        }
        if(!startup_warning.empty()){idle_status+=" · "+startup_warning;startup_warning.clear();status_text(idle_status);}
        update_navigation();persist();window->redraw();
    }
    std::shared_ptr<Job> active_job() {
        if(closing)for(auto&j:jobs)if(!j->done.load())return j;
        if(pending_fetch)return pending_fetch;
        for(auto i=jobs.rbegin();i!=jobs.rend();++i)if(!(*i)->done.load() && ((*i)->type==Job::Type::Download||(*i)->generation==generation))return *i;
        return {};
    }
    void cancel_current() {
        if(pending_fetch){pending_fetch->progress.cancel=true;pending_fetch->cancelled=true;auto j=std::move(pending_fetch);complete(j);return;}
        if(auto j=active_job()){j->progress.cancel=true;status_text("Cancelling…");cancel_button->deactivate();}
    }
    static void timer_cb(void* v){static_cast<App*>(v)->tick();}
    void tick() {
        remember_geometry();
        for(auto it=jobs.begin();it!=jobs.end();) {
            if((*it)->done.load(std::memory_order_acquire)) {auto j=*it;if(j->worker.joinable())j->worker.join();it=jobs.erase(it);complete(j);}
            else ++it;
        }
        launch_pending();
        auto active=active_job();spinner->running=bool(active);spinner->phase=(spinner->phase+1)%12;spinner->redraw();
        if(active){
            cancel_button->show();active->progress.cancel?cancel_button->deactivate():cancel_button->activate();
            transfer_bar->received=active->progress.received.load();transfer_bar->total=active->progress.total.load();
            std::string s=closing?"Closing…":active->progress.cancel?"Cancelling…":active->type==Job::Type::Download?"Downloading "+basename(active->location.path):"Fetching "+active->location.path;
            if(!active->progress.cancel&&transfer_bar->received){s+=" · "+human_size(transfer_bar->received);if(transfer_bar->total)s+=" / "+human_size(transfer_bar->total);else s+=" received";}
            if(!active->progress.cancel&&active->type==Job::Type::Fetch&&transfer_bar->total&&transfer_bar->received>=transfer_bar->total)s="Preparing preview…";
            status_text(s);
        } else {cancel_button->hide();transfer_bar->total=0;status_text(idle_status);}
        transfer_bar->redraw();
        if(closing&&jobs.empty()){if(settings)settings->hide();window->hide();return;}
        if(++tick_count%60==0&&!closing){bool dark=desktop_dark();if(dark!=dark_mode){set_palette(dark);palette_widgets();}}
        Fl::repeat_timeout(.08,timer_cb,this);
    }
    void save_as() {
        if(!file_view||closing)return;
        for(auto&j:jobs)if(j->type==Job::Type::Download&&!j->done){status_text("A download is already running. Finish it or use Cancel first.");return;}
        Fl_Native_File_Chooser chooser;chooser.title("Save file");chooser.type(Fl_Native_File_Chooser::BROWSE_SAVE_FILE);chooser.options(Fl_Native_File_Chooser::SAVEAS_CONFIRM);
        auto name=basename(current_path);
#ifdef _WIN32
        for(char&c:name)if(static_cast<unsigned char>(c)<32||std::string("<>:\"/\\|?*").find(c)!=std::string::npos)c='_';
        while(!name.empty()&&(name.back()=='.'||name.back()==' '))name.pop_back();
        if(name.empty())name="download";
        auto stem=lower_ascii(name.substr(0,name.find('.')));
        if(stem=="con"||stem=="prn"||stem=="aux"||stem=="nul"||
           (stem.size()==4&&(stem.rfind("com",0)==0||stem.rfind("lpt",0)==0)&&stem[3]>='1'&&stem[3]<='9'))name="_"+name;
#endif
        chooser.preset_file(name.c_str());
        if(chooser.show()!=0)return;
        auto j=std::make_shared<Job>();j->type=Job::Type::Download;j->snapshot=request_config();j->location={current_path,FetchKind::File,0,""};j->destination=std::filesystem::u8path(chooser.filename());jobs.push_back(j);
        try {
            j->worker=std::thread([j]{try{download(j->snapshot,j->location.path,j->destination,j->progress);}catch(const std::exception&e){j->cancelled=j->progress.cancel.load();j->error=safe_error(e.what(),j->snapshot);}j->done.store(true,std::memory_order_release);});
        }catch(const std::exception&){j->error="A download worker could not be started. Close other applications and try again.";j->done.store(true,std::memory_order_release);}
    }
    void open_browser() {
        try {char error[1024]={};std::string url=make_url(config.server,current_path);if(!fl_open_uri(url.c_str(),error,sizeof(error)))status_text(std::string("Could not open browser: ")+error);}
        catch(const std::exception&e){status_text(safe_error(e.what(),config));}
    }
    void open_settings() {
        if(closing)return;
        if(settings&&settings->shown()){settings->show();return;}
        if(!settings) {
            auto g=on_screen(config.settings_window,560,570);window->end();
            settings=std::make_unique<SettingsWindow>(g.x,g.y,g.w,g.h,"Copyparty Viewer — Settings");settings->set_non_modal();settings->size_range(560,570);
            if(app_icon->w()>0)settings->icon(app_icon.get());
            settings->begin();
            auto* heading=new Fl_Box(20,14,520,28,"Connection");heading->align(FL_ALIGN_LEFT|FL_ALIGN_INSIDE);heading->labelsize(17);heading->labelfont(FL_HELVETICA_BOLD);
            auto* url_label=new Fl_Box(20,56,105,28,"Server URL");url_label->align(FL_ALIGN_LEFT|FL_ALIGN_INSIDE);settings_url=new Fl_Input(127,56,413,28);
            auto* usr_label=new Fl_Box(20,98,105,28,"Username");usr_label->align(FL_ALIGN_LEFT|FL_ALIGN_INSIDE);settings_user=new Fl_Input(127,98,413,28);
            auto* pwd_label=new Fl_Box(20,140,105,28,"Password");pwd_label->align(FL_ALIGN_LEFT|FL_ALIGN_INSIDE);settings_pass=new Fl_Secret_Input(127,140,413,28);
            settings_note=new Fl_Box(20,180,520,56,"Main-window credentials stay in memory for this session.\nSave here to remember this window's username and password.\nStored credentials use portable, reversible obfuscation.");settings_note->align(FL_ALIGN_LEFT|FL_ALIGN_INSIDE);settings_note->labelsize(12);settings_note->labelcolor(muted);
            auto* listing_heading=new Fl_Box(20,244,520,28,"Directory listing");listing_heading->align(FL_ALIGN_LEFT|FL_ALIGN_INSIDE);listing_heading->labelsize(17);listing_heading->labelfont(FL_HELVETICA_BOLD);
            auto* view_label=new Fl_Box(20,282,150,32,"Directory view");view_label->align(FL_ALIGN_LEFT|FL_ALIGN_INSIDE);
            settings_modern=new AnimatedToggle(178,282,250,32,"Retro","Modern");settings_modern->tooltip("Retro terminal-style listing or Modern table · Space toggles; Left/Right selects");
            auto* sizes_label=new Fl_Box(20,330,150,32,"Directory sizes");sizes_label->align(FL_ALIGN_LEFT|FL_ALIGN_INSIDE);
            settings_sizes=new AnimatedToggle(178,330,250,32,"Off","On");settings_sizes->tooltip("Display directory sizes reported by Copyparty in both views");
            auto* code_label=new Fl_Box(20,380,150,30,"Code theme");code_label->align(FL_ALIGN_LEFT|FL_ALIGN_INSIDE);
            settings_code_theme=new Fl_Choice(178,380,250,30);settings_code_theme->textsize(14);
            for(const auto& theme:document::themes())settings_code_theme->add(theme.label);
            settings_code_theme->tooltip("Colors for source files and Markdown code blocks · follows the operating-system appearance");
            auto* listing_note=new Fl_Box(20,426,520,38,"Code colors apply to source files and Markdown code blocks.\nLight and dark palettes follow your operating system.");listing_note->align(FL_ALIGN_LEFT|FL_ALIGN_INSIDE);listing_note->labelsize(12);
            settings_error=new Fl_Box(20,448,520,30);settings_error->align(FL_ALIGN_LEFT|FL_ALIGN_INSIDE|FL_ALIGN_WRAP);settings_error->labelsize(12);settings_error->labelcolor(fl_rgb_color(190,65,65));
            settings_cancel=new Fl_Button(352,490,88,30,"Cancel");settings_save=new Fl_Return_Button(452,490,88,30,"Save");
            settings->end();
            // Position controls explicitly so resizing never shifts the toggle rows.
            settings->resizable(listing_note);
            settings->layout=[this,heading,listing_heading,listing_note,url_label,usr_label,pwd_label,view_label,sizes_label,code_label]{
                int W=settings->w(),H=settings->h();
                url_label->resize(20,56,105,28);usr_label->resize(20,98,105,28);pwd_label->resize(20,140,105,28);
                settings_url->resize(127,56,W-147,28);settings_user->resize(127,98,W-147,28);settings_pass->resize(127,140,W-147,28);
                heading->resize(20,14,W-40,28);settings_note->resize(20,180,W-40,56);
                listing_heading->resize(20,244,W-40,28);listing_note->resize(20,426,W-40,38);
                view_label->resize(20,282,150,32);settings_modern->resize(178,282,250,32);
                sizes_label->resize(20,330,150,32);settings_sizes->resize(178,330,250,32);
                code_label->resize(20,380,150,30);settings_code_theme->resize(178,380,std::min(320,W-198),30);
                settings_error->resize(20,H-92,W-40,30);
                settings_cancel->resize(W-208,H-50,88,30);settings_save->resize(W-108,H-50,88,30);
            };
            settings->layout();
            for(Fl_Input*i:std::vector<Fl_Input*>{settings_url,settings_user,settings_pass})i->textsize(14);
            settings->callback([](Fl_Widget*,void*v){static_cast<App*>(v)->dismiss_settings();},this);
            settings_cancel->callback([](Fl_Widget*,void*v){static_cast<App*>(v)->dismiss_settings();},this);
            settings_save->callback([](Fl_Widget*,void*v){static_cast<App*>(v)->apply_settings();},this);
        }
        settings_url->value(config.server.c_str());settings_user->value(config.username.c_str());settings_pass->value(config.password.c_str());
        settings_modern->value(config.modern_ui);settings_sizes->value(config.show_directory_sizes);
        settings_code_theme->value(0);
        for(std::size_t i=0;i<document::themes().size();++i)if(config.code_theme==document::themes()[i].id)settings_code_theme->value(int(i));
        settings_error->copy_label("");settings->show();apply_titlebar_theme(*settings,dark_mode);
    }
    void dismiss_settings() {
        if(!settings)return;
        config.settings_window={settings->x(),settings->y(),settings->w(),settings->h(),false};settings->hide();persist();
    }
    void apply_settings() {
        try {
            Config next=config;next.server=normalize_server(settings_url->value());next.username=settings_user->value();next.password=settings_pass->value();
            next.credentials_saved=true;
            next.modern_ui=settings_modern->value();next.show_directory_sizes=settings_sizes->value();
            const int theme_index=settings_code_theme->value();
            next.code_theme=theme_index>=0&&theme_index<int(document::themes().size())?document::themes()[std::size_t(theme_index)].id:"default";
            bool server_changed=next.server!=config.server;
            bool auth_changed=next.username!=config.username||next.password!=config.password;
            bool mode_changed=next.modern_ui!=config.modern_ui;
            bool theme_changed=next.code_theme!=config.code_theme;
            bool view_changed=mode_changed||next.show_directory_sizes!=config.show_directory_sizes;
            int saved_scroll=config.modern_ui?table->row_position():retro->scroll_position();
            next.settings_window={settings->x(),settings->y(),settings->w(),settings->h(),false};if(server_changed)next.last_path="/";
            std::string error;if(!save_config(config_path,next,error))throw std::runtime_error(error);
            config=std::move(next);
            if(server_changed||auth_changed){username->value(config.username.c_str());password->value(config.password.c_str());}
            settings->hide();
            if(theme_changed)palette_widgets();
            if(server_changed){history.clear();history_index=-1;navigate("/",FetchKind::Listing);}
            else if(auth_changed)reload();
            else {
                if(view_changed&&directory_view){
                    filter_table();table->row_position(saved_scroll);retro->scroll_position(saved_scroll);
                    if(mode_changed&&!config.modern_ui){reload();return;}
                }
                idle_status="Settings saved";status_text(idle_status);window->redraw();
            }
        }catch(const std::exception&e){settings_error->copy_label(safe_error(e.what(),config).c_str());settings_error->redraw();}
    }
    void close() {
        if(closing)return;
        remember_geometry(true);persist();closing=true;
        if(settings)settings->hide();
        pending_fetch.reset();for(auto&j:jobs)j->progress.cancel=true;
        if(jobs.empty())window->hide();else{status_text("Closing…");for(Fl_Widget*w:std::vector<Fl_Widget*>{path,username,password,fetch_button,back,forward,up,refresh,settings_button,filter,save_button,browser_button,source_button,encoding_choice})w->deactivate();}
    }
    int shortcut(int event) {
        if(event!=FL_SHORTCUT&&event!=FL_KEYDOWN)return 0;
        int key=Fl::event_key(),state=Fl::event_state();
        if((state&FL_CTRL)&&key=='l'){path->take_focus();path->insert_position(path->size(),0);return 1;}
        if((state&FL_CTRL)&&key=='f'&&directory_view){filter->take_focus();filter->insert_position(filter->size(),0);return 1;}
        if(((state&FL_CTRL)&&key=='r')||key==FL_F+5){reload();return 1;}
        if((state&FL_CTRL)&&key=='s'&&file_view){save_as();return 1;}
        if(state&FL_ALT){if(key==FL_Left){travel(-1);return 1;}if(key==FL_Right){travel(1);return 1;}if(key==FL_Up){navigate(parent_path(current_path),FetchKind::Listing);return 1;}}
        if(key==FL_Escape){cancel_current();return 1;}return 0;
    }
    int run(int argc,char**argv) {
        window->show(argc,argv);
        // show(argc,argv) reloads the scheme on the first window. Install the
        // focus-painting policy afterward so that reload cannot restore dots.
        configure_focus_drawing();window->redraw();
        apply_titlebar_theme(*window,dark_mode);if(config.main_window.maximized)window->maximize();
        Fl::add_timeout(.08,timer_cb,this);
        Fl::add_timeout(.05,[](void*v){auto*a=static_cast<App*>(v);if(a->closing)return;try{const auto p=a->config.last_path;a->navigate(p,p.back()=='/'?FetchKind::Listing:FetchKind::Auto);}catch(const std::exception&e){a->show_message(e.what());} },this);
        return Fl::run();
    }
};

void AppWindow::resize(int x,int y,int w,int h){Fl_Double_Window::resize(x,y,w,h);if(app)app->layout();}
int AppWindow::handle(int event){if(app&&app->shortcut(event))return 1;return Fl_Double_Window::handle(event);}
App* dispatch_app=nullptr;
Fl_Event_Dispatch previous_dispatch=nullptr;
int dispatch_events(int event,Fl_Window* window) {
    // Inputs consume several Ctrl keys as editing controls before FL_SHORTCUT.
    // Handle application shortcuts before focused widgets receive the key.
    if(dispatch_app&&event==FL_KEYDOWN&&window==dispatch_app->window.get()&&!Fl::modal()&&!Fl::grab())
        if(dispatch_app->shortcut(event))return 1;
    return previous_dispatch?previous_dispatch(event,window):Fl::handle_(event,window);
}
} // namespace
int run_application(int argc,char**argv){
    Fl::lock();App app;previous_dispatch=Fl::event_dispatch();dispatch_app=&app;Fl::event_dispatch(dispatch_events);
    try {
        int result=app.run(argc,argv);Fl::event_dispatch(previous_dispatch);dispatch_app=nullptr;return result;
    }catch(...){Fl::event_dispatch(previous_dispatch);dispatch_app=nullptr;throw;}
}
} // namespace cpv
