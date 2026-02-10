#pragma once

#include "liva/AST/ASTVisitor.h"
#include <ostream>
#include <string>

namespace liva {

class TranslationUnit;

/// Pretty-prints the AST in a tree-like format
class ASTPrinter : public ASTVisitor<ASTPrinter> {
public:
    explicit ASTPrinter(std::ostream &os) : os_(os) {}

    void print(TranslationUnit &tu);

    void visitFuncDecl(FuncDecl *node);
    void visitVarDecl(VarDecl *node);
    void visitStructDecl(StructDecl *node);
    void visitFieldDecl(FieldDecl *node);
    void visitEnumDecl(EnumDecl *node);
    void visitEnumCaseDecl(EnumCaseDecl *node);
    void visitImplDecl(ImplDecl *node);
    void visitProtocolDecl(ProtocolDecl *node);
    void visitImportDecl(ImportDecl *node);

    void visitExprStmt(ExprStmt *node);
    void visitReturnStmt(ReturnStmt *node);
    void visitIfStmt(IfStmt *node);
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
    void visitMatchExpr(MatchExpr *node);
    void visitArrayLiteralExpr(ArrayLiteralExpr *node);
    void visitCastExpr(CastExpr *node);
    void visitRefExpr(RefExpr *node);
    void visitGroupExpr(GroupExpr *node);
    void visitRangeExpr(RangeExpr *node);
    void visitUnwrapExpr(UnwrapExpr *node);

private:
    void indent();
    void increaseIndent() { indentLevel_ += 2; }
    void decreaseIndent() { indentLevel_ -= 2; }

    std::ostream &os_;
    int indentLevel_ = 0;
};

} // namespace liva
