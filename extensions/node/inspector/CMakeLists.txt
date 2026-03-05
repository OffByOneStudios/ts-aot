# inspector extension for ts-aot
#
# Stub implementation of Node.js inspector module.
# Built as a separate library (ts_inspector) linked when inspector module is imported.
#
# Uses split pattern: TsInspectorSession class implementation stays in tsruntime
# (inherits from TsEventEmitter), extern "C" wrappers are in this extension.

add_library(ts_inspector STATIC
    src/inspector.cpp
)

target_include_directories(ts_inspector PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_include_directories(ts_inspector PRIVATE
    ${CMAKE_SOURCE_DIR}/src/runtime/include
    ${CMAKE_SOURCE_DIR}/src/runtime/src  # For internal headers
)

target_link_libraries(ts_inspector PRIVATE
    tsruntime
)
