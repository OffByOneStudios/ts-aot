# Readline module extension
add_library(ts_readline STATIC
    src/readline.cpp
)

target_include_directories(ts_readline PUBLIC include)
target_include_directories(ts_readline PRIVATE ${CMAKE_SOURCE_DIR}/src/runtime/include)
target_link_libraries(ts_readline PRIVATE tsruntime)
