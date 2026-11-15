# Release validation — Copyparty Viewer 2.0.0

Copyright (C) 2026 davoda1. Local evidence recorded on **2026-11-15**.

## Scope and release requirements

This report records checks actually performed on the public 2.0.0 source and locally built executables. Hosted checks and uploaded assets are verified separately for the exact release commit. The authoritative hosted results are the commit's [GitHub Actions runs](https://github.com/Davoda1/CopypartyViewer/actions); a configured workflow alone is not evidence of a passing run.

| Evidence | Scope |
| --- | --- |
| Local Linux execution | Native amd64 Linux, including the display-backed Retro harness. All eight configured CTest suites passed. |
| Local Windows build | A clean x64 cross-build of the application and all test executables, followed by PE, metadata and privacy inspection. These local Windows executables were not run. |
| Hosted native CI | Ubuntu 24.04 and Windows Server 2025 build and run all eight suites, verify their inventory and inspect runtime packages. Publication requires the Repository, Linux and Windows jobs to pass. |
| Security analysis | Publication requires completed CodeQL analysis for the exact master commit, with no analysis errors, warnings or open findings. |
| Uploaded release assets | Publication downloads and verifies the exact three assets against approved SHA-256 hashes, package structure and current repository documentation. |

The public default is `https://a.ocv.me/pub/`, with **empty embedded username and password**. The application's `/` denotes that base directory, and saved user settings override compiled defaults. Public-default regression checks exercise the first-start path, configuration fallback and serialization as well as the literal default values.

The local Linux compatibility build uses GCC 10.2.1 with a Debian 11/glibc 2.31 baseline. The Windows cross-build uses MinGW GCC 14 with the POSIX thread model, native FLTK windows and Schannel. Application libraries and applicable GNU runtimes are linked statically; normal platform libraries remain necessary. Dependencies and hashes are pinned in `vendor/dependencies.json`, `vendor/sources/SHA256SUMS` and `cmake/Sources.cmake`, including FLTK 1.4.5 and MD4C 0.5.2.

## Observed local automated results

Both clean application builds and every configured test executable completed successfully. No application compiler warnings were observed. The Linux build emitted an upstream MD4C compiler warning and a Make jobserver warning; neither was a compilation or test failure.

| Suite or gate | Observed result |
| --- | --- |
| Linux core | **191 checks passed**, including eight public-default assertions. |
| Linux core plus controlled HTTP fixture | **222 checks passed**. This total includes the 191 core checks above. |
| Image and text previews | **97 checks passed**. |
| Markdown, code highlighting and document links | **502 checks passed**. |
| Basic RTF documents | **64 checks passed**. |
| CSV and TSV tables | **39 checks passed**. |
| Text encodings and line endings | **161 checks passed**. |
| Display-backed Retro interactions | **26 checks passed**. |
| Repository and release-helper tests | **46 offline tests passed**, covering source policy, configured suites, archives and publication rejection paths. |
| CTest inventory | All six unit suites, the HTTP fixture and the Retro harness were present, enabled and built; core and HTTP commands included `--public-defaults`. |

The six standalone unit suites contain **1,054 passing checks** on Linux. HTTP repeats the core suite before its additional network checks, so its full count should not be added to that figure as independent coverage. Windows omits two Linux-specific file-permission assertions; hosted CI provides the native Windows results.

### Markdown and highlighting

The document suite covers headings, emphasis, lists, quotations, tables, task markers, inline and fenced code, entities and language aliases. It checks all ten theme names and their light/dark palettes, token categories and unknown-language fallback. Core tests cover saved theme settings.

Link tests cover heading anchors, relative files, same-server navigation, explicit external HTTP/HTTPS destinations, queries, encoded paths and unsupported or malformed destinations. Raw HTML remains text. Document rendering does not execute scripts or fetch embedded images. Input, generated output and highlighting work are bounded; oversized or unsupported documents fall back to source with a status message.

### RTF

RTF tests cover grouped styles, bold/italic/underline, font sizes, RGB colors, paragraph alignment, tabs and line breaks. Character coverage includes Windows-1250, Windows-1252, declared UTF-8, Unicode escapes, surrogate pairs and fallback-byte handling.

Malformed nesting, incomplete escapes, unsupported encodings, large inputs and destination skipping are exercised. Embedded objects and pictures are omitted. Field instructions are ignored, with visible field-result text handled as text. RTF is a bounded basic preview, with no claim of complete word-processor compatibility.

### CSV, encodings and line endings

CSV tests cover comma, semicolon and tab detection; an optional `sep=` directive; quoted delimiters; escaped quotes; multiline cells; empty and trailing fields; uneven rows; literal formula-like text; and malformed quoting. Limits cover input bytes, field length, rows, columns, cell count and generated markup. The first data row is preserved as data.

Encoding tests cover UTF-8, UTF-16 LE/BE, UTF-32 LE/BE, byte-order marks, malformed sequences, supplementary Unicode characters, CP1250 and CP1252 mappings, content-type charset hints and conservative Unicode detection without a byte-order mark. Auto falls back to CP1250 for undecidable legacy text; the explicit CP1252 selector handles Western European files. CRLF, LF and CR normalize for display. Changing the encoding decodes the fetched bytes again; downloads retain the original bytes.

