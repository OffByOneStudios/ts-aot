# v8 extension for ts-aot
#
# Stub implementation of Node.js v8 module.
# The v8 module is V8-specific, these stubs return placeholder data.

add_library(ts_v8 STATIC
    src/v8.cpp
)

target_include_directories(ts_v8 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_include_directories(ts_v8 PRIVATE
    ${CMAKE_SOURCE_DIR}/src/runtime/include
    ${CMAKE_SOURCE_DIR}/src/runtime/src  # For internal headers
)

target_link_libraries(ts_v8 PRIVATE
    tsruntime
)
