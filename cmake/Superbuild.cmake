include(ExternalProject)
include(${CMAKE_CURRENT_LIST_DIR}/Sources.cmake)
set(defaults_cache "${CMAKE_BINARY_DIR}/private-defaults.cmake")
file(WRITE "${defaults_cache}" "# Generated local build defaults; excluded from source packages.\n")
foreach(key URL USERNAME PASSWORD)
  set(eq "=")
  string(FIND "${CPV_DEFAULT_${key}}" "]${eq}]" marker)
  while(NOT marker EQUAL -1)
    string(APPEND eq "=")
    string(FIND "${CPV_DEFAULT_${key}}" "]${eq}]" marker)
  endwhile()
  file(APPEND "${defaults_cache}" "set(CPV_DEFAULT_${key} [${eq}[${CPV_DEFAULT_${key}}]${eq}] CACHE STRING \"\" FORCE)\n")
endforeach()
set(dep_root "${CMAKE_BINARY_DIR}/deps")
set(prefix "${dep_root}/install")
# ExternalProject's logged command serialization cannot preserve embedded
# quotes in these generated prefix-map flags. Apply them through the child
# project's compile-options API instead, which also handles paths with spaces.
foreach(language C CXX)
  set(dependency_flags_${language} "${CMAKE_${language}_FLAGS}")
  foreach(mapping "${CMAKE_SOURCE_DIR}=." "${CMAKE_BINARY_DIR}=./build")
    string(REPLACE "\"-ffile-prefix-map=${mapping}\"" ""
      dependency_flags_${language} "${dependency_flags_${language}}")
  endforeach()
endforeach()
set(common
  -DCMAKE_BUILD_TYPE=Release
  -DCMAKE_INSTALL_PREFIX=${prefix}
  -DCMAKE_INSTALL_LIBDIR=lib
  -DCMAKE_PREFIX_PATH=${prefix}
  -DBUILD_SHARED_LIBS=OFF
  -DBUILD_TESTING=OFF
  -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
  -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
  -DCMAKE_C_FLAGS=${dependency_flags_C}
  -DCMAKE_CXX_FLAGS=${dependency_flags_CXX}
  -DCMAKE_PROJECT_INCLUDE=${CMAKE_CURRENT_LIST_DIR}/BuildPaths.cmake)
if(CMAKE_TOOLCHAIN_FILE)
  list(APPEND common -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE})
endif()
if(CMAKE_MAKE_PROGRAM)
  list(APPEND common -DCMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM})
endif()
if(CMAKE_ASM_NASM_COMPILER)
  list(APPEND common -DCMAKE_ASM_NASM_COMPILER=${CMAKE_ASM_NASM_COMPILER})
endif()
if(CMAKE_RC_COMPILER)
  list(APPEND common -DCMAKE_RC_COMPILER=${CMAKE_RC_COMPILER})
endif()
if(CMAKE_RC_FLAGS)
  list(APPEND common -DCMAKE_RC_FLAGS=${CMAKE_RC_FLAGS})
endif()
function(cpv_dep name)
  cmake_parse_arguments(D "" "" "ARGS;DEPENDS" ${ARGN})
  set(archive "${CMAKE_SOURCE_DIR}/vendor/sources/${CPV_${name}_FILE}")
  if(NOT EXISTS "${archive}")
    message(FATAL_ERROR "Missing pinned archive ${archive}. Run 'make fetch-deps' first or use the complete source release.")
  endif()
  ExternalProject_Add(dep-${name}
    URL "${archive}" URL_HASH "SHA256=${CPV_${name}_SHA256}"
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_DIR "${dep_root}/src/${name}"
    BINARY_DIR "${dep_root}/build/${name}"
    STAMP_DIR "${dep_root}/stamp/${name}"
    CMAKE_ARGS ${common} ${D_ARGS}
    BUILD_COMMAND "${CMAKE_COMMAND}" --build <BINARY_DIR> --parallel ${CPV_JOBS}
    INSTALL_COMMAND "${CMAKE_COMMAND}" --install <BINARY_DIR>
    DEPENDS ${D_DEPENDS}
    LOG_DOWNLOAD ON LOG_CONFIGURE ON LOG_BUILD ON LOG_INSTALL ON
    LOG_OUTPUT_ON_FAILURE ON)
endfunction()

if(WIN32)
  set(zlib_name libzs.a)
  set(jpeg_name libjpeg.a)
else()
  set(zlib_name libz.a)
  set(jpeg_name libjpeg.a)
