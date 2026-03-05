# vm extension for ts-aot
#
# Stub implementation of Node.js vm module.
# The vm module requires runtime code evaluation which is incompatible with AOT.

add_library(ts_vm STATIC
    src/vm.cpp
)

target_include_directories(ts_vm PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_include_directories(ts_vm PRIVATE
    ${CMAKE_SOURCE_DIR}/src/runtime/include
    ${CMAKE_SOURCE_DIR}/src/runtime/src  # For internal headers
)

target_link_libraries(ts_vm PRIVATE
    tsruntime
)