### Sorting, core and HTTP

Size sorting has regression coverage for mixed known and unknown values, equal sizes, zero-byte files, folder-first grouping, both directions and hidden directory sizes. Pair/triple comparisons check the ordering properties that prevent the previous comparator cycle. Unknown values follow known values in either direction; hidden folder sizes use a name comparison.

Core coverage also includes settings round trips, reversible credential obfuscation, explicit account-saving intent, legacy migration, Linux file permissions, Unicode names, percent escapes, relative and absolute paths, server-origin and base-directory boundaries, directory listings, terminal metadata and size colors.

The controlled HTTP fixture exercises response codes, redirect and response limits, truncated bodies, authentication boundaries, cancellation and transactional downloads. Failed or cancelled downloads preserve existing destination files. Terminal click targets use independently verified entry paths, not URLs extracted from display text.

### Images and Retro

Preview fixtures cover PNG alpha; grayscale/RGB/CMYK JPEG; lossy/lossless/animated WebP; bounded SVG; BMP bit depths, bitfields and RLE; ICO selection and transparency; GIF palettes, interlacing and LZW; malformed and oversized inputs; ANSI colors/bold; and text truncation. Animated WebP and GIF previews intentionally display their first frame.

The display-backed Retro harness checks clickable name bounds with 22-unit text, keyboard activation, scrolling, model replacement and deferred callback lifetime. Separate fixtures check a list that fits and a long line that must scroll.

## Observed desktop behavior

A fresh native Linux profile showed `https://a.ocv.me/pub/` in Settings with empty username and password fields. A separate controlled fixture exercised Markdown rendering and Source / Rendered switching, saved the Nord code theme, displayed RTF styles with CP1250 and Unicode text, and displayed CSV tables with CP1250 and an explicit CP1252 selection.

Equivalent CRLF and LF CSV fixtures produced identical table pixels. Source/rendered and encoding changes made no additional file requests. Both application runs exited normally. The README's Markdown screenshot is an actual capture from this public build using generic example content.

## Binary and metadata inspection

Both packaging-equivalent stripped executables passed the repository's binary privacy checks. Their generated public-default headers contain the official demo URL and blank credentials. No builder-specific paths or unrelated project references were found in the inspected release executables.

The Windows PE is an x64 GUI application. Its version resources and manifest identify **2.0.0.0**, and its company/copyright fields identify **davoda1**. Its `DllCharacteristics` value is `0x160`, including high-entropy address-space randomization, dynamic-base support and non-executable-data compatibility.

The Windows executable imports these 17 operating-system libraries:

`ADVAPI32.dll`, `bcrypt.dll`, `COMCTL32.dll`, `comdlg32.dll`, `CRYPT32.dll`, `dwmapi.dll`, `GDI32.dll`, `gdiplus.dll`, `IPHLPAPI.DLL`, `KERNEL32.dll`, `msvcrt.dll`, `ole32.dll`, `Secur32.dll`, `SHELL32.dll`, `USER32.dll`, `WINSPOOL.DRV`, `WS2_32.dll`.

No separate FLTK, curl, OpenSSL, codec, libstdc++, libgcc or winpthread DLL is required. The Linux ELF imports 12 permitted platform libraries, has no RPATH/RUNPATH, and requires no shared application/codec/GNU C++ runtime libraries. Its highest imported GLIBC symbol version is **2.29**, within the documented glibc 2.31 baseline.

An arbitrary rebuild can have a different dependency floor. Debian packaging inspects the final executable's GLIBC symbols and raises its declared dependency when needed, with a minimum baseline of 2.31. Recheck imports and runtime paths when changing a compiler, build flag or dependency.

## Release verification

A release has exactly three uploaded binary assets: the Windows portable ZIP, Debian amd64 package and Linux amd64 TAR.GZ. GitHub supplies tagged source downloads and uploaded-asset digests. The public source, package metadata and project copyright use the MIT license and **davoda1** attribution; bundled third-party components retain their own notices.

Release verification binds a successful master push CI run to the exact commit, requires all native and repository jobs to succeed, and requires completed CodeQL analysis with no open findings. It verifies the master and version-tag identities, downloaded hashes, executable layouts, current documentation and retained license notices. The Linux TAR and Debian package must contain identical executable bytes.

Package checks also reject links or unsafe archive paths, extra executables or companion DLLs, unexpected ownership or permissions, local profiles, credentials and builder-specific paths.

The publication workflow validates the existing draft, then revalidates the unchanged commit, CI run, tag, assets and verification receipt immediately before publishing. See the release's GitHub metadata and exact commit's Actions runs for final hosted and publication evidence.

## Coverage limits

Native Windows 10/11 hardware, monitor hot-plug and every DPI/font combination were not available for local testing. Hosted Windows CI adds native build/test evidence but does not certify every interactive desktop configuration. The Windows executable is unsigned. Linux needs the documented display, font and certificate infrastructure, and its title bar remains under the window manager's control.

These checks exercise the documented preview implementations and their limits; they do not establish compatibility with every Markdown extension, RTF feature, legacy encoding or malformed input.
