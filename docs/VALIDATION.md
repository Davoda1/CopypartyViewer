# Release validation — Copyparty Viewer 2.1.0

Copyright (C) 2026 davoda1. Local evidence recorded on **2026-11-19**.

## Scope and release requirements

This report records source, build and desktop checks actually performed on the public 2.1.0 source and freshly built local executables. Exact package assets and SHA-256 verification results are recorded separately. Hosted CI and CodeQL remain pending for this commit; hosted checks and uploaded assets must be verified for the exact release commit before publication. The authoritative hosted results are the commit's [GitHub Actions runs](https://github.com/Davoda1/CopypartyViewer/actions); a configured workflow alone is not evidence of a passing run.

| Evidence | Scope |
| --- | --- |
| Local Linux execution | Native amd64 Linux, including the display-backed Retro harness. All eight configured CTest suites and both optional preview GUI harnesses passed. |
| Local Windows build | A fresh x64 cross-build of the application, all configured test executables and both optional preview GUI harnesses, followed by PE, metadata and privacy inspection. These local Windows executables were not run. |
| Hosted native CI | Ubuntu 24.04 and Windows Server 2025 build and run all eight suites, verify their inventory and inspect runtime packages. Publication requires the Repository, Linux and Windows jobs to pass. |
| Security analysis | Publication requires completed CodeQL analysis for the exact master commit, with no analysis errors, warnings or open findings. |
| Uploaded release assets | Publication downloads and verifies the exact three assets against approved SHA-256 hashes, package structure and current repository documentation. |

The public default is `https://a.ocv.me/pub/`, with **empty embedded username and password**. The application's `/` denotes that base directory, and saved user settings override compiled defaults. Public-default regression checks exercise the first-start path, configuration fallback and serialization as well as the literal default values.

The local Linux compatibility build uses GCC 10.2.1 with a Debian 11/glibc 2.31 baseline. The Windows cross-build uses MinGW GCC 14 with the POSIX thread model, native FLTK windows and Schannel. Application libraries and applicable GNU runtimes are linked statically; normal platform libraries remain necessary. Dependencies and hashes are pinned in `vendor/dependencies.json`, `vendor/sources/SHA256SUMS` and `cmake/Sources.cmake`, including FLTK 1.4.5 and MD4C 0.5.2.

## Observed local automated results

Both fresh application builds and every configured test executable compiled successfully. No application compiler warnings were observed. Both builds emitted an upstream MD4C compiler warning and a Make jobserver warning; neither was a compilation or test failure. The native Linux CTest run executed all eight suites without a filter and passed with no failures.

| Suite or gate | Observed result |
| --- | --- |
| Linux core | **191 checks passed**, including eight public-default assertions. |
| Linux core plus controlled HTTP fixture | **222 checks passed**. This total includes the 191 core checks above. |
| Image and text previews | **101 checks passed**. |
| Markdown, code highlighting and document links | **504 checks passed**. |
| Basic RTF documents | **65 checks passed**. |
| CSV and TSV tables | **66 checks passed**. |
| Text encodings and line endings | **164 checks passed**. |
| Display-backed Retro interactions | **26 checks passed**. |
| Optional native preview geometry harness | **808 checks passed**, including the current public README. |
| Optional native Source long-line harness | **114 checks passed**. |
| Repository verification | **136 source files and eight pinned archives passed**, including public defaults, MD4C notices and workflow boundaries. |
| Repository and release-helper tests | **46 offline tests passed**, covering source policy, configured suites, archives and publication rejection paths. |
| CTest inventory | All six unit suites, the HTTP fixture and the Retro harness were present, enabled and built; core and HTTP commands included `--public-defaults`. |

The six standalone unit suites contain **1,091 passing checks** on Linux. HTTP repeats the core suite before its additional network checks, so its full count should not be added to that figure as independent coverage. Windows omits two Linux-specific file-permission assertions; its native execution results await hosted CI. The eight existing CI suites and their commands are unchanged. The two additional GUI harnesses are optional build targets, not added CTest registrations; their local execution results are Linux only.

### Preview geometry and long Source lines

Two guarded build-time patches apply to extracted FLTK 1.4.5 sources. The pinned upstream archive and dependency version are unchanged. The HelpView correction bases percentage table widths on the viewport, includes cell spacing and borders in the available width, accounts for the vertical scrollbar, assigns proportional rounding remainders consistently and keeps the horizontal scrollbar range aligned with the content extent. Markdown tables retain `WIDTH="100%"`; the correction leaves a small right inset without narrowing tables to an arbitrary percentage.

The native geometry harness checks consecutive viewport widths, two-, three- and seven-column Markdown tables, proportional column widths, vertical scrollbar presence and absence, larger text sizes, resizes and the actual public README. Wide-table coverage includes 64-column CSV and a 16-column hexadecimal byte table containing `00` through `FF`. Checks inspect the final cell bounds, right inset, complete horizontal reach and scrollbar maximum. Wide code blocks with repeated spaces are covered as well.

