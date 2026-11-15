// Copyright (C) 2026 davoda1 — SPDX-License-Identifier: MIT
#pragma once
#include <string>
#include <unordered_map>
#include <vector>

namespace cpv::document {
struct Theme { const char* id; const char* label; };
struct Palette {
    std::string background, foreground, header, label, border;
    std::string comment, keyword, string, number, title, type, attribute;
    std::string variable, meta, op, selector, addition, addition_background;
    std::string deletion, deletion_background;
};
const std::vector<Theme>& themes();
Palette palette(const std::string& theme_id, bool dark);

// One style byte per source byte, including every byte of UTF-8 characters.
// A plain, B comment, C keyword, D string, E number, F title/function,
// G type, H attribute, I variable, J metadata, K operator/punctuation.
// This is lexical coloring, not a language parser or validator.
std::string syntax_styles(const std::string& text, const std::string& language);
std::string language_for_filename(const std::string& filename);
bool markdown_supported(const std::string& filename, const std::string& content_type = "");

struct RenderedDocument {
    std::string html;
    // HTML contains only cpv:<index> links. Resolve these through this registry
    // in the host callback; never hand their targets back to Fl_Help_View.
    std::vector<std::string> links;
    // Full heading slug -> short native NAME. FLTK limits target names to 31
    // bytes, so the host must look up decoded fragments before calling topline.
    std::unordered_map<std::string, std::string> anchors;
};
RenderedDocument render_document(const std::string& text, const std::string& theme_id,
    bool dark, const std::string& foreground_hex, const std::string& background_hex,
    const std::string& link_hex);
std::string render_markdown(const std::string& text, const std::string& theme_id,
    bool dark, const std::string& foreground_hex, const std::string& background_hex,
    const std::string& link_hex);
} // namespace cpv::document
