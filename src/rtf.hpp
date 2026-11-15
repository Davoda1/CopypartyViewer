// Copyright (C) 2026 davoda1 — SPDX-License-Identifier: MIT
#pragma once
#include "document.hpp"
#include <string>

namespace cpv::document {
bool rtf_supported(const std::string& filename, const std::string& content_type = "");

// Basic text formatting only. Input must be original RTF bytes, not a UTF-8
// replacement/preview conversion. Throws std::runtime_error for malformed or
// unsupported encodings so the host can offer its unchanged Source view.
// No links, pictures, objects, fonts, or external resources are loaded.
RenderedDocument render_rtf(const std::string& raw, const std::string& foreground_hex,
    const std::string& background_hex, const std::string& link_hex);
} // namespace cpv::document
