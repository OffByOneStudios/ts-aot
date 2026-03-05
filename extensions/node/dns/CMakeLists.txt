# dns extension for ts-aot
#
# Node.js-compatible DNS resolution functions.
# Built as a separate library (ts_dns) linked when dns module is imported.

add_library(ts_dns STATIC
    src/dns.cpp
)

target_include_directories(ts_dns PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_include_directories(ts_dns PRIVATE
    ${CMAKE_SOURCE_DIR}/src/runtime/include
)

# c-ares is required for advanced DNS queries (MX, TXT, NS, etc.)
find_package(c-ares CONFIG REQUIRED)

# libuv is required for basic lookups (getaddrinfo/getnameinfo)
find_package(libuv CONFIG REQUIRED)

target_link_libraries(ts_dns PRIVATE
    tsruntime
    c-ares::cares
    $<IF:$<TARGET_EXISTS:libuv::uv_a>,libuv::uv_a,libuv::uv>
)