endif()
set(codec_args
  -DZLIB_ROOT=${prefix}
  -DZLIB_INCLUDE_DIR=${prefix}/include
  -DZLIB_LIBRARY=${prefix}/lib/${zlib_name}
  -DPNG_PNG_INCLUDE_DIR=${prefix}/include
  -DPNG_LIBRARY=${prefix}/lib/libpng16.a
  -DJPEG_INCLUDE_DIR=${prefix}/include
  -DJPEG_LIBRARY=${prefix}/lib/${jpeg_name})

cpv_dep(zlib ARGS -DZLIB_BUILD_SHARED=OFF -DZLIB_BUILD_STATIC=ON -DZLIB_BUILD_TESTING=OFF)
cpv_dep(jpeg ARGS -DENABLE_SHARED=OFF -DENABLE_STATIC=ON -DWITH_TURBOJPEG=OFF -DWITH_TOOLS=OFF -DWITH_TESTS=OFF -DREQUIRE_SIMD=ON)
cpv_dep(png DEPENDS dep-zlib ARGS ${codec_args} -DPNG_SHARED=OFF -DPNG_STATIC=ON -DPNG_TESTS=OFF -DPNG_TOOLS=OFF)
set(webp_simd ON)
# WebP 1.6.0's AVX2 code uses an intrinsic missing from older GCC headers.
# The Debian 11 compatibility compiler therefore uses the portable decoder.
if(CMAKE_C_COMPILER_ID STREQUAL "GNU" AND CMAKE_C_COMPILER_VERSION VERSION_LESS 11)
  set(webp_simd OFF)
endif()
cpv_dep(webp ARGS -DWEBP_ENABLE_SIMD=${webp_simd} -DWEBP_BUILD_ANIM_UTILS=OFF -DWEBP_BUILD_CWEBP=OFF -DWEBP_BUILD_DWEBP=OFF -DWEBP_BUILD_GIF2WEBP=OFF -DWEBP_BUILD_IMG2WEBP=OFF -DWEBP_BUILD_VWEBP=OFF -DWEBP_BUILD_WEBPINFO=OFF -DWEBP_BUILD_LIBWEBPMUX=OFF -DWEBP_BUILD_WEBPMUX=OFF -DWEBP_BUILD_EXTRAS=OFF)
cpv_dep(json ARGS -DJSON_BuildTests=OFF -DJSON_Install=ON -DJSON_MultipleHeaders=OFF)

set(tls_dep "")
if(WIN32)
  set(tls_args -DCURL_USE_SCHANNEL=ON -DCURL_USE_OPENSSL=OFF)
else()
  find_package(Perl REQUIRED)
  find_program(CPV_GNU_MAKE NAMES gmake make REQUIRED)
  set(openssl_env "CC=${CMAKE_C_COMPILER}" "AR=${CMAKE_AR}" "RANLIB=${CMAKE_RANLIB}")
  set(openssl_flags "${CMAKE_C_FLAGS}")
  if(CMAKE_SYSROOT)
    string(APPEND openssl_flags " --sysroot=${CMAKE_SYSROOT}")
  endif()
  separate_arguments(openssl_flags NATIVE_COMMAND "${openssl_flags}")
  ExternalProject_Add(dep-openssl
    URL "${CMAKE_SOURCE_DIR}/vendor/sources/${CPV_openssl_FILE}" URL_HASH "SHA256=${CPV_openssl_SHA256}"
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_DIR "${dep_root}/src/openssl" BINARY_DIR "${dep_root}/build/openssl"
    STAMP_DIR "${dep_root}/stamp/openssl"
    CONFIGURE_COMMAND "${CMAKE_COMMAND}" -E make_directory <BINARY_DIR>/apps/include
    COMMAND "${CMAKE_COMMAND}" -E env ${openssl_env} "${PERL_EXECUTABLE}" <SOURCE_DIR>/Configure linux-x86_64 no-shared no-module no-tests no-apps no-docs no-legacy --prefix=${prefix} --openssldir=/etc/ssl --libdir=lib ${openssl_flags}
    BUILD_COMMAND "${CMAKE_COMMAND}"
      -DCPV_OPENSSL_SOURCE=<SOURCE_DIR> -DCPV_OPENSSL_BINARY=<BINARY_DIR>
      -DCPV_PERL=${PERL_EXECUTABLE}
      -DCPV_COMPILER_DESCRIPTION=${CMAKE_C_COMPILER_ID}-${CMAKE_C_COMPILER_VERSION}-static
      -P "${CMAKE_CURRENT_LIST_DIR}/OpenSSLBuildInfo.cmake"
    COMMAND "${CPV_GNU_MAKE}" -j${CPV_JOBS}
      MODULESDIR=/usr/lib/ossl-modules ENGINESDIR=/usr/lib/engines-4
    INSTALL_COMMAND "${CPV_GNU_MAKE}" install_dev
      MODULESDIR=/usr/lib/ossl-modules ENGINESDIR=/usr/lib/engines-4
    LOG_DOWNLOAD ON LOG_CONFIGURE ON LOG_BUILD ON LOG_INSTALL ON LOG_OUTPUT_ON_FAILURE ON)
  set(tls_dep dep-openssl)
  set(tls_args -DCURL_USE_OPENSSL=ON -DCURL_USE_SCHANNEL=OFF -DOPENSSL_ROOT_DIR=${prefix} -DOPENSSL_INCLUDE_DIR=${prefix}/include -DOPENSSL_SSL_LIBRARY=${prefix}/lib/libssl.a -DOPENSSL_CRYPTO_LIBRARY=${prefix}/lib/libcrypto.a -DOPENSSL_USE_STATIC_LIBS=ON -DCURL_DISABLE_OPENSSL_AUTO_LOAD_CONFIG=ON -DCURL_CA_BUNDLE=/etc/ssl/certs/ca-certificates.crt -DCURL_CA_PATH=/etc/ssl/certs)
