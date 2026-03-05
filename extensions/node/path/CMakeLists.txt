# ts_path - Node.js path module extension library
#
# This library is conditionally linked when a TypeScript file imports 'path'.
# It provides all path manipulation functions defined in the path.ext.json contract.

add_library(ts_path STATIC
    src/path.cpp
)

# Include our own headers
target_include_directories(ts_path PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# We need access to tsruntime headers (TsString, TsArray, etc.)
target_include_directories(ts_path PRIVATE
    ${CMAKE_SOURCE_DIR}/src/runtime/include
)

# Link against tsruntime for TsString::Create, TsArray, etc.
# Note: This creates a dependency, but since we're statically linking,
# the linker will only pull in what's actually used.
target_link_libraries(ts_path PRIVATE
    tsruntime
)

# Copy the extension contract to the build directory so the compiler can find it
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/path.ext.json
    ${CMAKE_BINARY_DIR}/extensions/node/path/path.ext.json
    COPYONLY
)

# Also copy to the compiler's expected location
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/path.ext.json
    ${CMAKE_BINARY_DIR}/src/compiler/extensions/node/path.ext.json
    COPYONLY
)
