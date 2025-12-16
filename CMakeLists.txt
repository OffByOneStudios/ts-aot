cmake_minimum_required(VERSION 3.20)

project(ts-aot LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Use vcpkg toolchain
if(DEFINED ENV{VCPKG_ROOT} AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")
endif()

# Add subdirectories
add_subdirectory(src/runtime)
add_subdirectory(src/compiler)
# add_subdirectory(tests) # Uncomment when tests are ready
