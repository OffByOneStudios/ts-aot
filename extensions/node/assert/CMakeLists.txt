# assert extension for ts-aot
#
# Node.js-compatible assertion functions.
# Built as a separate library (ts_assert) linked when assert module is imported.

add_library(ts_assert STATIC
    src/assert.cpp
)

target_include_directories(ts_assert PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_include_directories(ts_assert PRIVATE
    ${CMAKE_SOURCE_DIR}/src/runtime/include
)

target_link_libraries(ts_assert PRIVATE
    tsruntime
)
