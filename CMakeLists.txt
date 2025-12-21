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
add_subdirectory(tests)

# Optional: Add a target for the http-server test app
find_package(libuv CONFIG REQUIRED)
find_package(llhttp CONFIG REQUIRED)

add_custom_command(
    OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/examples/http-server/http-server.json"
    COMMAND node "${CMAKE_CURRENT_SOURCE_DIR}/scripts/dump_ast.js" "${CMAKE_CURRENT_SOURCE_DIR}/examples/http-server/http-server.ts" "${CMAKE_CURRENT_SOURCE_DIR}/examples/http-server/http-server.json"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/examples/http-server/http-server.ts" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/dump_ast.js"
)

add_custom_command(
    OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/examples/http-server/http-server.obj"
    COMMAND "$<TARGET_FILE:ts-aot>" "${CMAKE_CURRENT_SOURCE_DIR}/examples/http-server/http-server.json" -o "${CMAKE_CURRENT_SOURCE_DIR}/examples/http-server/http-server.obj"
    DEPENDS ts-aot "${CMAKE_CURRENT_SOURCE_DIR}/examples/http-server/http-server.json"
)

add_executable(test_app examples/http-server/http-server.obj)
target_link_libraries(test_app PRIVATE tsruntime libuv::uv llhttp::llhttp_static ws2_32 user32 advapi32 iphlpapi shell32)
target_include_directories(test_app PRIVATE src/runtime/include)
