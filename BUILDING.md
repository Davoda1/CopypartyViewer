# Building Copyparty Viewer

Version **2.0.0**. Copyright (C) 2026 davoda1. The application is C++17. GNU Make is the entry point on both operating systems; CMake performs the project-local dependency builds and packaging. None of these tools is required to run the packaged application.

## Requirements

| Tool | Supported build use | Release build |
| --- | --- | --- |
| GNU Make | `make` on Linux, `mingw32-make` on Windows | 4.4.1 |
| CMake and CPack | CMake 3.24 or newer | 4.4.5 |
| tar | GNU tar or bsdtar for source and Linux TAR.GZ packaging; Windows includes native bsdtar | Host platform tool |
| GNU C/C++ compiler | C++17-capable `gcc` and `g++` in PATH | Linux GCC 10.2.1; Windows MinGW GCC 14-posix |
| NASM | Required for libjpeg-turbo x86-64 SIMD | 3.02 |
| Perl | Linux only, to configure/build OpenSSL | 5.44.0 |
| Linux development files | X11, Xft, Xrender, Xcursor, Xfixes, Xinerama, fontconfig and their development dependencies | Debian 11 compatibility sysroot |

On Debian or Ubuntu, install the normal build prerequisites using the distribution package manager:

```sh
sudo apt install g++ make cmake tar nasm perl pkg-config libx11-dev libxft-dev libxext-dev libxrender-dev libxcursor-dev libxfixes-dev libxinerama-dev libfontconfig-dev
```

On Windows, use a 64-bit MinGW toolchain containing matching `gcc.exe`, `g++.exe`, `windres.exe` and `mingw32-make.exe`, plus CMake and NASM in PATH. An existing WinLibs installation can be used directly. CMake and NASM can be unpacked into `build/tools` and added to PATH for the current terminal; neither requires a global installation. Use ordinary Command Prompt or PowerShell. An MSYS/Bash runtime is not required by the Makefile.

TAR.GZ packaging uses GNU tar or bsdtar. Windows provides a native bsdtar implementation as `tar.exe`; the build prefers the copy in `%SystemRoot%\System32`. See Microsoft's [tar on Windows documentation](https://learn.microsoft.com/en-us/windows/tar/). The optional local source bundle and Linux binary TAR.GZ record `root:root` ownership with numeric UID/GID 0, so their archive headers do not expose the account used to build them. This metadata normalization is implemented by `cmake/ArchiveTar.cmake` and does not require Python or an MSYS shell. GitHub generates the release's automatic source downloads separately.

The repository and GitHub-generated source archives include the exact dependency source archives in `vendor/sources`. The first build is offline once the listed compiler and OS development tools are available. Dependency source extraction, headers, static libraries and intermediate output are confined to the selected build directory. Existing globally installed versions of FLTK, curl, OpenSSL or image codecs do not replace the pinned copies.

## Native build

Clone the repository or download the release's automatically generated source archive. To check out version 2.0.0 specifically:

```sh
git clone --branch v2.0.0 --depth 1 https://github.com/Davoda1/CopypartyViewer.git
cd CopypartyViewer
```

Run these commands in the source directory:

```sh
make help
make JOBS=4
make test
make package
```

On Windows, replace `make` with `mingw32-make`:

```powershell
mingw32-make JOBS=4
mingw32-make test
mingw32-make package
```

Default outputs are `build/native/app/copyparty-viewer` on Linux or `build/native/app/CopypartyViewer.exe` on Windows. Packaging writes to `dist/`:

- Windows: `Copyparty-Viewer-2.0.0-Windows-x64.zip`.
- Linux: `Copyparty-Viewer-2.0.0-Linux-amd64.tar.gz` and `copyparty-viewer_2.0.0_amd64.deb`.

Each operating system builds and packages its native binary from the same source tree. Creating all three binary packages from one Linux workstation is also possible with the optional MinGW cross toolchain below. These are the only three assets uploaded to a GitHub release. GitHub generates its own source ZIP/TAR.GZ links from the release tag and computes the uploaded assets' SHA-256 digests.

`JOBS` limits each individual dependency build. At most three independent dependency builds run together, so `JOBS=2` uses up to about six compiler processes. A full first build takes substantially longer than later application-only rebuilds.

