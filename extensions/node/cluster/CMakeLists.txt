# Cluster module extension for ts-aot
# Provides multi-process forking functionality

add_library(ts_cluster STATIC
    src/cluster.cpp
)

target_include_directories(ts_cluster PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src/runtime/include
)

target_link_libraries(ts_cluster PRIVATE
    tsruntime
)

# Ensure C++20 standard
set_target_properties(ts_cluster PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)
