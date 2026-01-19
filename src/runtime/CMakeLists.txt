find_package(Python3 REQUIRED)
find_package(libuv CONFIG REQUIRED)
find_package(BDWgc CONFIG REQUIRED)
find_package(ICU COMPONENTS uc i18n data REQUIRED)
find_package(unofficial-sodium CONFIG REQUIRED)

# Fix broken unofficial-sodium target in some vcpkg versions
if (TARGET unofficial-sodium::sodium)
    get_target_property(SODIUM_INC unofficial-sodium::sodium INTERFACE_INCLUDE_DIRECTORIES)
    if (SODIUM_INC STREQUAL "/x64-windows-static/include")
        set_target_properties(unofficial-sodium::sodium PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/../../vcpkg_installed/x64-windows-static/include")
    endif()
    
    # Ensure IMPORTED_LOCATION is set for static libraries
    get_target_property(SODIUM_LOC unofficial-sodium::sodium IMPORTED_LOCATION_RELEASE)
    if (NOT SODIUM_LOC)
        find_library(SODIUM_LIB_RELEASE libsodium PATHS "${CMAKE_CURRENT_SOURCE_DIR}/../../vcpkg_installed/x64-windows-static/lib" NO_DEFAULT_PATH)
        find_library(SODIUM_LIB_DEBUG libsodium PATHS "${CMAKE_CURRENT_SOURCE_DIR}/../../vcpkg_installed/x64-windows-static/debug/lib" NO_DEFAULT_PATH)
        if (SODIUM_LIB_RELEASE)
            set_target_properties(unofficial-sodium::sodium PROPERTIES IMPORTED_LOCATION_RELEASE "${SODIUM_LIB_RELEASE}")
        endif()
        if (SODIUM_LIB_DEBUG)
            set_target_properties(unofficial-sodium::sodium PROPERTIES IMPORTED_LOCATION_DEBUG "${SODIUM_LIB_DEBUG}")
        endif()
    endif()
endif()
find_package(nlohmann_json CONFIG REQUIRED)
find_package(llhttp CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(libtommath CONFIG REQUIRED)

# Fix broken libtommath target in some vcpkg versions
if (TARGET libtommath)
    get_target_property(TOMMATH_INC libtommath INTERFACE_INCLUDE_DIRECTORIES)
    if (NOT EXISTS "${TOMMATH_INC}")
        set_target_properties(libtommath PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/../../vcpkg_installed/x64-windows-static/include")
    endif()
endif()

# ICU Support Modes:
# FULL: Link against full ICU libraries and data.
# SMALL: Link against ICU but use a filtered/minimal data set.
# NONE: Disable ICU (not recommended, breaks many JS features).
set(TS_ICU_MODE "FULL" CACHE STRING "ICU support mode: FULL, SMALL, or NONE")
set_property(CACHE TS_ICU_MODE PROPERTY STRINGS "FULL" "SMALL" "NONE")

# Fat Runtime: Merge all dependencies into a single tsruntime.lib
option(TS_FAT_RUNTIME "Merge all dependencies into tsruntime.lib" OFF)

add_library(tsruntime STATIC
    src/Core.cpp
    src/Memory.cpp
    src/EventLoop.cpp
    src/TsString.cpp
    src/TsArray.cpp
    src/node/fs.cpp
    src/node/path.cpp
    src/node/EventEmitter.cpp
    src/node/ReadStream.cpp
    src/node/WriteStream.cpp
    src/node/Writable.cpp
    src/node/Socket.cpp
    src/node/SecureSocket.cpp
    src/node/Server.cpp
    src/Primitives.cpp
    src/Math.cpp
    src/TsMap.cpp
    src/TsSet.cpp
    src/TsObject.cpp
    src/TsBoundFunction.cpp
    src/TsBigInt.cpp
    src/TsSymbol.cpp
    src/Crypto.cpp
    src/md5.cpp
    src/TsJSON.cpp
    src/TsDate.cpp
    src/TsRegExp.cpp
    src/TsPromise.cpp
    src/TsBuffer.cpp
    src/TsTextEncoding.cpp
    src/TsURL.cpp
    src/TsUtil.cpp
    src/TsBoxedPrimitives.cpp
    src/TsQueryString.cpp
    src/TsFetch.cpp
    src/TsHttp.cpp
    src/TsError.cpp
    src/TsCell.cpp
    src/TsProxy.cpp
    src/TsReflect.cpp
    src/TsOS.cpp
    src/TsTransform.cpp
    src/TsVM.cpp
    src/TsV8.cpp
    src/TsAssert.cpp
    src/TsStringDecoder.cpp
    src/TsPerfHooks.cpp
    src/TsAsyncHooks.cpp
    src/TsChildProcess.cpp
    src/TsCluster.cpp
    src/TsDNS.cpp
)

target_include_directories(tsruntime PUBLIC include)

target_link_libraries(tsruntime PUBLIC
    $<IF:$<TARGET_EXISTS:libuv::uv_a>,libuv::uv_a,libuv::uv>
    BDWgc::gc
    ICU::uc
    ICU::i18n
    unofficial-sodium::sodium
    nlohmann_json::nlohmann_json
    llhttp::llhttp_static
    OpenSSL::SSL
    OpenSSL::Crypto
    libtommath
)

if (WIN32)
    target_link_libraries(tsruntime PUBLIC Dbghelp)
endif()

if (TS_ICU_MODE STREQUAL "FULL")
    target_compile_definitions(tsruntime PUBLIC TS_ICU_FULL)
elseif (TS_ICU_MODE STREQUAL "SMALL")
    target_compile_definitions(tsruntime PUBLIC TS_ICU_SMALL)
else()
    target_compile_definitions(tsruntime PUBLIC TS_ICU_NONE)
endif()

target_compile_definitions(tsruntime PUBLIC TS_RUNTIME_EXPORT)

if (TS_FAT_RUNTIME AND MSVC)
    # List of dependencies to merge
    set(FAT_DEPS
        BDWgc::gc
        ICU::uc
        ICU::i18n
        ICU::data
        unofficial-sodium::sodium
        llhttp::llhttp_static
        OpenSSL::SSL
        OpenSSL::Crypto
        libtommath
    )
    
    # libuv is special because of the generator expression
    if (TARGET libuv::uv_a)
        list(APPEND FAT_DEPS libuv::uv_a)
    else()
        list(APPEND FAT_DEPS libuv::uv)
    endif()

    set(MERGE_LIBS "")
    foreach(dep ${FAT_DEPS})
        # Use a generator expression that only evaluates if the target exists and has a location
        list(APPEND MERGE_LIBS "$<TARGET_FILE:${dep}>")
    endforeach()

    # This is a helper to merge static libraries on Windows
    set(MERGE_LIB_SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/merge_libs.py")
    find_program(MSVC_LIB lib.exe)
    if (NOT MSVC_LIB)
        set(MSVC_LIB "lib.exe")
    endif()
    
    file(WRITE ${MERGE_LIB_SCRIPT} "
import sys
import subprocess
import os
import shutil

def merge():
    output = sys.argv[1]
    raw_libs = sys.argv[2:]
    
    # Handle semicolon-separated lists from CMake
    libs = []
    for l in raw_libs:
        if ';' in l:
            libs.extend(l.split(';'))
        else:
            libs.append(l)
    
    # Filter out empty strings and duplicates, preserving order
    seen = set()
    unique_libs = []
    for l in libs:
        if not l: continue
        # Normalize path for comparison
        abs_l = os.path.abspath(l).lower()
        if abs_l not in seen:
            if os.path.exists(l):
                unique_libs.append(l)
                seen.add(abs_l)
            else:
                print(f'Warning: Library not found: {l}')
    
    if not unique_libs:
        print('No libraries to merge!')
        return
    
    print(f'Merging {len(unique_libs)} libraries into {output}...')
    for l in unique_libs:
        print(f'  - {l}')
        
    temp_output = output + '.fat.lib'
    cmd = [r'${MSVC_LIB}', '/NOLOGO', f'/OUT:{temp_output}'] + unique_libs
    subprocess.check_call(cmd)
    
    # Replace original with fat library
    if os.path.exists(output):
        os.remove(output)
    os.rename(temp_output, output)

if __name__ == '__main__':
    merge()
")

    add_custom_command(
        TARGET tsruntime POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E echo "Merging dependencies into tsruntime.lib (Fat Build)..."
        COMMAND ${Python3_EXECUTABLE} ${MERGE_LIB_SCRIPT} 
                $<TARGET_FILE:tsruntime>
                $<TARGET_OBJECTS:tsruntime>
                ${MERGE_LIBS}
        COMMENT "Merging dependencies into tsruntime.lib (Fat Build)"
    )
endif()