| Make target | Action |
| --- | --- |
| `help` | Show targets and configurable build variables. |
| `configure` | Configure the selected build directory. |
| `all` or `build` | Build pinned dependencies and the application; this is the default target. |
| `deps` | Build only the pinned dependencies. |
| `test` | Build and run the ordinary headless regression suites. |
| `package` | Build and create the current platform's binary package or packages in `dist/`. |
| `source` | Create an optional local offline source bundle in `dist/`; it is not a GitHub release asset. |
| `fetch-deps` | Retrieve missing pinned source archives and verify their hashes. |
| `install` / `uninstall` | Install or remove the application under the selected Linux `PREFIX`. |
| `clean` | Remove application build output, keeping built dependencies. |
| `distclean` | Remove the selected `BUILD_DIR`, keeping `vendor/sources`. |

## Compiler and directory overrides

```sh
make CXX=/path/to/g++ CC=/path/to/gcc BUILD_DIR=build/custom JOBS=2
make CMAKE=/path/to/cmake
```

Windows example:

```powershell
mingw32-make "CXX=C:/Tools/mingw64/bin/g++.exe" "CC=C:/Tools/mingw64/bin/gcc.exe"
```

Keep the matching toolchain `bin` directory on PATH so the resource compiler and other GNU tools use the same installation. Use a fresh `BUILD_DIR` when changing compiler families, architecture or operating system. CMake does not support replacing the compiler inside an already configured build tree.

## Build-time server and account defaults

Official releases embed only `https://a.ocv.me/pub/`. Username and password defaults are empty. On a fresh profile, the application's `/` is the configured `/pub/` base directory and is fetched automatically. Saved user settings take precedence over compiled defaults.

Embedded account defaults are not copied into the JSON by automatic geometry or navigation saves; an explicit Settings Save records the account and makes even intentionally blank saved credentials override compiled defaults.

For a locally customized build, create `private-site.cmake` next to the Makefile. This example starts from the official release defaults:

```cmake
set(CPV_DEFAULT_URL [=[https://a.ocv.me/pub/]=] CACHE STRING "" FORCE)
set(CPV_DEFAULT_USERNAME [=[]=] CACHE STRING "" FORCE)
set(CPV_DEFAULT_PASSWORD [=[]=] CACHE STRING "" FORCE)
```

Change the URL or account strings locally if required, then run:

```sh
make BUILD_DIR=build/private CMAKE_ARGS="-DCPV_SITE_CONFIG=$PWD/private-site.cmake"
```

Prefer an absolute path for `CPV_SITE_CONFIG`, especially on Windows or when building outside the project. For example:

```powershell
mingw32-make BUILD_DIR=build/private "CMAKE_ARGS=-DCPV_SITE_CONFIG=C:/Projects/CopypartyViewer/private-site.cmake"
```

Do not commit customized defaults or saved settings. The private file and generated build caches are excluded from the optional local source bundle by an explicit file allowlist. Never share a customized executable containing credentials with people who should not know them: values compiled into an executable can be recovered. Delete the private build tree when it is no longer needed. The runtime JSON uses the separately documented portable reversible obfuscation scheme. Official release checks require the demo URL and empty compiled credentials.

## Pinned libraries

| Library | Version | Role |
| --- | --- | --- |
| FLTK | 1.4.5 | Cross-platform windows and controls; static libraries |
| libcurl | 8.23.0 | HTTP/HTTPS with threaded DNS and cancellation |
| OpenSSL | 4.1.0 | Linux TLS, static; Windows uses native Schannel |
| zlib | 1.4 | PNG and HTTP decompression |
| libpng | 1.6.59 | PNG preview |
| libjpeg-turbo | 3.2.1 | JPEG preview, traditional libjpeg API |
| libwebp | 1.6.0 | WebP decoder and demuxer |
| nlohmann/json | 3.12.0 | Single-file settings and server listing parsing |
| MD4C | 0.5.2 | Native Markdown parser and entity decoding; compiled into the application |
| NanoSVG | FLTK 1.4.5 bundled revision | SVG parsing and rasterization |

