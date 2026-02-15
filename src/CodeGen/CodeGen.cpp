#include "liva/CodeGen/CodeGen.h"

#ifdef LIVA_HAS_LLVM
#include <llvm/Bitcode/BitcodeWriter.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/PGOOptions.h>
#include <llvm/Support/VirtualFileSystem.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/TargetParser/Host.h>
#include <llvm/TargetParser/Triple.h>
#endif

#include <cstdlib>
#include <fstream>
#include <iostream>

namespace liva {

static std::string findClang() {
    // 1. Environment variable override
    if (const char *env = std::getenv("LIVA_CLANG_PATH"))
        return env;
    // 2. Platform-specific common paths
#ifdef _WIN32
    const char *paths[] = {
        "C:\\LLVM\\bin\\clang.exe",
        "C:\\Program Files\\LLVM\\bin\\clang.exe",
        nullptr
    };
#elif defined(__APPLE__)
    const char *paths[] = {
        "/opt/homebrew/opt/llvm/bin/clang",
        "/usr/local/opt/llvm/bin/clang",
        "/usr/bin/clang",
        nullptr
    };
#else
    const char *paths[] = {
        "/usr/bin/clang",
        "/usr/local/bin/clang",
        "/usr/lib/llvm-21/bin/clang",
        "/usr/lib/llvm-18/bin/clang",
        "/usr/lib/llvm-17/bin/clang",
        nullptr
    };
#endif
    for (auto *p = paths; *p; ++p) {
        std::ifstream f(*p);
        if (f.is_open()) return *p;
    }
    return "clang"; // fallback: rely on PATH
}

CodeGen::CodeGen(DiagnosticsEngine &diag, const TargetInfo &target)
    : diag_(diag), target_(target) {
#ifdef LIVA_HAS_LLVM
    llvm::InitializeAllTargets();
    llvm::InitializeAllTargetMCs();
    llvm::InitializeAllAsmParsers();
    llvm::InitializeAllAsmPrinters();
#endif
}

#ifdef LIVA_HAS_LLVM

bool CodeGen::emitObjectFile(llvm::Module &module, const std::string &outputPath) {
    std::string error;
    auto *target = llvm::TargetRegistry::lookupTarget(target_.triple, error);
    if (!target) {
        std::cerr << "error: failed to lookup target '" << target_.triple << "': " << error << "\n";
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
    if (ec) {
        std::cerr << "error: cannot open output file '" << outputPath << "': " << ec.message() << "\n";
        return false;
    }

    llvm::legacy::PassManager pass;
    if (targetMachine->addPassesToEmitFile(pass, dest, nullptr,
                                            llvm::CodeGenFileType::ObjectFile)) {
        std::cerr << "error: target does not support object file emission\n";
        return false;
    }

    pass.run(module);
    dest.flush();
    return true;
}

bool CodeGen::emitAssembly(llvm::Module &module, const std::string &outputPath) {
    std::string error;
    auto *target = llvm::TargetRegistry::lookupTarget(target_.triple, error);
    if (!target) {
        std::cerr << "error: failed to lookup target '" << target_.triple << "': " << error << "\n";
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
    if (ec) {
        std::cerr << "error: cannot open output file '" << outputPath << "': " << ec.message() << "\n";
        return false;
    }

    llvm::legacy::PassManager pass;
    if (targetMachine->addPassesToEmitFile(pass, dest, nullptr,
                                            llvm::CodeGenFileType::AssemblyFile)) {
        std::cerr << "error: target does not support assembly file emission\n";
        return false;
    }

    pass.run(module);
    dest.flush();
    return true;
}

bool CodeGen::emitBitcode(llvm::Module &module, const std::string &outputPath) {
    // Set target layout/triple before writing bitcode
    std::string error;
    auto *target = llvm::TargetRegistry::lookupTarget(target_.triple, error);
    if (target) {
        llvm::TargetOptions opt;
        auto targetMachine = target->createTargetMachine(
            llvm::Triple(target_.triple), target_.cpu, target_.features, opt,
            llvm::Reloc::PIC_);
        module.setDataLayout(targetMachine->createDataLayout());
        module.setTargetTriple(llvm::Triple(target_.triple));
    }

    std::error_code ec;
    llvm::raw_fd_ostream dest(outputPath, ec, llvm::sys::fs::OF_None);
    if (ec) {
        std::cerr << "error: cannot open output file '" << outputPath << "': "
                  << ec.message() << "\n";
        return false;
    }

    llvm::WriteBitcodeToFile(module, dest);
    dest.flush();
    return true;
}

void CodeGen::optimize(llvm::Module &module, int optLevel,
                       const std::string &ltoMode,
                       const std::string &pgoMode,
                       const std::string &pgoProfile) {
    llvm::LoopAnalysisManager lam;
    llvm::FunctionAnalysisManager fam;
    llvm::CGSCCAnalysisManager cgam;
    llvm::ModuleAnalysisManager mam;

    // Build PGO options if requested
    std::optional<llvm::PGOOptions> pgoOpts;
    if (pgoMode == "generate") {
        pgoOpts = llvm::PGOOptions("", "", "", "",
                                    nullptr,
                                    llvm::PGOOptions::IRInstr);
    } else if (pgoMode == "use" && !pgoProfile.empty()) {
        pgoOpts = llvm::PGOOptions(pgoProfile, "", "", "",
                                    nullptr,
                                    llvm::PGOOptions::IRUse);
    }

    llvm::PassBuilder pb(nullptr, llvm::PipelineTuningOptions(), pgoOpts);
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

    llvm::ModulePassManager mpm;
    if (ltoMode == "thin") {
        mpm = pb.buildThinLTOPreLinkDefaultPipeline(level);
    } else if (ltoMode == "full") {
        mpm = pb.buildLTODefaultPipeline(level, nullptr);
    } else {
        mpm = pb.buildPerModuleDefaultPipeline(level);
    }
    mpm.run(module, mam);
}

#endif // LIVA_HAS_LLVM

bool CodeGen::link(const std::vector<std::string> &objectFiles,
                   const std::string &outputPath,
                   const std::vector<std::string> &extraFlags,
                   const std::string &ltoMode,
                   const std::string &pgoMode) {
    std::string clang = findClang();
    std::string cmd = "\"" + clang + "\" -o \"" + outputPath + "\"";

    // Cross-compilation target
    if (target_.isCrossCompiling())
        cmd += " --target=" + target_.triple;

    // WASM-specific flags
    if (target_.isWasm()) {
        cmd += " -nostdlib -Wl,--no-entry -Wl,--export-all";
    }

    // LTO flags
    if (ltoMode == "thin")
        cmd += " -flto=thin";
    else if (ltoMode == "full")
        cmd += " -flto";

    // PGO flags — generate mode needs profiling runtime linked
    if (pgoMode == "generate")
        cmd += " -fprofile-generate";

    for (auto &obj : objectFiles)
        cmd += " \"" + obj + "\"";
    for (auto &flag : extraFlags)
        cmd += " " + flag;

    // Platform-specific flags (not for WASM)
    if (!target_.isWasm()) {
#ifdef _WIN32
        cmd += " -lmsvcrt -Wl,/NODEFAULTLIB:libcmt";
#endif
    }

#ifdef _WIN32
    // Wrap entire command for cmd.exe /c quoting rules
    cmd = "\"" + cmd + "\"";
#endif
    return std::system(cmd.c_str()) == 0;
}

} // namespace liva
