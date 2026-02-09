#pragma once

#include "liva/AST/Decl.h"
#include "liva/Common/Diagnostics.h"
#include <string>
#include <unordered_map>
#include <vector>

namespace liva {

/// Lifetime identifier (e.g., @a)
struct Lifetime {
    std::string name;
    bool isStatic = false;

    static Lifetime staticLifetime() { return {"static", true}; }
    static Lifetime anonymous() { return {"_", false}; }
};

/// Performs lifetime analysis (placeholder for future NLL implementation)
class LifetimeAnalysis {
public:
    LifetimeAnalysis(DiagnosticsEngine &diag);

    /// Analyze lifetimes in a function
    void analyzeFunction(FuncDecl *func);

    bool hasErrors() const { return diag_.hasErrors(); }

private:
    DiagnosticsEngine &diag_;
};

} // namespace liva
