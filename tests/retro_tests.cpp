// SPDX-License-Identifier: MIT
// Copyright (C) 2026 davoda1
// Optional display-backed widget regression test. It sends synthetic FLTK
// events to the public widget API; it does not move the desktop mouse/keyboard.
#include "retro.hpp"
#include "preview.hpp"
#include <FL/Fl.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Scrollbar.H>
#include <FL/fl_draw.H>
#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
namespace {
unsigned checks=0;
void check(bool okay,const char* description) {++checks;if(!okay)throw std::runtime_error(description);}
void key(cpv::RetroListing& view,int keycode,int state=0) {
    Fl::e_keysym=keycode;Fl::e_state=state;view.handle(FL_KEYDOWN);
}
void click(cpv::RetroListing& view,int x,int y,bool dispatch=true) {
    Fl::e_x=x;Fl::e_y=y;Fl::e_keysym=FL_Button+FL_LEFT_MOUSE;Fl::e_state=FL_BUTTON1;Fl::e_is_click=1;
    view.handle(FL_PUSH);Fl::e_state=0;view.handle(FL_RELEASE);
    if(dispatch)Fl::check();
}
Fl_Scrollbar* scrollbar(cpv::RetroListing& view,bool horizontal) {
    for(int i=0;i<view.children();++i)
        if(auto* bar=dynamic_cast<Fl_Scrollbar*>(view.child(i));bar&&bool(bar->type()==FL_HORIZONTAL)==horizontal)return bar;
    return nullptr;
}
int header_lines(const std::string& header) {
    std::vector<unsigned char> bytes(header.begin(),header.end());auto plain=cpv::prepare_text(bytes);
    return static_cast<int>(std::count(plain.begin(),plain.end(),'\n'))+(!plain.empty()&&plain.back()!='\n'?1:0);
}
}
int main() {
    try {
        Fl::scheme("gtk+");
        Fl_Double_Window window(960,480,"Copyparty Viewer — Retro widget checks");
        auto* view=new cpv::RetroListing(10,10,940,460);window.end();window.show();
        cpv::Listing listing;listing.path="/tests/";
        listing.header_ansi="# acct: \033[36msynthetic-test\033[0m\n# perms: [\033[32mread\033[0m]\n\n";
        const std::vector<std::string> names={"space in name.txt","Árvíztűrő 100% ?# 漢字.txt",std::string("\xef\xbb\xbf")+"BOM name.txt","folder space %漢字"};
        for(std::size_t i=0;i<names.size();++i) {
            const bool directory=i+1==names.size();
            listing.entries.push_back({names[i],listing.path+names[i]+(directory?"/":""),directory,12500,1735689600,true,false,{}});
        }
        std::vector<std::string> opened;
        view->open=[&](const cpv::Entry& entry){opened.push_back(entry.path);};
        view->set_listing(listing,listing.entries,true);Fl::check();
        // A small fixture is defined by terminal capacity, not a fixed pixel
        // width. Native fonts (and Wine substitutions) have different metrics.
        // Reserve 80 columns and 12 rows while retaining separate overflow cases.
        fl_font(FL_COURIER,cpv::RetroListing::text_size);
        const double cell=std::max(1.0,fl_width("M"));
        const int fixture_height=std::max(cpv::RetroListing::text_size+6,fl_height()+4);
        const int fixture_width=std::max(940,static_cast<int>(std::ceil(80*cell))+26);
        const int fixture_pixels_high=std::max(460,12*fixture_height+20);
        window.resize(window.x(),window.y(),fixture_width+20,fixture_pixels_high+20);
        view->resize(10,10,fixture_width,fixture_pixels_high);Fl::check();
        std::cout<<"Retro fixture: "<<fixture_width<<" x "<<fixture_pixels_high
                 <<", font="<<cpv::RetroListing::text_size<<", cell="<<cell<<".\n";
        fl_font(FL_COURIER,cpv::RetroListing::text_size);
        const int height=std::max(cpv::RetroListing::text_size+6,fl_height()+4);
        const int row0y=view->y()+1+9+header_lines(listing.header_ansi)*height+height/2;
        std::vector<unsigned char> raw;
        const auto ansi=cpv::terminal_entry_ansi(listing.entries[0]);raw.assign(ansi.begin(),ansi.end());
        const auto plain=cpv::prepare_text(raw);
        const auto prefix=plain.substr(0,plain.size()-listing.entries[0].name.size());
        const int name_x=view->x()+1+12+static_cast<int>(fl_width(prefix.c_str()))+5;
        for(std::size_t i=0;i<names.size();++i) {
            click(*view,name_x,row0y+static_cast<int>(i)*height);
            check(opened.size()==i+1,"one name click opens once");
            check(opened.back()==listing.entries[i].path,"spaces/Unicode/percent/question/hash/BOM preserve structured path identity");
        }
        check(Fl::focus()==view,"larger terminal retains keyboard focus without a dotted outline");
        const auto count=opened.size();click(*view,view->x()+25,row0y);
        check(opened.size()==count,"date/size text is not a file link");
        check(!scrollbar(*view,false)->visible()&&!scrollbar(*view,true)->visible(),"small listing needs no scrollbars");
        Fl::e_keysym=FL_Up;Fl::e_state=FL_ALT;
        check(view->handle(FL_KEYDOWN)==0,"Alt navigation propagates to the main window");
        Fl::e_state=0;key(*view,FL_Home);key(*view,FL_Down);key(*view,FL_Enter);Fl::check();
        check(opened.back()==listing.entries[1].path,"keyboard opens selected path");
        // A model replacement must cancel an already queued open from old rows.
        click(*view,name_x,row0y,false);
        auto reversed=listing.entries;std::reverse(reversed.begin(),reversed.end());
        const auto before=opened.size();view->set_listing(listing,reversed,false);Fl::check();
        check(opened.size()==before,"model replacement cancels stale click callback");
        click(*view,name_x,row0y);
        check(opened.back()==reversed.front().path,"sorting uses the displayed entry's identity");
        cpv::Listing large=listing;large.entries.clear();
        for(unsigned i=0;i<1000;++i)large.entries.push_back({"item-"+std::to_string(i),listing.path+std::to_string(i),false,i,1735689600,true,false,{}});
        view->set_listing(large,large.entries);Fl::check();
        check(scrollbar(*view,false)->visible(),"large listing has vertical scrollbar");
        view->scroll_position(500);check(view->scroll_position()==500,"entry scroll position round-trips");
        view->scroll_position(0);check(view->scroll_position()==0,"row zero restores header");
        key(*view,FL_End);check(view->selected_entry()&&view->selected_entry()->path==large.entries.back().path,"End selects final row");
        key(*view,FL_Home);check(view->selected_entry()&&view->selected_entry()->path==large.entries.front().path,"Home selects first row");
        auto long_entry=listing.entries[0];long_entry.name=std::string(250,'M')+" 100% 漢字.txt";long_entry.path=listing.path+long_entry.name;
        view->set_listing(listing,{long_entry});Fl::check();
        check(scrollbar(*view,true)->visible(),"long terminal line has horizontal scrollbar");
        check(!scrollbar(*view,false)->visible(),"horizontal scrollbar does not create unnecessary vertical scrollbar");
        Fl::e_x=view->x()+50;Fl::e_y=row0y;Fl::e_dx=0;Fl::e_dy=2;Fl::e_state=FL_SHIFT;
        view->handle(FL_MOUSEWHEEL);check(scrollbar(*view,true)->value()>0,"Shift-wheel scrolls horizontally");
        Fl::e_state=0;click(*view,name_x,row0y);check(opened.back()==long_entry.path,"horizontal scrolling keeps name links correct");
        view->palette(true);view->redraw();Fl::check();view->palette(false);Fl::check();
        // Owner destruction removes the pending timeout. No callback may survive.
        window.begin();auto pending=std::make_unique<cpv::RetroListing>(10,10,940,460);window.end();
        pending->set_listing(listing,listing.entries);pending->open=[&](const cpv::Entry&){opened.push_back("stale");};
        key(*pending,FL_Home);key(*pending,FL_Enter);const auto old_count=opened.size();pending.reset();Fl::check();
        check(opened.size()==old_count,"destruction cancels deferred opening");
        view->clear();check(!view->selected_entry()&&view->scroll_position()==0,"clear drops selection and scroll state");
        window.hide();
        std::cout<<"Retro widget tests passed ("<<checks<<" checks).\n";
        return EXIT_SUCCESS;
    } catch(const std::exception& error) {
        std::cerr<<"Retro widget test failed: "<<error.what()<<'\n';return EXIT_FAILURE;
    }
}
