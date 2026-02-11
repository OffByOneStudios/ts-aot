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
    codegen/TsAotGC.cpp
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

# Extract icudt74l.dat from the vcpkg ICU static library and place it next to
# ts-aot.exe. This file is needed at runtime by executables compiled without
# --bundle-icu (the default). The extraction parses the COFF archive to find
# the .rdata section containing the raw ICU data package.
find_package(ICU COMPONENTS data REQUIRED)
get_target_property(ICU_DATA_LIB ICU::data IMPORTED_LOCATION_RELEASE)
if (NOT ICU_DATA_LIB)
    get_target_property(ICU_DATA_LIB ICU::data IMPORTED_LOCATION)
endif()
if (ICU_DATA_LIB)
    set(ICU_DAT_OUTPUT "$<TARGET_FILE_DIR:ts-aot>/icudt74l.dat")
    set(ICU_EXTRACT_SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/extract_icu_data.py")
    file(WRITE ${ICU_EXTRACT_SCRIPT} "
import struct, sys, os

lib_path, out_path = sys.argv[1], sys.argv[2]
if os.path.exists(out_path):
    sys.exit(0)  # Already extracted

with open(lib_path, 'rb') as f:
    data = f.read()

# Scan for COFF object with AMD64 machine type
pos = 8  # Skip '!<arch>\\n'
while pos < len(data):
    idx = data.find(b'\\x64\\x86', pos)
    if idx == -1:
        break
    machine = struct.unpack_from('<H', data, idx)[0]
    num_sections = struct.unpack_from('<H', data, idx + 2)[0]
    if machine == 0x8664 and 1 <= num_sections <= 20:
        for i in range(num_sections):
            sec_off = idx + 20 + i * 40
            sec_name = data[sec_off:sec_off+8].rstrip(b'\\x00').decode('ascii', errors='replace')
            sec_rawsize = struct.unpack_from('<I', data, sec_off + 16)[0]
            sec_rawoff = struct.unpack_from('<I', data, sec_off + 20)[0]
            if sec_name == '.rdata' and sec_rawsize > 1000000:
                icu_data = data[idx + sec_rawoff:idx + sec_rawoff + sec_rawsize]
                os.makedirs(os.path.dirname(out_path) or '.', exist_ok=True)
                with open(out_path, 'wb') as out:
                    out.write(icu_data)
                print(f'Extracted icudt74l.dat ({len(icu_data):,} bytes)')
                sys.exit(0)
        break
    pos = idx + 2

print('ERROR: Could not extract ICU data from ' + lib_path, file=sys.stderr)
sys.exit(1)
")
    find_package(Python3 REQUIRED)
    add_custom_command(
        TARGET ts-aot POST_BUILD
        COMMAND ${Python3_EXECUTABLE} ${ICU_EXTRACT_SCRIPT}
                "${ICU_DATA_LIB}" "${ICU_DAT_OUTPUT}"
        COMMENT "Extracting icudt74l.dat next to ts-aot.exe"
    )
endif()
