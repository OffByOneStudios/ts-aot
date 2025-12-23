find_package(Python3 REQUIRED)
find_package(libuv CONFIG REQUIRED)
find_package(BDWgc CONFIG REQUIRED)
find_package(ICU COMPONENTS uc i18n REQUIRED)
find_package(unofficial-sodium CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(llhttp CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)

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
    src/IO.cpp
    src/Primitives.cpp
    src/Math.cpp
    src/TsMap.cpp
    src/TsObject.cpp
    src/Crypto.cpp
    src/md5.cpp
    src/TsJSON.cpp
    src/TsDate.cpp
    src/TsRegExp.cpp
    src/TsPromise.cpp
    src/TsBuffer.cpp
    src/TsURL.cpp
    src/TsFetch.cpp
    src/TsHttp.cpp
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
)

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
    )
    
    # libuv is special because of the generator expression
    if (TARGET libuv::uv_a)
        list(APPEND FAT_DEPS libuv::uv_a)
    else()
        list(APPEND FAT_DEPS libuv::uv)
    endif()

    set(MERGE_LIBS "")
    foreach(dep ${FAT_DEPS})
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

def merge():
    output = sys.argv[1]
    libs = sys.argv[2:]
    
    valid_libs = []
    for l in libs:
        if not l: continue
        if os.path.exists(l):
            valid_libs.append(l)
        else:
            print(f'Warning: Library not found: {l}')
    
    if not valid_libs:
        print('No libraries to merge!')
        return
    
    cmd = [r'${MSVC_LIB}', '/NOLOGO', f'/OUT:{output}'] + valid_libs
    print(f'Merging {len(valid_libs)} libraries into {output}...')
    subprocess.check_call(cmd)

if __name__ == '__main__':
    merge()
")

    add_custom_command(
        TARGET tsruntime POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E echo "Merging dependencies into tsruntime.lib (Fat Build)..."
        COMMAND ${Python3_EXECUTABLE} ${MERGE_LIB_SCRIPT} 
                $<TARGET_FILE:tsruntime>
                $<TARGET_FILE:tsruntime>
                ${MERGE_LIBS}
        COMMENT "Merging dependencies into tsruntime.lib (Fat Build)"
    )
endif()
