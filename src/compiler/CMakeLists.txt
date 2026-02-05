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
    analysis/Analyzer_StdLib_FS.cpp
    analysis/Analyzer_StdLib_Path.cpp
    analysis/Analyzer_StdLib_Events.cpp
    analysis/Analyzer_StdLib_Stream.cpp
    analysis/Analyzer_StdLib_Buffer.cpp
    analysis/Analyzer_StdLib_Net.cpp
    analysis/Analyzer_StdLib_HTTP.cpp
    analysis/Analyzer_StdLib_Process.cpp
    analysis/Analyzer_StdLib_Util.cpp
    analysis/Analyzer_StdLib_OS.cpp
    analysis/Analyzer_StdLib_Crypto.cpp
    analysis/Analyzer_StdLib_QueryString.cpp
    analysis/Analyzer_StdLib_VM.cpp
    analysis/Analyzer_StdLib_V8.cpp
    analysis/Analyzer_StdLib_Assert.cpp
    analysis/Analyzer_StdLib_StringDecoder.cpp
    analysis/Analyzer_StdLib_PerfHooks.cpp
    analysis/Analyzer_StdLib_AsyncHooks.cpp
    analysis/Analyzer_StdLib_ChildProcess.cpp
    analysis/Analyzer_StdLib_Cluster.cpp
    analysis/Analyzer_StdLib_DNS.cpp
    analysis/Analyzer_StdLib_Dgram.cpp
    analysis/Analyzer_StdLib_Inspector.cpp
    analysis/Analyzer_StdLib_Readline.cpp
    analysis/Analyzer_StdLib_Tls.cpp
    analysis/Analyzer_StdLib_TTY.cpp
    analysis/Analyzer_StdLib_Http2.cpp
    analysis/Analyzer_StdLib_Zlib.cpp
    analysis/Analyzer_StdLib_Module.cpp
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
    codegen/BoxingPolicy.cpp
    codegen/LinkerDriver.cpp
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
    codegen/IRGenerator_Expressions_Calls_Builtin_FS.cpp
    # Path module now handled via extension contract (path.ext.json)
    # codegen/IRGenerator_Expressions_Calls_Builtin_Path.cpp
    codegen/IRGenerator_Expressions_Calls_Builtin_Events.cpp
    codegen/IRGenerator_Expressions_Calls_Builtin_Promise.cpp
    codegen/IRGenerator_Expressions_Calls_Builtin_Stream.cpp
    codegen/IRGenerator_Expressions_Calls_Builtin_Buffer.cpp
    codegen/IRGenerator_Expressions_Calls_Builtin_Net.cpp
    codegen/IRGenerator_Expressions_Calls_Builtin_HTTP.cpp
    codegen/IRGenerator_Expressions_Calls_Builtin_Process.cpp
    codegen/IRGenerator_Expressions_Calls_Builtin_TextEncoding.cpp
    codegen/IRGenerator_Expressions_Calls_Builtin_URL.cpp
    codegen/IRGenerator_Expressions_Calls_Builtin_Util.cpp
    codegen/IRGenerator_Expressions_Calls_Builtin_QueryString.cpp
    codegen/IRGenerator_Expressions_Calls_Builtin_OS.cpp
    codegen/IRGenerator_Expressions_Calls_Builtin_VM.cpp
    codegen/IRGenerator_Expressions_Calls_Builtin_V8.cpp
    codegen/IRGenerator_Expressions_Calls_Builtin_Assert.cpp
    codegen/IRGenerator_Expressions_Calls_Builtin_StringDecoder.cpp
    codegen/IRGenerator_Expressions_Calls_Builtin_PerfHooks.cpp
    codegen/IRGenerator_Expressions_Calls_Builtin_AsyncHooks.cpp
    codegen/IRGenerator_Expressions_Calls_Builtin_ChildProcess.cpp
    codegen/IRGenerator_Expressions_Calls_Builtin_Cluster.cpp
    codegen/IRGenerator_Expressions_Calls_Builtin_DNS.cpp
    codegen/IRGenerator_Expressions_Calls_Builtin_Dgram.cpp
    codegen/IRGenerator_Expressions_Calls_Builtin_Inspector.cpp
    codegen/IRGenerator_Expressions_Calls_Builtin_Readline.cpp
    codegen/IRGenerator_Expressions_Calls_Builtin_Tls.cpp
    codegen/IRGenerator_Expressions_Calls_Builtin_TTY.cpp
    codegen/IRGenerator_Expressions_Calls_Builtin_Http2.cpp
    codegen/IRGenerator_Expressions_Calls_Builtin_Zlib.cpp
    codegen/IRGenerator_Expressions_Calls_Builtin_Module.cpp
    codegen/IRGenerator_Expressions_Calls_Member.cpp
    codegen/IRGenerator_Extensions.cpp
    codegen/IRGenerator_Expressions_Literals.cpp
    codegen/IRGenerator_Expressions_Access.cpp
    codegen/IRGenerator_Expressions_Async.cpp
    codegen/IRGenerator_JSX.cpp
    hir/HIR.cpp
    hir/ASTToHIR.cpp
    hir/HIRPrinter.cpp
    hir/HIRToLLVM.cpp
    hir/BuiltinRegistry.cpp
    hir/LoweringRegistry.cpp
    hir/handlers/HandlerRegistry.cpp
    hir/handlers/MathHandler.cpp
    hir/passes/HIRPass.cpp
    hir/passes/PassManager.cpp
    hir/passes/MethodResolutionPass.cpp
    hir/passes/BuiltinResolutionPass.cpp
    hir/passes/TypePropagationPass.cpp
    hir/passes/IntegerOptimizationPass.cpp
    hir/passes/ConstantFoldingPass.cpp
    hir/passes/DeadCodeEliminationPass.cpp
    hir/passes/InliningPass.cpp
    extensions/ExtensionLoader.cpp
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
