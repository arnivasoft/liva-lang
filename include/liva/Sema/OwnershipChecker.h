#pragma once

#include "liva/AST/ASTVisitor.h"
#include "liva/AST/Decl.h"
#include "liva/Common/Diagnostics.h"
#include <deque>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace liva {

/// Tracks the ownership state of a value
enum class OwnershipState : uint8_t {
    Owned,            // Value is owned and valid
    Moved,            // Value has been moved
    BorrowedImmutable,// Value has an active immutable borrow
    BorrowedMutable,  // Value has an active mutable borrow
    Dropped,          // Value has been dropped
};

/// Information about a tracked variable
struct OwnershipInfo {
    std::string name;
    OwnershipState state = OwnershipState::Owned;
    bool isMutable = false;
    bool isCopyType = false;  // Primitive types are Copy
    SourceLocation declLocation;
    SourceLocation lastMoveLocation;
    SourceLocation lastBorrowLocation;
    int borrowCount = 0;
    bool hasMutableBorrow = false;
};

/// Performs ownership and borrow checking on the AST
class OwnershipChecker : public ASTVisitor<OwnershipChecker> {
public:
    OwnershipChecker(DiagnosticsEngine &diag);

    /// Check ownership for a translation unit
    void check(TranslationUnit &tu);

    /// Provide the set of class type names. Classes are reference types, so
    /// they are treated as Copy (passing a class value does not move it).
    void setClassNames(std::unordered_set<std::string> names) {
        classNames_ = std::move(names);
    }

    void visitFuncDecl(FuncDecl *node);
    void visitClassDecl(ClassDecl *node);
    void visitTestDecl(TestDecl *node);
    void visitVarDecl(VarDecl *node);
    void visitBlockStmt(BlockStmt *node);
    void visitReturnStmt(ReturnStmt *node);
    void visitExprStmt(ExprStmt *node);
    void visitIfStmt(IfStmt *node);
    void visitWhileStmt(WhileStmt *node);
    void visitForStmt(ForStmt *node);

    void visitIdentifierExpr(IdentifierExpr *node);
    void visitAssignExpr(AssignExpr *node);
    void visitCallExpr(CallExpr *node);
    void visitBinaryExpr(BinaryExpr *node);
    void visitRefExpr(RefExpr *node);

    bool hasErrors() const { return diag_.hasErrors(); }

private:
    /// Track a new variable
    void trackVariable(const std::string &name, bool isMutable, bool isCopyType,
                       SourceLocation loc);

    /// Mark a variable as moved
    void markMoved(const std::string &name, SourceLocation loc);

    /// Check if a variable can be used (not moved/dropped)
    bool checkUse(const std::string &name, SourceLocation loc);

    /// Check if a variable can be mutated
    bool checkMutation(const std::string &name, SourceLocation loc);

    /// Add a borrow
    bool addBorrow(const std::string &name, bool isMutable, SourceLocation loc);

    /// Release borrows at scope exit
    void releaseBorrows(const std::string &name);

    /// Check if a type is a Copy type (primitives)
    bool isCopyType(const TypeRepr *type) const;

    /// Drop all owned values at scope exit
    void dropScopeVariables();

    /// Push/pop scope for ownership tracking
    void pushOwnershipScope();
    void popOwnershipScope();

    /// Get ownership info for a variable
    OwnershipInfo *getInfo(const std::string &name);

    DiagnosticsEngine &diag_;

    /// Stack of scopes, each containing variable ownership info
    /// Using deque so that emplace_back() never invalidates pointers
    /// stored in allVariables_ (vector reallocation would dangle them).
    std::deque<std::unordered_map<std::string, OwnershipInfo>> scopeStack_;

    /// All tracked variables (flat lookup)
    std::unordered_map<std::string, OwnershipInfo *> allVariables_;

    /// Class type names — treated as Copy (reference types, not moved).
    std::unordered_set<std::string> classNames_;
};

} // namespace liva