The TextDisplay correction bounds native font measurement and drawing batches, skips drawing outside the visible clip and avoids repeated whole-prefix measurement during far-right cursor lookup. Short-run behavior and stored Source text and styles remain unchanged. UTF-8 boundaries are preserved; an exceptionally long composed sequence is split at code-point boundaries to keep the native request bounded.

The separate Source harness paints an unbroken **25 MiB + 1 byte** buffer at its beginning and horizontally scrolled ending. It checks measured width, end-marker visibility, scrollbar reach, far-right character and cursor lookup, selection painting and byte-for-byte buffer preservation. Additional cases exercise repeated accented/CJK/emoji text, compare long-run cursor lookup with equivalent short text using actual native font measurements, and cover a composed tag sequence larger than the native batch bound.

### Markdown and highlighting

The document suite covers headings, emphasis, lists, quotations, tables, task markers, inline and fenced code, entities and language aliases. It checks all ten theme names and their light/dark palettes, token categories and unknown-language fallback. Core tests cover saved theme settings.

Link tests cover heading anchors, relative files, same-server navigation, explicit external HTTP/HTTPS destinations, queries, encoded paths and unsupported or malformed destinations. Raw HTML remains text. Document rendering does not execute scripts or fetch embedded images. Markdown accepts formatted-preview input up to **25 MiB**; tests preserve an ending marker at exactly that size and reject input one byte larger. Code highlighting retains its separate work limit. Oversized or unsupported documents fall back to Source with a status message.

### RTF

RTF tests cover grouped styles, bold/italic/underline, font sizes, RGB colors, paragraph alignment, tabs and line breaks. Character coverage includes Windows-1250, Windows-1252, declared UTF-8, Unicode escapes, surrogate pairs and fallback-byte handling.

Malformed nesting, incomplete escapes, unsupported encodings, large inputs and destination skipping are exercised. Input boundary checks accept exactly **25 MiB** while preserving the visible ending and reject larger input. Embedded objects and pictures are omitted. Field instructions are ignored, with visible field-result text handled as text. RTF is a bounded basic preview, with no claim of complete word-processor compatibility.

### CSV, encodings and line endings

CSV tests cover comma, semicolon and tab detection; an optional `sep=` directive; quoted delimiters; escaped quotes; multiline cells; empty and trailing fields; uneven rows; literal formula-like text; and malformed quoting. The first actual record is retained and rendered in bold, with left-aligned cells and padding for uneven columns.

Standalone `#` comment records are omitted from the rendered table at the beginning, between records and at the end. Tests cover LF, CRLF and CR endings, space indentation, comment punctuation during delimiter detection, byte-order marks and separator directives. Quoted first-field hashes, hashes inside multiline quoted cells, hashes in later fields and empty TSV fields remain data. Source bytes are not rewritten. Comment records do not consume the data-row limit.

Limits cover input bytes, field length, rows, columns, cell count and generated markup. The input limit is **25 MiB**, including ignored comment bytes. An exact-boundary fixture with a large comment followed by two CSV records renders successfully; larger input is rejected. Existing field, row, column, cell and generated-markup limits remain independently enforced.

Encoding tests cover UTF-8, UTF-16 LE/BE, UTF-32 LE/BE, byte-order marks, malformed sequences, supplementary Unicode characters, CP1250 and CP1252 mappings, content-type charset hints and conservative Unicode detection without a byte-order mark. Auto falls back to CP1250 for undecidable legacy text; the explicit CP1252 selector handles Western European files. CRLF, LF and CR normalize for display. Changing the encoding decodes the fetched bytes again; downloads retain the original bytes. Boundary checks verify that formatted documents can use the full 25 MiB allowance, truncation is reported and partial UTF-8 input at the boundary becomes a valid replacement character. Truncated input is identified before attempting formatted rendering.

### Sorting, core and HTTP

Size sorting has regression coverage for mixed known and unknown values, equal sizes, zero-byte files, folder-first grouping, both directions and hidden directory sizes. Pair/triple comparisons check the ordering properties that prevent the previous comparator cycle. Unknown values follow known values in either direction; hidden folder sizes use a name comparison.

Core coverage also includes settings round trips, reversible credential obfuscation, explicit account-saving intent, legacy migration, Linux file permissions, Unicode names, percent escapes, relative and absolute paths, server-origin and base-directory boundaries, directory listings, terminal metadata and size colors.

The controlled HTTP fixture exercises response codes, redirect and response limits, truncated bodies, authentication boundaries, cancellation and transactional downloads. Failed or cancelled downloads preserve existing destination files. Terminal click targets use independently verified entry paths, not URLs extracted from display text.

### Images and Retro

