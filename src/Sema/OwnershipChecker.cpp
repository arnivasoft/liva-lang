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
        trackVariable(param.name, param.isMutRef, copyType, param.location);
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

    bool copyType = node->getType() ? isCopyType(node->getType()) : true;
    trackVariable(node->getName(), node->isMutable(), copyType, node->getStartLoc());
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
    trackVariable(node->getVarName(), false, true, node->getStartLoc());
    visit(const_cast<ASTNode *>(node->getBody()));
    dropScopeVariables();
    popOwnershipScope();
}

void OwnershipChecker::visitIdentifierExpr(IdentifierExpr *node) {
    checkUse(node->getName(), node->getStartLoc());
}

void OwnershipChecker::visitAssignExpr(AssignExpr *node) {
    // Visit the value first (may move a value)
    visit(node->getValue());

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
                                      bool isCopyType, SourceLocation loc) {
    OwnershipInfo info;
    info.name = name;
    info.state = OwnershipState::Owned;
    info.isMutable = isMutable;
    info.isCopyType = isCopyType;
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
        diag_.report(loc, DiagID::err_double_move, name);
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
        diag_.report(loc, DiagID::err_use_after_move, name);
        diag_.report(info->lastMoveLocation, DiagID::note_moved_here, name);
        diag_.report(loc, DiagID::note_consider_ref, name);
        return false;
    }

    if (info->state == OwnershipState::Dropped) {
        diag_.report(loc, DiagID::err_use_after_move, name);
        return false;
    }

    return true;
}

bool OwnershipChecker::checkMutation(const std::string &name, SourceLocation loc) {
    auto *info = getInfo(name);
    if (!info)
        return true;

    if (!info->isMutable) {
        diag_.report(loc, DiagID::err_assign_to_immutable, name);
        diag_.report(loc, DiagID::note_use_var_for_mutable);
        return false;
    }

    return true;
}

bool OwnershipChecker::addBorrow(const std::string &name, bool isMutable,
                                  SourceLocation loc) {
    auto *info = getInfo(name);
    if (!info)
        return true;

    if (info->state == OwnershipState::Moved) {
        diag_.report(loc, DiagID::err_use_after_move, name);
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
    // Primitives and booleans are Copy types
    return type->isPrimitive() && !type->isVoid();
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
