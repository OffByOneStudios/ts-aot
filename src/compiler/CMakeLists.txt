find_package(LLVM REQUIRED CONFIG)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(cxxopts CONFIG REQUIRED)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

# Map LLVM components
llvm_map_components_to_libnames(llvm_libs core support codegen irreader x86codegen x86asmparser)

add_library(tscompiler_lib STATIC
    ast/AstLoader.cpp
    analysis/SymbolTable.cpp
    analysis/Analyzer.cpp
    analysis/Analyzer_Classes.cpp
    analysis/Analyzer_Functions.cpp
    analysis/Analyzer_Statements.cpp
    analysis/Analyzer_Expressions.cpp
    analysis/Monomorphizer.cpp
    codegen/IRGenerator.cpp
    codegen/IRGenerator_Classes.cpp
    codegen/IRGenerator_Functions.cpp
    codegen/IRGenerator_Statements.cpp
    codegen/IRGenerator_Expressions.cpp
)

target_include_directories(tscompiler_lib PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${LLVM_INCLUDE_DIRS}
)

target_link_libraries(tscompiler_lib PUBLIC
    ${llvm_libs}
    nlohmann_json::nlohmann_json
    fmt::fmt
    cxxopts::cxxopts
)

target_compile_definitions(tscompiler_lib PUBLIC ${LLVM_DEFINITIONS})

add_executable(ts-aot
    main.cpp
)

target_link_libraries(ts-aot PRIVATE
    tscompiler_lib
)
