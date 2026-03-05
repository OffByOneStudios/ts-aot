# String decoder module extension
add_library(ts_string_decoder STATIC
    src/string_decoder.cpp
)

target_include_directories(ts_string_decoder PUBLIC include)
target_include_directories(ts_string_decoder PRIVATE ${CMAKE_SOURCE_DIR}/src/runtime/include)
target_link_libraries(ts_string_decoder PRIVATE tsruntime)