`vendor/dependencies.json`, `vendor/sources/SHA256SUMS` and `cmake/Sources.cmake` record exact archive URLs and SHA-256 hashes. NanoSVG header hashes identify its bundled revision. The dependency build verifies archive hashes before extracting them. `make fetch-deps` retrieves only missing pinned archives and refuses to silently replace a file with an unexpected hash.

FLTK is configured to use the pinned PNG, JPEG and zlib archives instead of its older bundled codec copies. Only the X11 backend is enabled on Linux; Wayland, OpenGL, FLUID, forms, examples and print support are disabled. Font rendering uses Xft/fontconfig. The Windows build uses native FLTK windows.

curl supports HTTP and HTTPS only, with basic authentication and the threaded resolver. The curl command-line tool, SSH, LDAP, HTTP/2/3 and optional compression libraries are not built. gzip/deflate use the already included zlib. Linux OpenSSL has no shared libraries, loadable providers, applications, tests or documentation in the runtime package. Its standard provider is built into the executable. System CA certificates remain required on Linux. Windows certificate verification uses Schannel and the Windows certificate store.

The Linux compatibility build uses GCC 10 and the portable WebP decoder because libwebp 1.6.0's AVX2 source uses an intrinsic absent from that compiler's headers. WebP decoding and first-frame support remain available. Builds with GCC 11 or newer enable WebP's CPU-dispatched SIMD. JPEG SIMD is enabled on both release platforms through NASM.

## Documents, code themes and text decoding

`src/document.cpp` uses bundled MD4C with raw HTML disabled, simple tables, task lists and URL autolinks. It emits a restricted HTML subset for FLTK's native `Fl_Help_View`. The application's lexical highlighter supplies ten light/dark code palettes to Markdown code blocks and recognized source-file previews. It does not execute code or start a language server. MD4C's unmodified source hashes and MIT notice are retained in `vendor/md4c`.

`src/rtf.cpp` provides bounded basic RTF formatting and text decoding. It reads original bytes, handles CP1250/CP1252/UTF-8 declarations and Unicode escapes, and skips embedded resources, metadata and field instructions. `src/csv.cpp` parses comma-, semicolon- or tab-delimited data, including quoted multiline fields, and escapes every cell before emitting a native table. Neither parser loads document resources or executes fields or formulas.

`src/text_encoding.cpp` converts bounded previews to UTF-8. Auto uses BOMs, supported server charset declarations, conservative Unicode byte-alignment checks, valid UTF-8 and a CP1250 fallback. Explicit choices cover CP1250, CP1252 and UTF-8/16/32 in the documented byte orders. The existing text preparer normalizes CRLF/LF/CR and retains ordinary ANSI styles. Original downloads are unaffected. Unicode's mapping-data notice is included with the packages.

Formatted previews are limited to 512 KiB. CSV adds row, column, field, cell-count and output limits documented in the README. Larger or unsupported documents retain source preview and a status explanation. Text input/display is bounded at 8 MiB. Markdown images become alternative-text links; raw HTML is escaped. Rendered links are inert identifiers in an in-memory registry until explicitly clicked. Matching server links use the viewer's normal boundary checks; external HTTP/HTTPS and query links go to the browser without application credentials.

Settings stores the code theme as `appearance.code_theme` in the existing JSON schema. Missing or invalid values use `default`; account, geometry and listing settings retain their existing meanings. The file encoding selector is local to the open file and resets to Auto when navigating.

## Static linking and platform compatibility

Application libraries, codecs, libcurl and the GNU C++ runtime are statically linked. Windows builds also use `-static` to incorporate the selected MinGW thread/runtime libraries. The resulting Windows executable should import only normal Windows system DLLs. Audit each different toolchain: an MCF WinLibs build must not accidentally require `libmcfgthread-2.dll` beside the application.

The public build omits machine-local source and build paths from diagnostic metadata. GNU compiler prefix mappings apply to the application and dependency builds. On Linux, `cmake/OpenSSLBuildInfo.cmake` invokes OpenSSL's own metadata generator with a neutral compiler/version description, and the build uses generic `/usr/lib/ossl-modules` and `/usr/lib/engines-4` metadata paths. The static build does not load those optional modules or engines. These settings are part of the supplied build files; the pinned upstream source archives and their checksums remain unchanged. Audit rebuilt binaries after changing toolchains or adding compiler flags, since third-party diagnostic strings can otherwise retain local paths.

