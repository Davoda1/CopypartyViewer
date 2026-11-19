// Copyright (C) 2026 davoda1 — SPDX-License-Identifier: MIT
#pragma once
#include "document.hpp"
#include <string>

namespace cpv::document {
bool csv_supported(const std::string& filename, const std::string& content_type = "");

// Input is decoded UTF-8 text. Comma, semicolon and tab delimiters are detected
// outside quoted fields; an optional Excel sep= line explicitly selects one.
// At a new unquoted record, # after optional spaces starts a comment line.
// Tabs are delimiters, not comment indentation; quoted # and later cells stay
// data. The first non-comment record is bold without changing its cell values.
// This preview never evaluates formulas or creates active links/images.
// Malformed or oversized tables throw for Source fallback.
RenderedDocument render_csv(const std::string& decoded_utf8,
    const std::string& foreground_hex, const std::string& background_hex,
    const std::string& border_hex);
} // namespace cpv::document
