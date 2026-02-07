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
    # analysis/Analyzer_StdLib_Events.cpp - removed, now handled by extensions/node/events/events.ext.json
    # analysis/Analyzer_StdLib_Stream.cpp - removed, now handled by extensions/node/stream/stream.ext.json
    analysis/Analyzer_StdLib_Buffer.cpp
    # analysis/Analyzer_StdLib_Net.cpp - removed, now handled by extensions/node/net/net.ext.json
    analysis/Analyzer_StdLib_HTTP.cpp
    analysis/Analyzer_StdLib_Process.cpp
    # analysis/Analyzer_StdLib_Util.cpp - removed, now handled by extensions/node/util/util.ext.json
    # analysis/Analyzer_StdLib_OS.cpp - removed, now handled by extensions/node/os/os.ext.json
    # analysis/Analyzer_StdLib_Crypto.cpp - removed, now handled by extensions/node/crypto/crypto.ext.json
    # analysis/Analyzer_StdLib_VM.cpp - removed, now handled by extensions/node/vm/vm.ext.json
    # analysis/Analyzer_StdLib_V8.cpp - removed, now handled by extensions/node/v8/v8.ext.json
    # analysis/Analyzer_StdLib_StringDecoder.cpp - removed, now handled by extensions/node/string_decoder/string_decoder.ext.json
    # analysis/Analyzer_StdLib_AsyncHooks.cpp - removed, now handled by extensions/node/async_hooks/async_hooks.ext.json
    # analysis/Analyzer_StdLib_ChildProcess.cpp - removed, now handled by extensions/node/child_process/child_process.ext.json
    # analysis/Analyzer_StdLib_Cluster.cpp - removed, now handled by extensions/node/cluster/cluster.ext.json
    # analysis/Analyzer_StdLib_DNS.cpp - removed, now handled by extensions/node/dns/dns.ext.json
    # analysis/Analyzer_StdLib_Dgram.cpp - removed, now handled by extensions/node/dgram/dgram.ext.json
    # analysis/Analyzer_StdLib_Inspector.cpp - removed, now handled by extensions/node/inspector/inspector.ext.json
    # analysis/Analyzer_StdLib_Readline.cpp - removed, now handled by extensions/node/readline/readline.ext.json
    # analysis/Analyzer_StdLib_Tls.cpp - removed, now handled by extensions/node/tls/tls.ext.json
    # analysis/Analyzer_StdLib_TTY.cpp - removed, now handled by extensions/node/tty/tty.ext.json
    analysis/Analyzer_StdLib_Http2.cpp
    # analysis/Analyzer_StdLib_Zlib.cpp - removed, now handled by extensions/node/zlib/zlib.ext.json
    # analysis/Analyzer_StdLib_Module.cpp - removed, now handled by extensions/node/module/module.ext.json
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
