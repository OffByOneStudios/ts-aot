# util extension for ts-aot
#
# Node.js-compatible util functions.
# Built as a separate library (ts_util) linked when util module is imported.

add_library(ts_util STATIC
    src/util.cpp
)

target_include_directories(ts_util PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_include_directories(ts_util PRIVATE
    ${CMAKE_SOURCE_DIR}/src/runtime/include
)

target_link_libraries(ts_util PRIVATE
    tsruntime
)
