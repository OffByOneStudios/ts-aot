find_package(LLVM REQUIRED CONFIG)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(cxxopts CONFIG REQUIRED)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

# Map LLVM components
llvm_map_components_to_libnames(llvm_libs core support codegen irreader x86codegen x86asmparser)

add_library(tscompiler_lib STATIC
    ast/AstLoader_Core.cpp
    ast/AstLoader_Expressions.cpp
    ast/AstLoader_Statements.cpp
    analysis/SymbolTable.cpp
    analysis/Analyzer.cpp
    analysis/Analyzer_Core.cpp
    analysis/Analyzer_StdLib.cpp
    analysis/Analyzer_Declarations.cpp
    analysis/Analyzer_Helpers.cpp
    analysis/Analyzer_Classes.cpp
    analysis/Analyzer_Functions.cpp
    analysis/Analyzer_Statements.cpp
    analysis/Analyzer_Expressions_Binary.cpp
    analysis/Analyzer_Expressions_Unary.cpp
    analysis/Analyzer_Expressions_Calls.cpp
    analysis/Analyzer_Expressions_Literals.cpp
    analysis/Analyzer_Expressions_Access.cpp
    analysis/Analyzer_Expressions_Async.cpp
    analysis/Monomorphizer.cpp
    codegen/IRGenerator.cpp
    codegen/IRGenerator_Core.cpp
    codegen/IRGenerator_Classes.cpp
    codegen/IRGenerator_Functions.cpp
    codegen/IRGenerator_Statements.cpp
    codegen/IRGenerator_Statements_Loops.cpp
    codegen/IRGenerator_Expressions_Binary.cpp
    codegen/IRGenerator_Expressions_Unary.cpp
    codegen/IRGenerator_Expressions_Calls.cpp
    codegen/IRGenerator_Expressions_Calls_Builtin.cpp
    codegen/IRGenerator_Expressions_Calls_Member.cpp
    codegen/IRGenerator_Expressions_Literals.cpp
    codegen/IRGenerator_Expressions_Access.cpp
    codegen/IRGenerator_Expressions_Async.cpp
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
