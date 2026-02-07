# nodecore: Node.js infrastructure layer between tsruntime and extensions
# Contains: Buffer, HTTP, Sockets, ChildProcess, etc.
# Source files remain in src/runtime/src/ - referenced via ../src/

add_library(nodecore STATIC
    ../src/TsBuffer.cpp
    ../src/TsHttp.cpp
    ../src/TsHttp2.cpp
    ../src/TsFetch.cpp
    ../src/TsChildProcess.cpp
    ../src/TsCluster.cpp
    ../src/TsAsyncHooks.cpp
    ../src/TsPerfHooks.cpp
    ../src/TsReadline.cpp
    ../src/TsStringDecoder.cpp
    ../src/TsTransform.cpp
    ../src/TsInspector.cpp
    ../src/node/Socket.cpp
    ../src/node/SecureSocket.cpp
    ../src/node/Server.cpp
    ../src/node/TTY.cpp
)

target_include_directories(nodecore PUBLIC ../include)

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
