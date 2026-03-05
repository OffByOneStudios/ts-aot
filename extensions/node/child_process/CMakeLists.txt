# Child Process module extension for ts-aot
# Provides process spawning functionality

add_library(ts_child_process STATIC
    src/child_process.cpp
)

target_include_directories(ts_child_process PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src/runtime/include
)

target_link_libraries(ts_child_process PRIVATE
    tsruntime
)

# Ensure C++20 standard
set_target_properties(ts_child_process PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)
