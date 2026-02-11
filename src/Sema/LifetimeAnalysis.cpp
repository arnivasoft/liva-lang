#include "liva/Sema/LifetimeAnalysis.h"

namespace liva {

LifetimeAnalysis::LifetimeAnalysis(DiagnosticsEngine &diag) : diag_(diag) {}

void LifetimeAnalysis::analyzeFunction(FuncDecl *func) {
    if (!func->hasBody()) return;

    currentDepth_ = 0;
    variables_.clear();

    // Register parameters at depth 0
    for (auto &param : func->getParams()) {
        variables_[param.name] = {0, param.location, ""};
    }

    visitBlockStmt(const_cast<BlockStmt *>(func->getBody()));
}

void LifetimeAnalysis::visitNode(ASTNode *node) {
    if (!node) return;

    switch (node->getKind()) {
    case ASTNode::NodeKind::BlockStmt:
        visitBlockStmt(static_cast<BlockStmt *>(node));
        break;
    case ASTNode::NodeKind::VarDecl:
        visitVarDecl(static_cast<VarDecl *>(node));
        break;
    case ASTNode::NodeKind::ExprStmt: {
        auto *es = static_cast<ExprStmt *>(node);
        if (es->getExpr()->getKind() == ASTNode::NodeKind::AssignExpr) {
            visitAssignExpr(static_cast<AssignExpr *>(es->getExpr()));
        }
        break;
    }
    case ASTNode::NodeKind::IfStmt:
        visitIfStmt(static_cast<IfStmt *>(node));
        break;
    case ASTNode::NodeKind::WhileStmt:
        visitWhileStmt(static_cast<WhileStmt *>(node));
        break;
    case ASTNode::NodeKind::ForStmt:
        visitForStmt(static_cast<ForStmt *>(node));
        break;
    case ASTNode::NodeKind::ReturnStmt:
        // No lifetime concerns for return in this simple analysis
        break;
    default:
        break;
    }
}

void LifetimeAnalysis::visitBlockStmt(BlockStmt *node) {
    currentDepth_++;

    for (auto &stmt : node->getStatements()) {
        visitNode(stmt.get());
    }

    checkScopeExit(currentDepth_);

    // Remove variables at current depth
    for (auto it = variables_.begin(); it != variables_.end(); ) {
        if (it->second.scopeDepth == currentDepth_)
            it = variables_.erase(it);
        else
            ++it;
    }

    currentDepth_--;
}

void LifetimeAnalysis::visitVarDecl(VarDecl *node) {
    // Record the variable at current scope depth
    variables_[node->getName()] = {currentDepth_, node->getStartLoc(), ""};

    // Check if init is a ref expression: let r = ref x
    if (node->hasInit()) {
        std::string target = getRefTarget(node->getInit());
        if (!target.empty()) {
            variables_[node->getName()].refTarget = target;

            // Immediate check: if referenced var is at deeper scope, it's an error
            auto it = variables_.find(target);
            if (it != variables_.end() && it->second.scopeDepth > currentDepth_) {
                diag_.report(node->getStartLoc(), DiagID::err_borrow_outlives_value, target);
            }
        }
    }
}

void LifetimeAnalysis::visitAssignExpr(AssignExpr *node) {
    // Check: r = ref x (reassigning a reference variable)
    if (node->getTarget()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
        auto *ident = static_cast<IdentifierExpr *>(node->getTarget());
        std::string target = getRefTarget(node->getValue());
        if (!target.empty()) {
            auto refIt = variables_.find(ident->getName());
            auto valIt = variables_.find(target);
            if (refIt != variables_.end() && valIt != variables_.end()) {
                refIt->second.refTarget = target;
                // If referenced var is at deeper scope than ref var, error
                if (valIt->second.scopeDepth > refIt->second.scopeDepth) {
                    diag_.report(node->getStartLoc(), DiagID::err_borrow_outlives_value, target);
                }
            }
        }
    }
}

void LifetimeAnalysis::visitIfStmt(IfStmt *node) {
    visitNode(node->getThenBody());
    if (node->hasElse()) {
        visitNode(node->getElseBody());
    }
}

void LifetimeAnalysis::visitWhileStmt(WhileStmt *node) {
    visitNode(const_cast<ASTNode *>(node->getBody()));
}

void LifetimeAnalysis::visitForStmt(ForStmt *node) {
    visitNode(const_cast<ASTNode *>(node->getBody()));
}

void LifetimeAnalysis::checkScopeExit(int exitingDepth) {
    // For each ref variable at a shallower depth,
    // check if it references a variable at the exiting depth
    for (auto &[name, info] : variables_) {
        if (info.refTarget.empty()) continue;
        if (info.scopeDepth >= exitingDepth) continue; // ref is also dying, no problem

        auto targetIt = variables_.find(info.refTarget);
        if (targetIt != variables_.end() && targetIt->second.scopeDepth == exitingDepth) {
            diag_.report(info.declLoc, DiagID::err_borrow_outlives_value, info.refTarget);
        }
    }
}

std::string LifetimeAnalysis::getRefTarget(const Expr *expr) const {
    if (!expr) return "";
    if (expr->getKind() == ASTNode::NodeKind::RefExpr) {
        auto *refExpr = static_cast<const RefExpr *>(expr);
        if (refExpr->getExpr()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
            auto *ident = static_cast<const IdentifierExpr *>(refExpr->getExpr());
            return ident->getName();
        }
    }
    return "";
}

} // namespace liva
