# zlib extension for ts-aot
#
# Node.js-compatible compression functions (gzip, deflate, brotli).
# Built as a separate library (ts_zlib) linked when zlib module is imported.

add_library(ts_zlib STATIC
    src/zlib.cpp
)

target_include_directories(ts_zlib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_include_directories(ts_zlib PRIVATE
    ${CMAKE_SOURCE_DIR}/src/runtime/include
)

# zlib is required for gzip/deflate compression
find_package(ZLIB REQUIRED)

# brotli is required for Brotli compression
find_package(unofficial-brotli CONFIG REQUIRED)

target_link_libraries(ts_zlib PRIVATE
    tsruntime
    ZLIB::ZLIB
    unofficial::brotli::brotlidec
    unofficial::brotli::brotlienc
    unofficial::brotli::brotlicommon
)
