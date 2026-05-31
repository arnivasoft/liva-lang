#include "liva/Sema/Sema.h"

namespace liva {

Sema::Sema(DiagnosticsEngine &diag, ModuleLoader *loader)
    : diag_(diag), typeChecker_(diag, loader), ownershipChecker_(diag),
      lifetimeAnalysis_(diag) {}

bool Sema::analyze(TranslationUnit &tu) {
    // Phase 1: Type checking (includes name resolution)
    typeChecker_.check(tu);
    if (diag_.hasErrors())
        return false;

    // Phase 2: Ownership checking — classes are reference types (Copy).
    {
        auto names = typeChecker_.getAllClassNames();
        ownershipChecker_.setClassNames({names.begin(), names.end()});
    }
    ownershipChecker_.check(tu);
    if (diag_.hasErrors())
        return false;

    // Phase 3: Lifetime analysis (scope-based borrow checking)
    for (auto &decl : tu.getDeclarations()) {
        if (decl->getKind() == ASTNode::NodeKind::FuncDecl) {
            lifetimeAnalysis_.analyzeFunction(static_cast<FuncDecl *>(decl.get()));
        }
    }
    return !diag_.hasErrors();
}

bool Sema::typeCheck(TranslationUnit &tu) {
    typeChecker_.check(tu);
    return !diag_.hasErrors();
}

bool Sema::ownershipCheck(TranslationUnit &tu) {
    ownershipChecker_.check(tu);
    return !diag_.hasErrors();
}

} // namespace liva
