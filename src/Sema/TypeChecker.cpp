#include "liva/Sema/TypeChecker.h"

namespace liva {

TypeChecker::TypeChecker(DiagnosticsEngine &diag) : diag_(diag) { registerBuiltins(); }

void TypeChecker::registerBuiltins() {
    // Register built-in functions
    Symbol printSym;
    printSym.name = "print";
    printSym.kind = Symbol::Kind::Function;
    scopes_.declare("print", printSym);

    Symbol printlnSym;
    printlnSym.name = "println";
    printlnSym.kind = Symbol::Kind::Function;
    scopes_.declare("println", printlnSym);
}

void TypeChecker::check(TranslationUnit &tu) {
    // First pass: register all top-level declarations
    for (auto &decl : tu.getDeclarations()) {
        if (decl->getKind() == ASTNode::NodeKind::FuncDecl) {
            auto *funcDecl = static_cast<FuncDecl *>(decl.get());
            Symbol sym;
            sym.name = funcDecl->getName();
            sym.kind = Symbol::Kind::Function;
            sym.funcDecl = funcDecl;
            sym.type = funcDecl->getReturnType();
            if (!scopes_.declare(sym.name, sym)) {
                diag_.report(decl->getStartLoc(), DiagID::err_redefinition,
                             funcDecl->getName());
            }
        } else if (decl->getKind() == ASTNode::NodeKind::StructDecl) {
            auto *structDecl = static_cast<StructDecl *>(decl.get());
            Symbol sym;
            sym.name = structDecl->getName();
            sym.kind = Symbol::Kind::StructType;
            sym.structDecl = structDecl;
            if (!scopes_.declare(sym.name, sym)) {
                diag_.report(decl->getStartLoc(), DiagID::err_redefinition,
                             structDecl->getName());
            }
        } else if (decl->getKind() == ASTNode::NodeKind::EnumDecl) {
            auto *enumDecl = static_cast<EnumDecl *>(decl.get());
            Symbol sym;
            sym.name = enumDecl->getName();
            sym.kind = Symbol::Kind::EnumType;
            if (!scopes_.declare(sym.name, sym)) {
                diag_.report(decl->getStartLoc(), DiagID::err_redefinition,
                             enumDecl->getName());
            }
        }
    }

    // Second pass: check declarations
    for (auto &decl : tu.getDeclarations()) {
        visit(decl.get());
    }
}

void TypeChecker::visitFuncDecl(FuncDecl *node) {
    scopes_.pushScope();

    // Register parameters
    for (auto &param : node->getParams()) {
        Symbol sym;
        sym.name = param.name;
        sym.kind = Symbol::Kind::Parameter;
        sym.type = param.type.get();
        sym.isMutable = param.isMutRef;
        scopes_.declare(sym.name, sym);
    }

    currentReturnType_ = node->getReturnType();

    if (node->getBody()) {
        visitBlockStmt(const_cast<BlockStmt *>(node->getBody()));
    }

    currentReturnType_ = nullptr;
    scopes_.popScope();
}

void TypeChecker::visitVarDecl(VarDecl *node) {
    if (node->hasInit()) {
        visit(const_cast<Expr *>(node->getInit()));
    }

    Symbol sym;
    sym.name = node->getName();
    sym.kind = Symbol::Kind::Variable;
    sym.type = node->getType();
    sym.isMutable = node->isMutable();

    if (!scopes_.declare(sym.name, sym)) {
        diag_.report(node->getStartLoc(), DiagID::err_redefinition, node->getName());
    }
}

void TypeChecker::visitStructDecl(StructDecl *node) {
    for (auto &field : node->getFields()) {
        visitFieldDecl(field.get());
    }
}

void TypeChecker::visitEnumDecl(EnumDecl *node) {
    for (auto &c : node->getCases()) {
        visit(c.get());
    }
}

void TypeChecker::visitImplDecl(ImplDecl *node) {
    // Look up the struct type
    auto *sym = scopes_.lookup(node->getTypeName());
    if (!sym) {
        diag_.report(node->getStartLoc(), DiagID::err_undefined_type, node->getTypeName());
        return;
    }

    for (auto &method : node->getMethods()) {
        visitFuncDecl(method.get());
    }
}

void TypeChecker::visitProtocolDecl(ProtocolDecl *node) {
    for (auto &method : node->getMethods()) {
        // Protocol methods may not have bodies
        // Just register the signature
        (void)method;
    }
}

void TypeChecker::visitExprStmt(ExprStmt *node) { visit(node->getExpr()); }

void TypeChecker::visitReturnStmt(ReturnStmt *node) {
    if (node->hasValue()) {
        visit(node->getValue());
    }
}

void TypeChecker::visitIfStmt(IfStmt *node) {
    visit(const_cast<Expr *>(node->getCondition()));

    visit(node->getThenBody());

    if (node->hasElse()) {
        visit(node->getElseBody());
    }
}

void TypeChecker::visitWhileStmt(WhileStmt *node) {
    visit(const_cast<Expr *>(node->getCondition()));
    ++loopDepth_;
    visit(const_cast<ASTNode *>(node->getBody()));
    --loopDepth_;
}

void TypeChecker::visitForStmt(ForStmt *node) {
    visit(const_cast<Expr *>(node->getIterable()));

    scopes_.pushScope();

    Symbol sym;
    sym.name = node->getVarName();
    sym.kind = Symbol::Kind::Variable;
    sym.isMutable = false;
    scopes_.declare(sym.name, sym);

    ++loopDepth_;
    visit(const_cast<ASTNode *>(node->getBody()));
    --loopDepth_;

    scopes_.popScope();
}

void TypeChecker::visitBlockStmt(BlockStmt *node) {
    scopes_.pushScope();
    for (auto &stmt : node->getStatements()) {
        visit(stmt.get());
    }
    scopes_.popScope();
}

void TypeChecker::visitBreakStmt(BreakStmt *node) {
    if (loopDepth_ == 0) {
        diag_.report(node->getStartLoc(), DiagID::err_break_outside_loop);
    }
}

void TypeChecker::visitContinueStmt(ContinueStmt *node) {
    if (loopDepth_ == 0) {
        diag_.report(node->getStartLoc(), DiagID::err_continue_outside_loop);
    }
}

void TypeChecker::visitIntegerLiteralExpr(IntegerLiteralExpr *node) {
    node->setResolvedType(makeI32Type());
}

void TypeChecker::visitFloatLiteralExpr(FloatLiteralExpr *node) {
    node->setResolvedType(makeF64Type());
}

void TypeChecker::visitBoolLiteralExpr(BoolLiteralExpr *node) {
    node->setResolvedType(makeBoolType());
}

void TypeChecker::visitStringLiteralExpr(StringLiteralExpr *node) {
    node->setResolvedType(makeStringType());
}

void TypeChecker::visitNilLiteralExpr(NilLiteralExpr *) {
    // nil type is resolved contextually
}

void TypeChecker::visitIdentifierExpr(IdentifierExpr *node) {
    auto *sym = scopes_.lookup(node->getName());
    if (!sym) {
        diag_.report(node->getStartLoc(), DiagID::err_undeclared_identifier, node->getName());
        return;
    }

    if (sym->type) {
        node->setResolvedType(makePrimitiveType(sym->type->getKind()));
    }
}

void TypeChecker::visitBinaryExpr(BinaryExpr *node) {
    visit(node->getLHS());
    visit(node->getRHS());

    // Result type depends on operator
    switch (node->getOp()) {
    case BinaryExpr::Op::Eq:
    case BinaryExpr::Op::NotEq:
    case BinaryExpr::Op::Less:
    case BinaryExpr::Op::LessEq:
    case BinaryExpr::Op::Greater:
    case BinaryExpr::Op::GreaterEq:
    case BinaryExpr::Op::And:
    case BinaryExpr::Op::Or:
        node->setResolvedType(makeBoolType());
        break;
    default:
        // Arithmetic: result type matches operand types
        if (node->getLHS()->getResolvedType()) {
            node->setResolvedType(
                makePrimitiveType(node->getLHS()->getResolvedType()->getKind()));
        }
        break;
    }
}

void TypeChecker::visitUnaryExpr(UnaryExpr *node) {
    visit(node->getOperand());
    if (node->getOp() == UnaryExpr::Op::Not) {
        node->setResolvedType(makeBoolType());
    } else if (node->getOperand()->getResolvedType()) {
        node->setResolvedType(
            makePrimitiveType(node->getOperand()->getResolvedType()->getKind()));
    }
}

void TypeChecker::visitCallExpr(CallExpr *node) {
    visit(node->getCallee());
    for (auto &arg : node->getArgs()) {
        visit(arg.get());
    }

    // Try to resolve return type from callee
    if (node->getCallee()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
        auto *ident = static_cast<IdentifierExpr *>(node->getCallee());
        auto *sym = scopes_.lookup(ident->getName());
        if (sym && sym->funcDecl && sym->funcDecl->getReturnType()) {
            node->setResolvedType(
                makePrimitiveType(sym->funcDecl->getReturnType()->getKind()));
        }
    }
}

void TypeChecker::visitMemberExpr(MemberExpr *node) {
    visit(node->getObject());
}

void TypeChecker::visitIndexExpr(IndexExpr *node) {
    visit(const_cast<Expr *>(node->getBase()));
    visit(const_cast<Expr *>(node->getIndex()));
}

void TypeChecker::visitAssignExpr(AssignExpr *node) {
    visit(node->getTarget());
    visit(node->getValue());

    // Check mutability
    if (node->getTarget()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
        auto *ident = static_cast<IdentifierExpr *>(node->getTarget());
        auto *sym = scopes_.lookup(ident->getName());
        if (sym && !sym->isMutable) {
            diag_.report(node->getStartLoc(), DiagID::err_assign_to_immutable,
                         ident->getName());
        }
    }
}

void TypeChecker::visitStructLiteralExpr(StructLiteralExpr *node) {
    auto *sym = scopes_.lookup(node->getTypeName());
    if (!sym || sym->kind != Symbol::Kind::StructType) {
        diag_.report(node->getStartLoc(), DiagID::err_undefined_type, node->getTypeName());
        return;
    }

    for (auto &field : node->getFields()) {
        visit(field.value.get());
    }

    node->setResolvedType(makeNamedType(node->getTypeName()));
}

void TypeChecker::visitArrayLiteralExpr(ArrayLiteralExpr *node) {
    for (auto &elem : node->getElements()) {
        visit(elem.get());
    }
}

void TypeChecker::visitCastExpr(CastExpr *node) {
    visit(const_cast<Expr *>(node->getExpr()));
    node->setResolvedType(makePrimitiveType(node->getTargetType()->getKind()));
}

void TypeChecker::visitRefExpr(RefExpr *node) {
    visit(const_cast<Expr *>(node->getExpr()));
}

void TypeChecker::visitGroupExpr(GroupExpr *node) {
    visit(node->getExpr());
    if (node->getExpr()->getResolvedType()) {
        node->setResolvedType(
            makePrimitiveType(node->getExpr()->getResolvedType()->getKind()));
    }
}

bool TypeChecker::typesCompatible(const TypeRepr *expected, const TypeRepr *actual) const {
    if (!expected || !actual)
        return true;
    if (expected->isInferred() || actual->isInferred())
        return true;
    return expected->getKind() == actual->getKind();
}

std::string TypeChecker::typeToString(const TypeRepr *type) const {
    if (!type)
        return "<unknown>";
    return type->toString();
}

const TypeRepr *TypeChecker::resolveExprType(Expr *expr) {
    visit(expr);
    return expr->getResolvedType();
}

void TypeChecker::visitRangeExpr(RangeExpr *node) {
    visit(node->getStart());
    visit(node->getEnd());
}

} // namespace liva
