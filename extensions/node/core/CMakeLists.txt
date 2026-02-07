# nodecore: Node.js infrastructure layer between tsruntime and extensions
# Contains: Buffer, HTTP, Sockets, ChildProcess, etc.

# Find Node.js-specific dependencies
find_package(llhttp CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(c-ares CONFIG REQUIRED)
find_package(ZLIB REQUIRED)
find_package(unofficial-brotli CONFIG REQUIRED)

# nghttp2 doesn't provide CMake config, find it manually
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

add_library(nodecore STATIC
    src/TsBuffer.cpp
    src/TsHttp.cpp
    src/TsHttp2.cpp
    src/TsFetch.cpp
    src/TsChildProcess.cpp
    src/TsCluster.cpp
    src/TsAsyncHooks.cpp
    src/TsPerfHooks.cpp
    src/TsReadline.cpp
    src/TsStringDecoder.cpp
    src/TsTransform.cpp
    src/TsInspector.cpp
    src/node/Socket.cpp
    src/node/SecureSocket.cpp
    src/node/Server.cpp
    src/node/TTY.cpp
)

# Runtime headers (TsObject.h, TsString.h, etc.)
target_include_directories(nodecore PUBLIC
    ${CMAKE_SOURCE_DIR}/src/runtime/include
)

target_link_libraries(nodecore PUBLIC
    tsruntime
    llhttp::llhttp_static
    OpenSSL::SSL
    OpenSSL::Crypto
    c-ares::cares
    nghttp2::nghttp2
    ZLIB::ZLIB
    unofficial::brotli::brotlidec
    unofficial::brotli::brotlienc
    unofficial::brotli::brotlicommon
)
