# net extension for ts-aot
#
# Node.js-compatible TCP networking operations.
# Built as a separate library (ts_net) linked when net module is imported.
#
# Uses split pattern: TsSocket, TsSecureSocket, TsServer class implementations
# stay in tsruntime (used by http/https), extern "C" wrappers are in this extension.

find_package(libuv CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)

add_library(ts_net STATIC
    src/net.cpp
)

target_include_directories(ts_net PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_include_directories(ts_net PRIVATE
    ${CMAKE_SOURCE_DIR}/src/runtime/include
    ${CMAKE_SOURCE_DIR}/src/runtime/src  # For internal headers
)

target_link_libraries(ts_net PRIVATE
    tsruntime
    $<IF:$<TARGET_EXISTS:libuv::uv_a>,libuv::uv_a,libuv::uv>
    OpenSSL::SSL
    OpenSSL::Crypto
)
