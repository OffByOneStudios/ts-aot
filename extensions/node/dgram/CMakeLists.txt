# dgram extension for ts-aot
#
# Node.js-compatible UDP socket functions.
# Built as a separate library (ts_dgram) linked when dgram module is imported.

add_library(ts_dgram STATIC
    src/dgram.cpp
)

target_include_directories(ts_dgram PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_include_directories(ts_dgram PRIVATE
    ${CMAKE_SOURCE_DIR}/src/runtime/include
)

# libuv is required for UDP socket operations
find_package(libuv CONFIG REQUIRED)

target_link_libraries(ts_dgram PRIVATE
    tsruntime
    $<IF:$<TARGET_EXISTS:libuv::uv_a>,libuv::uv_a,libuv::uv>
)
