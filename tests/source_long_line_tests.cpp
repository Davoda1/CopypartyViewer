// SPDX-License-Identifier: MIT
// Copyright (C) 2026 davoda1
// Optional native checks for intact Source lines beyond the preview input cap.
#include <FL/Fl.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Image_Surface.H>
#include <FL/Fl_RGB_Image.H>
#include <FL/Fl_Scrollbar.H>
#include <FL/Fl_Text_Buffer.H>
#include <FL/Fl_Text_Display.H>
#include <FL/fl_draw.H>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
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
class SourceProbe : public Fl_Text_Display {
public:
    using Fl_Text_Display::Fl_Text_Display;
    double width(const std::string& s) const {
        return string_width(s.data(), static_cast<int>(s.size()), 'A');
    }
    int character_at(const std::string& s, int x) const {
        return find_x(s.data(), static_cast<int>(s.size()), 'A', x);
    }
    Fl_Scrollbar* horizontal_bar() const { return mHScrollBar; }
    int viewport_width() const { return text_area.w; }
};

struct Pixels { unsigned marker = 0, selection = 0; };
Pixels painted_pixels(SourceProbe& view) {
    Fl_Image_Surface surface(view.w(), view.h());
    Fl_Surface_Device::push_current(&surface);
    surface.draw(&view);
    std::unique_ptr<Fl_RGB_Image> image(surface.image());
    Fl_Surface_Device::pop_current();
    check(image && image->d() >= 3, "native image surface is available");
    const auto* data = reinterpret_cast<const unsigned char*>(image->data()[0]);
    const int stride = image->ld() ? image->ld() : image->w() * image->d();
    Pixels pixels;
    for (int y = 0; y < image->h(); ++y) for (int x = 0; x < image->w(); ++x) {
        const auto* p = data + y * stride + x * image->d();
        if (p[0] > 150 && p[1] < 100 && p[2] > 100) ++pixels.marker;
        if (p[0] == 0x24 && p[1] == 0x68 && p[2] == 0xac) ++pixels.selection;
    }
    return pixels;
}

const std::string begin_marker = "BEGIN_";
const std::string end_marker = "_THE_END";
void run_line(SourceProbe& view, Fl_Text_Buffer& buffer, Fl_Text_Buffer& styles,
              const std::string& text, double expected_width) {
    std::cout << "Checking " << context << std::endl;
    std::string style(text.size(), 'A');
    std::fill_n(style.begin(), begin_marker.size(), 'B');
    std::fill(style.end() - end_marker.size(), style.end(), 'B');
    buffer.unselect();
    styles.text(style.c_str());
    buffer.text(text.c_str());
    view.scroll(1, 0);
    Fl::check();

    const double width = view.width(text);
    check(width > view.viewport_width(), "the fixture extends beyond the viewport");
    if (expected_width > 0)
        check(std::abs(width - expected_width) <= 1.0,
              "long width equals the sum of independently measured short text");
    check(view.horizontal_bar()->visible(), "long Source line exposes horizontal scrolling");
    const int maximum = static_cast<int>(view.horizontal_bar()->maximum());
    check(std::abs(maximum + view.viewport_width() - width) <= 2.0,
          "scrollbar maximum includes the complete source width");
    check(painted_pixels(view).marker > 0, "beginning marker is painted");

    view.scroll(1, maximum);
    Fl::check();
    check(view.scroll_col() == maximum, "the end of the line is reachable");
    check(painted_pixels(view).marker > 0, "ending marker is painted at the scroll maximum");
    const double suffix_width = view.width(end_marker);
    const int suffix_start = static_cast<int>(text.size() - end_marker.size());
    const int suffix_x = static_cast<int>(width - suffix_width + view.width("_") * 0.25);
    check(view.character_at(text, suffix_x) == suffix_start,
          "a far-right character lookup reaches the correct original byte");
    check(view.character_at(text, -suffix_x) == suffix_start,
          "a far-right cursor lookup reaches the nearest original boundary");

    buffer.select(0, buffer.length());
    Fl::focus(&view);
    Fl::check();
    check(painted_pixels(view).selection > 0,
          "selection background remains visible at the huge horizontal offset");
    using Text = std::unique_ptr<char, decltype(&std::free)>;
    Text selected(buffer.selection_text(), &std::free);
    check(selected && text == selected.get(), "selection preserves every original source byte");
    Text retained(buffer.text(), &std::free);
    check(retained && text == retained.get(), "painting and scrolling do not alter the source");
    check(styles.length() == buffer.length(), "the source style length is unchanged");
    buffer.unselect();
}
} // namespace

int main() {
    try {
        Fl::scheme("gtk+");
        Fl_Text_Buffer buffer, styles;
        Fl_Text_Display::Style_Table_Entry table[] = {
            {FL_BLACK, FL_COURIER, 14, 0, FL_WHITE},
            {fl_rgb_color(200, 10, 180), FL_COURIER, 14, 0, FL_WHITE}
        };
        Fl_Double_Window window(760, 220, "Source line regression checks");
        SourceProbe view(10, 10, 740, 200);
        view.textfont(FL_COURIER);
        view.textsize(14);
        view.color(FL_WHITE);
        view.selection_color(fl_rgb_color(0x24, 0x68, 0xac));
        view.buffer(buffer);
        view.highlight_data(&styles, table, 2, 0, nullptr, nullptr);
        window.end();
        window.show();
        Fl::check();

        context = "25 MiB + 1 unbroken Source line";
        const std::size_t size = 25u * 1024u * 1024u + 1u;
        std::string text = begin_marker + std::string(size - begin_marker.size() - end_marker.size(), 'x') + end_marker;
        const double expected = view.width(begin_marker) +
            (size - begin_marker.size() - end_marker.size()) * view.width("x") + view.width(end_marker);
        check(expected > 32767.0, "fixture exceeds the native signed-short advance range");
        run_line(view, buffer, styles, text, expected);

        context = "UTF-8 and ordinary composed boundaries";
        const std::string unit = u8"e\u0301\u4e2d\U0001f642";
        text = begin_marker;
        for (int i = 0; i < 5000; ++i) text += unit;
        text += end_marker;
        run_line(view, buffer, styles, text,
                 view.width(begin_marker) + 5000 * view.width(unit) + view.width(end_marker));
        // Compare with FLTK's established short-run behavior. Font fallback
        // can give the combining mark a nonzero advance; do not assume that
        // its glyph width or cursor boundary is the same as the base letter.
        const std::string short_text = begin_marker + unit + unit;
        const int first_x = static_cast<int>(view.width(begin_marker));
        const int last_x = static_cast<int>(view.width(begin_marker + unit));
        for (int x = first_x; x <= last_x; ++x) {
            check(view.character_at(text, x) == view.character_at(short_text, x),
                  "long-run character lookup matches the equivalent short text");
            check(view.character_at(text, -x) == view.character_at(short_text, -x),
                  "long-run cursor lookup preserves the short-text boundaries");
        }

        context = "composed tag sequence larger than the native batch bound";
        text = begin_marker + u8"\U0001f3f4";
        for (int i = 0; i < 20000; ++i) text += u8"\U000e0061";
        text += u8"\U000e007f";
        text += std::string(20000, 'x') + end_marker;
        run_line(view, buffer, styles, text, 0);
        window.hide();
        std::cout << "PASS " << checks << " Source long-line checks\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
