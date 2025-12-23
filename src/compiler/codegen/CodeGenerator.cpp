#include "CodeGenerator.h"

#include <llvm/MC/TargetRegistry.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/TargetParser/Host.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Transforms/IPO/ModuleInliner.h>
#include <llvm/Transforms/Scalar/SROA.h>
#include <llvm/Transforms/Scalar/GVN.h>
#include <llvm/Transforms/IPO/GlobalDCE.h>
#include <llvm/Bitcode/BitcodeWriter.h>

#include <fmt/core.h>
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE
#include <spdlog/spdlog.h>

namespace ts {

CodeGenerator::CodeGenerator(llvm::Module* module) : module(module) {}

bool CodeGenerator::setupTargetMachine(const std::string& optLevel) {
    auto targetTriple = llvm::sys::getDefaultTargetTriple();
    module->setTargetTriple(targetTriple);

    std::string error;
    auto target = llvm::TargetRegistry::lookupTarget(targetTriple, error);

    if (!target) {
        SPDLOG_ERROR("Could not find target for triple {}: {}", targetTriple, error);
        return false;
    }

    auto cpu = "generic";
    auto features = "";

    llvm::TargetOptions opt;
    auto rm = llvm::Reloc::Model::PIC_;
    
    llvm::CodeGenOptLevel optLvl = llvm::CodeGenOptLevel::None;
    if (optLevel == "1") optLvl = llvm::CodeGenOptLevel::Less;
    else if (optLevel == "2") optLvl = llvm::CodeGenOptLevel::Default;
    else if (optLevel == "3") optLvl = llvm::CodeGenOptLevel::Aggressive;

    targetMachine = std::unique_ptr<llvm::TargetMachine>(
        target->createTargetMachine(targetTriple, cpu, features, opt, rm, std::nullopt, optLvl));

    if (!targetMachine) {
        SPDLOG_ERROR("Could not create target machine");
        return false;
    }

    module->setDataLayout(targetMachine->createDataLayout());
    return true;
}

void CodeGenerator::runOptimizations(const std::string& optLevel) {
    llvm::LoopAnalysisManager LAM;
    llvm::FunctionAnalysisManager FAM;
    llvm::CGSCCAnalysisManager CGAM;
    llvm::ModuleAnalysisManager MAM;

    llvm::PassBuilder PB(targetMachine.get());

    PB.registerModuleAnalyses(MAM);
    PB.registerCGSCCAnalyses(CGAM);
    PB.registerFunctionAnalyses(FAM);
    PB.registerLoopAnalyses(LAM);
    PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);

    llvm::OptimizationLevel level;
    if (optLevel == "0") level = llvm::OptimizationLevel::O0;
    else if (optLevel == "1") level = llvm::OptimizationLevel::O1;
    else if (optLevel == "2") level = llvm::OptimizationLevel::O2;
    else if (optLevel == "3") level = llvm::OptimizationLevel::O3;
    else if (optLevel == "s") level = llvm::OptimizationLevel::Os;
    else if (optLevel == "z") level = llvm::OptimizationLevel::Oz;
    else level = llvm::OptimizationLevel::O0;

    if (level != llvm::OptimizationLevel::O0) {
        SPDLOG_INFO("Running IR optimizations (Level O{})", optLevel);
        llvm::ModulePassManager MPM;
        
        MPM.addPass(llvm::ModuleInlinerWrapperPass());
        
        llvm::FunctionPassManager FPM;
        FPM.addPass(llvm::SROAPass(llvm::SROAOptions::ModifyCFG));
        FPM.addPass(llvm::GVNPass());
        MPM.addPass(llvm::createModuleToFunctionPassAdaptor(std::move(FPM)));
        
        MPM.addPass(PB.buildPerModuleDefaultPipeline(level));
        MPM.addPass(llvm::GlobalDCEPass());
        
        MPM.run(*module, MAM);
    }
}

bool CodeGenerator::emitObjectFile(const std::string& filename, const std::string& optLevel) {
    if (!setupTargetMachine(optLevel)) {
        return false;
    }

    runOptimizations(optLevel);

    std::error_code ec;
    llvm::raw_fd_ostream dest(filename, ec, llvm::sys::fs::OF_None);

    if (ec) {
        SPDLOG_ERROR("Could not open file {}: {}", filename, ec.message());
        return false;
    }

    llvm::legacy::PassManager pass;
    auto fileType = llvm::CodeGenFileType::ObjectFile;

    if (targetMachine->addPassesToEmitFile(pass, dest, nullptr, fileType)) {
        SPDLOG_ERROR("TargetMachine can't emit a file of this type");
        return false;
    }

    pass.run(*module);
    dest.flush();

    return true;
}

bool CodeGenerator::emitBitcode(const std::string& filename) {
    std::error_code ec;
    llvm::raw_fd_ostream dest(filename, ec, llvm::sys::fs::OF_None);
    if (ec) {
        SPDLOG_ERROR("Could not open file {}: {}", filename, ec.message());
        return false;
    }
    llvm::WriteBitcodeToFile(*module, dest);
    return true;
}

bool CodeGenerator::emitAssembly(const std::string& filename) {
    std::error_code ec;
    llvm::raw_fd_ostream dest(filename, ec, llvm::sys::fs::OF_None);
    if (ec) {
        SPDLOG_ERROR("Could not open file {}: {}", filename, ec.message());
        return false;
    }
    module->print(dest, nullptr);
    return true;
}

} // namespace ts
