# Perf hooks module extension
add_library(ts_perf_hooks STATIC
    src/perf_hooks.cpp
)

target_include_directories(ts_perf_hooks PUBLIC include)
target_include_directories(ts_perf_hooks PRIVATE ${CMAKE_SOURCE_DIR}/src/runtime/include)
target_link_libraries(ts_perf_hooks PRIVATE tsruntime)
