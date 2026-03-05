# HTTP module extension for ts-aot
# Provides HTTP server/client functionality

add_library(ts_http STATIC
    src/http.cpp
)

target_include_directories(ts_http PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src/runtime/include
)

target_link_libraries(ts_http PRIVATE
    tsruntime
)

# Ensure C++20 standard
set_target_properties(ts_http PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)
