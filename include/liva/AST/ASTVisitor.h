#pragma once

#include "liva/AST/Decl.h"
#include "liva/AST/Expr.h"
#include "liva/AST/Stmt.h"

namespace liva {

/// Visitor pattern for AST traversal
template <typename Derived, typename RetTy = void>
class ASTVisitor {
public:
    RetTy visit(ASTNode *node) {
        switch (node->getKind()) {
        // Declarations
        case ASTNode::NodeKind::FuncDecl:
            return static_cast<Derived *>(this)->visitFuncDecl(static_cast<FuncDecl *>(node));
        case ASTNode::NodeKind::VarDecl:
            return static_cast<Derived *>(this)->visitVarDecl(static_cast<VarDecl *>(node));
        case ASTNode::NodeKind::StructDecl:
            return static_cast<Derived *>(this)->visitStructDecl(
                static_cast<StructDecl *>(node));
        case ASTNode::NodeKind::FieldDecl:
            return static_cast<Derived *>(this)->visitFieldDecl(
                static_cast<FieldDecl *>(node));
        case ASTNode::NodeKind::EnumDecl:
            return static_cast<Derived *>(this)->visitEnumDecl(static_cast<EnumDecl *>(node));
        case ASTNode::NodeKind::EnumCaseDecl:
            return static_cast<Derived *>(this)->visitEnumCaseDecl(
                static_cast<EnumCaseDecl *>(node));
        case ASTNode::NodeKind::ImplDecl:
            return static_cast<Derived *>(this)->visitImplDecl(static_cast<ImplDecl *>(node));
        case ASTNode::NodeKind::ProtocolDecl:
            return static_cast<Derived *>(this)->visitProtocolDecl(
                static_cast<ProtocolDecl *>(node));
        case ASTNode::NodeKind::ImportDecl:
            return static_cast<Derived *>(this)->visitImportDecl(
                static_cast<ImportDecl *>(node));

        // Statements
        case ASTNode::NodeKind::ExprStmt:
            return static_cast<Derived *>(this)->visitExprStmt(static_cast<ExprStmt *>(node));
        case ASTNode::NodeKind::ReturnStmt:
            return static_cast<Derived *>(this)->visitReturnStmt(
                static_cast<ReturnStmt *>(node));
        case ASTNode::NodeKind::IfStmt:
            return static_cast<Derived *>(this)->visitIfStmt(static_cast<IfStmt *>(node));
        case ASTNode::NodeKind::WhileStmt:
            return static_cast<Derived *>(this)->visitWhileStmt(
                static_cast<WhileStmt *>(node));
        case ASTNode::NodeKind::ForStmt:
            return static_cast<Derived *>(this)->visitForStmt(static_cast<ForStmt *>(node));
        case ASTNode::NodeKind::BlockStmt:
            return static_cast<Derived *>(this)->visitBlockStmt(
                static_cast<BlockStmt *>(node));
        case ASTNode::NodeKind::BreakStmt:
            return static_cast<Derived *>(this)->visitBreakStmt(
                static_cast<BreakStmt *>(node));
        case ASTNode::NodeKind::ContinueStmt:
            return static_cast<Derived *>(this)->visitContinueStmt(
                static_cast<ContinueStmt *>(node));
        case ASTNode::NodeKind::IfLetStmt:
            return static_cast<Derived *>(this)->visitIfLetStmt(
                static_cast<IfLetStmt *>(node));

        // Expressions
        case ASTNode::NodeKind::IntegerLiteralExpr:
            return static_cast<Derived *>(this)->visitIntegerLiteralExpr(
                static_cast<IntegerLiteralExpr *>(node));
        case ASTNode::NodeKind::FloatLiteralExpr:
            return static_cast<Derived *>(this)->visitFloatLiteralExpr(
                static_cast<FloatLiteralExpr *>(node));
        case ASTNode::NodeKind::BoolLiteralExpr:
            return static_cast<Derived *>(this)->visitBoolLiteralExpr(
                static_cast<BoolLiteralExpr *>(node));
        case ASTNode::NodeKind::StringLiteralExpr:
            return static_cast<Derived *>(this)->visitStringLiteralExpr(
                static_cast<StringLiteralExpr *>(node));
        case ASTNode::NodeKind::NilLiteralExpr:
            return static_cast<Derived *>(this)->visitNilLiteralExpr(
                static_cast<NilLiteralExpr *>(node));
        case ASTNode::NodeKind::IdentifierExpr:
            return static_cast<Derived *>(this)->visitIdentifierExpr(
                static_cast<IdentifierExpr *>(node));
        case ASTNode::NodeKind::BinaryExpr:
            return static_cast<Derived *>(this)->visitBinaryExpr(
                static_cast<BinaryExpr *>(node));
        case ASTNode::NodeKind::UnaryExpr:
            return static_cast<Derived *>(this)->visitUnaryExpr(
                static_cast<UnaryExpr *>(node));
        case ASTNode::NodeKind::CallExpr:
            return static_cast<Derived *>(this)->visitCallExpr(static_cast<CallExpr *>(node));
        case ASTNode::NodeKind::MemberExpr:
            return static_cast<Derived *>(this)->visitMemberExpr(
                static_cast<MemberExpr *>(node));
        case ASTNode::NodeKind::IndexExpr:
            return static_cast<Derived *>(this)->visitIndexExpr(
                static_cast<IndexExpr *>(node));
        case ASTNode::NodeKind::AssignExpr:
            return static_cast<Derived *>(this)->visitAssignExpr(
                static_cast<AssignExpr *>(node));
        case ASTNode::NodeKind::StructLiteralExpr:
            return static_cast<Derived *>(this)->visitStructLiteralExpr(
                static_cast<StructLiteralExpr *>(node));
        case ASTNode::NodeKind::MatchExpr:
            return static_cast<Derived *>(this)->visitMatchExpr(
                static_cast<MatchExpr *>(node));
        case ASTNode::NodeKind::ArrayLiteralExpr:
            return static_cast<Derived *>(this)->visitArrayLiteralExpr(
                static_cast<ArrayLiteralExpr *>(node));
        case ASTNode::NodeKind::CastExpr:
            return static_cast<Derived *>(this)->visitCastExpr(static_cast<CastExpr *>(node));
        case ASTNode::NodeKind::RefExpr:
            return static_cast<Derived *>(this)->visitRefExpr(static_cast<RefExpr *>(node));
        case ASTNode::NodeKind::GroupExpr:
            return static_cast<Derived *>(this)->visitGroupExpr(
                static_cast<GroupExpr *>(node));
        case ASTNode::NodeKind::RangeExpr:
            return static_cast<Derived *>(this)->visitRangeExpr(
                static_cast<RangeExpr *>(node));
        case ASTNode::NodeKind::UnwrapExpr:
            return static_cast<Derived *>(this)->visitUnwrapExpr(
                static_cast<UnwrapExpr *>(node));
        case ASTNode::NodeKind::ClosureExpr:
            return static_cast<Derived *>(this)->visitClosureExpr(
                static_cast<ClosureExpr *>(node));
        case ASTNode::NodeKind::TryExpr:
            return static_cast<Derived *>(this)->visitTryExpr(
                static_cast<TryExpr *>(node));
        }
        return RetTy();
    }

