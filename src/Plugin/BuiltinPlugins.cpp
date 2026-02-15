#include "liva/Plugin/BuiltinPlugins.h"
#include "liva/AST/Decl.h"
#include "liva/AST/Expr.h"
#include "liva/AST/Stmt.h"
#include "liva/Common/Diagnostics.h"
#include <cctype>
#include <set>
#include <string>

namespace liva {

// === Helpers ===

static bool isPascalCase(const std::string &name) {
    if (name.empty())
        return false;
    if (!std::isupper(static_cast<unsigned char>(name[0])))
        return false;
    for (char c : name) {
        if (c == '_')
            return false;
    }
    return true;
}

static bool isCamelCase(const std::string &name) {
    if (name.empty())
        return false;
    if (!std::islower(static_cast<unsigned char>(name[0])))
        return false;
    for (char c : name) {
        if (c == '_')
            return false;
    }
    return true;
}

// === NamingConventionPlugin ===

bool NamingConventionPlugin::afterParse(TranslationUnit &tu, DiagnosticsEngine &diag) {
    for (const auto &decl : tu.getDeclarations()) {
        auto kind = decl->getKind();

        // Check struct/enum/class/protocol → PascalCase
        if (kind == ASTNode::NodeKind::StructDecl) {
            auto *s = static_cast<const StructDecl *>(decl.get());
            if (!isPascalCase(s->getName())) {
                diag.report(s->getStartLoc(), DiagID::warn_lint_type_naming,
                            "struct", s->getName());
            }
        } else if (kind == ASTNode::NodeKind::EnumDecl) {
            auto *e = static_cast<const EnumDecl *>(decl.get());
            if (!isPascalCase(e->getName())) {
                diag.report(e->getStartLoc(), DiagID::warn_lint_type_naming,
                            "enum", e->getName());
            }
        } else if (kind == ASTNode::NodeKind::ClassDecl) {
            auto *c = static_cast<const ClassDecl *>(decl.get());
            if (!isPascalCase(c->getName())) {
                diag.report(c->getStartLoc(), DiagID::warn_lint_type_naming,
                            "class", c->getName());
            }
        } else if (kind == ASTNode::NodeKind::ProtocolDecl) {
            auto *p = static_cast<const ProtocolDecl *>(decl.get());
            if (!isPascalCase(p->getName())) {
                diag.report(p->getStartLoc(), DiagID::warn_lint_type_naming,
                            "protocol", p->getName());
            }
        } else if (kind == ASTNode::NodeKind::FuncDecl) {
            auto *f = static_cast<const FuncDecl *>(decl.get());
            // Skip main and methods (methods are inside impl blocks)
            if (f->getName() != "main" && !f->isMethod()) {
                if (!isCamelCase(f->getName())) {
                    diag.report(f->getStartLoc(), DiagID::warn_plugin_naming_func,
                                f->getName());
                }
            }
        }
    }
    return true; // always continue pipeline
}

// === UnusedFunctionPlugin — helpers ===

static void collectCallees(const Expr *expr, std::set<std::string> &callees);

static void collectCalleesFromNode(const ASTNode *node, std::set<std::string> &callees) {
    if (!node)
        return;

    switch (node->getKind()) {
    case ASTNode::NodeKind::ExprStmt: {
        auto *es = static_cast<const ExprStmt *>(node);
        collectCallees(es->getExpr(), callees);
        break;
    }
    case ASTNode::NodeKind::ReturnStmt: {
        auto *rs = static_cast<const ReturnStmt *>(node);
        if (rs->hasValue())
            collectCallees(rs->getValue(), callees);
        break;
    }
    case ASTNode::NodeKind::VarDecl: {
        auto *vd = static_cast<const VarDecl *>(node);
        if (vd->hasInit())
            collectCallees(vd->getInit(), callees);
        break;
    }
    case ASTNode::NodeKind::BlockStmt: {
        auto *block = static_cast<const BlockStmt *>(node);
        for (const auto &s : block->getStatements())
            collectCalleesFromNode(s.get(), callees);
        break;
    }
    case ASTNode::NodeKind::IfStmt: {
        auto *ifS = static_cast<const IfStmt *>(node);
        collectCallees(ifS->getCondition(), callees);
        collectCalleesFromNode(ifS->getThenBody(), callees);
        if (ifS->hasElse())
            collectCalleesFromNode(ifS->getElseBody(), callees);
        break;
    }
    case ASTNode::NodeKind::WhileStmt: {
        auto *ws = static_cast<const WhileStmt *>(node);
        collectCallees(ws->getCondition(), callees);
        collectCalleesFromNode(ws->getBody(), callees);
        break;
    }
    case ASTNode::NodeKind::ForStmt: {
        auto *fs = static_cast<const ForStmt *>(node);
        collectCalleesFromNode(fs->getBody(), callees);
        break;
    }
    case ASTNode::NodeKind::FuncDecl: {
        auto *fd = static_cast<const FuncDecl *>(node);
        if (fd->hasBody()) {
            for (const auto &s : fd->getBody()->getStatements())
                collectCalleesFromNode(s.get(), callees);
        }
        break;
    }
    default:
        break;
    }
}

static void collectCallees(const Expr *expr, std::set<std::string> &callees) {
    if (!expr)
        return;

    if (expr->getKind() == ASTNode::NodeKind::CallExpr) {
        auto *call = static_cast<const CallExpr *>(expr);
        // Get callee name
        if (call->getCallee() &&
            call->getCallee()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
            auto *ident = static_cast<const IdentifierExpr *>(call->getCallee());
            callees.insert(ident->getName());
        }
        // Recurse into callee and args
        collectCallees(call->getCallee(), callees);
        for (const auto &arg : call->getArgs())
            collectCallees(arg.get(), callees);
    } else if (expr->getKind() == ASTNode::NodeKind::BinaryExpr) {
        auto *bin = static_cast<const BinaryExpr *>(expr);
        collectCallees(bin->getLHS(), callees);
        collectCallees(bin->getRHS(), callees);
    } else if (expr->getKind() == ASTNode::NodeKind::UnaryExpr) {
        auto *un = static_cast<const UnaryExpr *>(expr);
        collectCallees(un->getOperand(), callees);
    } else if (expr->getKind() == ASTNode::NodeKind::StructLiteralExpr) {
        auto *sl = static_cast<const StructLiteralExpr *>(expr);
        for (const auto &field : sl->getFields())
            collectCallees(field.value.get(), callees);
    }
}

// === UnusedFunctionPlugin ===

bool UnusedFunctionPlugin::afterSema(TranslationUnit &tu, DiagnosticsEngine &diag) {
    // Step 1: Collect all top-level function names (excluding main, extern, methods)
    struct FuncInfo {
        std::string name;
        SourceLocation loc;
    };
    std::vector<FuncInfo> definedFuncs;

    for (const auto &decl : tu.getDeclarations()) {
        if (decl->getKind() == ASTNode::NodeKind::FuncDecl) {
            auto *f = static_cast<const FuncDecl *>(decl.get());
            if (f->getName() == "main")
                continue;
            if (f->isExtern())
                continue;
            if (f->isMethod())
                continue;
            definedFuncs.push_back({f->getName(), f->getStartLoc()});
        }
    }

    if (definedFuncs.empty())
        return true;

    // Step 2: Collect all callee names from the entire TU
    std::set<std::string> callees;
    for (const auto &decl : tu.getDeclarations()) {
        collectCalleesFromNode(decl.get(), callees);
    }

    // Step 3: Report defined but uncalled functions
    for (const auto &fi : definedFuncs) {
        if (callees.find(fi.name) == callees.end()) {
            diag.report(fi.loc, DiagID::warn_plugin_unused_function, fi.name);
        }
    }

    return true; // always continue pipeline
}

} // namespace liva
