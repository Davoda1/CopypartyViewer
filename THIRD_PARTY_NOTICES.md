# Third-party notices

Copyparty Viewer 2.1.0 is Copyright (C) 2026 davoda1. Project-owned code and documentation are distributed under the MIT License. Third-party copyrights and licenses remain with their respective authors. The project's license does not replace the terms listed below.

Copyparty Viewer is based in part on the work of the FLTK project (https://www.fltk.org). This software is based in part on the work of the Independent JPEG Group.

The build applies local FLTK table-layout and long-line text-display corrections through `cmake/PatchFltkHelpView.cmake` and `cmake/PatchFltkTextDisplay.cmake`. The original FLTK source archive, copyright and license notices are retained unchanged.

| Component | Included version | License / notice file |
| --- | --- | --- |
| FLTK | 1.4.5 | GNU Library GPL version 2 with FLTK exceptions; `third-party-licenses/FLTK-COPYING.txt` |
| NanoSVG | Revision bundled with FLTK 1.4.5 | zlib-style license; `third-party-licenses/NanoSVG-LICENSE.txt` |
| libcurl | 8.23.0 | curl license; `third-party-licenses/curl-COPYING.txt` |
| OpenSSL (Linux) | 4.1.0 | Apache License 2.0; `third-party-licenses/OpenSSL-LICENSE.txt` |
| libpng | 1.6.59 | PNG Reference Library License version 2 and retained historical notices; `third-party-licenses/libpng-LICENSE.txt` |
| libjpeg-turbo | 3.2.1 | IJG and BSD-style component licenses; `third-party-licenses/libjpeg-turbo-LICENSE.md`, `third-party-licenses/README.ijg` |
| zlib | 1.4 | zlib license; `third-party-licenses/zlib-LICENSE.txt` |
| libwebp | 1.6.0 | BSD 3-clause license and patent grant; `third-party-licenses/libwebp-COPYING.txt`, `third-party-licenses/libwebp-PATENTS.txt` |
| nlohmann/json | 3.12.0 | MIT license; `third-party-licenses/nlohmann-json-LICENSE.txt` |
| MD4C | 0.5.2 | MIT license, Copyright © 2016–2024 Martin Mitáš; `third-party-licenses/MD4C-LICENSE.md` |
| Unicode CP1250 / CP1252 mapping data | Microsoft mapping tables 2.01, 1998-04-15 | Unicode License v3 (`Unicode-3.0`); `third-party-licenses/Unicode-LICENSE.txt` |
| GNU compiler runtimes | Linux GCC 10 / Windows GCC 14-posix | GPL with GCC Runtime Library Exception and component notices; `third-party-licenses/GCC-Linux-runtime-copyright.txt`, `third-party-licenses/GCC-Windows-runtime-copyright.txt`, `third-party-licenses/GPL-3.0.txt` |
| MinGW-w64 runtime and winpthreads (Windows) | MinGW-w64 12 package | Public-domain, permissive and other retained component notices; `third-party-licenses/MinGW-w64-copyright.txt` |

The complete notices above accompany binary packages. In the source package they are in `packaging/third-party-licenses/`; the pinned upstream source archives and vendored MD4C sources retain their own complete notices and licensing information. Exact archive, NanoSVG and MD4C hashes are recorded in `vendor/dependencies.json`.

The Windows-1250 and Windows-1252 conversion data used by the text and RTF previews derives from Unicode's published [CP1250 mapping](https://www.unicode.org/Public/MAPPINGS/VENDORS/MICSFT/WINDOWS/CP1250.TXT) and [CP1252 mapping](https://www.unicode.org/Public/MAPPINGS/VENDORS/MICSFT/WINDOWS/CP1252.TXT). Unicode's [Terms of Use](https://www.unicode.org/copyright.html) identify these public data files as covered by the [Unicode License v3](https://www.unicode.org/license.txt). The complete copyright and permission notice is retained in `Unicode-LICENSE.txt`; undefined code-page entries are represented as the Unicode replacement character in this application.

The program uses normal operating-system libraries and services for windows, fonts, networking and certificate trust. Those system components are not redistributed in the portable application packages. Windows uses the system Schannel TLS implementation. Linux uses the host's X11/font libraries and CA certificate data.

The application icon was supplied with the original project. The settings gear was supplied by davoda1 as `settings-gear.svg`; its existing SVG Repo provenance comment is retained in the source asset. Project ownership attribution does not claim authorship of third-party components or overwrite their notices.
