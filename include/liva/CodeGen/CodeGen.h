#pragma once

#include "liva/CodeGen/TargetInfo.h"
#include "liva/Common/Diagnostics.h"
#include <string>
#include <vector>

#ifdef LIVA_HAS_LLVM
#include <llvm/IR/Module.h>
#endif

namespace liva {

/// Generates native object code from LLVM IR
class CodeGen {
public:
    CodeGen(DiagnosticsEngine &diag, const TargetInfo &target = TargetInfo::getHostTarget());

#ifdef LIVA_HAS_LLVM
    /// Compile LLVM module to object file
    bool emitObjectFile(llvm::Module &module, const std::string &outputPath);

    /// Compile LLVM module to assembly
    bool emitAssembly(llvm::Module &module, const std::string &outputPath);

    /// Run optimization passes
    void optimize(llvm::Module &module, int optLevel = 2);
#endif

    /// Link object files into executable
    bool link(const std::vector<std::string> &objectFiles,
              const std::string &outputPath,
              const std::vector<std::string> &extraFlags = {});

private:
    DiagnosticsEngine &diag_;
    TargetInfo target_;
};

} // namespace liva
