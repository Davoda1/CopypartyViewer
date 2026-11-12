# Release validation — Copyparty Viewer 1.0.0

Copyright (C) 2026 davoda1. Local runtime evidence recorded on **2026-11-12**.

## Scope and status

This document separates local runtime checks from release-asset verification and hosted CI. The local results below cover the 1.0.0 application behavior with the official demo URL and empty embedded credentials. License, documentation and release-workflow changes do not by themselves establish that a new binary or commit has passed those checks.

| Evidence | Scope |
| --- | --- |
| Local Linux execution | Native amd64 Linux with an X11 desktop. |
| Local Windows execution | The actual x64 Windows executables under Wine 5.0.3; not native Windows 10/11 certification. |
| Hosted GitHub CI | **Pending for this repository revision when this document was prepared.** The authoritative result is the [Actions run](https://github.com/Davoda1/CopypartyViewer/actions) for the exact commit. A configured workflow is not evidence of a passing run. |
| Published binary assets | Release checks must verify the final three uploaded files and the exact commit before publication. Local baseline results do not certify changed assets. |

The intended release targets are Windows x64 and X11-compatible Linux amd64. The Linux compatibility build uses GCC 10.2.1 with a Debian 11/glibc 2.31 baseline. The Windows build uses MinGW GCC 14 with the POSIX thread model, native FLTK windows and Schannel. Application libraries and applicable GNU runtimes are linked statically; normal platform libraries remain necessary.

The default URL is `https://a.ocv.me/pub/`. Both embedded account fields are empty. The application's `/` denotes that configured base directory, and saved settings override compiled defaults. Dependencies and source hashes are pinned in `vendor/dependencies.json`, `vendor/sources/SHA256SUMS` and `cmake/Sources.cmake`; FLTK is 1.4.5.

## Observed local automated results

Both application/test targets built from this repository without compiler warnings or errors. The core, HTTP, preview and Retro suites below were run with the current native Linux and Windows/Wine builds. Earlier source-fallback and sanitizer evidence is identified separately. Hosted CI remains distinct from these local executions.

| Check | Observed result |
| --- | --- |
| Linux core unit tests | **132 checks passed**, including eight assertions for the official URL, blank credentials, first-start root, configuration fallback and serialization. |
| Windows core unit tests under Wine | **130 checks passed**. The two Linux-only file-permission checks do not apply on Windows. |
| Core plus controlled HTTP fixtures | **163 checks passed on Linux; 161 on Windows under Wine.** These totals include the core unit subsets above. |
| Image and text previews | **95 checks passed on Linux and 95 on Windows under Wine**, using each platform's preview libraries and test executable. |
| Retro widget interactions | **26 checks passed on each platform**, including click bounds with 22-unit text, keyboard activation, scrolling, model replacement and deferred callback lifetime. |
| Repository and release-helper tests | **37 offline tests passed**, including archive and publication rejection paths. These are local helper tests, not a hosted CI result. |
| Source fallback defaults | An earlier check of the unchanged core, compiled without the generated defaults header, passed **132 checks** on Linux. |
| Preview sanitizers | The unchanged preview implementation passed earlier AddressSanitizer and UndefinedBehaviorSanitizer checks. This was not a sanitizer run of the Retro widget. LeakSanitizer was unavailable in that test environment and is not claimed as tested. |

### Core and HTTP coverage

Tests cover settings round trips, reversible credential obfuscation, explicit account-saving intent, legacy credential migration, both listing preferences, Linux file permissions, Unicode names, percent escapes, relative and absolute paths, server-origin and base-directory boundaries, listing parsing, known/unknown sizes, directory totals, terminal metadata and size colors.

The controlled HTTP fixture exercises error codes, redirect limits, response limits, truncated bodies, authentication boundaries, cancellation and transactional downloads. Cancellation while waiting for headers or receiving a slow response completed within three seconds in the controlled tests. Failed and cancelled downloads preserved existing destination files.

Terminal rows are matched against independently parsed JSON entries. Click targets use the verified entry path, not a URL extracted from display text. Date tests cover readable output from compact wire rows and local fallback rows, preserved ANSI styles and click identities, leap days, malformed dates and missing timestamps.

### Preview coverage

Fixtures cover PNG alpha; grayscale/RGB/CMYK JPEG; lossy/lossless/animated WebP; bounded SVG; 1/4/8/16/24/32-bit BMP, bitfields and RLE; PNG/DIB ICO selection and transparency; GIF palettes, transparency, interlacing and LZW; malformed and oversized inputs; UTF-8 repair; ANSI colors/bold; and text truncation.

SVG and PNG decoding were exercised concurrently without initializing a GUI. Animated WebP and GIF previews intentionally display their first frame. These tests cover the documented implementations and limits, not every possible encoding or malformed file.

### Retro coverage

The display-backed harness checks clickable name bounds, row selection and Enter activation, scrolling, model replacement and deferred callbacks. Its small-list fixture reserves measured columns and rows so that the no-scroll case remains valid across font metrics. A separate long-line fixture checks actual overflow. The application retains its 22-unit requested font size.

## Observed desktop behavior

Fresh profiles with the current native Linux and Windows/Wine builds automatically listed the official demo without an account. The observed root contained seven directories and one file. Both main-window and Settings account fields started empty; Settings showed the exact demo URL. On both platforms, an explicit Settings Save left the persisted account empty. Modern/Retro switching, readable timestamps, anonymous `ping.html` text preview and Back restoration were exercised, and both application runs exited with code zero.

The current native Linux build was checked in both dark and light appearance, including animated fetching. The README screenshots are unmodified captures of those native Linux application windows. Additional recorded desktop checks of the unchanged interaction code used controlled local fixtures and covered:

- Modern columns fitting the panel at normal and smaller window sizes, a 122-row directory with vertical scrolling, and long names remaining within the name column.
- Modern 17-unit listing text, 16-unit headings and 34-unit rows; Retro 22-unit monospaced text with metric-derived row spacing and click extents.
- Colored file/folder sizes, known zero-byte values, unknown-size dashes, and saved Directory sizes On/Off behavior without hiding file sizes.
- Retro account, permission and server-information headers, colored `YYYY-MM-DD HH:MM:SS` timestamps, clickable names and horizontal scrolling for long lines.
- Retained arrow-key selection, Ctrl+L, Tab and Space activation without dotted focus outlines.
- Animated view and directory-size switches, persistence after Save and discarded drafts after Cancel.
- Main-window session credentials remaining absent from saved JSON; Settings showing the saved account rather than temporary login fields.
- Saving only display preferences preserving the active session login without persisting it; an explicitly saved synthetic account surviving restart in obfuscated form.
- Names containing literal percent and question-mark characters opening the correct preview and restoring the correct location after restart.
- A usable separate Settings window during slow transfers, animated activity and byte progress, and cancellation from the bottom status bar.
- ICO and ANSI-colored text previews, Back navigation with filter/scroll restoration, Save As, failed-destination messages and clean shutdown.
- Light/dark appearance, main-window maximization with normal geometry restoration, and saved Settings-window geometry.

Wine substituted fonts in the test environment and drew its window frames through the Linux window manager. These sessions therefore do not establish native Windows font rendering, title-bar appearance, all monitor arrangements or every DPI scale. The Windows caption helper uses documented DWM attributes with system-theme fallback; API references are in `BUILDING.md`.

## Local binary import evidence

The locally checked Windows PE imported these operating-system libraries:

`ADVAPI32.dll`, `bcrypt.dll`, `COMCTL32.dll`, `comdlg32.dll`, `CRYPT32.dll`, `dwmapi.dll`, `GDI32.dll`, `gdiplus.dll`, `IPHLPAPI.DLL`, `KERNEL32.dll`, `msvcrt.dll`, `ole32.dll`, `Secur32.dll`, `SHELL32.dll`, `USER32.dll`, `WINSPOOL.DRV`, `WS2_32.dll`.

No separate FLTK, curl, OpenSSL, codec, libstdc++, libgcc, winpthread or MCF DLL was required. The Linux ELF imported normal glibc/pthread/dl/math and X11/Xft/fontconfig libraries, with no shared application/codec/GNU C++ runtime libraries and no RPATH/RUNPATH. Its highest imported GLIBC symbol version was **2.29**. The current repository builds independently passed these import/default checks; the equivalent pre-release Linux binary also launched with the glibc 2.31 loader.

An arbitrary rebuild can have a different dependency floor. Debian packaging inspects the final executable's GLIBC symbol versions and raises the declared dependency as needed, with a minimum baseline of 2.31. Recheck imports and runtime paths when changing a compiler, build flag or dependency.

## Release verification

A release has exactly three uploaded binary assets: the Windows portable ZIP, Debian amd64 package and Linux amd64 TAR.GZ. GitHub supplies source downloads from the tag and computes the uploaded asset digests. No separately uploaded source archive or checksum file is required.

Release verification must bind a successful CI run to the exact commit and require completed CodeQL analysis for that commit with no analysis errors/warnings or open findings. It must inspect the uploaded asset names, types, sizes and hashes, and check the executable layouts and retained license notices. The runtime packages must contain the actual `CopypartyViewer.exe` or `copyparty-viewer`, without application launcher scripts or companion DLLs. Linux TAR and Debian payloads must contain matching executable bytes. Packaging checks also cover current documentation, source/dependency identity where applicable, executable permissions, neutral archive ownership and exclusion of credentials, local profiles and builder-specific paths.

See the release's GitHub metadata and the exact commit's Actions run for publication evidence. The local checks above are not presented as an unobserved hosted CI pass.

## Coverage limits

Native Windows 10/11 hardware, monitor hot-plug and every DPI/font combination were not available for the recorded local validation. Native hosted Windows CI, when it passes, provides additional build/test evidence but does not replace an interactive desktop check. The Windows executable is unsigned. Linux needs the documented display, font and certificate infrastructure; title bars remain under the window manager's control.
