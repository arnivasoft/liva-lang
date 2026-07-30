#include "liva/AST/ASTWalk.h"

#include "liva/AST/Decl.h"
#include "liva/AST/Expr.h"
#include "liva/AST/Stmt.h"

namespace liva {

namespace {

/// Null çocuk yaygın (değersiz return, else'siz if, gövdesiz protokol metodu),
/// bu yüzden her yayım buradan geçiyor.
inline void emit(const ASTNode *child,
                 const std::function<void(const ASTNode *)> &fn) {
    if (child)
        fn(child);
}

} // namespace

bool forEachChild(const ASTNode *node,
                  const std::function<void(const ASTNode *)> &fn) {
    if (!node)
        return true;

    using K = ASTNode::NodeKind;
    switch (node->getKind()) {
    // === Bildirimler ===
    case K::FuncDecl: {
        auto *d = static_cast<const FuncDecl *>(node);
        for (const auto &p : d->getParams())
            emit(p.defaultValue.get(), fn);
        emit(d->getBody(), fn);
        return true;
    }
    case K::VarDecl:
        emit(static_cast<const VarDecl *>(node)->getInit(), fn);
        return true;
    case K::StructDecl: {
        auto *d = static_cast<const StructDecl *>(node);
        for (const auto &f : d->getFields())
            emit(f.get(), fn);
        return true;
    }
    case K::FieldDecl: {
        auto *d = static_cast<const FieldDecl *>(node);
        emit(d->getGetter(), fn);
        emit(d->getSetter(), fn);
        emit(d->getWillSet(), fn);
        emit(d->getDidSet(), fn);
        emit(d->getLazyInit(), fn);
        return true;
    }
    case K::EnumDecl: {
        auto *d = static_cast<const EnumDecl *>(node);
        for (const auto &c : d->getCases())
            emit(c.get(), fn);
        return true;
    }
    // İlişkili tipler TypeRepr, ASTNode değil — AST çocuğu yok.
    case K::EnumCaseDecl:
        return true;
    case K::ImplDecl: {
        auto *d = static_cast<const ImplDecl *>(node);
        for (const auto &m : d->getMethods())
            emit(m.get(), fn);
        return true;
    }
    case K::ProtocolDecl: {
        auto *d = static_cast<const ProtocolDecl *>(node);
        for (const auto &m : d->getMethods())
            emit(m.get(), fn);
        return true;
    }
    case K::ImportDecl:
    case K::TypeAliasDecl:
    // MacroDecl gövdesini ham kaynak metni olarak tutuyor, AST olarak değil.
    case K::MacroDecl:
        return true;
    case K::ClassDecl: {
        auto *d = static_cast<const ClassDecl *>(node);
        for (const auto &m : d->getMembers()) {
            emit(m.field.get(), fn);
            emit(m.method.get(), fn);
        }
        return true;
    }
    case K::TestDecl:
        emit(static_cast<const TestDecl *>(node)->getBody(), fn);
        return true;

    // === Deyimler ===
    case K::ExprStmt:
        emit(static_cast<const ExprStmt *>(node)->getExpr(), fn);
        return true;
    case K::ReturnStmt:
        emit(static_cast<const ReturnStmt *>(node)->getValue(), fn);
        return true;
    case K::IfStmt: {
        auto *s = static_cast<const IfStmt *>(node);
        emit(s->getCondition(), fn);
        emit(s->getThenBody(), fn);
        emit(s->getElseBody(), fn);
        return true;
    }
    case K::WhileStmt: {
        auto *s = static_cast<const WhileStmt *>(node);
        emit(s->getCondition(), fn);
        emit(s->getBody(), fn);
        return true;
    }
    case K::ForStmt: {
        auto *s = static_cast<const ForStmt *>(node);
        emit(s->getIterable(), fn);
        emit(s->getBody(), fn);
        return true;
    }
    case K::BlockStmt: {
        auto *s = static_cast<const BlockStmt *>(node);
        for (const auto &st : s->getStatements())
            emit(st.get(), fn);
        return true;
    }
    case K::BreakStmt:
    case K::ContinueStmt:
        return true;
    case K::IfLetStmt: {
        auto *s = static_cast<const IfLetStmt *>(node);
        emit(s->getOptionalExpr(), fn);
        emit(s->getThenBody(), fn);
        emit(s->getElseBody(), fn);
        return true;
    }
    case K::WhileLetStmt: {
        auto *s = static_cast<const WhileLetStmt *>(node);
        emit(s->getOptionalExpr(), fn);
        emit(s->getBody(), fn);
        return true;
    }

    // === İfadeler ===
    case K::IntegerLiteralExpr:
    case K::FloatLiteralExpr:
    case K::BoolLiteralExpr:
    case K::StringLiteralExpr:
    case K::NilLiteralExpr:
    // Turbofish tip argümanları TypeRepr, ASTNode değil.
    case K::IdentifierExpr:
        return true;
    case K::BinaryExpr: {
        auto *e = static_cast<const BinaryExpr *>(node);
        emit(e->getLHS(), fn);
        emit(e->getRHS(), fn);
        return true;
    }
    case K::UnaryExpr:
        emit(static_cast<const UnaryExpr *>(node)->getOperand(), fn);
        return true;
    case K::CallExpr: {
        auto *e = static_cast<const CallExpr *>(node);
        emit(e->getCallee(), fn);
        for (const auto &a : e->getArgs())
            emit(a.get(), fn);
        return true;
    }
    case K::MemberExpr:
        emit(static_cast<const MemberExpr *>(node)->getObject(), fn);
        return true;
    case K::IndexExpr: {
        auto *e = static_cast<const IndexExpr *>(node);
        emit(e->getBase(), fn);
        emit(e->getIndex(), fn);
        return true;
    }
    case K::AssignExpr: {
        auto *e = static_cast<const AssignExpr *>(node);
        emit(e->getTarget(), fn);
        emit(e->getValue(), fn);
        return true;
    }
    case K::StructLiteralExpr: {
        auto *e = static_cast<const StructLiteralExpr *>(node);
        for (const auto &f : e->getFields())
            emit(f.value.get(), fn);
        return true;
    }
    // Pattern ayrı bir hiyerarşi (ASTNode DEĞİL) ve patternler ad BAĞLAR, ad
    // okumaz — atlamak bir kullanımı kaçırmaz.
    case K::MatchExpr: {
        auto *e = static_cast<const MatchExpr *>(node);
        emit(e->getSubject(), fn);
        for (const auto &arm : e->getArms()) {
            emit(arm.guard.get(), fn);
            emit(arm.body.get(), fn);
        }
        return true;
    }
    case K::ArrayLiteralExpr: {
        auto *e = static_cast<const ArrayLiteralExpr *>(node);
        for (const auto &el : e->getElements())
            emit(el.get(), fn);
        return true;
    }
    case K::TupleLiteralExpr: {
        auto *e = static_cast<const TupleLiteralExpr *>(node);
        for (const auto &el : e->getElements())
            emit(el.get(), fn);
        return true;
    }
    case K::CastExpr:
        emit(static_cast<const CastExpr *>(node)->getExpr(), fn);
        return true;
    case K::IsExpr:
        emit(static_cast<const IsExpr *>(node)->getExpr(), fn);
        return true;
    case K::RefExpr:
        emit(static_cast<const RefExpr *>(node)->getExpr(), fn);
        return true;
    case K::GroupExpr:
        emit(static_cast<const GroupExpr *>(node)->getExpr(), fn);
        return true;
    case K::RangeExpr: {
        auto *e = static_cast<const RangeExpr *>(node);
        emit(e->getStart(), fn);
        emit(e->getEnd(), fn);
        return true;
    }
    case K::UnwrapExpr:
        emit(static_cast<const UnwrapExpr *>(node)->getOperand(), fn);
        return true;
    case K::ClosureExpr:
        emit(static_cast<const ClosureExpr *>(node)->getBody(), fn);
        return true;
    case K::TryExpr:
        emit(static_cast<const TryExpr *>(node)->getOperand(), fn);
        return true;
    case K::TernaryExpr: {
        auto *e = static_cast<const TernaryExpr *>(node);
        emit(e->getCondition(), fn);
        emit(e->getThenExpr(), fn);
        emit(e->getElseExpr(), fn);
        return true;
    }
    case K::AwaitExpr:
        emit(static_cast<const AwaitExpr *>(node)->getOperand(), fn);
        return true;
    case K::YieldExpr:
        emit(static_cast<const YieldExpr *>(node)->getValue(), fn);
        return true;
    case K::ComptimeExpr:
        emit(static_cast<const ComptimeExpr *>(node)->getBody(), fn);
        return true;
    // GENİŞLETİLMEMİŞ bir makronun argüman token'ları henüz AST değil.
    // Eksiksizliğe ihtiyacı olan çağıranlar genişletilmemiş MacroInvokeExpr'i
    // opak saymalı (bkz. findLastUse geri-çekilme kuralı 3).
    case K::MacroInvokeExpr:
        emit(static_cast<const MacroInvokeExpr *>(node)->getExpanded(), fn);
        return true;
    }

    return false; // -Wswitch açıkken ulaşılamaz.
}

bool walkSubtree(const ASTNode *node,
                 const std::function<void(const ASTNode *)> &fn) {
    if (!node)
        return true;

    fn(node);

    bool complete = true;
    bool known = forEachChild(node, [&](const ASTNode *child) {
        if (!walkSubtree(child, fn))
            complete = false;
    });
    return known && complete;
}

} // namespace liva
