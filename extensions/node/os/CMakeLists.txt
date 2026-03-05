# ts_os - Node.js os module extension library
#
# Provides operating system information functions.

add_library(ts_os STATIC
    src/os.cpp
)

target_include_directories(ts_os PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_include_directories(ts_os PRIVATE
    ${CMAKE_SOURCE_DIR}/src/runtime/include
)

target_link_libraries(ts_os PRIVATE tsruntime)

# Copy extension contract to build directory
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/os.ext.json)
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/os.ext.json
        ${CMAKE_BINARY_DIR}/extensions/node/os/os.ext.json
        COPYONLY
    )
    # Also copy to compiler directory for ExtensionLoader
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/os.ext.json
        ${CMAKE_BINARY_DIR}/src/compiler/extensions/node/os.ext.json
        COPYONLY
    )
endif()
