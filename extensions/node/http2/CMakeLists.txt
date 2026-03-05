# HTTP/2 module extension for ts-aot
# Provides HTTP/2 server/client functionality

add_library(ts_http2 STATIC
    src/http2.cpp
)

target_include_directories(ts_http2 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src/runtime/include
)

target_link_libraries(ts_http2 PRIVATE
    tsruntime
)

# Ensure C++20 standard
set_target_properties(ts_http2 PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)
