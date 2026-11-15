#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace cpv {
struct ImageData { int width = 0, height = 0, channels = 0; std::vector<unsigned char> pixels; std::string description; };
bool image_supported(const std::string& filename, const std::string& content_type);
// Runs on a worker. Never draws, accesses a widget, or owns a native window.
// Enforce decoded-pixel limits and reject malformed/unsupported data clearly.
ImageData decode_image(const std::vector<unsigned char>& bytes,
                       const std::string& filename, const std::string& content_type);
bool text_supported(const std::string& filename, const std::string& content_type,
                    const std::vector<unsigned char>& bytes,
                    bool allow_encoding_override = false);
// Remove terminal control sequences, normalize newlines, replace invalid UTF-8 safely.
std::string prepare_text(const std::vector<unsigned char>& bytes);
// Byte-for-byte FLTK style map: A default, B bold; C..J = black, red,
// green, yellow, blue, magenta, cyan, white; K..R = bold versions.
struct PreparedText { std::string text, styles; };
PreparedText prepare_styled_text(const std::vector<unsigned char>& bytes);
}