Preview fixtures cover PNG alpha; grayscale/RGB/CMYK JPEG; lossy/lossless/animated WebP; bounded SVG; BMP bit depths, bitfields and RLE; ICO selection and transparency; GIF palettes, interlacing and LZW; malformed and oversized inputs; ANSI colors/bold; and text truncation. Animated WebP and GIF previews intentionally display their first frame.

The display-backed Retro harness checks clickable name bounds with 22-unit text, keyboard activation, scrolling, model replacement and deferred callback lifetime. Separate fixtures check a list that fits and a long line that must scroll.

## Observed desktop behavior

A fresh native Linux profile fetched the live public demo listing and showed `https://a.ocv.me/pub/` in Settings with empty username and password fields. Both the application and Settings titles remained unversioned.

An isolated profile then used a controlled loopback fixture. The current public README's Downloads table filled the available width, retained its complete right border and left a small gap before the vertical scrollbar. A 12-column CSV showed a bold header and four data rows, omitted leading and between-record comments, and retained a quoted first-field `#003`, color hashes and a hash-prefixed line inside a multiline quoted field.

Resizing the window from about 1,100 pixels to its approximately 880-pixel minimum kept every CSV column within the viewport by wrapping content, with the right margin intact. Vertical scrolling reached the complete final row. Source displayed the original standalone comments, horizontal dragging reached the final text, and switching back to Rendered restored the table. The fixture recorded only the two listing and two file requests; toggling modes and resizing made no additional requests. The application exited normally with status 0.

## Binary and metadata inspection

Both packaging-equivalent stripped executables passed the repository's binary privacy checks. Their generated public-default headers contain the official demo URL and blank credentials. No builder-specific paths or unrelated project references were found in the inspected release executables.

The Windows PE is an x64 GUI application. Its version resources and manifest identify **2.1.0.0**, its manifest name remains `CopypartyViewer`, and its company/copyright fields identify **davoda1**. Its `DllCharacteristics` value is `0x160`, including high-entropy address-space randomization, dynamic-base support and non-executable-data compatibility.

The Windows executable imports these 17 operating-system libraries:

`ADVAPI32.dll`, `bcrypt.dll`, `COMCTL32.dll`, `comdlg32.dll`, `CRYPT32.dll`, `dwmapi.dll`, `GDI32.dll`, `gdiplus.dll`, `IPHLPAPI.DLL`, `KERNEL32.dll`, `msvcrt.dll`, `ole32.dll`, `Secur32.dll`, `SHELL32.dll`, `USER32.dll`, `WINSPOOL.DRV`, `WS2_32.dll`.

No separate FLTK, curl, OpenSSL, codec, libstdc++, libgcc or winpthread DLL is required. The Linux ELF imports 12 permitted platform libraries, has no RPATH/RUNPATH, and requires no shared application/codec/GNU C++ runtime libraries. Its highest imported GLIBC symbol version is **2.29**, within the documented glibc 2.31 baseline.

An arbitrary rebuild can have a different dependency floor. Debian packaging inspects the final executable's GLIBC symbols and raises its declared dependency when needed, with a minimum baseline of 2.31. Recheck imports and runtime paths when changing a compiler, build flag or dependency.

## Release verification

The following are package and publication requirements for the 2.1.0 release. Verification records for the prepared packages and exact uploaded assets, including their SHA-256 hashes, are maintained separately from this source, build and desktop report.

A release must have exactly three uploaded binary assets: the Windows portable ZIP, Debian amd64 package and Linux amd64 TAR.GZ. GitHub supplies tagged source downloads and uploaded-asset digests. The public source, package metadata and project copyright use the MIT license and **davoda1** attribution; bundled third-party components retain their own notices.

Release verification binds a successful master push CI run to the exact commit, requires all native and repository jobs to succeed, and requires completed CodeQL analysis with no open findings. It verifies the master and version-tag identities, downloaded hashes, executable layouts, current documentation and retained license notices. The Linux TAR and Debian package must contain identical executable bytes.

Package checks also reject links or unsafe archive paths, extra executables or companion DLLs, unexpected ownership or permissions, local profiles, credentials and builder-specific paths.

The publication workflow validates the existing draft, then revalidates the unchanged commit, CI run, tag, assets and verification receipt immediately before publishing. See the release's GitHub metadata and exact commit's Actions runs for final hosted and publication evidence.

## Coverage limits

Native Windows 10/11 hardware, monitor hot-plug and every DPI/font combination were not available for local testing. Hosted Windows CI adds native build/test evidence but does not certify every interactive desktop configuration. The Windows executable is unsigned. Linux needs the documented display, font and certificate infrastructure, and its title bar remains under the window manager's control.

These checks exercise the documented preview implementations and their limits; they do not establish compatibility with every Markdown extension, RTF feature, legacy encoding or malformed input.
