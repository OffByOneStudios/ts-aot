# crypto extension for ts-aot
#
# Node.js-compatible cryptographic functions.
# Built as a separate library (ts_crypto) linked when crypto module is imported.

# OpenSSL is required for cryptographic operations
find_package(OpenSSL REQUIRED)

# libsodium is required for some crypto operations
find_package(unofficial-sodium CONFIG REQUIRED)

# Fix broken unofficial-sodium target in some vcpkg versions
if (TARGET unofficial-sodium::sodium)
    get_target_property(SODIUM_INC unofficial-sodium::sodium INTERFACE_INCLUDE_DIRECTORIES)
    if (SODIUM_INC STREQUAL "/x64-windows-static/include")
        set_target_properties(unofficial-sodium::sodium PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}/vcpkg_installed/x64-windows-static/include")
    endif()

    # Ensure IMPORTED_LOCATION is set for static libraries
    get_target_property(SODIUM_LOC unofficial-sodium::sodium IMPORTED_LOCATION_RELEASE)
    if (NOT SODIUM_LOC)
        find_library(SODIUM_LIB_RELEASE libsodium PATHS "${CMAKE_SOURCE_DIR}/vcpkg_installed/x64-windows-static/lib" NO_DEFAULT_PATH)
        find_library(SODIUM_LIB_DEBUG libsodium PATHS "${CMAKE_SOURCE_DIR}/vcpkg_installed/x64-windows-static/debug/lib" NO_DEFAULT_PATH)
        if (SODIUM_LIB_RELEASE)
            set_target_properties(unofficial-sodium::sodium PROPERTIES IMPORTED_LOCATION_RELEASE "${SODIUM_LIB_RELEASE}")
        endif()
        if (SODIUM_LIB_DEBUG)
            set_target_properties(unofficial-sodium::sodium PROPERTIES IMPORTED_LOCATION_DEBUG "${SODIUM_LIB_DEBUG}")
        endif()
    endif()
endif()

# libuv is required for async operations
find_package(libuv CONFIG REQUIRED)

add_library(ts_crypto STATIC
    src/crypto.cpp
)

target_include_directories(ts_crypto PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_include_directories(ts_crypto PRIVATE
    ${CMAKE_SOURCE_DIR}/src/runtime/include
    ${CMAKE_SOURCE_DIR}/src/runtime/src  # For md5.h
)

target_link_libraries(ts_crypto PRIVATE
    tsruntime
    OpenSSL::SSL
    OpenSSL::Crypto
    unofficial-sodium::sodium
    $<IF:$<TARGET_EXISTS:libuv::uv_a>,libuv::uv_a,libuv::uv>
)
