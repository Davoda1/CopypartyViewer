// Copyparty Viewer 2.1.0 — Copyright (C) 2026 davoda1
// SPDX-License-Identifier: MIT
#pragma once
#include <cstddef>
#include <string>
#include <vector>

namespace cpv {
struct DecodedText {
    std::string text, encoding;
    bool truncated = false;
};
// Decode a bounded preview to valid UTF-8. Original download bytes are untouched.
// Auto: BOM, declared charset, strong Unicode byte alignment, valid UTF-8,
// then CP1250. Ambiguous legacy files can be selected explicitly as CP1252.
// NUL and line endings are retained for the existing text filter/preparer.
DecodedText decode_text(const std::vector<unsigned char>& bytes,
                        const std::string& choice = "auto",
                        const std::string& content_type = "",
                        std::size_t byte_limit = 8u * 1024u * 1024u);
}
