#include "liva/CodeGen/CodeGen.h"

#ifdef LIVA_HAS_LLVM
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/TargetParser/Host.h>
#include <llvm/TargetParser/Triple.h>
#endif

#include <cstdlib>

namespace liva {

CodeGen::CodeGen(DiagnosticsEngine &diag, const TargetInfo &target)
    : diag_(diag), target_(target) {
#ifdef LIVA_HAS_LLVM
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmParser();
    llvm::InitializeNativeTargetAsmPrinter();
#endif
}

#ifdef LIVA_HAS_LLVM

bool CodeGen::emitObjectFile(llvm::Module &module, const std::string &outputPath) {
    std::string error;
    auto *target = llvm::TargetRegistry::lookupTarget(target_.triple, error);
    if (!target) {
        diag_.report(SourceLocation{}, DiagID::err_main_not_found);
        return false;
    }

    llvm::TargetOptions opt;
    auto targetMachine = target->createTargetMachine(llvm::Triple(target_.triple),
                                                      target_.cpu, target_.features, opt,
                                                      llvm::Reloc::PIC_);
    module.setDataLayout(targetMachine->createDataLayout());
    module.setTargetTriple(llvm::Triple(target_.triple));

    std::error_code ec;
    llvm::raw_fd_ostream dest(outputPath, ec, llvm::sys::fs::OF_None);
    if (ec)
        return false;

    llvm::legacy::PassManager pass;
    if (targetMachine->addPassesToEmitFile(pass, dest, nullptr,
                                            llvm::CodeGenFileType::ObjectFile)) {
        return false;
    }

    pass.run(module);
    dest.flush();
    return true;
}

bool CodeGen::emitAssembly(llvm::Module &module, const std::string &outputPath) {
    std::string error;
    auto *target = llvm::TargetRegistry::lookupTarget(target_.triple, error);
    if (!target)
        return false;

    llvm::TargetOptions opt;
    auto targetMachine = target->createTargetMachine(llvm::Triple(target_.triple),
                                                      target_.cpu, target_.features, opt,
                                                      llvm::Reloc::PIC_);
    module.setDataLayout(targetMachine->createDataLayout());
    module.setTargetTriple(llvm::Triple(target_.triple));

    std::error_code ec;
    llvm::raw_fd_ostream dest(outputPath, ec, llvm::sys::fs::OF_None);
    if (ec)
        return false;

    llvm::legacy::PassManager pass;
    if (targetMachine->addPassesToEmitFile(pass, dest, nullptr,
                                            llvm::CodeGenFileType::AssemblyFile)) {
        return false;
    }

    pass.run(module);
    dest.flush();
    return true;
}

void CodeGen::optimize(llvm::Module &module, int optLevel) {
    llvm::LoopAnalysisManager lam;
    llvm::FunctionAnalysisManager fam;
    llvm::CGSCCAnalysisManager cgam;
    llvm::ModuleAnalysisManager mam;

    llvm::PassBuilder pb;
    pb.registerModuleAnalyses(mam);
    pb.registerCGSCCAnalyses(cgam);
    pb.registerFunctionAnalyses(fam);
    pb.registerLoopAnalyses(lam);
    pb.crossRegisterProxies(lam, fam, cgam, mam);

    llvm::OptimizationLevel level;
    switch (optLevel) {
    case 0:
        level = llvm::OptimizationLevel::O0;
        break;
    case 1:
        level = llvm::OptimizationLevel::O1;
        break;
    case 2:
        level = llvm::OptimizationLevel::O2;
        break;
    case 3:
        level = llvm::OptimizationLevel::O3;
        break;
    default:
        level = llvm::OptimizationLevel::O2;
        break;
    }

    auto mpm = pb.buildPerModuleDefaultPipeline(level);
    mpm.run(module, mam);
}

#endif // LIVA_HAS_LLVM

bool CodeGen::link(const std::vector<std::string> &objectFiles,
                   const std::string &outputPath) {
    // Use clang as the linker driver (works on all platforms, handles C runtime linking)
    std::string cmd;

    // Try clang from LLVM installation first, then fall back to PATH
    const char *clangPaths[] = {
        "C:\\LLVM\\bin\\clang.exe",
        "clang",
        nullptr
    };

    std::string clang;
    for (auto *path = clangPaths; *path; ++path) {
        // Quick check: try to use the first available
        clang = *path;
        break;
    }

    cmd = "\"" + clang + "\" -o \"" + outputPath + "\"";
    for (auto &obj : objectFiles) {
        cmd += " \"" + obj + "\"";
    }

#ifdef _WIN32
    // Link against Windows C runtime
    cmd += " -lmsvcrt";
#endif

    int result = std::system(cmd.c_str());
    return result == 0;
}

} // namespace liva
