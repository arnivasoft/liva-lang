#pragma once

#include "liva/AST/ASTVisitor.h"
#include "liva/AST/Decl.h"
#include "liva/Common/Diagnostics.h"
#include "liva/Sema/Scope.h"
#include <memory>
#include <string>
#include <set>
#include <unordered_map>
#include <vector>

namespace liva {

class ModuleLoader;

/// Performs type checking on the AST
class TypeChecker : public ASTVisitor<TypeChecker> {
public:
    TypeChecker(DiagnosticsEngine &diag, ModuleLoader *loader = nullptr);

    /// Type-check an entire translation unit
    void check(TranslationUnit &tu);

    // Visit methods
    void visitFuncDecl(FuncDecl *node);
    void visitVarDecl(VarDecl *node);
    void visitStructDecl(StructDecl *node);
    void visitEnumDecl(EnumDecl *node);
    void visitImplDecl(ImplDecl *node);
    void visitProtocolDecl(ProtocolDecl *node);
    void visitTypeAliasDecl(TypeAliasDecl *node);

    void visitExprStmt(ExprStmt *node);
    void visitReturnStmt(ReturnStmt *node);
    void visitIfStmt(IfStmt *node);
    void visitIfLetStmt(IfLetStmt *node);
    void visitWhileLetStmt(WhileLetStmt *node);
    void visitWhileStmt(WhileStmt *node);
    void visitForStmt(ForStmt *node);
    void visitBlockStmt(BlockStmt *node);
    void visitBreakStmt(BreakStmt *node);
    void visitContinueStmt(ContinueStmt *node);

    void visitIntegerLiteralExpr(IntegerLiteralExpr *node);
    void visitFloatLiteralExpr(FloatLiteralExpr *node);
    void visitBoolLiteralExpr(BoolLiteralExpr *node);
    void visitStringLiteralExpr(StringLiteralExpr *node);
    void visitNilLiteralExpr(NilLiteralExpr *node);
    void visitIdentifierExpr(IdentifierExpr *node);
    void visitBinaryExpr(BinaryExpr *node);
    void visitUnaryExpr(UnaryExpr *node);
    void visitCallExpr(CallExpr *node);
    void visitMemberExpr(MemberExpr *node);
    void visitIndexExpr(IndexExpr *node);
    void visitAssignExpr(AssignExpr *node);
    void visitStructLiteralExpr(StructLiteralExpr *node);
    void visitArrayLiteralExpr(ArrayLiteralExpr *node);
    void visitTupleLiteralExpr(TupleLiteralExpr *node);
    void visitCastExpr(CastExpr *node);
    void visitRefExpr(RefExpr *node);
    void visitGroupExpr(GroupExpr *node);
    void visitMatchExpr(MatchExpr *node);
    void visitRangeExpr(RangeExpr *node);
    void visitUnwrapExpr(UnwrapExpr *node);
    void visitClosureExpr(ClosureExpr *node);
    void visitTryExpr(TryExpr *node);
    void visitTernaryExpr(TernaryExpr *node);

    bool hasErrors() const { return diag_.hasErrors(); }

private:
    /// Register built-in types and functions
    void registerBuiltins();

    /// Check if two types are compatible
    bool typesCompatible(const TypeRepr *expected, const TypeRepr *actual) const;

    /// Get a string representation of a type for diagnostics
    std::string typeToString(const TypeRepr *type) const;

    /// Resolve the type of an expression
    const TypeRepr *resolveExprType(Expr *expr);

    DiagnosticsEngine &diag_;
    ModuleLoader *moduleLoader_ = nullptr;
    ScopeStack scopes_;

    // Currently being checked function's return type
    const TypeRepr *currentReturnType_ = nullptr;
    int loopDepth_ = 0;

    /// Protocol conformance tracking: protocolName → [conforming type names]
    std::unordered_map<std::string, std::vector<std::string>> protocolConformances_;

    /// Protocol method info: protocolName → [method names in order]
    std::unordered_map<std::string, std::vector<std::string>> protocolMethods_;

    /// Protocol associated types: protocolName → [associated type names]
    std::unordered_map<std::string, std::vector<std::string>> protocolAssociatedTypes_;

    /// Type alias tracking: aliasName → targetTypeRepr*
    std::unordered_map<std::string, const TypeRepr *> typeAliases_;

    /// Resolve a type through aliases (returns target if alias, else original)
    const TypeRepr *resolveAlias(const TypeRepr *type) const;

    /// File-typed variable tracking
    std::set<std::string> fileVariables_;
};

} // namespace liva
