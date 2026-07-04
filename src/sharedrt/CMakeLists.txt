# ---------------------------------------------------------------------------
# tsruntime_shared: the runtime as a single shared library (DLL / .so)
# ---------------------------------------------------------------------------
# Produces tsruntime_shared.dll (+ an import library tsruntime_shared.lib on
# Windows) that bundles the core runtime + the nodecore layer and their
# third-party dependencies, exporting the ts_* C ABI. Built alongside the
# static `tsruntime`; emitted executables compiled with `ts-aot
# --shared-runtime` link only against the import library, shrinking each binary
# from ~3MB to tens of KB.
#
# The DLL reuses the already-compiled object files of the global `tsruntime`
# and `nodecore` targets via $<TARGET_OBJECTS> so sources + their compile
# definitions (e.g. TS_ICU_FULL) are shared and WINDOWS_EXPORT_ALL_SYMBOLS can
# scan them for the auto-generated export table (no runtime-source recompile).
# nodecore is required because tsruntime's objects reference Buffer / ArrayBuffer
# / TypedArray symbols that live in nodecore. find_package only LOCATES the
# already-installed vcpkg static libs (no vcpkg rebuild). The static targets are
# untouched, so the default static-link path is unaffected.
#
# Node.js extension modules (fs/http/net/...) are NOT yet bundled here, so
# programs importing them should use the default static link; pure-ECMAScript
# programs (e.g. the test262 suite) are fully covered.

find_package(libuv CONFIG REQUIRED)
find_package(ICU COMPONENTS uc i18n data REQUIRED)
find_package(unofficial-sodium CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(libtommath CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(llhttp CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(c-ares CONFIG REQUIRED)
find_package(ZLIB REQUIRED)
find_package(unofficial-brotli CONFIG REQUIRED)

# nghttp2 doesn't provide CMake config, find it manually (mirrors nodecore).
find_path(NGHTTP2_INCLUDE_DIR nghttp2/nghttp2.h)
find_library(NGHTTP2_LIBRARY nghttp2)
if (NOT TARGET nghttp2::nghttp2)
    if (NOT NGHTTP2_INCLUDE_DIR OR NOT NGHTTP2_LIBRARY)
        message(FATAL_ERROR "nghttp2 library not found")
    endif()
    add_library(nghttp2::nghttp2 STATIC IMPORTED)
    set_target_properties(nghttp2::nghttp2 PROPERTIES
        IMPORTED_LOCATION "${NGHTTP2_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${NGHTTP2_INCLUDE_DIR}"
    )
endif()

# Fix broken libtommath target in some vcpkg versions (mirrors src/runtime).
if (TARGET libtommath)
    get_target_property(TOMMATH_INC libtommath INTERFACE_INCLUDE_DIRECTORIES)
    if (NOT EXISTS "${TOMMATH_INC}")
        set_target_properties(libtommath PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include")
    endif()
endif()

# Complete fat runtime: core runtime + nodecore + every Node.js extension, so
# the DLL is self-contained and usable by any program (not just pure JS). The
# extension objects are pulled in because nodecore references them (e.g. TsHttp
# uses ts_net / ts_fetch), and including all of them gives a one-DLL runtime.
add_library(tsruntime_shared SHARED
    shared_runtime_stubs.cpp
    $<TARGET_OBJECTS:tsparse>
    $<TARGET_OBJECTS:tsruntime>
    $<TARGET_OBJECTS:nodecore>
    $<TARGET_OBJECTS:ts_assert>
    $<TARGET_OBJECTS:ts_async_hooks>
    $<TARGET_OBJECTS:ts_child_process>
    $<TARGET_OBJECTS:ts_cluster>
    $<TARGET_OBJECTS:ts_crypto>
    $<TARGET_OBJECTS:ts_dgram>
    $<TARGET_OBJECTS:ts_dns>
    $<TARGET_OBJECTS:ts_events>
    $<TARGET_OBJECTS:ts_fetch>
    $<TARGET_OBJECTS:ts_fs>
    $<TARGET_OBJECTS:ts_http>
    $<TARGET_OBJECTS:ts_http2>
    $<TARGET_OBJECTS:ts_inspector>
    $<TARGET_OBJECTS:ts_module>
    $<TARGET_OBJECTS:ts_net>
    $<TARGET_OBJECTS:ts_os>
    $<TARGET_OBJECTS:ts_path>
    $<TARGET_OBJECTS:ts_perf_hooks>
    $<TARGET_OBJECTS:ts_readline>
    $<TARGET_OBJECTS:ts_stream>
    $<TARGET_OBJECTS:ts_string_decoder>
    $<TARGET_OBJECTS:ts_tty>
    $<TARGET_OBJECTS:ts_url>
    $<TARGET_OBJECTS:ts_util>
    $<TARGET_OBJECTS:ts_v8>
    $<TARGET_OBJECTS:ts_vm>
    $<TARGET_OBJECTS:ts_zlib>
)

set_target_properties(tsruntime_shared PROPERTIES
    OUTPUT_NAME tsruntime_shared
    WINDOWS_EXPORT_ALL_SYMBOLS ON
)

target_include_directories(tsruntime_shared PUBLIC ${CMAKE_SOURCE_DIR}/src/runtime/include)

# Third-party dependencies are embedded PRIVATE (consumed internally; the
# emitted exe never references them directly). icudt_stub supplies the ICU data
# symbol so ICU loads its data from the external icudt74l.dat at runtime
# (matching the default non --bundle-icu executable behavior).
target_link_libraries(tsruntime_shared PRIVATE
    $<IF:$<TARGET_EXISTS:libuv::uv_a>,libuv::uv_a,libuv::uv>
    ICU::uc
    ICU::i18n
    unofficial-sodium::sodium
    nlohmann_json::nlohmann_json
    libtommath
    fmt::fmt
    spdlog::spdlog
    llhttp::llhttp_static
    OpenSSL::SSL
    OpenSSL::Crypto
    c-ares::cares
    nghttp2::nghttp2
    ZLIB::ZLIB
    unofficial::brotli::brotlidec
    unofficial::brotli::brotlienc
    unofficial::brotli::brotlicommon
    icudt_stub
)

if (WIN32)
    target_link_libraries(tsruntime_shared PRIVATE
        Dbghelp ws2_32 advapi32 iphlpapi userenv
        shell32 ole32 oleaut32 user32 crypt32 bcrypt)
else()
    target_link_libraries(tsruntime_shared PRIVATE pthread dl)
endif()
