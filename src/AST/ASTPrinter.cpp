#include "liva/AST/ASTPrinter.h"
#include "liva/AST/Decl.h"

namespace liva {

void ASTPrinter::indent() {
    for (int i = 0; i < indentLevel_; ++i)
        os_ << ' ';
}

void ASTPrinter::print(TranslationUnit &tu) {
    os_ << "TranslationUnit\n";
    increaseIndent();
    for (auto &decl : tu.getDeclarations()) {
        visit(decl.get());
    }
    decreaseIndent();
}

// === Declarations ===

void ASTPrinter::visitFuncDecl(FuncDecl *node) {
    indent();
    os_ << "FuncDecl '" << node->getName() << "'";
    if (node->isPublic())
        os_ << " pub";
    if (node->getReturnType())
        os_ << " -> " << node->getReturnType()->toString();
    os_ << "\n";

    increaseIndent();
    for (auto &param : node->getParams()) {
        indent();
        os_ << "Param '" << param.name << "'";
        if (param.isSelf)
            os_ << " self";
        if (param.isRef)
            os_ << " ref";
        if (param.isMutRef)
            os_ << " mut";
        if (param.type)
            os_ << " : " << param.type->toString();
        os_ << "\n";
    }
    if (node->getBody())
        visitBlockStmt(const_cast<BlockStmt *>(node->getBody()));
    decreaseIndent();
}

void ASTPrinter::visitVarDecl(VarDecl *node) {
    indent();
    os_ << "VarDecl '" << node->getName() << "'";
    os_ << (node->isMutable() ? " var" : " let");
    if (node->hasTypeAnnotation())
        os_ << " : " << node->getType()->toString();
    os_ << "\n";

    if (node->hasInit()) {
        increaseIndent();
        visit(const_cast<Expr *>(node->getInit()));
        decreaseIndent();
    }
}

void ASTPrinter::visitStructDecl(StructDecl *node) {
    indent();
    os_ << "StructDecl '" << node->getName() << "'";
    if (node->isPublic())
        os_ << " pub";
    os_ << "\n";

    increaseIndent();
    for (auto &field : node->getFields()) {
        visitFieldDecl(field.get());
    }
    decreaseIndent();
}

void ASTPrinter::visitFieldDecl(FieldDecl *node) {
    indent();
    os_ << "FieldDecl '" << node->getName() << "'";
    os_ << (node->isMutable() ? " var" : " let");
    if (node->getType())
        os_ << " : " << node->getType()->toString();
    os_ << "\n";
}

void ASTPrinter::visitEnumDecl(EnumDecl *node) {
    indent();
    os_ << "EnumDecl '" << node->getName() << "'\n";
    increaseIndent();
    for (auto &c : node->getCases()) {
        visitEnumCaseDecl(c.get());
    }
    decreaseIndent();
}

void ASTPrinter::visitEnumCaseDecl(EnumCaseDecl *node) {
    indent();
    os_ << "EnumCase '" << node->getName() << "'";
    if (node->hasAssociatedValues()) {
        os_ << "(";
        for (size_t i = 0; i < node->getAssociatedTypes().size(); ++i) {
            if (i > 0)
                os_ << ", ";
            os_ << node->getAssociatedTypes()[i]->toString();
        }
        os_ << ")";
    }
    os_ << "\n";
}

void ASTPrinter::visitImplDecl(ImplDecl *node) {
    indent();
    os_ << "ImplDecl '" << node->getTypeName() << "'";
    if (node->hasProtocol())
        os_ << " : " << node->getProtocolName();
    os_ << "\n";

    increaseIndent();
    for (auto &method : node->getMethods()) {
        visitFuncDecl(method.get());
    }
    decreaseIndent();
}

void ASTPrinter::visitProtocolDecl(ProtocolDecl *node) {
    indent();
    os_ << "ProtocolDecl '" << node->getName() << "'\n";
    increaseIndent();
    for (auto &method : node->getMethods()) {
        visitFuncDecl(method.get());
    }
    decreaseIndent();
}

void ASTPrinter::visitImportDecl(ImportDecl *node) {
    indent();
    os_ << "ImportDecl '" << node->getPathString() << "'\n";
}

// === Statements ===

void ASTPrinter::visitExprStmt(ExprStmt *node) {
    indent();
    os_ << "ExprStmt\n";
    increaseIndent();
    visit(node->getExpr());
    decreaseIndent();
}

void ASTPrinter::visitReturnStmt(ReturnStmt *node) {
    indent();
    os_ << "ReturnStmt\n";
    if (node->hasValue()) {
        increaseIndent();
        visit(node->getValue());
        decreaseIndent();
    }
}

void ASTPrinter::visitIfStmt(IfStmt *node) {
    indent();
    os_ << "IfStmt\n";
    increaseIndent();

    indent();
    os_ << "Condition:\n";
    increaseIndent();
    visit(const_cast<Expr *>(node->getCondition()));
    decreaseIndent();

    indent();
    os_ << "Then:\n";
    increaseIndent();
    visit(node->getThenBody());
    decreaseIndent();

    if (node->hasElse()) {
        indent();
        os_ << "Else:\n";
        increaseIndent();
        visit(node->getElseBody());
        decreaseIndent();
    }

    decreaseIndent();
}

void ASTPrinter::visitWhileStmt(WhileStmt *node) {
    indent();
    os_ << "WhileStmt\n";
    increaseIndent();
    indent();
    os_ << "Condition:\n";
    increaseIndent();
    visit(const_cast<Expr *>(node->getCondition()));
    decreaseIndent();
    indent();
    os_ << "Body:\n";
    increaseIndent();
    visit(const_cast<ASTNode *>(node->getBody()));
    decreaseIndent();
    decreaseIndent();
}

void ASTPrinter::visitForStmt(ForStmt *node) {
    indent();
    os_ << "ForStmt '" << node->getVarName() << "'\n";
    increaseIndent();
    indent();
    os_ << "Iterable:\n";
    increaseIndent();
    visit(const_cast<Expr *>(node->getIterable()));
    decreaseIndent();
    indent();
    os_ << "Body:\n";
    increaseIndent();
    visit(const_cast<ASTNode *>(node->getBody()));
    decreaseIndent();
    decreaseIndent();
}

void ASTPrinter::visitBlockStmt(BlockStmt *node) {
    indent();
    os_ << "Block\n";
    increaseIndent();
    for (auto &stmt : node->getStatements()) {
        visit(stmt.get());
    }
    decreaseIndent();
}

void ASTPrinter::visitBreakStmt(BreakStmt *) {
    indent();
    os_ << "BreakStmt\n";
}

void ASTPrinter::visitContinueStmt(ContinueStmt *) {
    indent();
    os_ << "ContinueStmt\n";
}

// === Expressions ===

void ASTPrinter::visitIntegerLiteralExpr(IntegerLiteralExpr *node) {
    indent();
    os_ << "IntegerLiteral " << node->getValue() << "\n";
}

void ASTPrinter::visitFloatLiteralExpr(FloatLiteralExpr *node) {
    indent();
    os_ << "FloatLiteral " << node->getValue() << "\n";
}

void ASTPrinter::visitBoolLiteralExpr(BoolLiteralExpr *node) {
    indent();
    os_ << "BoolLiteral " << (node->getValue() ? "true" : "false") << "\n";
}

void ASTPrinter::visitStringLiteralExpr(StringLiteralExpr *node) {
    indent();
    os_ << "StringLiteral \"" << node->getValue() << "\"\n";
}

void ASTPrinter::visitNilLiteralExpr(NilLiteralExpr *) {
    indent();
    os_ << "NilLiteral\n";
}

void ASTPrinter::visitIdentifierExpr(IdentifierExpr *node) {
    indent();
    os_ << "Identifier '" << node->getName() << "'\n";
}

void ASTPrinter::visitBinaryExpr(BinaryExpr *node) {
    indent();
    os_ << "BinaryExpr '" << node->getOpSpelling() << "'\n";
    increaseIndent();
    visit(node->getLHS());
    visit(node->getRHS());
    decreaseIndent();
}

void ASTPrinter::visitUnaryExpr(UnaryExpr *node) {
    indent();
    os_ << "UnaryExpr '" << node->getOpSpelling() << "'\n";
    increaseIndent();
    visit(node->getOperand());
    decreaseIndent();
}

void ASTPrinter::visitCallExpr(CallExpr *node) {
    indent();
    os_ << "CallExpr\n";
    increaseIndent();
    indent();
    os_ << "Callee:\n";
    increaseIndent();
    visit(node->getCallee());
    decreaseIndent();
    for (auto &arg : node->getArgs()) {
        indent();
        os_ << "Arg:\n";
        increaseIndent();
        visit(arg.get());
        decreaseIndent();
    }
    decreaseIndent();
}

void ASTPrinter::visitMemberExpr(MemberExpr *node) {
    indent();
    os_ << "MemberExpr '." << node->getMember() << "'\n";
    increaseIndent();
    visit(node->getObject());
    decreaseIndent();
}

void ASTPrinter::visitIndexExpr(IndexExpr *node) {
    indent();
    os_ << "IndexExpr\n";
    increaseIndent();
    visit(const_cast<Expr *>(node->getBase()));
    visit(const_cast<Expr *>(node->getIndex()));
    decreaseIndent();
}

void ASTPrinter::visitAssignExpr(AssignExpr *node) {
    indent();
    os_ << "AssignExpr\n";
    increaseIndent();
    visit(node->getTarget());
    visit(node->getValue());
    decreaseIndent();
}

void ASTPrinter::visitStructLiteralExpr(StructLiteralExpr *node) {
    indent();
    os_ << "StructLiteral '" << node->getTypeName() << "'\n";
    increaseIndent();
    for (auto &field : node->getFields()) {
        indent();
        os_ << "Field '" << field.name << "':\n";
        increaseIndent();
        visit(field.value.get());
        decreaseIndent();
    }
    decreaseIndent();
}

void ASTPrinter::visitMatchExpr(MatchExpr *node) {
    indent();
    os_ << "MatchExpr\n";
    increaseIndent();
    visit(const_cast<Expr *>(node->getSubject()));
    for (auto &arm : node->getArms()) {
        indent();
        os_ << "Arm '" << arm.pattern << "':\n";
        increaseIndent();
        visit(arm.body.get());
        decreaseIndent();
    }
    decreaseIndent();
}

void ASTPrinter::visitArrayLiteralExpr(ArrayLiteralExpr *node) {
    indent();
    os_ << "ArrayLiteral\n";
    increaseIndent();
    for (auto &elem : node->getElements()) {
        visit(elem.get());
    }
    decreaseIndent();
}

void ASTPrinter::visitCastExpr(CastExpr *node) {
    indent();
    os_ << "CastExpr -> " << node->getTargetType()->toString() << "\n";
    increaseIndent();
    visit(const_cast<Expr *>(node->getExpr()));
    decreaseIndent();
}

void ASTPrinter::visitRefExpr(RefExpr *node) {
    indent();
    os_ << "RefExpr" << (node->isMutable() ? " mut" : "") << "\n";
    increaseIndent();
    visit(const_cast<Expr *>(node->getExpr()));
    decreaseIndent();
}

void ASTPrinter::visitGroupExpr(GroupExpr *node) {
    indent();
    os_ << "GroupExpr\n";
    increaseIndent();
    visit(node->getExpr());
    decreaseIndent();
}

} // namespace liva
