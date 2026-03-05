# module extension for ts-aot
#
# Node.js module module utilities.
# Built as a separate library (ts_module) linked when module module is imported.

add_library(ts_module STATIC
    src/module.cpp
)

target_include_directories(ts_module PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_include_directories(ts_module PRIVATE
    ${CMAKE_SOURCE_DIR}/src/runtime/include
    ${CMAKE_SOURCE_DIR}/src/runtime/src  # For internal headers
)

target_link_libraries(ts_module PRIVATE
    tsruntime
)
