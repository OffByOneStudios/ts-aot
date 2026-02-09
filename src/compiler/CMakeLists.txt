find_package(LLVM REQUIRED CONFIG)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(cxxopts CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")
message(STATUS "LLVM_LIBRARY_DIRS: ${LLVM_LIBRARY_DIRS}")

# Map LLVM components
llvm_map_components_to_libnames(llvm_libs
    Support
    Core
    IRReader
    AsmParser
    Analysis
    TransformUtils
    ScalarOpts
    IPO
    Passes
    Target
    MC
    Object
    BitWriter
    BitReader
    TargetParser
    X86
    Option
    LTO
    WindowsDriver
    WindowsManifest
    LibDriver
    OrcJIT
    OrcShared
    OrcTargetProcess
    ExecutionEngine
)

# LLD libraries are not always part of the standard component map
find_library(LLD_COFF_LIB lldCOFF PATHS ${LLVM_LIBRARY_DIRS} NO_DEFAULT_PATH)
find_library(LLD_COMMON_LIB lldCommon PATHS ${LLVM_LIBRARY_DIRS} NO_DEFAULT_PATH)
set(lld_libs ${LLD_COFF_LIB} ${LLD_COMMON_LIB})

add_library(tscompiler_lib STATIC
    ast/AstLoader_Core.cpp
    ast/AstLoader_Expressions.cpp
    ast/AstLoader_Statements.cpp
    ast/AstPrinter.cpp
    analysis/SymbolTable.cpp
    analysis/ModuleResolver.cpp
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
    analysis/Analyzer_JSX.cpp
    analysis/Analyzer_Escape.cpp
    analysis/Monomorphizer.cpp
    Driver.cpp
    codegen/CodeGenerator.cpp
    codegen/LinkerDriver.cpp
    hir/HIR.cpp
    hir/ASTToHIR.cpp
    hir/HIRPrinter.cpp
    hir/HIRToLLVM.cpp
    hir/BuiltinRegistry.cpp
    hir/LoweringRegistry.cpp
    hir/handlers/HandlerRegistry.cpp
    hir/handlers/MathHandler.cpp
    hir/handlers/ConsoleHandler.cpp
    hir/handlers/ArrayHandler.cpp
    hir/handlers/MapSetHandler.cpp
    hir/handlers/TimerHandler.cpp
    hir/handlers/BigIntHandler.cpp
    hir/handlers/PathHandler.cpp
    hir/handlers/GeneratorHandler.cpp
    hir/handlers/RegExpHandler.cpp
    hir/passes/HIRPass.cpp
    hir/passes/PassManager.cpp
    hir/passes/MethodResolutionPass.cpp
    hir/passes/BuiltinResolutionPass.cpp
    hir/passes/TypePropagationPass.cpp
    hir/passes/IntegerOptimizationPass.cpp
    hir/passes/ConstantFoldingPass.cpp
    hir/passes/DeadCodeEliminationPass.cpp
    hir/passes/InliningPass.cpp
    hir/passes/EscapeAnalysisPass.cpp
    extensions/ExtensionLoader.cpp
    parser/Lexer.cpp
    parser/Parser.cpp
    parser/Parser_Expressions.cpp
    parser/Parser_TypeAnnotations.cpp
    parser/AstSerializer.cpp
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
    spdlog::spdlog
)

target_compile_definitions(tscompiler_lib PUBLIC ${LLVM_DEFINITIONS})

add_executable(ts-aot
    main.cpp
)

target_link_libraries(ts-aot
    PRIVATE
    tscompiler_lib
    ${llvm_libs}
    ${lld_libs}
    cxxopts::cxxopts
    fmt::fmt
    nlohmann_json::nlohmann_json
    spdlog::spdlog
)
