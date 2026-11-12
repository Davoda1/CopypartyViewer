// SPDX-License-Identifier: MIT
// Copyright (C) 2026 davoda1
#pragma once
#include "core.hpp"
#include <FL/Fl_Group.H>
#include <array>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
class Fl_Scrollbar;

namespace cpv {
// A terminal-style directory panel. Its rows keep structured Entry identities;
// displayed text is never parsed as a URL or used as a navigation target.
class RetroListing final : public Fl_Group {
public:
    // FLTK logical font size: physical pixels follow the window's DPI scale.
    static constexpr int text_size = 22;
    RetroListing(int x, int y, int width, int height);
    ~RetroListing() override;
    std::function<void(const Entry&)> open;

    void set_listing(const Listing&, const std::vector<Entry>& visible_entries,
                     bool show_directory_sizes = true);
    void set_header_ansi(const std::string& header);
    void clear();
    void palette(bool dark);
    // Shared with the modern table: first visible entry index, not pixel offset.
    // Row zero restores the top of the panel, including the terminal header.
    int scroll_position() const;
    void scroll_position(int first_entry);
    const Entry* selected_entry() const;
    void resize(int x, int y, int width, int height) override;
    int handle(int event) override;

private:
    struct Line {
        std::string text, styles;
        std::vector<unsigned char> reversed;
        std::size_t name_begin = 0, name_end = 0;
        double width = -1, name_x = -1, name_width = -1;
    };
    Fl_Scrollbar *vertical_ = nullptr, *horizontal_ = nullptr;
    std::vector<Entry> entries_;
    std::vector<Line> header_;
    std::unordered_map<std::size_t, Line> row_cache_;
    std::array<Fl_Color,18> colors_{};
    Fl_Color background_ = FL_WHITE;
    std::string model_path_;
    std::optional<Entry> pending_open_;
    bool show_sizes_ = true;
    int selected_ = -1, hovered_ = -1, pressed_ = -1;
    int line_height_ = text_size + 8, baseline_ = text_size + 2;
    int viewport_w_ = 0, viewport_h_ = 0;
    int content_width_ = 0, content_height_ = 0;
    std::size_t longest_name_columns_ = 0;
    std::size_t prefix_columns_ = 25;
    double cell_width_ = text_size * 0.6;

    void draw() override;
    void draw_line(Line&, int screen_y, bool selected, bool hover);
    static Line parse_line(const std::string& ansi);
    Line& row_line(std::size_t row);
    void measure(Line&);
    void metrics();
    void scrollbars();
    void move_scroll(int x, int y);
    void ensure_visible(int row);
    int hit_name(int x, int y);
    void hover(int row);
    void queue_open(int row);
    static void open_timeout(void*);
};
} // namespace cpv
