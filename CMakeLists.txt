cmake_minimum_required(VERSION 3.20)

# Use static libraries - set default triplet per platform
if(NOT DEFINED VCPKG_TARGET_TRIPLET)
    if(WIN32)
        set(VCPKG_TARGET_TRIPLET "x64-windows-static-md" CACHE STRING "")
    else()
        set(VCPKG_TARGET_TRIPLET "x64-linux" CACHE STRING "")
    endif()
endif()

# Use vcpkg_installed in project root (not build directory)
# This avoids rebuilding packages on each clean build
set(VCPKG_INSTALLED_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg_installed" CACHE PATH "")

# Linked git worktrees (agent worktrees under .claude/worktrees, or any
# `git worktree add`) must NOT install their own 26GB vcpkg_installed copy.
# A linked worktree has a `.git` FILE (gitdir: <main>/.git/worktrees/<name>)
# instead of a directory: resolve the main checkout and share its install
# read-only. VCPKG_MANIFEST_INSTALL=OFF stops configure from ever writing
# into the shared dir (concurrent worktree configures would race).
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.git" AND NOT IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/.git")
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/.git" _ts_gitfile)
    string(STRIP "${_ts_gitfile}" _ts_gitfile)
    string(REPLACE "gitdir: " "" _ts_gitdir "${_ts_gitfile}")
    file(TO_CMAKE_PATH "${_ts_gitdir}" _ts_gitdir)
    string(REGEX REPLACE "/\\.git/worktrees/[^/]+$" "" _ts_main "${_ts_gitdir}")
    if(NOT _ts_main STREQUAL _ts_gitdir AND EXISTS "${_ts_main}/vcpkg_installed")
        set(VCPKG_INSTALLED_DIR "${_ts_main}/vcpkg_installed" CACHE PATH "" FORCE)
        set(VCPKG_MANIFEST_INSTALL OFF CACHE BOOL "" FORCE)
        message(STATUS "ts-aot: linked worktree - sharing vcpkg_installed from ${_ts_main}")
    endif()
endif()

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
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    add_compile_options(-g)
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

# Build the runtime ALSO as a single shared library (DLL/.so) alongside the
# static tsruntime, so emitted executables can link it via `ts-aot
# --shared-runtime`. This reuses the static target's already-compiled object
# files ($<TARGET_OBJECTS:tsruntime>) and the already-installed vcpkg static
# libs, so it adds only one link step (no runtime-source or vcpkg rebuild).
# Must come after src/runtime and extensions so its target dependencies exist.
add_subdirectory(src/sharedrt)

# LTO Support: Build runtime bitcode
find_package(LLVM REQUIRED CONFIG)
find_program(CLANG_EXECUTABLE clang HINTS "${LLVM_TOOLS_BINARY_DIR}")
find_program(LLVM_LINK_EXECUTABLE llvm-link HINTS "${LLVM_TOOLS_BINARY_DIR}")

if(CLANG_EXECUTABLE AND LLVM_LINK_EXECUTABLE)
    set(RUNTIME_BC_OUTPUT "${CMAKE_BINARY_DIR}/tsruntime.bc")
    set(RUNTIME_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/src")
    set(RUNTIME_INC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/include")
    
    file(GLOB_RECURSE RUNTIME_SOURCES "${RUNTIME_SRC_DIR}/*.cpp")
    file(GLOB_RECURSE NODECORE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/extensions/node/core/src/*.cpp")
    list(APPEND RUNTIME_SOURCES ${NODECORE_SOURCES})
    set(RUNTIME_BC_FILES "")
    
    foreach(src ${RUNTIME_SOURCES})
        get_filename_component(name ${src} NAME_WE)
        set(bc_file "${CMAKE_BINARY_DIR}/runtime_bc/${name}.bc")
        list(APPEND RUNTIME_BC_FILES ${bc_file})
        
        set(BC_COMPILE_DEFS -DTS_RUNTIME_EXPORT)
        if(WIN32)
            list(APPEND BC_COMPILE_DEFS -D_CRT_SECURE_NO_WARNINGS -D_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH)
        endif()

        add_custom_command(
            OUTPUT ${bc_file}
            COMMAND ${CLANG_EXECUTABLE} -c -emit-llvm -O3 -std=c++20 -fstack-protector-strong
                    -I${RUNTIME_INC_DIR}
                    -I${CMAKE_CURRENT_SOURCE_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}/include
                    ${BC_COMPILE_DEFS}
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
