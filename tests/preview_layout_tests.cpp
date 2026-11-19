// SPDX-License-Identifier: MIT
// Copyright (C) 2026 davoda1
// Optional display-backed checks of the real FLTK layout and painted pixels.
// Keep this separate from routine headless CTest, like retro-tests.
#include "document.hpp"
#include "csv.hpp"
#include <FL/Fl.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Help_View.H>
#include <FL/Fl_Image_Surface.H>
#include <FL/Fl_RGB_Image.H>
#include <FL/Fl_Scrollbar.H>
#include <FL/fl_draw.H>
#include <algorithm>
#include <climits>
#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>

namespace {
unsigned checks = 0;
std::string context;
void check(bool okay, const char* message) {
    ++checks;
    if (!okay) throw std::runtime_error(context + ": " + message);
}

// A distinctive background makes the selected cell's real painted bounds
// measurable without depending on glyph shapes, private FLTK fields, or OCR.
std::string mark_column_cells(std::string html, unsigned columns, unsigned selected) {
    unsigned cell = 0;
    for (std::size_t at = 0; (at = html.find('<', at)) != std::string::npos; ++at) {
        if (html.compare(at, 3, "<TD") && html.compare(at, 3, "<TH")) continue;
        const auto end = html.find('>', at);
        if (end == std::string::npos) throw std::runtime_error("Unclosed fixture cell");
        if (cell++ % columns != selected) continue;
        const auto color = html.find("BGCOLOR=\"", at);
        if (color < end) {
            const auto value = color + 9, finish = html.find('"', value);
            html.replace(value, finish - value, "#2468ac");
        } else html.insert(end, " BGCOLOR=\"#2468ac\"");
    }
    return html;
}
std::string mark_last_cells(std::string html, unsigned columns) {
    return mark_column_cells(html, columns, columns - 1);
}

struct Bounds { int left = INT_MAX, right = -1, top = INT_MAX, bottom = -1; };
Bounds painted_cell(Fl_Help_View& view, bool required = true) {
    Fl_Image_Surface surface(view.w(), view.h());
    Fl_Surface_Device::push_current(&surface);
    fl_color(FL_WHITE);
    fl_rectf(0, 0, view.w(), view.h());
    surface.draw(&view);
    std::unique_ptr<Fl_RGB_Image> image(surface.image());
    Fl_Surface_Device::pop_current();
    check(image && image->w() == view.w() && image->h() == view.h(),
          "native image surface has the requested logical size");
    const auto* bytes = reinterpret_cast<const unsigned char*>(image->data()[0]);
    const int stride = image->ld() ? image->ld() : image->w() * image->d();
    Bounds bounds;
    for (int y = 0; y < image->h(); ++y) for (int x = 0; x < image->w(); ++x) {
        const auto* pixel = bytes + y * stride + x * image->d();
        if (pixel[0] == 0x24 && pixel[1] == 0x68 && pixel[2] == 0xac) {
            bounds.left = std::min(bounds.left, x); bounds.right = std::max(bounds.right, x);
            bounds.top = std::min(bounds.top, y); bounds.bottom = std::max(bounds.bottom, y);
        }
    }
    if (required) check(bounds.right >= bounds.left, "the actual final cell is visible");
    return bounds;
}

int viewport_right(const Fl_Help_View& view) {
    auto& mutable_view = const_cast<Fl_Help_View&>(view);
    return view.w() - Fl::box_dw(view.box()) + Fl::box_dx(view.box()) -
           (mutable_view.scrollbar()->visible() ? mutable_view.scrollbar()->w() : 0);
}
void check_scrollbars(Fl_Help_View& view) {
    for (auto* bar : {view.scrollbar(), view.hscrollbar()}) {
        if (!bar->visible()) continue;
        check(bar->x() >= view.x() + Fl::box_dx(view.box()) &&
              bar->y() >= view.y() + Fl::box_dy(view.box()) &&
              bar->x() + bar->w() <= view.x() + view.w() - Fl::box_dw(view.box()) + Fl::box_dx(view.box()) &&
              bar->y() + bar->h() <= view.y() + view.h() - Fl::box_dh(view.box()) + Fl::box_dy(view.box()),
              "scrollbar stays inside the preview border");
    }
    if (view.hscrollbar()->visible() && view.scrollbar()->visible())
        check(view.hscrollbar()->x() + view.hscrollbar()->w() <= view.scrollbar()->x(),
              "horizontal and vertical scrollbars do not overlap");
}
int check_right_inset(Fl_Help_View& view) {
    const auto bounds = painted_cell(view);
    const int inset = viewport_right(view) - 1 - bounds.right;
    check(inset >= 3 && inset <= 8, "final cell retains a small inset before the viewport edge");
    check_scrollbars(view);
    return inset;
}
void resize(Fl_Double_Window& window, Fl_Help_View& view, int width, int height) {
    window.resize(window.x(), window.y(), width + 20, height + 20);
    view.resize(10, 10, width, height);
    Fl::check();
}
std::string markdown_table(unsigned columns, unsigned rows, bool wrapping) {
    std::string text;
    for (unsigned c = 0; c < columns; ++c) text += "| h";
    text += "|\n";
    for (unsigned c = 0; c < columns; ++c) text += "| ---";
    text += "|\n";
    for (unsigned r = 0; r < rows; ++r) {
        for (unsigned c = 0; c < columns; ++c) {
            text += '|';
            const unsigned words = wrapping ? (c % 2 ? 21u : 12u) : 1u;
            for (unsigned word = 0; word < words; ++word) text += " x";
        }
        text += "|\n";
    }
    return text;
}
std::string markdown_html(const std::string& text) {
    return cpv::document::render_document(text, "default", false,
               "#202020", "#ffffff", "#2369a8").html;
}

void full_width_tables(Fl_Double_Window& window, Fl_Help_View& view) {
    for (const bool wrapping : {false, true}) for (const unsigned columns : {2u, 3u, 7u}) {
        const auto html = mark_last_cells(markdown_html(markdown_table(columns, 1, wrapping)), columns);
        resize(window, view, 323, 360);
        view.value(html.c_str());
        int reference_inset = -1;
        // Consecutive widths exercise indivisible equal/proportional column
        // allocations. Resize the existing document rather than reloading it.
        for (int width = 323; width <= 335; ++width) {
            context = std::string(wrapping ? "proportional" : "expanded") +
                      " Markdown, columns=" + std::to_string(columns) + ", width=" + std::to_string(width);
            resize(window, view, width, 360);
            check(!view.scrollbar()->visible(), "short table does not reserve a vertical scrollbar");
            check(!view.hscrollbar()->visible(), "fitting table needs no horizontal scrolling");
            const int inset = check_right_inset(view);
            if (reference_inset >= 0) check(inset == reference_inset, "column rounding does not lose right-edge width");
            reference_inset = inset;
        }
    }
    for (const int text_size : {15, 19, 23}) {
        context = "tall Markdown, text size=" + std::to_string(text_size);
        view.textsize(text_size);
        const auto html = mark_last_cells(markdown_html(markdown_table(3, 45, false)), 3);
        view.value(html.c_str());
        for (const int width : {331, 679, 1001, 332}) {
            resize(window, view, width, 240);
            check(view.scrollbar()->visible(), "tall table reserves its vertical scrollbar");
            check(!view.hscrollbar()->visible(), "full-width table fits beside its vertical scrollbar");
            check_right_inset(view);
        }
    }
    view.textsize(15);
}

void wide_csv(Fl_Double_Window& window, Fl_Help_View& view) {
    for (const unsigned rows : {2u, 40u}) {
        context = "64-column CSV, rows=" + std::to_string(rows);
        std::string csv;
        for (unsigned r = 0; r < rows; ++r) {
            for (unsigned c = 0; c < 64; ++c) {
                if (c) csv += ',';
                csv += c == 63 ? "FINAL_COLUMN_REACHABLE" : "column_" + std::to_string(c) + std::string(20, 'W');
            }
            csv += '\n';
        }
        const auto html = mark_last_cells(cpv::document::render_csv(csv,
                           "#202020", "#ffffff", "#b8bec5").html, 64);
        view.value(html.c_str());
        for (const int width : {511, 800, 512}) {
            resize(window, view, width, 240);
            check(view.hscrollbar()->visible(), "wide CSV has horizontal scrolling");
            check(bool(view.scrollbar()->visible()) == (rows > 2), "CSV vertical scrollbar follows its row height");
            check(view.hscrollbar()->maximum() > 5000, "long unbreakable columns retain a meaningful scroll range");
            view.leftline(INT_MAX);
            check(view.leftline() == int(view.hscrollbar()->maximum()) &&
                  view.leftline() == int(view.hscrollbar()->value()), "rightmost document position equals the scrollbar maximum");
            check_right_inset(view);
            const auto bounds = painted_cell(view);
            check(bounds.right - bounds.left > 100, "the final CSV column retains its content width");
            view.leftline(-100);
            check(view.leftline() == 0 && view.hscrollbar()->value() == 0, "left scrolling clamps consistently to zero");
        }
    }
}

void wide_code(Fl_Double_Window& window, Fl_Help_View& view) {
    context = "wide code with repeated spaces";
    const auto html = mark_last_cells(markdown_html("```text\nleft" + std::string(160, ' ') + "RIGHT_END\n```\n"), 1);
    resize(window, view, 533, 240);
    view.value(html.c_str());
    check(view.hscrollbar()->visible(), "unwrapped code line remains horizontally scrollable");
    check(view.hscrollbar()->maximum() > 500, "repeated PRE spaces remain part of the code line width");
    view.leftline(INT_MAX);
    check_right_inset(view);
    context = "short document after wide code";
    view.value("<HTML><BODY>Short text.</BODY></HTML>");
    check(!view.hscrollbar()->visible() && !view.scrollbar()->visible() && view.leftline() == 0,
          "changing documents clears unnecessary scrollbars and horizontal position");
}

void byte_table(Fl_Double_Window& window, Fl_Help_View& view) {
    const auto hex = [](unsigned value) {
        const char* digits = "0123456789ABCDEF";
        return std::string{digits[(value >> 4) & 15], digits[value & 15]};
    };
    std::string markdown;
    for (unsigned column = 0; column < 16; ++column) markdown += "| " + hex(column);
    markdown += "|\n";
    for (unsigned column = 0; column < 16; ++column) markdown += "| ---";
    markdown += "|\n";
    for (unsigned row = 0; row < 16; ++row) {
        for (unsigned column = 0; column < 16; ++column) markdown += "| " + hex(row * 16 + column);
        markdown += "|\n";
    }
    const auto html = markdown_html(markdown);
    int widest_byte = 1;
    for (const auto font : {view.textfont(), view.textfont() | FL_BOLD}) {
        fl_font(font, view.textsize());
        for (unsigned value = 0; value < 256; ++value)
            widest_byte = std::max(widest_byte, int(std::ceil(fl_width(hex(value).c_str()))));
    }
    // Four measured byte cells cannot contain a sixteen-column table. Derive
    // the wide case from its observed scroll extent, not assumed font metrics.
    const int narrow_width = std::max(120, 4 * widest_byte);
    context = "00-FF byte table, initial narrow viewport";
    resize(window, view, narrow_width, 500);
    view.value(mark_last_cells(html, 16).c_str());
    check(view.hscrollbar()->visible(), "sixteen byte columns require horizontal scrolling in the narrow fixture");
    const int wide_width = 2 * narrow_width + int(view.hscrollbar()->maximum());
    for (const int width : {narrow_width, wide_width}) {
        resize(window, view, width, 500);
        context = "00-FF byte table, width=" + std::to_string(width);
        check(bool(view.hscrollbar()->visible()) == (width == narrow_width),
              "byte-table overflow follows the actual viewport width");
        for (unsigned column = 0; column < 16; ++column) {
            context = "00-FF byte table, width=" + std::to_string(width) + ", column=" + hex(column);
            const auto marked = mark_column_cells(html, 16, column);
            view.value(marked.c_str());
            const int maximum = int(view.hscrollbar()->maximum());
            const int step = std::max(1, (viewport_right(view) - Fl::box_dx(view.box())) / 4);
            bool accessible = false;
            for (int position = 0;; position = std::min(maximum, position + step)) {
                view.leftline(position);
                const auto bounds = painted_cell(view, false);
                if (bounds.right >= bounds.left && bounds.left > Fl::box_dx(view.box()) &&
                    bounds.right < viewport_right(view) - 1) {
                    accessible = true;
                    break;
                }
                if (position == maximum) break;
            }
            check(accessible, "every byte column can be displayed completely inside the viewport");
            if (column == 15) {
                view.leftline(INT_MAX);
                check(view.leftline() == int(view.hscrollbar()->maximum()) &&
                      view.leftline() == int(view.hscrollbar()->value()),
                      "byte-table rightmost position equals its scrollbar maximum");
                check_right_inset(view);
            }
        }
    }
}
} // namespace

int main(int argc, char** argv) {
    try {
        Fl::scheme("gtk+");
        Fl_Double_Window window(720,500,"Copyparty Viewer preview layout checks");
        auto* view = new Fl_Help_View(10,10,700,480);
        view->box(FL_BORDER_BOX); view->textsize(15);
        window.end(); window.show(); Fl::check();
        full_width_tables(window, *view);
        wide_csv(window, *view);
        wide_code(window, *view);
        byte_table(window, *view);
        if (argc > 1) {
            context = "reference README";
            std::ifstream file(argv[1], std::ios::binary);
            check(bool(file), "reference file opens");
            const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            const auto html = markdown_html(text);
            view->value(html.c_str());
            for (const int width : {656, 856, 1100, 657}) {
                resize(window, *view, width, 492);
                check_scrollbars(*view);
                view->leftline(INT_MAX);
                check(view->leftline() == int(view->hscrollbar()->maximum()), "reference horizontal range clamps consistently");
                view->leftline(0);
            }
        }
        window.hide();
        std::cout << "PASS: " << checks << " native preview layout checks\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL after " << checks << " checks: " << error.what() << '\n';
        return 1;
    }
}
