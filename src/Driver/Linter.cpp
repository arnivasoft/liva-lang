#include "liva/Driver/Driver.h"
#include "liva/AST/Decl.h"
#include "liva/AST/Expr.h"
#include "liva/AST/Stmt.h"
#include "liva/Common/Diagnostics.h"
#include <cctype>
#include <string>

namespace liva {

namespace {

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

static void checkExpr(const Expr *expr, DiagnosticsEngine &diag, int &count);

static void checkNode(const ASTNode *node, DiagnosticsEngine &diag, int &count);

static void checkFuncDecl(const FuncDecl *func, DiagnosticsEngine &diag, int &count) {
    // Rule 3: empty function body
    if (func->hasBody()) {
        const BlockStmt *body = func->getBody();
        if (body->getStatements().empty()) {
            diag.report(func->getStartLoc(), DiagID::warn_lint_empty_func_body,
                        func->getName());
            ++count;
        }
    }

    // Rule 5: default param before non-default
    const auto &params = func->getParams();
    bool seenDefault = false;
    for (const auto &param : params) {
        if (param.isSelf)
            continue;
        if (param.hasDefault()) {
            seenDefault = true;
        } else if (seenDefault) {
            diag.report(param.location, DiagID::warn_lint_default_param_order,
                        param.name);
            ++count;
        }
    }

    // Rule 4: missing doc comment on public declaration
    if (func->isPublic() && !func->hasDocComment()) {
        diag.report(func->getStartLoc(), DiagID::warn_lint_missing_doc_comment,
                    "function", func->getName());
        ++count;
    }

    // Recurse into body
    if (func->hasBody()) {
        for (const auto &stmt : func->getBody()->getStatements()) {
            checkNode(stmt.get(), diag, count);
        }
    }
}

static void checkStructDecl(const StructDecl *s, DiagnosticsEngine &diag, int &count) {
    // Rule 1: PascalCase naming
    if (!isPascalCase(s->getName())) {
        diag.report(s->getStartLoc(), DiagID::warn_lint_type_naming,
                    "struct", s->getName());
        ++count;
    }

    // Rule 4: missing doc comment on public
    if (s->isPublic() && !s->hasDocComment()) {
        diag.report(s->getStartLoc(), DiagID::warn_lint_missing_doc_comment,
                    "struct", s->getName());
        ++count;
    }
}

static void checkEnumDecl(const EnumDecl *e, DiagnosticsEngine &diag, int &count) {
    // Rule 1: PascalCase naming
    if (!isPascalCase(e->getName())) {
        diag.report(e->getStartLoc(), DiagID::warn_lint_type_naming,
                    "enum", e->getName());
        ++count;
    }

    // Rule 4: missing doc comment on public
    if (e->isPublic() && !e->hasDocComment()) {
        diag.report(e->getStartLoc(), DiagID::warn_lint_missing_doc_comment,
                    "enum", e->getName());
        ++count;
    }
}

static void checkProtocolDecl(const ProtocolDecl *p, DiagnosticsEngine &diag, int &count) {
    // Rule 1: PascalCase naming
    if (!isPascalCase(p->getName())) {
        diag.report(p->getStartLoc(), DiagID::warn_lint_type_naming,
                    "protocol", p->getName());
        ++count;
    }

    // Rule 4: missing doc comment on public
    if (p->isPublic() && !p->hasDocComment()) {
        diag.report(p->getStartLoc(), DiagID::warn_lint_missing_doc_comment,
                    "protocol", p->getName());
        ++count;
    }

    // Recurse into methods
    for (const auto &m : p->getMethods()) {
        checkFuncDecl(m.get(), diag, count);
    }
}

static void checkExpr(const Expr *expr, DiagnosticsEngine &diag, int &count) {
    if (!expr)
        return;

    if (expr->getKind() == ASTNode::NodeKind::BinaryExpr) {
        auto *bin = static_cast<const BinaryExpr *>(expr);

        // Rule 2: unnecessary bool comparison (x == true, x == false, x != true, x != false)
        if (bin->getOp() == BinaryExpr::Op::Eq || bin->getOp() == BinaryExpr::Op::NotEq) {
            bool lhsBool = bin->getLHS()->getKind() == ASTNode::NodeKind::BoolLiteralExpr;
            bool rhsBool = bin->getRHS()->getKind() == ASTNode::NodeKind::BoolLiteralExpr;
            if (lhsBool || rhsBool) {
                diag.report(bin->getStartLoc(), DiagID::warn_lint_bool_comparison);
                ++count;
            }
        }

        // Rule 6: self-comparison (x == x, x != x, x < x, etc.)
        if (bin->getOp() == BinaryExpr::Op::Eq || bin->getOp() == BinaryExpr::Op::NotEq ||
            bin->getOp() == BinaryExpr::Op::Less || bin->getOp() == BinaryExpr::Op::LessEq ||
            bin->getOp() == BinaryExpr::Op::Greater || bin->getOp() == BinaryExpr::Op::GreaterEq) {
            if (bin->getLHS()->getKind() == ASTNode::NodeKind::IdentifierExpr &&
                bin->getRHS()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
                auto *lhs = static_cast<const IdentifierExpr *>(bin->getLHS());
                auto *rhs = static_cast<const IdentifierExpr *>(bin->getRHS());
                if (lhs->getName() == rhs->getName()) {
                    diag.report(bin->getStartLoc(), DiagID::warn_lint_self_comparison,
                                lhs->getName());
                    ++count;
                }
            }
        }

        // Recurse into operands
        checkExpr(bin->getLHS(), diag, count);
        checkExpr(bin->getRHS(), diag, count);
    } else if (expr->getKind() == ASTNode::NodeKind::UnaryExpr) {
        auto *un = static_cast<const UnaryExpr *>(expr);
        checkExpr(un->getOperand(), diag, count);
    } else if (expr->getKind() == ASTNode::NodeKind::CallExpr) {
        auto *call = static_cast<const CallExpr *>(expr);
        checkExpr(call->getCallee(), diag, count);
        for (const auto &arg : call->getArgs()) {
            checkExpr(arg.get(), diag, count);
        }
    }
}

static void checkNode(const ASTNode *node, DiagnosticsEngine &diag, int &count) {
    if (!node)
        return;

    switch (node->getKind()) {
    case ASTNode::NodeKind::FuncDecl:
        checkFuncDecl(static_cast<const FuncDecl *>(node), diag, count);
        break;
    case ASTNode::NodeKind::StructDecl:
        checkStructDecl(static_cast<const StructDecl *>(node), diag, count);
        break;
    case ASTNode::NodeKind::EnumDecl:
        checkEnumDecl(static_cast<const EnumDecl *>(node), diag, count);
        break;
    case ASTNode::NodeKind::ProtocolDecl:
        checkProtocolDecl(static_cast<const ProtocolDecl *>(node), diag, count);
        break;
    case ASTNode::NodeKind::ImplDecl: {
        auto *impl = static_cast<const ImplDecl *>(node);
        for (const auto &m : impl->getMethods()) {
            checkFuncDecl(m.get(), diag, count);
        }
        break;
    }
    case ASTNode::NodeKind::BlockStmt: {
        auto *block = static_cast<const BlockStmt *>(node);
        for (const auto &s : block->getStatements()) {
            checkNode(s.get(), diag, count);
        }
        break;
    }
    case ASTNode::NodeKind::ExprStmt: {
        auto *es = static_cast<const ExprStmt *>(node);
        checkExpr(es->getExpr(), diag, count);
        break;
    }
    case ASTNode::NodeKind::IfStmt: {
        auto *ifS = static_cast<const IfStmt *>(node);
        checkExpr(ifS->getCondition(), diag, count);
        checkNode(ifS->getThenBody(), diag, count);
        if (ifS->hasElse())
            checkNode(ifS->getElseBody(), diag, count);
        break;
    }
    case ASTNode::NodeKind::WhileStmt: {
        auto *ws = static_cast<const WhileStmt *>(node);
        checkExpr(ws->getCondition(), diag, count);
        checkNode(ws->getBody(), diag, count);
        break;
    }
    case ASTNode::NodeKind::ForStmt: {
        auto *fs = static_cast<const ForStmt *>(node);
        checkNode(fs->getBody(), diag, count);
        break;
    }
    case ASTNode::NodeKind::ReturnStmt: {
        auto *rs = static_cast<const ReturnStmt *>(node);
        if (rs->hasValue())
            checkExpr(rs->getValue(), diag, count);
        break;
    }
    case ASTNode::NodeKind::VarDecl: {
        auto *vd = static_cast<const VarDecl *>(node);
        if (vd->hasInit())
            checkExpr(vd->getInit(), diag, count);
        break;
    }
    default:
        break;
    }
}

} // anonymous namespace

int lintLivaSource(const TranslationUnit &tu, DiagnosticsEngine &diag) {
    int count = 0;
    for (const auto &decl : tu.getDeclarations()) {
        checkNode(decl.get(), diag, count);
    }
    return count;
}

} // namespace liva
