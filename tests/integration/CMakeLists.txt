cmake_minimum_required(VERSION 3.15)
project(test_app)

set(CMAKE_CXX_STANDARD 20)

# Find dependencies
find_package(BDWgc REQUIRED)
find_package(icu REQUIRED)
find_package(libuv REQUIRED)

# Add the test executable
add_executable(test_app main.cpp)

# Link against the runtime and dependencies
target_link_libraries(test_app PRIVATE 
    tsruntime
    BDWgc::bdwgc
    icu::icu
    libuv::libuv
)

# Include directories
target_include_directories(test_app PRIVATE 
    ${CMAKE_SOURCE_DIR}/../../src/runtime/include
)

# Link to the runtime library directory
target_link_directories(test_app PRIVATE
    ${CMAKE_BINARY_DIR}/../../src/runtime/Debug
)
