# tty extension for ts-aot
#
# Node.js-compatible TTY operations.
# Built as a separate library (ts_tty) linked when tty module is imported.
#
# Uses split pattern: TsTTYReadStream/TsTTYWriteStream class implementations
# stay in tsruntime (inherit from TsReadable/TsWritable), extern "C" wrappers
# are in this extension.

find_package(libuv CONFIG REQUIRED)

add_library(ts_tty STATIC
    src/tty.cpp
)

target_include_directories(ts_tty PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_include_directories(ts_tty PRIVATE
    ${CMAKE_SOURCE_DIR}/src/runtime/include
    ${CMAKE_SOURCE_DIR}/src/runtime/src  # For internal headers
)

target_link_libraries(ts_tty PRIVATE
    tsruntime
    $<IF:$<TARGET_EXISTS:libuv::uv_a>,libuv::uv_a,libuv::uv>
)