    // Default implementations - derived classes override those they care about
    RetTy visitFuncDecl(FuncDecl *) { return RetTy(); }
    RetTy visitVarDecl(VarDecl *) { return RetTy(); }
    RetTy visitStructDecl(StructDecl *) { return RetTy(); }
    RetTy visitFieldDecl(FieldDecl *) { return RetTy(); }
    RetTy visitEnumDecl(EnumDecl *) { return RetTy(); }
    RetTy visitEnumCaseDecl(EnumCaseDecl *) { return RetTy(); }
    RetTy visitImplDecl(ImplDecl *) { return RetTy(); }
    RetTy visitProtocolDecl(ProtocolDecl *) { return RetTy(); }
    RetTy visitImportDecl(ImportDecl *) { return RetTy(); }

    RetTy visitExprStmt(ExprStmt *) { return RetTy(); }
    RetTy visitReturnStmt(ReturnStmt *) { return RetTy(); }
    RetTy visitIfStmt(IfStmt *) { return RetTy(); }
    RetTy visitWhileStmt(WhileStmt *) { return RetTy(); }
    RetTy visitForStmt(ForStmt *) { return RetTy(); }
    RetTy visitBlockStmt(BlockStmt *) { return RetTy(); }
    RetTy visitBreakStmt(BreakStmt *) { return RetTy(); }
    RetTy visitContinueStmt(ContinueStmt *) { return RetTy(); }
    RetTy visitIfLetStmt(IfLetStmt *) { return RetTy(); }

    RetTy visitIntegerLiteralExpr(IntegerLiteralExpr *) { return RetTy(); }
    RetTy visitFloatLiteralExpr(FloatLiteralExpr *) { return RetTy(); }
    RetTy visitBoolLiteralExpr(BoolLiteralExpr *) { return RetTy(); }
    RetTy visitStringLiteralExpr(StringLiteralExpr *) { return RetTy(); }
    RetTy visitNilLiteralExpr(NilLiteralExpr *) { return RetTy(); }
    RetTy visitIdentifierExpr(IdentifierExpr *) { return RetTy(); }
    RetTy visitBinaryExpr(BinaryExpr *) { return RetTy(); }
    RetTy visitUnaryExpr(UnaryExpr *) { return RetTy(); }
    RetTy visitCallExpr(CallExpr *) { return RetTy(); }
    RetTy visitMemberExpr(MemberExpr *) { return RetTy(); }
    RetTy visitIndexExpr(IndexExpr *) { return RetTy(); }
    RetTy visitAssignExpr(AssignExpr *) { return RetTy(); }
    RetTy visitStructLiteralExpr(StructLiteralExpr *) { return RetTy(); }
    RetTy visitMatchExpr(MatchExpr *) { return RetTy(); }
    RetTy visitArrayLiteralExpr(ArrayLiteralExpr *) { return RetTy(); }
    RetTy visitCastExpr(CastExpr *) { return RetTy(); }
    RetTy visitRefExpr(RefExpr *) { return RetTy(); }
    RetTy visitGroupExpr(GroupExpr *) { return RetTy(); }
    RetTy visitRangeExpr(RangeExpr *) { return RetTy(); }
    RetTy visitUnwrapExpr(UnwrapExpr *) { return RetTy(); }
    RetTy visitClosureExpr(ClosureExpr *) { return RetTy(); }
    RetTy visitTryExpr(TryExpr *) { return RetTy(); }
};

} // namespace liva
