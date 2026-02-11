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

/// Performs scope-based lifetime analysis.
/// Detects when a reference outlives the value it borrows.
class LifetimeAnalysis {
public:
    LifetimeAnalysis(DiagnosticsEngine &diag);

    /// Analyze lifetimes in a function
    void analyzeFunction(FuncDecl *func);

    bool hasErrors() const { return diag_.hasErrors(); }

private:
    DiagnosticsEngine &diag_;

    /// Variable tracking info
    struct VarInfo {
        int scopeDepth;
        SourceLocation declLoc;
        std::string refTarget; // non-empty if this var is a reference to another var
    };

    int currentDepth_ = 0;
    std::unordered_map<std::string, VarInfo> variables_;

    void visitNode(ASTNode *node);
    void visitBlockStmt(BlockStmt *node);
    void visitVarDecl(VarDecl *node);
    void visitAssignExpr(AssignExpr *node);
    void visitIfStmt(IfStmt *node);
    void visitWhileStmt(WhileStmt *node);
    void visitForStmt(ForStmt *node);

    /// Check for outlives violations when scope exits
    void checkScopeExit(int exitingDepth);

    /// Extract ref target name from a RefExpr
    std::string getRefTarget(const Expr *expr) const;
};

} // namespace liva
