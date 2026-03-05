# Async hooks module extension
add_library(ts_async_hooks STATIC
    src/async_hooks.cpp
)

target_include_directories(ts_async_hooks PUBLIC include)
target_include_directories(ts_async_hooks PRIVATE ${CMAKE_SOURCE_DIR}/src/runtime/include)
target_link_libraries(ts_async_hooks PRIVATE tsruntime)
