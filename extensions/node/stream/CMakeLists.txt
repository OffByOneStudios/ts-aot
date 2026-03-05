# stream extension for ts-aot
#
# Node.js-compatible stream module.
# Built as a separate library (ts_stream) linked when stream module is imported.
# Provides: ReadStream, WriteStream, Readable, Writable, Transform, Duplex,
# pipe, pipeline, finished, Readable.from, readable.wrap, readable.read

add_library(ts_stream STATIC
    src/stream.cpp
)

target_include_directories(ts_stream PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_include_directories(ts_stream PRIVATE
    ${CMAKE_SOURCE_DIR}/src/runtime/include
)

target_link_libraries(ts_stream PRIVATE
    tsruntime
)
