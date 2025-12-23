cmake_minimum_required(VERSION 3.20)

project(ts-aot LANGUAGES CXX)

set(CMAKE_CONFIGURATION_TYPES "Debug;Release" CACHE STRING "" FORCE)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Support static CRT for static builds
if(VCPKG_TARGET_TRIPLET STREQUAL "x64-windows-static")
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# Use vcpkg toolchain
if(DEFINED ENV{VCPKG_ROOT} AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")
endif()

# Add subdirectories
add_subdirectory(src/runtime)
add_subdirectory(src/compiler)
add_subdirectory(tests)

# LTO Support: Build runtime bitcode
find_package(LLVM REQUIRED CONFIG)
find_program(CLANG_EXECUTABLE clang HINTS "${LLVM_TOOLS_BINARY_DIR}")
find_program(LLVM_LINK_EXECUTABLE llvm-link HINTS "${LLVM_TOOLS_BINARY_DIR}")

if(CLANG_EXECUTABLE AND LLVM_LINK_EXECUTABLE)
    set(RUNTIME_BC_OUTPUT "${CMAKE_BINARY_DIR}/tsruntime.bc")
    set(RUNTIME_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/src")
    set(RUNTIME_INC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/include")
    
    file(GLOB RUNTIME_SOURCES "${RUNTIME_SRC_DIR}/*.cpp")
    set(RUNTIME_BC_FILES "")
    
    foreach(src ${RUNTIME_SOURCES})
        get_filename_component(name ${src} NAME_WE)
        set(bc_file "${CMAKE_BINARY_DIR}/runtime_bc/${name}.bc")
        list(APPEND RUNTIME_BC_FILES ${bc_file})
        
        add_custom_command(
            OUTPUT ${bc_file}
            COMMAND ${CLANG_EXECUTABLE} -c -emit-llvm -O3 -std=c++20 -fstack-protector-strong
                    -I${RUNTIME_INC_DIR} 
                    -I${CMAKE_CURRENT_SOURCE_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}/include
                    -DTS_RUNTIME_EXPORT -D_CRT_SECURE_NO_WARNINGS -D_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH
                    ${src} -o ${bc_file}
            DEPENDS ${src}
            COMMENT "Compiling ${name}.cpp to bitcode"
        )
    endforeach()
    
    add_custom_command(
        OUTPUT ${RUNTIME_BC_OUTPUT}
        COMMAND ${LLVM_LINK_EXECUTABLE} ${RUNTIME_BC_FILES} -o ${RUNTIME_BC_OUTPUT}
        DEPENDS ${RUNTIME_BC_FILES}
        COMMENT "Linking runtime bitcode"
    )
    
    add_custom_target(tsruntime_bc DEPENDS ${RUNTIME_BC_OUTPUT})
endif()

# Optional: Add a target for the http-server test app
find_package(libuv CONFIG REQUIRED)
if(TARGET libuv::uv_a)
    set(LIBUV_TARGET libuv::uv_a)
else()
    set(LIBUV_TARGET libuv::uv)
endif()

find_package(llhttp CONFIG REQUIRED)
find_package(BDWgc CONFIG REQUIRED)
find_package(ICU COMPONENTS uc i18n REQUIRED)
find_package(unofficial-sodium CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)

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
target_link_libraries(test_app PRIVATE tsruntime ${LIBUV_TARGET} llhttp::llhttp_static ws2_32 user32 advapi32 iphlpapi shell32)
target_include_directories(test_app PRIVATE src/runtime/include)

add_custom_command(
    OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/examples/raytracer.json"
    COMMAND node "${CMAKE_CURRENT_SOURCE_DIR}/scripts/dump_ast.js" "${CMAKE_CURRENT_SOURCE_DIR}/examples/raytracer.ts" "${CMAKE_CURRENT_SOURCE_DIR}/examples/raytracer.json"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/examples/raytracer.ts" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/dump_ast.js"
)

add_custom_command(
    OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/examples/raytracer.obj"
    COMMAND "$<TARGET_FILE:ts-aot>" "${CMAKE_CURRENT_SOURCE_DIR}/examples/raytracer.json" -o "${CMAKE_CURRENT_SOURCE_DIR}/examples/raytracer.obj" -O3 --runtime-bc "${CMAKE_BINARY_DIR}/tsruntime.bc"
    DEPENDS ts-aot tsruntime_bc "${CMAKE_CURRENT_SOURCE_DIR}/examples/raytracer.json"
)

add_executable(raytracer_bench examples/raytracer.obj)
target_link_libraries(raytracer_bench PRIVATE tsruntime ${LIBUV_TARGET} llhttp::llhttp_static ws2_32 user32 advapi32 iphlpapi shell32)
target_include_directories(raytracer_bench PRIVATE src/runtime/include)

add_custom_command(
    OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/examples/number_test.json"
    COMMAND node "${CMAKE_CURRENT_SOURCE_DIR}/scripts/dump_ast.js" "${CMAKE_CURRENT_SOURCE_DIR}/examples/number_test.ts" "${CMAKE_CURRENT_SOURCE_DIR}/examples/number_test.json"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/examples/number_test.ts" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/dump_ast.js"
)

add_custom_command(
    OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/examples/number_test.obj"
    COMMAND "$<TARGET_FILE:ts-aot>" "${CMAKE_CURRENT_SOURCE_DIR}/examples/number_test.json" -o "${CMAKE_CURRENT_SOURCE_DIR}/examples/number_test.obj"
    DEPENDS ts-aot "${CMAKE_CURRENT_SOURCE_DIR}/examples/number_test.json"
)

add_executable(number_test examples/number_test.obj)
target_link_libraries(number_test PRIVATE tsruntime ${LIBUV_TARGET} llhttp::llhttp_static ws2_32 user32 advapi32 iphlpapi shell32)
target_include_directories(number_test PRIVATE src/runtime/include)

add_custom_command(
    OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/examples/buffer_string_test.json"
    COMMAND node "${CMAKE_CURRENT_SOURCE_DIR}/scripts/dump_ast.js" "${CMAKE_CURRENT_SOURCE_DIR}/examples/buffer_string_test.ts" "${CMAKE_CURRENT_SOURCE_DIR}/examples/buffer_string_test.json"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/examples/buffer_string_test.ts" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/dump_ast.js"
)

add_custom_command(
    OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/examples/buffer_string_test.obj"
    COMMAND "$<TARGET_FILE:ts-aot>" "${CMAKE_CURRENT_SOURCE_DIR}/examples/buffer_string_test.json" -o "${CMAKE_CURRENT_SOURCE_DIR}/examples/buffer_string_test.obj"
    DEPENDS ts-aot "${CMAKE_CURRENT_SOURCE_DIR}/examples/buffer_string_test.json"
)

add_executable(buffer_string_test examples/buffer_string_test.obj)
target_link_libraries(buffer_string_test PRIVATE tsruntime ${LIBUV_TARGET} llhttp::llhttp_static ws2_32 user32 advapi32 iphlpapi shell32)
target_include_directories(buffer_string_test PRIVATE src/runtime/include)

add_custom_command(
    OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/examples/sha256.json"
    COMMAND node "${CMAKE_CURRENT_SOURCE_DIR}/scripts/dump_ast.js" "${CMAKE_CURRENT_SOURCE_DIR}/examples/sha256.ts" "${CMAKE_CURRENT_SOURCE_DIR}/examples/sha256.json"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/examples/sha256.ts" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/dump_ast.js"
)

add_custom_command(
    OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/examples/sha256.obj"
    COMMAND "$<TARGET_FILE:ts-aot>" "${CMAKE_CURRENT_SOURCE_DIR}/examples/sha256.json" -o "${CMAKE_CURRENT_SOURCE_DIR}/examples/sha256.obj" -O3 --runtime-bc "${CMAKE_BINARY_DIR}/tsruntime.bc"
    DEPENDS ts-aot tsruntime_bc "${CMAKE_CURRENT_SOURCE_DIR}/examples/sha256.json"
)

add_executable(sha256_bench examples/sha256.obj)
target_link_libraries(sha256_bench PRIVATE tsruntime ${LIBUV_TARGET} llhttp::llhttp_static ws2_32 user32 advapi32 iphlpapi shell32)
target_include_directories(sha256_bench PRIVATE src/runtime/include)

add_executable(raytracer_cpp examples/raytracer.cpp)

add_custom_command(
    OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/examples/escape_test.obj"
    COMMAND "$<TARGET_FILE:ts-aot>" "${CMAKE_CURRENT_SOURCE_DIR}/examples/escape_test.json" -o "${CMAKE_CURRENT_SOURCE_DIR}/examples/escape_test.obj"
    DEPENDS ts-aot "${CMAKE_CURRENT_SOURCE_DIR}/examples/escape_test.json"
)

add_executable(escape_test examples/escape_test.obj)
target_link_libraries(escape_test PRIVATE tsruntime ${LIBUV_TARGET} llhttp::llhttp_static ws2_32 user32 advapi32 iphlpapi shell32)
target_include_directories(escape_test PRIVATE src/runtime/include)

add_custom_command(
    OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/examples/struct_test.obj"
    COMMAND "$<TARGET_FILE:ts-aot>" "${CMAKE_CURRENT_SOURCE_DIR}/examples/struct_test.json" -o "${CMAKE_CURRENT_SOURCE_DIR}/examples/struct_test.obj"
    DEPENDS ts-aot "${CMAKE_CURRENT_SOURCE_DIR}/examples/struct_test.json"
)

add_executable(struct_test examples/struct_test.obj)
target_link_libraries(struct_test PRIVATE tsruntime ${LIBUV_TARGET} llhttp::llhttp_static ws2_32 user32 advapi32 iphlpapi shell32)
target_include_directories(struct_test PRIVATE src/runtime/include)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/http-server-lto.obj")
    add_executable(test_app_lto http-server-lto.obj)
    set_target_properties(test_app_lto PROPERTIES LINKER_LANGUAGE CXX)
    target_link_libraries(test_app_lto PRIVATE ${LIBUV_TARGET} llhttp::llhttp_static ws2_32 user32 advapi32 iphlpapi shell32 BDWgc::gc ICU::uc ICU::i18n unofficial-sodium::sodium OpenSSL::SSL OpenSSL::Crypto)
    
    if(MSVC)
        target_link_options(test_app_lto PRIVATE /OPT:REF /OPT:ICF)
    endif()
endif()
