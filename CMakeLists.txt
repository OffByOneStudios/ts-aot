cmake_minimum_required(VERSION 3.20)

# Use static libraries with dynamic CRT (/MD) - no DLLs needed at runtime
if(NOT DEFINED VCPKG_TARGET_TRIPLET)
    set(VCPKG_TARGET_TRIPLET "x64-windows-static-md" CACHE STRING "")
endif()

# Use vcpkg_installed in project root (not build directory)
# This avoids rebuilding packages on each clean build
set(VCPKG_INSTALLED_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg_installed" CACHE PATH "")

project(ts-aot LANGUAGES CXX)

# Force Release mode only
set(CMAKE_CONFIGURATION_TYPES "Release" CACHE STRING "" FORCE)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Emit debug symbols even in Release (project forces Release-only builds).
# This makes crash addresses actionable without changing optimization level.
if(MSVC)
    set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT ProgramDatabase)
    add_compile_options(/Zi)
    add_link_options(/DEBUG:FULL)
endif()

# Support static CRT for static builds
if(VCPKG_TARGET_TRIPLET STREQUAL "x64-windows-static")
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# Use vcpkg toolchain
if(DEFINED ENV{VCPKG_ROOT} AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")
endif()

# Ensure packages in vcpkg_installed are found
# Set _VCPKG_INSTALLED_DIR for packages that use it (like unofficial-sodium)
set(_VCPKG_INSTALLED_DIR "${VCPKG_INSTALLED_DIR}" CACHE PATH "" FORCE)
list(APPEND CMAKE_PREFIX_PATH "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")

# Add subdirectories
add_subdirectory(src/runtime)
add_subdirectory(src/compiler)
add_subdirectory(extensions)

# LTO Support: Build runtime bitcode
find_package(LLVM REQUIRED CONFIG)
find_program(CLANG_EXECUTABLE clang HINTS "${LLVM_TOOLS_BINARY_DIR}")
find_program(LLVM_LINK_EXECUTABLE llvm-link HINTS "${LLVM_TOOLS_BINARY_DIR}")

if(CLANG_EXECUTABLE AND LLVM_LINK_EXECUTABLE)
    set(RUNTIME_BC_OUTPUT "${CMAKE_BINARY_DIR}/tsruntime.bc")
    set(RUNTIME_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/src")
    set(RUNTIME_INC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/include")
    
    file(GLOB_RECURSE RUNTIME_SOURCES "${RUNTIME_SRC_DIR}/*.cpp")
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
