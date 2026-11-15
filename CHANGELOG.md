# Changelog

## 2.0.0

- Adds native Markdown rendering for headings, emphasis, lists, quotations, tables, task markers, links and code blocks, with a Source / Rendered switch.
- Adds lexical highlighting for common source/configuration files and Markdown code blocks. Settings offers Default, VS Code, GitHub, Monokai, Nord, Dracula, Solarized, Codex, Xcode Classic and Ayu, with automatic light/dark palettes.
- Adds basic RTF previews with styles, font sizes, RGB colors, paragraphs and alignment; supports CP1250, CP1252, declared UTF-8 and Unicode escapes while ignoring embedded resources and field instructions.
- Adds CSV/TSV table previews with quoted delimiters, escaped quotes, multiline and empty cells, automatic comma/semicolon/tab detection and optional Excel separator directives.
- Adds CP1250/CP1252 and UTF-8/16/32 decoding, BOM/charset detection and a file encoding selector that reuses already fetched bytes. CRLF, LF and CR line endings are normalized for viewing; downloads preserve original bytes.
- Fixes inconsistent size comparisons when known and unknown values mix. Unknown sizes follow known sizes in either direction, folders remain first, and ties use deterministic name/path comparisons.
- Resolves explicit document links through the configured server boundary or opens HTTP/HTTPS/query links in the browser without attaching application credentials. Long and duplicate Markdown headings use safe native anchor identifiers.
- Bounds formatted previews and falls back to source with an explanation for oversized or unsupported documents. Raw HTML is escaped, Markdown images become alternative-text links, and CSV values never execute formulas.
- Preserves existing Modern/Retro navigation, image previews, account-saving behavior and settings compatibility.
- Retains the public demo URL, empty compiled credentials, MIT licensing, pinned offline dependencies and the existing native build/security/publication gates. Adds MD4C and Unicode mapping-data notices.

## 1.0.0

Initial cross-platform release of **Copyparty Viewer**, by **davoda1**.

- Licenses project-owned code and documentation under MIT; third-party licenses and required notices remain included.
- Uses shared C++17, FLTK 1.4.5 and libcurl code for Windows x64 and Linux X11 amd64.
- Moves network access, directory parsing and preview decoding to workers. Adds a bottom status-bar activity indicator, transfer progress and Cancel.
- Adds a sortable, clickable directory table, Back/Forward/Up/Refresh, filtering and keyboard navigation while keeping the editable path and visible login fields.
- Adds read-only text with ANSI color/bold, and PNG, JPEG, WebP, SVG, BMP, ICO and GIF previews. Unsupported files remain downloadable.
- Adds a separate, modeless Settings window using the supplied settings gear.
- Stores the server, portable obfuscated credentials, last path and window geometry in one per-user JSON file. Restores the last path and fetches automatically after startup.
- Includes verified HTTPS, same-origin redirects, bounded previews, transfer timeouts and safe, cancellable single-file downloads.
- Starts at the public demo server `https://a.ocv.me/pub/` with blank embedded username and password defaults. Retains optional custom build defaults and saved server settings.
- Provides a GNU Make build interface, pinned offline dependency sources, a Windows portable ZIP, a Debian package and a Linux manual-install archive. Every desktop entry points to the real application executable.
- Publishes three binary assets through GitHub Releases, with GitHub-generated source archives and GitHub-computed asset SHA-256 digests.
- Uses neutral toolchain examples and omits machine-local build paths from public diagnostic metadata while retaining the pinned upstream source archives.

### Interface and settings

- Fits Modern-view columns to the available width automatically.
- Adds a clickable, monospaced Retro directory view and an animated Modern/Retro switch in Settings; remembers the selected presentation in the existing JSON file.
- Adds a saved directory-size preference, enabled by default; file sizes remain visible and color-coded in both views.
- Makes main-window username and password edits apply to the running session. Only Settings Save changes the persistent account.
- Applies native Windows dark/light title-bar styling to both main and Settings windows, with safe system-theme fallback on unsupported Windows versions and preserved high-contrast colors.
- Keeps version information in release metadata and documentation while leaving title bars and executable filenames without a version suffix.
- Uses readable Modern listing text and headings, and large monospaced Retro text.
- Normalizes Retro timestamps to `YYYY-MM-DD HH:MM:SS` for both genuine server rows and the local formatting fallback.
- Removes dotted focus outlines from controls and listings while preserving keyboard navigation, shortcuts and selection.