The native title-bar helper is in `src/platform.cpp`. The UI calls `apply_titlebar_theme(Fl_Window&, bool)` after showing each main/Settings window and when the application palette changes. On Windows it obtains the native handle using FLTK 1.4's `fl_win32_xid` and calls the documented `DwmSetWindowAttribute` API. It requests immersive dark mode and, where supported, native caption/text colors. Unsupported attributes retain the system frame; high-contrast mode resets color overrides. The helper uses the normal Windows `dwmapi` and `user32` libraries. It does not use undocumented ordinals, additional frameworks, custom title-bar drawing or bundled DLLs. The Linux implementation leaves decorations to the window manager.

Primary API references: [Microsoft Win32 theme guidance](https://learn.microsoft.com/en-us/windows/apps/desktop/modernize/ui/apply-windows-themes), [DWM window attributes and compatibility](https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/ne-dwmapi-dwmwindowattribute), [DwmSetWindowAttribute](https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/nf-dwmapi-dwmsetwindowattribute) and [Windows high-contrast handling](https://learn.microsoft.com/en-us/windows/win32/winauto/high-contrast-parameter). FLTK's exact native-handle declaration is in the bundled `FL/win32.H`, included through `FL/platform.H`.

The Modern table and Retro listing share the same parsed directory entries and navigation callbacks. `src/retro.cpp` implements the clickable terminal-style presentation using the existing static FLTK library, with no new dependency. Saved display preferences are separate from session login fields: requests take a credential snapshot from the current UI, while only Settings Save replaces persistent account values.

Retro additionally requests the server's `?ls=v` terminal listing through the embedded libcurl library. Matching ANSI rows supply presentation, while parsed JSON entries remain the source of navigation identities. An unavailable terminal representation falls back to the structured metadata without disabling directory browsing; cancellation still cancels the worker.

Linux keeps glibc, pthread/dl/math and normal X11/font libraries dynamic. A fully static graphical Linux binary would reduce compatibility with host display/font services. The release is built against Debian 11/glibc 2.31 and conservatively requires that baseline or newer. An arbitrary native rebuild inherits the minimum libc version of the system used to build it. Debian packaging inspects the actual executable's GLIBC symbol versions and raises the declared libc dependency when a newer native rebuild needs it.

Audit actual binary imports after compiler or build option changes:

```sh
objdump -p build/native/app/copyparty-viewer
readelf --version-info build/native/app/copyparty-viewer
```

For Windows, use the matching `objdump.exe -p build/native/app/CopypartyViewer.exe`. No `libstdc++-6.dll`, `libgcc_s_*.dll`, `libwinpthread-1.dll`, `libmcfgthread-2.dll`, FLTK, curl, OpenSSL or codec DLL should appear in the import list. Windows system DLL imports are expected.

## Tests

With the default options, `make test` runs six suites: core, image/text previews, Markdown/highlighting, RTF, CSV and text encodings. They require no display server or real account. Coverage includes settings, URL/path boundaries, strict size ordering, safe document links, formatting, encoding and line-ending cases, malformed input and preview limits.

Additional CTest coverage is opt-in:

| CMake option | Added coverage or requirement |
| --- | --- |
| `CPV_VERIFY_PUBLIC_DEFAULTS=ON` | Assert the official demo URL and empty compiled account, including configuration fallback. |
| `CPV_TEST_HTTP=ON` | Run the controlled local HTTP fixture through CTest; requires Python 3.8 or newer. |
| `CPV_TEST_RETRO=ON` | Build and run the Retro interaction harness through CTest; requires a graphical display. |

For all checks on Linux:

```sh
make BUILD_DIR=build/ci JOBS=2 CMAKE_ARGS="-DCPV_VERIFY_PUBLIC_DEFAULTS=ON -DCPV_TEST_HTTP=ON -DCPV_TEST_RETRO=ON"
xvfb-run -a make BUILD_DIR=build/ci test
```

Install Xvfb to use `xvfb-run`, or run the test command directly in an existing X11 session. On Windows, run from a session that supports native desktop windows:

```powershell
mingw32-make BUILD_DIR=build/ci JOBS=2 "CMAKE_ARGS=-DCPV_VERIFY_PUBLIC_DEFAULTS=ON -DCPV_TEST_HTTP=ON -DCPV_TEST_RETRO=ON"
mingw32-make BUILD_DIR=build/ci test
```

The configured options remain in the selected build directory. Omit the public-default assertions when intentionally testing different custom compiled defaults. Python and Xvfb are test aids, not application runtime dependencies.

An optional Python 3 fixture exercises HTTP errors, truncated bodies, redirects, authentication boundaries, cancellation and transactional downloads. Python is a developer test aid only:

```sh
python3 scripts/run_http_tests.py build/native/app/core-tests --public-defaults
```

On Windows use `python scripts/run_http_tests.py build/native/app/core-tests.exe --public-defaults` if Python is available. GUI behavior, DPI, monitor changes and platform-specific window management also require an interactive test of the actual application.

The Retro widget interaction harness checks click targets, keyboard selection, scrolling and callback lifetime. Without `CPV_TEST_RETRO=ON` it is excluded from the ordinary build and CTest run, but can be run explicitly:

```sh
cmake --build build/native/app --target retro-tests
build/native/app/retro-tests
```

Use `retro-tests.exe` on Windows. On Linux, an existing X11 session or a developer-provided Xvfb display can host it. It is a source test tool and is not included in the binary packages.

## GitHub CI and releases

The **CI** workflow in `.github/workflows/ci.yml` provides the **Repository**, **Linux** and **Windows** checks. Platform jobs build the pinned dependencies and run all six regression suites with public-default assertions, plus local HTTP and Retro interaction tests. Linux supplies the test display through Xvfb and also runs CodeQL C++ analysis; Windows executes native MinGW-built tests. These are configured checks, not a claim that a particular commit has passed: consult its [Actions run](https://github.com/Davoda1/CopypartyViewer/actions).

The separately dispatched **Publish release** workflow in `.github/workflows/publish.yml` publishes an existing draft. It takes the exact commit, its CI run ID, the draft release ID and the three locally verified binary SHA-256 digests. **Validate draft** waits a bounded time for that exact CI run, checks the commit against `master` and the release tag, and requires exactly the three expected binary assets. It also requires completed CodeQL analysis for that exact commit, without analysis errors/warnings or open findings. It compares the supplied digests with GitHub's asset metadata and freshly downloaded bytes. The separate **Publish** job revalidates the release before publishing it.

GitHub restricts access to unpublished drafts to callers with push access, so both release jobs need a repository-scoped `contents: write` token. **Validate draft** performs only read operations. Only **Publish** invokes the helper's `--publish` mode, whose sole mutation changes the verified draft into a published release. Neither job uploads, replaces or deletes assets.

Publication does not rebuild the application or create additional source/checksum assets. Build, inspect and test the three binary packages before uploading them to a draft; supply their local hashes when dispatching the workflow. GitHub's automatic source links refer to the release tag, and GitHub computes the uploaded assets' digests. Tag and release events do not trigger an additional build/publish cycle.

## Installation and cleanup

For a local Linux source install:

```sh
make
sudo make install PREFIX=/usr/local
sudo make uninstall PREFIX=/usr/local
```

`make clean` removes application build output while retaining dependencies. `make distclean` removes the selected `BUILD_DIR`, including extracted sources and built dependencies, while retaining the source archives in `vendor/sources`. Neither target removes user settings.

## Optional Linux-to-Windows cross build

With a suitable x86_64 MinGW compiler and NASM installed:

```sh
cmake -S . -B build/windows -DCMAKE_TOOLCHAIN_FILE=cmake/mingw64.cmake -DCPV_JOBS=2
cmake --build build/windows --parallel 3
cmake -DCPV_BUILD_DIR="$PWD/build/windows" -P cmake/PackageRelease.cmake
```

For a relocated compiler, set `-DCPV_MINGW_ROOT=/absolute/path/to/prefix` where that prefix contains `bin/x86_64-w64-mingw32-g++-posix` or `bin/x86_64-w64-mingw32-g++`. Add its `bin` directory to PATH. Cross compilation does not itself verify Windows runtime behavior; test the resulting executable on Windows.
