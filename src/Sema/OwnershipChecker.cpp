#include "liva/Sema/OwnershipChecker.h"

namespace liva {

OwnershipChecker::OwnershipChecker(DiagnosticsEngine &diag) : diag_(diag) {}

void OwnershipChecker::check(TranslationUnit &tu) {
    pushOwnershipScope();

    for (auto &decl : tu.getDeclarations()) {
        visit(decl.get());
    }

    popOwnershipScope();
}

void OwnershipChecker::visitFuncDecl(FuncDecl *node) {
    pushOwnershipScope();

    // Track parameters
    for (auto &param : node->getParams()) {
        bool copyType = param.type ? isCopyType(param.type.get()) : true;
        bool dropType = param.type ? isDropType(param.type.get()) : false;
        trackVariable(param.name, param.isMutRef, copyType, dropType, param.location);
    }

    if (node->getBody()) {
        visitBlockStmt(const_cast<BlockStmt *>(node->getBody()));
    }

    dropScopeVariables();
    popOwnershipScope();
}

void OwnershipChecker::visitTestDecl(TestDecl *node) {
    pushOwnershipScope();
    if (node->getBody())
        visitBlockStmt(const_cast<BlockStmt *>(node->getBody()));
    popOwnershipScope();
}

void OwnershipChecker::visitClassDecl(ClassDecl *node) {
    // Check ownership for each method body
    for (auto &m : node->getMembers()) {
        if (m.method) {
            visitFuncDecl(const_cast<FuncDecl *>(m.method.get()));
        }
    }
}

void OwnershipChecker::visitVarDecl(VarDecl *node) {
    // Visit initializer first
    if (node->hasInit()) {
        visit(const_cast<Expr *>(node->getInit()));
    }

    bool copyType;
    bool dropType = false;
    const TypeRepr *type = node->getType();
    if (type && !type->isInferred()) {
        // Explicit type annotation — use it directly
        copyType = isCopyType(type);
        dropType = isDropType(type);
    } else if (node->hasInit() && node->getInit()->getResolvedType()) {
        // Inferred type — use the init expression's resolved type
        copyType = isCopyType(node->getInit()->getResolvedType());
        dropType = isDropType(node->getInit()->getResolvedType());
    } else {
        // No type info available — default to Copy
        copyType = true;
    }

    // Move semantics: `let b = a` where `a` is a Drop-conforming struct moves
    // `a` (conservative scope — only Drop types; plain structs keep copy
    // behavior unchanged). Use the SOURCE variable's own tracked isDropType
    // flag rather than recomputing from `b`'s type, since that's the value
    // actually being consumed.
    if (node->hasInit() &&
        node->getInit()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
        auto *initIdent = static_cast<IdentifierExpr *>(
            const_cast<Expr *>(node->getInit()));
        auto *srcInfo = getInfo(initIdent->getName());
        if (srcInfo && srcInfo->isDropType) {
            markMoved(initIdent->getName(), node->getStartLoc());
        }
    }

    trackVariable(node->getName(), node->isMutable(), copyType, dropType,
                 node->getStartLoc());
}

void OwnershipChecker::visitBlockStmt(BlockStmt *node) {
    pushOwnershipScope();
    for (auto &stmt : node->getStatements()) {
        visit(stmt.get());
    }
    dropScopeVariables();
    popOwnershipScope();
}

void OwnershipChecker::visitReturnStmt(ReturnStmt *node) {
    if (node->hasValue()) {
        visit(node->getValue());
    }
}

void OwnershipChecker::visitExprStmt(ExprStmt *node) { visit(node->getExpr()); }

void OwnershipChecker::visitIfStmt(IfStmt *node) {
    visit(const_cast<Expr *>(node->getCondition()));
    visit(node->getThenBody());
    if (node->hasElse()) {
        visit(node->getElseBody());
    }
}

void OwnershipChecker::visitWhileStmt(WhileStmt *node) {
    visit(const_cast<Expr *>(node->getCondition()));
    visit(const_cast<ASTNode *>(node->getBody()));
}

void OwnershipChecker::visitForStmt(ForStmt *node) {
    visit(const_cast<Expr *>(node->getIterable()));
    pushOwnershipScope();
    trackVariable(node->getVarName(), false, true, false, node->getStartLoc());
    visit(const_cast<ASTNode *>(node->getBody()));
    dropScopeVariables();
    popOwnershipScope();
}

// Task 2 (Drop/Move Tracking): if-let/while-let ownership transfer. Neither
// statement had an override before (ASTVisitor's default is a no-op), so
// nothing inside them was tracked at all. The binding takes ownership of a
// Drop-conforming payload; the source Optional identifier (if any) is marked
// moved — same conservative scope as `let b = a` (Task 1), extended to
// Optional<Drop-struct> via isCopyType/isDropType's new Optional-recursion.
//
// Decision (#2, repeated if-let): visiting the optional expr FIRST (before
// markMoved) routes an identifier source through the normal
// visitIdentifierExpr -> checkUse dispatch, so a SECOND if-let (or any other
// use) over an already-consumed Optional<Drop-struct> reports
// err_use_after_move — the conservative v1 chosen for this task (no
// re-entrant if-let over the same Drop-payload optional).
void OwnershipChecker::visitIfLetStmt(IfLetStmt *node) {
    visit(node->getOptionalExpr());

    bool payloadIsDrop = false;
    if (node->getOptionalExpr()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
        auto *ident = static_cast<IdentifierExpr *>(node->getOptionalExpr());
        auto *srcInfo = getInfo(ident->getName());
        if (srcInfo && srcInfo->isDropType) {
            payloadIsDrop = true;
            markMoved(ident->getName(), node->getStartLoc());
        }
    }

    pushOwnershipScope();
    trackVariable(node->getBindingName(), /*isMutable=*/false,
                  /*isCopyType=*/!payloadIsDrop, /*isDropType=*/payloadIsDrop,
                  node->getStartLoc());
    visit(node->getThenBody());
    dropScopeVariables();
    popOwnershipScope();

    if (node->hasElse()) {
        visit(node->getElseBody());
    }
}

// Decision (#3, while-let): same ownership-transfer shape as if-let, applied
// once at compile time (the AST node is visited once regardless of how many
// runtime iterations occur) — mirrors if-let's "mark source moved" exactly.
void OwnershipChecker::visitWhileLetStmt(WhileLetStmt *node) {
    visit(node->getOptionalExpr());

    bool payloadIsDrop = false;
    if (node->getOptionalExpr()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
        auto *ident = static_cast<IdentifierExpr *>(node->getOptionalExpr());
        auto *srcInfo = getInfo(ident->getName());
        if (srcInfo && srcInfo->isDropType) {
            payloadIsDrop = true;
            markMoved(ident->getName(), node->getStartLoc());
        }
    }

    pushOwnershipScope();
    trackVariable(node->getBindingName(), /*isMutable=*/false,
                  /*isCopyType=*/!payloadIsDrop, /*isDropType=*/payloadIsDrop,
                  node->getStartLoc());
    visit(node->getBody());
    dropScopeVariables();
    popOwnershipScope();
}

void OwnershipChecker::visitIdentifierExpr(IdentifierExpr *node) {
    checkUse(node->getName(), node->getStartLoc());
}

void OwnershipChecker::visitAssignExpr(AssignExpr *node) {
    // Visit the value first (may move a value)
    visit(node->getValue());

    // Move semantics: `b = a` where `a` is a Drop-conforming struct moves `a`
    // (same conservative scope as the `let b = a` case above). Note: `b`'s
    // OVERWRITTEN old value is not dropped here — that's a documented,
    // double-free-safe leak (see spec point 2), out of scope for this task.
    //
    // Gate mirrors IRGen's suppression EXACTLY (IRGenCall.cpp's plain-
    // identifier-target branch, itself gated on `Op::Assign`): IRGen only
    // suppresses the source's drop when the op is plain `=` AND the
    // assignment TARGET is a bare identifier. For any other target shape
    // (`x.f = a`, `arr[i] = a`) or any compound op (`+=` etc.), IRGen does
    // NOT suppress the drop — `a` still gets dropped normally at scope exit
    // — so marking it moved here would make Sema reject programs that
    // compile and run correctly (spurious err_use_after_move).
    if (node->getOp() == AssignExpr::Op::Assign &&
        node->getTarget()->getKind() == ASTNode::NodeKind::IdentifierExpr &&
        node->getValue()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
        auto *valIdent = static_cast<IdentifierExpr *>(
            const_cast<Expr *>(node->getValue()));
        auto *srcInfo = getInfo(valIdent->getName());
        if (srcInfo && srcInfo->isDropType) {
            markMoved(valIdent->getName(), node->getStartLoc());
        }
    }

    // Check target is mutable
    if (node->getTarget()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
        auto *ident = static_cast<IdentifierExpr *>(node->getTarget());
        if (!checkMutation(ident->getName(), node->getStartLoc())) {
            return;
        }

        // If assigning a non-copy value, it's a move
        auto *info = getInfo(ident->getName());
        if (info && info->state == OwnershipState::BorrowedImmutable) {
            diag_.report(node->getStartLoc(), DiagID::err_move_while_borrowed,
                         ident->getName());
        }
    }

    visit(node->getTarget());
}

void OwnershipChecker::visitCallExpr(CallExpr *node) {
    visit(node->getCallee());

    // Each argument is either copied (if Copy type) or moved
    for (auto &arg : node->getArgs()) {
        // Check if it's a ref expression
        if (arg->getKind() == ASTNode::NodeKind::RefExpr) {
            visit(arg.get());
            continue;
        }

        visit(arg.get());

        // If argument is a simple identifier and non-Copy, it's moved
        if (arg->getKind() == ASTNode::NodeKind::IdentifierExpr) {
            auto *ident = static_cast<IdentifierExpr *>(arg.get());
            auto *info = getInfo(ident->getName());
            if (info && !info->isCopyType) {
                markMoved(ident->getName(), arg->getStartLoc());
            }
        }
    }
}

void OwnershipChecker::visitBinaryExpr(BinaryExpr *node) {
    visit(node->getLHS());
    visit(node->getRHS());
}

void OwnershipChecker::visitRefExpr(RefExpr *node) {
    if (node->getExpr()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
        auto *ident = static_cast<IdentifierExpr *>(
            const_cast<Expr *>(node->getExpr()));
        if (!addBorrow(ident->getName(), node->isMutable(), node->getStartLoc())) {
            return;
        }

        // Check mutable ref to immutable variable
        if (node->isMutable()) {
            auto *info = getInfo(ident->getName());
            if (info && !info->isMutable) {
                diag_.report(node->getStartLoc(), DiagID::err_mut_ref_to_immutable,
                             ident->getName());
                diag_.report(node->getStartLoc(), DiagID::note_use_var_for_mutable);
            }
        }
    }

    visit(const_cast<Expr *>(node->getExpr()));
}

// === Private helpers ===

void OwnershipChecker::trackVariable(const std::string &name, bool isMutable,
                                      bool isCopyType, bool isDropType,
                                      SourceLocation loc) {
    OwnershipInfo info;
    info.name = name;
    info.state = OwnershipState::Owned;
    info.isMutable = isMutable;
    info.isCopyType = isCopyType;
    info.isDropType = isDropType;
    info.declLocation = loc;

    if (!scopeStack_.empty()) {
        scopeStack_.back()[name] = info;
        allVariables_[name] = &scopeStack_.back()[name];
    }
}

void OwnershipChecker::markMoved(const std::string &name, SourceLocation loc) {
    auto *info = getInfo(name);
    if (!info)
        return;

    if (info->isCopyType)
        return; // Copy types don't move

    if (info->state == OwnershipState::Moved) {
        diag_.reportRange(loc, static_cast<uint32_t>(name.size()),
                          DiagID::err_double_move, name);
        diag_.report(info->lastMoveLocation, DiagID::note_moved_here, name);
        return;
    }

    if (info->borrowCount > 0 || info->hasMutableBorrow) {
        diag_.report(loc, DiagID::err_move_while_borrowed, name);
        return;
    }

    info->state = OwnershipState::Moved;
    info->lastMoveLocation = loc;
}

bool OwnershipChecker::checkUse(const std::string &name, SourceLocation loc) {
    auto *info = getInfo(name);
    if (!info)
        return true; // Unknown variable, let TypeChecker handle it

    if (info->state == OwnershipState::Moved) {
        diag_.reportRange(loc, static_cast<uint32_t>(name.size()),
                          DiagID::err_use_after_move, name);
        diag_.report(info->lastMoveLocation, DiagID::note_moved_here, name);
        diag_.report(loc, DiagID::note_consider_ref, name);
        return false;
    }

    if (info->state == OwnershipState::Dropped) {
        diag_.reportRange(loc, static_cast<uint32_t>(name.size()),
                          DiagID::err_use_after_move, name);
        return false;
    }

    return true;
}

bool OwnershipChecker::checkMutation(const std::string &name, SourceLocation loc) {
    auto *info = getInfo(name);
    if (!info)
        return true; // Not tracked (e.g. global, field, or non-owned) — allow silently

    if (!info->isMutable) {
        diag_.reportRange(loc, static_cast<uint32_t>(name.size()),
                          DiagID::err_assign_to_immutable, name);
        diag_.reportHelp(loc, static_cast<uint32_t>(name.size()),
                         "declare with 'var' instead of 'let' to make it mutable",
                         "", DiagID::note_use_var_for_mutable);
        return false;
    }

    return true;
}

bool OwnershipChecker::addBorrow(const std::string &name, bool isMutable,
                                  SourceLocation loc) {
    auto *info = getInfo(name);
    if (!info)
        return true; // Not tracked (e.g. global, field, or non-owned) — allow silently

    if (info->state == OwnershipState::Moved) {
        diag_.reportRange(loc, static_cast<uint32_t>(name.size()),
                          DiagID::err_use_after_move, name);
        return false;
    }

    if (isMutable) {
        // Mutable borrow: no other borrows allowed
        if (info->borrowCount > 0 || info->hasMutableBorrow) {
            diag_.report(loc, DiagID::err_mut_borrow_conflict, name);
            if (info->lastBorrowLocation.isValid()) {
                diag_.report(info->lastBorrowLocation, DiagID::note_borrowed_here, name);
            }
            return false;
        }
        info->hasMutableBorrow = true;
        info->state = OwnershipState::BorrowedMutable;
    } else {
        // Immutable borrow: no mutable borrow allowed
        if (info->hasMutableBorrow) {
            diag_.report(loc, DiagID::err_immut_borrow_conflict, name);
            return false;
        }
        info->borrowCount++;
        info->state = OwnershipState::BorrowedImmutable;
    }

    info->lastBorrowLocation = loc;
    return true;
}

void OwnershipChecker::releaseBorrows(const std::string &name) {
    auto *info = getInfo(name);
    if (!info)
        return;

    info->borrowCount = 0;
    info->hasMutableBorrow = false;
    if (info->state == OwnershipState::BorrowedImmutable ||
        info->state == OwnershipState::BorrowedMutable) {
        info->state = OwnershipState::Owned;
    }
}

bool OwnershipChecker::isCopyType(const TypeRepr *type) const {
    if (!type)
        return true;

    // Primitives (i8-u64, f32, f64, bool, string) are Copy — but not void
    if (type->isPrimitive() && !type->isVoid())
        return true;

    // NamedTypeRepr("String") — parser sometimes creates this instead of Kind::String
    if (type->getKind() == TypeRepr::Kind::Named) {
        auto *named = static_cast<const NamedTypeRepr *>(type);
        const auto &n = named->getName();
        if (n == "String" || n == "Map" || n == "Set")
            return true;
        // Classes are reference types — passing a class value shares the
        // reference rather than consuming it, so treat them as Copy.
        if (classNames_.count(n))
            return true;
    }

    // Arrays, Tuples, and Function types are Copy
    auto k = type->getKind();
    if (k == TypeRepr::Kind::Array || k == TypeRepr::Kind::Tuple ||
        k == TypeRepr::Kind::Function)
        return true;

    // Optional<T>: Copy UNLESS its payload is a Drop-conforming struct.
    // Optional<Drop-struct> owns at most one Drop value (or none, if nil) —
    // treating it as Copy would let `let b = a` / call-arg passing / if-let
    // silently duplicate ownership of that payload (double-drop risk). All
    // other Optionals (primitives, plain structs, arrays, ...) are unchanged
    // (still Copy) — conservative scope, spec point 5.
    if (k == TypeRepr::Kind::Optional) {
        auto *opt = static_cast<const OptionalTypeRepr *>(type);
        if (isDropType(opt->getInner()))
            return false;
        return true;
    }

    return false;
}

bool OwnershipChecker::isDropType(const TypeRepr *type) const {
    if (!type)
        return false;

    // Optional<T>: inherits Drop-ness from its payload (see isCopyType
    // above) — this is what lets `let b = a` / if-let / call-arg passing on
    // an Optional<Drop-struct> variable participate in move tracking.
    if (type->getKind() == TypeRepr::Kind::Optional) {
        auto *opt = static_cast<const OptionalTypeRepr *>(type);
        return isDropType(opt->getInner());
    }

    // Only NAMED struct types can conform to Drop.
    if (type->getKind() != TypeRepr::Kind::Named)
        return false;

    auto *named = static_cast<const NamedTypeRepr *>(type);
    return dropTypeNames_.count(named->getName()) > 0;
}

void OwnershipChecker::dropScopeVariables() {
    if (scopeStack_.empty())
        return;

    for (auto &[name, info] : scopeStack_.back()) {
        if (info.state == OwnershipState::Owned && !info.isCopyType) {
            // Drop the value (in codegen, this would insert drop calls)
            info.state = OwnershipState::Dropped;
        }
        // Release any borrows
        releaseBorrows(name);
        // Remove from flat lookup
        allVariables_.erase(name);
    }
}

void OwnershipChecker::pushOwnershipScope() {
    scopeStack_.emplace_back();
}

void OwnershipChecker::popOwnershipScope() {
    if (!scopeStack_.empty())
        scopeStack_.pop_back();
}

OwnershipInfo *OwnershipChecker::getInfo(const std::string &name) {
    auto it = allVariables_.find(name);
    if (it != allVariables_.end())
        return it->second;
    return nullptr;
}

} // namespace liva
