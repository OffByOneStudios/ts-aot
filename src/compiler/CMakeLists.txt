find_package(LLVM REQUIRED CONFIG)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(cxxopts CONFIG REQUIRED)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

# Map LLVM components
llvm_map_components_to_libnames(llvm_libs core support codegen irreader x86codegen)

add_executable(ts-aot
    main.cpp
    ast/AstLoader.cpp
    analysis/SymbolTable.cpp
    analysis/Analyzer.cpp
    analysis/Monomorphizer.cpp
    codegen/IRGenerator.cpp
    # Add other source files here
)

target_include_directories(ts-aot PRIVATE ${LLVM_INCLUDE_DIRS})

target_link_libraries(ts-aot PRIVATE
    ${llvm_libs}
    nlohmann_json::nlohmann_json
    fmt::fmt
    cxxopts::cxxopts
    # tsruntime # Link if needed, but usually compiler generates code that links to runtime, not links to it itself
)

target_compile_definitions(ts-aot PRIVATE ${LLVM_DEFINITIONS})
