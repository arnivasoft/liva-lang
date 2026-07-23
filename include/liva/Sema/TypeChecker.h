#pragma once

#include "liva/AST/ASTVisitor.h"
#include "liva/AST/Decl.h"
#include "liva/Common/Diagnostics.h"
#include "liva/Macro/MacroExpander.h"
#include "liva/Sema/Scope.h"
#include <memory>
#include <optional>
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
    void visitClassDecl(ClassDecl *node);
    void visitTestDecl(TestDecl *node);

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

    // --- visitCallExpr fazları (TypeCheckerCall.cpp) — sıralı, void, yan-etkiyle çalışır ---
    void propagateClosureParamTypes(CallExpr *node);
    void propagateDynArrayClosureTypes(CallExpr *node);
    void checkCallArgCount(CallExpr *node);
    void resolveCallReturnType(CallExpr *node);
    void resolveMapSetMethodCall(CallExpr *node);

    void visitMemberExpr(MemberExpr *node);
    void visitIndexExpr(IndexExpr *node);
    void visitAssignExpr(AssignExpr *node);
    void visitStructLiteralExpr(StructLiteralExpr *node);
    void visitArrayLiteralExpr(ArrayLiteralExpr *node);
    void visitTupleLiteralExpr(TupleLiteralExpr *node);
    void visitCastExpr(CastExpr *node);
    void visitIsExpr(IsExpr *node);
    void visitRefExpr(RefExpr *node);
    void visitGroupExpr(GroupExpr *node);
    void visitMatchExpr(MatchExpr *node);
    void visitRangeExpr(RangeExpr *node);
    void visitUnwrapExpr(UnwrapExpr *node);
    void visitClosureExpr(ClosureExpr *node);
    void visitTryExpr(TryExpr *node);
    void visitTernaryExpr(TernaryExpr *node);
    void visitAwaitExpr(AwaitExpr *node);
    void visitYieldExpr(YieldExpr *node);
    void visitComptimeExpr(ComptimeExpr *node);
    void visitMacroDecl(MacroDecl *node);
    void visitMacroInvokeExpr(MacroInvokeExpr *node);

    /// Apply Rust-style lifetime elision rules to functions without explicit lifetimes
    void elideFunctionLifetimes(FuncDecl *node);

    bool hasErrors() const { return diag_.hasErrors(); }

    /// Access the macro expander
    MacroExpander &getMacroExpander() { return macroExpander_; }

    /// Names of all classes known after the first pass — locally declared plus
    /// those brought in via `import` (collected from the type scope). Used by
    /// the ownership checker to treat classes as reference (Copy) types.
    std::vector<std::string> getAllClassNames() const {
        std::vector<std::string> out;
        for (auto &kv : classDecls_)
            out.push_back(kv.first);
        scopes_.collectNames(Symbol::Kind::ClassType, out);
        return out;
    }

private:
    /// Register built-in types and functions
    void registerBuiltins();

    /// Compute edit distance between two strings (Levenshtein)
    static size_t editDistance(const std::string &a, const std::string &b);

    /// Find closest match from candidates; returns empty if no good match
    static std::string findClosestMatch(const std::string &name,
                                         const std::vector<std::string> &candidates,
                                         size_t maxDistance = 0);

    /// Suggest a similar name after reporting an error
    void suggestSimilar(SourceLocation loc, const std::string &name,
                        const std::vector<std::string> &candidates);

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

    /// Check if a code path always returns a value
    bool alwaysReturns(const ASTNode *node) const;

    /// Extract leaf bindings from a pattern AST node (supports nested patterns)
    void extractPatternBindings(const Pattern *pattern);

    /// Declare a binding for one sub-pattern slot inside an EnumCasePattern's
    /// parens: a nested EnumCasePattern recurses via extractPatternBindings;
    /// anything else (Identifier/Wildcard/IntLiteral) declares a binding
    /// named after its spelling — unconditionally, matching the legacy
    /// string splitter's per-slot rule (it only special-cased '.', not
    /// "_"/int-literal-ness, inside parens).
    void declarePatternSubBinding(const Pattern *sub);

    /// Async function tracking
    bool currentIsAsync_ = false;
    std::set<std::string> asyncFuncNames_;

    /// Generator function tracking
    bool currentIsGenerator_ = false;

    /// GATs: param counts per associated type ("Proto::Item" → {lifetimeCount, typeCount})
    std::unordered_map<std::string, std::pair<int, int>> protocolGATParamCounts_;

    /// Iterator item types: typeName → item TypeRepr*
    std::unordered_map<std::string, const TypeRepr *> iteratorItemTypes_;

    /// Async iterator item types: typeName → item TypeRepr*
    std::unordered_map<std::string, const TypeRepr *> asyncIteratorItemTypes_;

    /// File-typed variable tracking
    std::set<std::string> fileVariables_;

    /// Owned types for variadic param DynArray wrappers
    std::vector<std::unique_ptr<TypeRepr>> variadicArrayTypes_;

    /// Compile-time constant evaluation
    struct ConstValue {
        enum Kind { Integer, Float, Bool, String };
        Kind kind;
        int64_t intVal = 0;
        double floatVal = 0.0;
        bool boolVal = false;
        std::string strVal;
    };
    std::optional<ConstValue> evaluateConstExpr(const Expr *expr);
    std::optional<ConstValue> evaluateComptimeBlock(const BlockStmt *block);
    std::unordered_map<std::string, ConstValue> constValues_;
    std::unordered_map<std::string, ConstValue> comptimeLocals_;

    /// Macro expander
    MacroExpander macroExpander_;
    void expandMacros(TranslationUnit &tu);
    void expandMacrosInExpr(std::unique_ptr<Expr> &expr);
    void expandMacrosInStmt(ASTNode *node);

    /// Object safety check: returns false if protocol has generic methods
    bool isObjectSafe(const std::string &protocolName, std::string &unsafeMethodName);

    /// "TypeName::ProtocolName::AssocType" -> concrete type name
    std::unordered_map<std::string, std::string> implAssociatedTypeResolutions_;

    /// Method return-type tracking for user-defined types: "TypeName::methodName"
    /// → return type (raw pointer, owned by the FuncDecl in the AST)
    std::unordered_map<std::string, const TypeRepr *> typeMethodReturnTypes_;

    /// Class declaration tracking
    std::unordered_map<std::string, const ClassDecl *> classDecls_;
    std::unordered_map<std::string, std::string> classParent_;
    std::unordered_map<std::string, std::set<std::string>> classPrivateMembers_;
    std::string currentClassName_;
    std::string currentImplTypeName_;

    /// Unused variable tracking (per function)
    std::set<std::string> usedSymbols_;
    std::vector<std::pair<std::string, SourceLocation>> currentFuncVars_;
    std::set<std::string> forLoopVars_;

    /// Type-param bounds for the current generic function (paramName → [boundProtos])
    std::unordered_map<std::string, std::vector<std::string>> currentTypeParamBounds_;

    /// Helper: returns true if a concrete TypeRepr implicitly conforms to a built-in
    /// iterable (DynArray / Array), used to accept [T] arguments in `where I: Iterator`.
    static bool isDynArrayType(const TypeRepr *type);
};

} // namespace liva
