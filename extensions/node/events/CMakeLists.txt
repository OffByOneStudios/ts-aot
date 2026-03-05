# events extension for ts-aot
#
# Node.js-compatible EventEmitter module.
# Built as a separate library (ts_events) linked when events module is imported.

add_library(ts_events STATIC
    src/events.cpp
)

target_include_directories(ts_events PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_include_directories(ts_events PRIVATE
    ${CMAKE_SOURCE_DIR}/src/runtime/include
)

target_link_libraries(ts_events PRIVATE
    tsruntime
)
