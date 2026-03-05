# fs extension for ts-aot
#
# Node.js-compatible file system operations.
# Built as a separate library (ts_fs) linked when fs module is imported.

# libuv is required for async file system operations
find_package(libuv CONFIG REQUIRED)

add_library(ts_fs STATIC
    src/fs.cpp
)

target_include_directories(ts_fs PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_include_directories(ts_fs PRIVATE
    ${CMAKE_SOURCE_DIR}/src/runtime/include
    ${CMAKE_SOURCE_DIR}/src/runtime/src  # For internal headers
)

target_link_libraries(ts_fs PRIVATE
    tsruntime
    $<IF:$<TARGET_EXISTS:libuv::uv_a>,libuv::uv_a,libuv::uv>
)
