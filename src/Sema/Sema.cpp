#include "liva/Sema/Sema.h"

namespace liva {

Sema::Sema(DiagnosticsEngine &diag)
    : diag_(diag), typeChecker_(diag), ownershipChecker_(diag) {}

bool Sema::analyze(TranslationUnit &tu) {
    // Phase 1: Type checking (includes name resolution)
    typeChecker_.check(tu);
    if (diag_.hasErrors())
        return false;

    // Phase 2: Ownership checking
    ownershipChecker_.check(tu);
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