endif()

set(curl_args -DBUILD_CURL_EXE=OFF -DBUILD_SHARED_LIBS=OFF -DBUILD_STATIC_LIBS=ON -DBUILD_EXAMPLES=OFF -DBUILD_LIBCURL_DOCS=OFF -DBUILD_MISC_DOCS=OFF -DENABLE_CURL_MANUAL=OFF -DENABLE_THREADED_RESOLVER=ON -DCURL_USE_PKGCONFIG=OFF -DCURL_USE_LIBPSL=OFF -DCURL_USE_LIBSSH2=OFF -DCURL_USE_LIBSSH=OFF -DCURL_BROTLI=OFF -DCURL_ZSTD=OFF -DUSE_NGHTTP2=OFF -DUSE_LIBIDN2=OFF -DCURL_ZLIB=ON -DCURL_DISABLE_LDAP=ON -DCURL_DISABLE_LDAPS=ON)
foreach(protocol DICT FILE FTP GOPHER IMAP IPFS MQTT POP3 RTSP SMTP TELNET TFTP WEBSOCKETS)
  list(APPEND curl_args -DCURL_DISABLE_${protocol}=ON)
endforeach()
cpv_dep(curl DEPENDS dep-zlib ${tls_dep} ARGS ${codec_args} ${curl_args} ${tls_args})

cpv_dep(fltk DEPENDS dep-png dep-jpeg dep-zlib ARGS ${codec_args}
  -DFLTK_USE_SYSTEM_LIBPNG=ON -DFLTK_USE_SYSTEM_LIBJPEG=ON -DFLTK_USE_SYSTEM_ZLIB=ON
  -DFLTK_BUILD_SHARED_LIBS=OFF -DFLTK_BUILD_FLUID=OFF -DFLTK_BUILD_FLTK_OPTIONS=OFF -DFLTK_BUILD_FORMS=OFF -DFLTK_BUILD_EXAMPLES=OFF -DFLTK_BUILD_TEST=OFF -DFLTK_BUILD_GL=OFF -DFLTK_BACKEND_WAYLAND=OFF -DFLTK_BACKEND_X11=ON -DFLTK_OPTION_SVG=ON -DFLTK_OPTION_PRINT_SUPPORT=OFF -DFLTK_USE_LIBDECOR_GTK=OFF)

ExternalProject_Add(cpv-app
  DOWNLOAD_COMMAND "" SOURCE_DIR "${CMAKE_SOURCE_DIR}" BINARY_DIR "${CMAKE_BINARY_DIR}/app"
  CMAKE_ARGS ${common} ${codec_args} ${tls_args}
    -DCPV_SUPERBUILD=OFF -DCPV_BUILD_TESTS=${CPV_BUILD_TESTS}
    -DCPV_VERIFY_PUBLIC_DEFAULTS=${CPV_VERIFY_PUBLIC_DEFAULTS}
    -DCPV_TEST_HTTP=${CPV_TEST_HTTP} -DCPV_TEST_RETRO=${CPV_TEST_RETRO}
    -DCPV_DEPS_PREFIX=${prefix} -DCPV_FLTK_SOURCE_DIR=${dep_root}/src/fltk
    -DCPV_SITE_CONFIG=${defaults_cache}
  BUILD_COMMAND "${CMAKE_COMMAND}" --build <BINARY_DIR> --parallel ${CPV_JOBS}
  INSTALL_COMMAND "" BUILD_ALWAYS TRUE
  DEPENDS dep-fltk dep-curl dep-webp dep-json
  LOG_CONFIGURE ON LOG_OUTPUT_ON_FAILURE ON)
add_custom_target(deps DEPENDS dep-fltk dep-curl dep-webp dep-json)
