#pragma once
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace cpv {
inline constexpr const char* version = "1.0.0";
struct Geometry { int x = -1, y = -1, w = 1100, h = 700; bool maximized = false; };
struct Config {
    std::string server, username, password, last_path = "/";
    bool modern_ui = true;
    bool show_directory_sizes = true;
    // Only explicit Settings Save promotes credentials to persistent values.
    bool credentials_saved = false;
    Geometry main_window;
    Geometry settings_window{-1, -1, 560, 360, false};
};
std::filesystem::path settings_path();
Config default_config();
Config load_config(const std::filesystem::path&, std::string& warning);
bool save_config(const std::filesystem::path&, const Config&, std::string& error);
std::string obfuscate(const std::string&);
std::string deobfuscate(const std::string&);

// Canonical path is decoded UTF-8, absolute within the configured server base.
// This routine never accepts a foreign origin, credentials, fragments, or queries.
std::string normalize_server(const std::string&);
std::string resolve_path(const std::string& input, const std::string& current,
                         const std::string& server);
std::string make_url(const std::string& server, const std::string& path);
std::string parent_path(const std::string& path);
std::string basename(const std::string& path);
std::string human_size(std::uint64_t);
std::string format_time(std::int64_t epoch);
// Copyparty's terse terminal size format and ANSI foreground index (0..7).
std::string terminal_size(std::uint64_t);
int size_color_index(std::uint64_t);
// Display terminal timestamps in UTC as YYYY-MM-DD HH:MM:SS, retaining SGR.
std::string terminal_time(std::int64_t epoch);
std::string terminal_time_ansi(std::int64_t epoch);
std::string safe_error(const std::string&, const Config&);

struct Entry {
    std::string name, path;
    bool directory = false;
    std::uint64_t size = 0;
    std::int64_t modified = 0;
    bool size_known = false;
    bool directory_size_indexed = false;
    // Optional sanitized ?ls=v row with a display-formatted timestamp,
    // mapped only to a verified JSON entry name.
    std::string terminal_line_ansi;
};
struct Listing {
    std::vector<Entry> entries;
    std::string path, account, server_info, header_ansi;
    std::vector<std::string> permissions;
};
Listing parse_listing(const std::string& json, const std::string& requested_path);
std::string terminal_entry_ansi(const Entry&, bool show_directory_sizes = true);
// Match the server's compact timestamps and exact JSON names first, then format
// timestamps for display. Terminal text never supplies navigation URLs.
// Returns the number of valid server rows attached to verified entries.
std::size_t apply_terminal_listing(Listing&, const std::string& terminal);

struct TransferControl {
    std::atomic<bool> cancel{false};
    std::atomic<std::uint64_t> received{0}, total{0};
};
struct Response {
    std::vector<unsigned char> bytes;
    std::string content_type, final_url;
    long status = 0;
};
enum class FetchKind { Listing, File, Auto, Terminal };
// One worker owns its curl handle. Requests never change the server.
Response fetch(const Config& snapshot, const std::string& path, FetchKind,
               TransferControl&, std::size_t max_bytes = 64u * 1024u * 1024u);
// Saves through a sibling temporary file; cancellation/failure leaves target intact.
void download(const Config& snapshot, const std::string& path,
              const std::filesystem::path& target, TransferControl&);
void network_init();
void network_cleanup();
} // namespace cpv
