#pragma once

#include "liva/AST/Decl.h"
#include "liva/Common/Diagnostics.h"
#include "liva/Sema/LifetimeAnalysis.h"
#include "liva/Sema/OwnershipChecker.h"
#include "liva/Sema/TypeChecker.h"

namespace liva {

class ModuleLoader;

/// Main semantic analysis driver
class Sema {
public:
    Sema(DiagnosticsEngine &diag, ModuleLoader *loader = nullptr);

    /// Run all semantic analysis passes on the translation unit
    bool analyze(TranslationUnit &tu);

    /// Run only type checking
    bool typeCheck(TranslationUnit &tu);

    /// Run only ownership checking
    bool ownershipCheck(TranslationUnit &tu);

    bool hasErrors() const { return diag_.hasErrors(); }

    TypeChecker &getTypeChecker() { return typeChecker_; }

private:
    DiagnosticsEngine &diag_;
    TypeChecker typeChecker_;
    OwnershipChecker ownershipChecker_;
    LifetimeAnalysis lifetimeAnalysis_;
};

} // namespace liva
