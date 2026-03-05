# Fetch API extension for ts-aot
# Provides global fetch() function and Response class

add_library(ts_fetch STATIC
    src/fetch.cpp
)

target_include_directories(ts_fetch PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src/runtime/include
    ${CMAKE_SOURCE_DIR}/extensions/node/core/include
)

target_link_libraries(ts_fetch PRIVATE
    nodecore
)

# Ensure C++20 standard
set_target_properties(ts_fetch PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)
