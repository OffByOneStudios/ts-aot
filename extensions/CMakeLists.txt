# Extension libraries for ts-aot
#
# Each extension is a separate static library that gets conditionally linked
# when the corresponding Node.js module is imported in TypeScript code.
#
# Structure:
#   extensions/
#   ├── CMakeLists.txt (this file)
#   └── node/
#       ├── CMakeLists.txt
#       └── <module>/
#           ├── CMakeLists.txt
#           ├── <module>.ext.json
#           ├── include/
#           └── src/

add_subdirectory(node)
