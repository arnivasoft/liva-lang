#include "liva/Sema/TypeChecker.h"
#include "liva/Sema/ModuleLoader.h"
#include <set>

namespace liva {

TypeChecker::TypeChecker(DiagnosticsEngine &diag, ModuleLoader *loader)
    : diag_(diag), moduleLoader_(loader) { registerBuiltins(); }

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

    Symbol lenSym;
    lenSym.name = "len";
    lenSym.kind = Symbol::Kind::Function;
    scopes_.declare("len", lenSym);

    Symbol toStringSym;
    toStringSym.name = "toString";
    toStringSym.kind = Symbol::Kind::Function;
    scopes_.declare("toString", toStringSym);
}

void TypeChecker::check(TranslationUnit &tu) {
    // First pass: register all top-level declarations
    for (auto &decl : tu.getDeclarations()) {
        if (decl->getKind() == ASTNode::NodeKind::ImportDecl) {
            auto *importDecl = static_cast<ImportDecl *>(decl.get());
            if (moduleLoader_) {
                auto *mod = moduleLoader_->loadModule(
                    importDecl->getPath(), diag_, importDecl->getStartLoc());
                if (mod) {
                    for (auto &sym : mod->exportedSymbols) {
                        scopes_.declare(sym.name, sym);
                    }
                }
            }
        } else if (decl->getKind() == ASTNode::NodeKind::FuncDecl) {
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
            sym.enumDecl = enumDecl;
            if (!scopes_.declare(sym.name, sym)) {
                diag_.report(decl->getStartLoc(), DiagID::err_redefinition,
                             enumDecl->getName());
            }
        } else if (decl->getKind() == ASTNode::NodeKind::ProtocolDecl) {
            auto *protocolDecl = static_cast<ProtocolDecl *>(decl.get());
            Symbol sym;
            sym.name = protocolDecl->getName();
            sym.kind = Symbol::Kind::ProtocolType;
            sym.protocolDecl = protocolDecl;
            if (!scopes_.declare(sym.name, sym)) {
                diag_.report(decl->getStartLoc(), DiagID::err_redefinition,
                             protocolDecl->getName());
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

    // Register type parameters in scope
    for (const auto &tp : node->getTypeParams()) {
        Symbol sym;
        sym.name = tp;
        sym.kind = Symbol::Kind::TypeParam;
        scopes_.declare(tp, sym);
    }

    // Validate trait bounds reference real protocols
    for (auto &[paramName, boundProto] : node->getTypeParamBounds()) {
        auto *protoSym = scopes_.lookup(boundProto);
        if (!protoSym || protoSym->kind != Symbol::Kind::ProtocolType) {
            diag_.report(node->getStartLoc(), DiagID::err_undefined_protocol, boundProto);
        }
    }

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
    // Propagate function type annotation to untyped closure params
    if (node->hasTypeAnnotation() && node->getType() &&
        node->getType()->getKind() == TypeRepr::Kind::Function &&
        node->hasInit() &&
        node->getInit()->getKind() == ASTNode::NodeKind::ClosureExpr) {
        auto *funcType = static_cast<const FunctionTypeRepr *>(node->getType());
        auto *closure = static_cast<ClosureExpr *>(const_cast<Expr *>(node->getInit()));
        for (size_t i = 0; i < closure->getParams().size() && i < funcType->getParams().size(); ++i) {
            if (!closure->getParams()[i].type) {
                closure->setParamType(i, cloneTypeRepr(funcType->getParams()[i].get()));
            }
        }
    }

    if (node->hasInit()) {
        visit(const_cast<Expr *>(node->getInit()));
    }

    if (node->hasInit() &&
        node->getInit()->getKind() == ASTNode::NodeKind::NilLiteralExpr) {
        if (!node->hasTypeAnnotation() ||
            node->getType()->getKind() != TypeRepr::Kind::Optional) {
            diag_.report(node->getStartLoc(), DiagID::err_nil_without_optional);
        }
    }

    Symbol sym;
    sym.name = node->getName();
    sym.kind = Symbol::Kind::Variable;
    sym.type = node->getType();
    sym.isMutable = node->isMutable();

    // Propagate init's resolved type when annotation is inferred
    if ((!sym.type || sym.type->isInferred()) && node->hasInit() &&
        node->getInit()->getResolvedType()) {
        sym.type = node->getInit()->getResolvedType();
    }

    if (!scopes_.declare(sym.name, sym)) {
        diag_.report(node->getStartLoc(), DiagID::err_redefinition, node->getName());
    }
}

void TypeChecker::visitStructDecl(StructDecl *node) {
    scopes_.pushScope();
    for (const auto &tp : node->getTypeParams()) {
        Symbol sym;
        sym.name = tp;
        sym.kind = Symbol::Kind::TypeParam;
        scopes_.declare(tp, sym);
    }
    // Validate trait bounds reference real protocols
    for (auto &[paramName, boundProto] : node->getTypeParamBounds()) {
        auto *protoSym = scopes_.lookup(boundProto);
        if (!protoSym || protoSym->kind != Symbol::Kind::ProtocolType) {
            diag_.report(node->getStartLoc(), DiagID::err_undefined_protocol, boundProto);
        }
    }
    for (auto &field : node->getFields()) {
        visitFieldDecl(field.get());
    }
    scopes_.popScope();
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

    // Protocol conformance check
    if (node->hasProtocol()) {
        auto *protoSym = scopes_.lookup(node->getProtocolName());
        if (!protoSym || protoSym->kind != Symbol::Kind::ProtocolType) {
            diag_.report(node->getStartLoc(), DiagID::err_undefined_protocol,
                         node->getProtocolName());
        } else if (protoSym->protocolDecl) {
            for (auto &protoMethod : protoSym->protocolDecl->getMethods()) {
                bool found = false;
                for (auto &implMethod : node->getMethods()) {
                    if (implMethod->getName() == protoMethod->getName()) {
                        found = true;
                        break;
                    }
                }
                if (!found && !protoMethod->hasBody()) {
                    diag_.report(node->getStartLoc(),
                                 DiagID::err_missing_protocol_method,
                                 protoMethod->getName(), node->getProtocolName());
                }
            }
            // Record successful conformance
            protocolConformances_[node->getProtocolName()].push_back(node->getTypeName());
        }
    }

    scopes_.pushScope();
    for (const auto &tp : node->getTypeParams()) {
        Symbol tpSym;
        tpSym.name = tp;
        tpSym.kind = Symbol::Kind::TypeParam;
        scopes_.declare(tp, tpSym);
    }
    // Validate trait bounds reference real protocols
    for (auto &[paramName, boundProto] : node->getTypeParamBounds()) {
        auto *protoSym = scopes_.lookup(boundProto);
        if (!protoSym || protoSym->kind != Symbol::Kind::ProtocolType) {
            diag_.report(node->getStartLoc(), DiagID::err_undefined_protocol, boundProto);
        }
    }
    for (auto &method : node->getMethods()) {
        visitFuncDecl(method.get());
    }
    scopes_.popScope();
}

void TypeChecker::visitProtocolDecl(ProtocolDecl *node) {
    // Record method names in order (for vtable index)
    std::vector<std::string> methodNames;
    for (auto &method : node->getMethods()) {
        methodNames.push_back(method->getName());
        // Type-check default method bodies
        if (method->hasBody()) {
            visitFuncDecl(method.get());
        }
    }
    protocolMethods_[node->getName()] = std::move(methodNames);
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

void TypeChecker::visitIfLetStmt(IfLetStmt *node) {
    visit(node->getOptionalExpr());

    scopes_.pushScope();
    Symbol sym;
    sym.name = node->getBindingName();
    sym.kind = Symbol::Kind::Variable;
    sym.isMutable = false;
    scopes_.declare(sym.name, sym);
    visitBlockStmt(node->getThenBody());
    scopes_.popScope();

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
        // Result is a built-in type constructor, not a declared identifier
        if (node->getName() == "Result") return;
        diag_.report(node->getStartLoc(), DiagID::err_undeclared_identifier, node->getName());
        return;
    }

    if (sym->type) {
        if (sym->type->getKind() == TypeRepr::Kind::Named) {
            auto *namedType = static_cast<const NamedTypeRepr *>(sym->type);
            node->setResolvedType(makeNamedType(namedType->getName()));
        } else {
            node->setResolvedType(makePrimitiveType(sym->type->getKind()));
        }
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
    case BinaryExpr::Op::NilCoalesce:
        if (node->getRHS()->getResolvedType()) {
            node->setResolvedType(
                makePrimitiveType(node->getRHS()->getResolvedType()->getKind()));
        }
        break;
    default:
        // Arithmetic: result type matches operand types
        if (node->getLHS()->getResolvedType()) {
            if (node->getLHS()->getResolvedType()->getKind() == TypeRepr::Kind::String) {
                node->setResolvedType(makeStringType());
            } else {
                node->setResolvedType(
                    makePrimitiveType(node->getLHS()->getResolvedType()->getKind()));
            }
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

    // Propagate function param types to untyped closure args
    if (node->getCallee()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
        auto *ident = static_cast<IdentifierExpr *>(node->getCallee());
        auto *sym = scopes_.lookup(ident->getName());
        const std::vector<ParamDecl> *formalParams = nullptr;
        if (sym && sym->funcDecl)
            formalParams = &sym->funcDecl->getParams();
        if (formalParams) {
            for (size_t i = 0; i < node->getArgs().size() && i < formalParams->size(); ++i) {
                if (node->getArgs()[i]->getKind() == ASTNode::NodeKind::ClosureExpr &&
                    (*formalParams)[i].type &&
                    (*formalParams)[i].type->getKind() == TypeRepr::Kind::Function) {
                    auto *ft = static_cast<const FunctionTypeRepr *>((*formalParams)[i].type.get());
                    auto *closure = static_cast<ClosureExpr *>(node->getArgs()[i].get());
                    for (size_t j = 0; j < closure->getParams().size() && j < ft->getParams().size(); ++j) {
                        if (!closure->getParams()[j].type) {
                            closure->setParamType(j, cloneTypeRepr(ft->getParams()[j].get()));
                        }
                    }
                }
            }
        }
    }

    for (auto &arg : node->getArgs()) {
        visit(arg.get());
    }

    // Try to resolve return type from callee
    if (node->getCallee()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
        auto *ident = static_cast<IdentifierExpr *>(node->getCallee());

        if (ident->getName() == "len") {
            node->setResolvedType(makeI64Type());
        } else if (ident->getName() == "toString") {
            node->setResolvedType(makeStringType());
        } else {
            auto *sym = scopes_.lookup(ident->getName());
            if (sym && sym->type &&
                sym->type->getKind() == TypeRepr::Kind::Function) {
                auto *ft = static_cast<const FunctionTypeRepr *>(sym->type);
                if (ft->getReturnType() && !ft->getReturnType()->isVoid()) {
                    node->setResolvedType(
                        makePrimitiveType(ft->getReturnType()->getKind()));
                }
            } else if (sym && sym->funcDecl) {
                if (sym->funcDecl->isGeneric()) {
                    // Infer type parameters from argument types
                    std::unordered_map<std::string, const TypeRepr *> typeBindings;
                    const auto &typeParams = sym->funcDecl->getTypeParams();
                    const auto &formalParams = sym->funcDecl->getParams();

                    for (size_t i = 0; i < formalParams.size() && i < node->getArgs().size(); ++i) {
                        const TypeRepr *paramType = formalParams[i].type.get();
                        if (paramType && paramType->getKind() == TypeRepr::Kind::Named) {
                            auto *named = static_cast<const NamedTypeRepr *>(paramType);
                            for (const auto &tp : typeParams) {
                                if (named->getName() == tp) {
                                    const TypeRepr *argType = node->getArgs()[i]->getResolvedType();
                                    if (argType) typeBindings[tp] = argType;
                                    break;
                                }
                            }
                        }
                    }

                    // Check trait bounds
                    const auto &bounds = sym->funcDecl->getTypeParamBounds();
                    for (auto &[pName, boundProto] : bounds) {
                        auto bindIt = typeBindings.find(pName);
                        if (bindIt == typeBindings.end()) continue;
                        std::string concreteName = typeToString(bindIt->second);
                        auto confIt = protocolConformances_.find(boundProto);
                        bool conforms = false;
                        if (confIt != protocolConformances_.end()) {
                            for (const auto &t : confIt->second)
                                if (t == concreteName) { conforms = true; break; }
                        }
                        if (!conforms)
                            diag_.report(node->getStartLoc(), DiagID::err_no_conformance, concreteName, boundProto);
                    }

                    // Resolve return type
                    const TypeRepr *retType = sym->funcDecl->getReturnType();
                    if (retType && retType->getKind() == TypeRepr::Kind::Named) {
                        auto *named = static_cast<const NamedTypeRepr *>(retType);
                        auto it = typeBindings.find(named->getName());
                        if (it != typeBindings.end()) {
                            node->setResolvedType(makePrimitiveType(it->second->getKind()));
                        }
                    } else if (retType && !retType->isVoid()) {
                        node->setResolvedType(makePrimitiveType(retType->getKind()));
                    }
                } else if (sym->funcDecl->getReturnType()) {
                    node->setResolvedType(
                        makePrimitiveType(sym->funcDecl->getReturnType()->getKind()));
                }
            }
        }
    }
}

void TypeChecker::visitMemberExpr(MemberExpr *node) {
    visit(node->getObject());

    // string.length → i64
    if (node->getObject()->getResolvedType() &&
        node->getObject()->getResolvedType()->getKind() == TypeRepr::Kind::String &&
        node->getMember() == "length") {
        node->setResolvedType(makeI64Type());
    }

    // Result.isOk / Result.isErr → bool
    if (node->getMember() == "isOk" || node->getMember() == "isErr") {
        node->setResolvedType(makeBoolType());
    }

    // Result.ok / Result.err — static constructor access (no error)
    if (node->getObject()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
        auto *ident = static_cast<IdentifierExpr *>(node->getObject());
        if (ident->getName() == "Result") {
            // Result.ok or Result.err — accepted
        }
    }

    // Optional chaining: wrap resolved type in Optional
    if (node->isOptionalChain() && node->getResolvedType()) {
        auto optType = std::make_unique<OptionalTypeRepr>(
            cloneTypeRepr(node->getResolvedType()));
        node->setResolvedType(std::move(optType));
    }
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

    // Check trait bounds for generic structs
    if (sym->structDecl && sym->structDecl->isGeneric()) {
        const auto &bounds = sym->structDecl->getTypeParamBounds();
        if (!bounds.empty()) {
            // Infer type bindings from field values
            std::unordered_map<std::string, const TypeRepr *> typeBindings;
            const auto &typeParams = sym->structDecl->getTypeParams();
            const auto &fields = sym->structDecl->getFields();
            for (size_t i = 0; i < fields.size() && i < node->getFields().size(); ++i) {
                const TypeRepr *fieldType = fields[i]->getType();
                if (fieldType && fieldType->getKind() == TypeRepr::Kind::Named) {
                    auto *named = static_cast<const NamedTypeRepr *>(fieldType);
                    for (const auto &tp : typeParams) {
                        if (named->getName() == tp) {
                            const TypeRepr *argType = node->getFields()[i].value->getResolvedType();
                            if (argType) typeBindings[tp] = argType;
                            break;
                        }
                    }
                }
            }
            // Check conformance for each bound
            for (auto &[pName, boundProto] : bounds) {
                auto bindIt = typeBindings.find(pName);
                if (bindIt == typeBindings.end()) continue;
                std::string concreteName = typeToString(bindIt->second);
                auto confIt = protocolConformances_.find(boundProto);
                bool conforms = false;
                if (confIt != protocolConformances_.end()) {
                    for (const auto &t : confIt->second)
                        if (t == concreteName) { conforms = true; break; }
                }
                if (!conforms)
                    diag_.report(node->getStartLoc(), DiagID::err_no_conformance, concreteName, boundProto);
            }
        }
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
    // Propagate ReferenceTypeRepr wrapping the inner type
    if (auto *innerType = node->getExpr()->getResolvedType()) {
        auto refType = std::make_unique<ReferenceTypeRepr>(
            cloneTypeRepr(innerType), node->isMutable());
        node->setResolvedType(std::move(refType));
    }
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

void TypeChecker::visitMatchExpr(MatchExpr *node) {
    visit(const_cast<Expr *>(node->getSubject()));

    // Determine if subject is an enum type by examining arm patterns
    const EnumDecl *subjectEnum = nullptr;
    std::string enumName;
    for (auto &arm : node->getArms()) {
        auto dotPos = arm.pattern.find('.');
        if (dotPos != std::string::npos) {
            enumName = arm.pattern.substr(0, dotPos);
            auto *sym = scopes_.lookup(enumName);
            if (sym && sym->kind == Symbol::Kind::EnumType && sym->enumDecl) {
                subjectEnum = sym->enumDecl;
            }
            break;
        }
    }

    bool hasWildcard = false;
    std::set<std::string> coveredCases;

    for (auto &arm : node->getArms()) {
        // Check for wildcard
        if (arm.pattern == "_") {
            hasWildcard = true;
        } else if (subjectEnum) {
            // Extract case name from pattern like "Color.Red" or "Shape.Circle(r)"
            auto dotPos = arm.pattern.find('.');
            if (dotPos != std::string::npos) {
                auto afterDot = arm.pattern.substr(dotPos + 1);
                auto parenPos = afterDot.find('(');
                std::string caseName = (parenPos != std::string::npos)
                    ? afterDot.substr(0, parenPos)
                    : afterDot;
                if (coveredCases.count(caseName)) {
                    diag_.report(node->getStartLoc(), DiagID::warn_unreachable_match_arm,
                                 arm.pattern);
                } else {
                    coveredCases.insert(caseName);
                }
            }
        }

        if (arm.body) {
            // Extract bindings from pattern: Shape.Circle(r) -> r
            scopes_.pushScope();
            auto parenPos = arm.pattern.find('(');
            if (parenPos != std::string::npos) {
                auto closePos = arm.pattern.find(')', parenPos);
                if (closePos != std::string::npos) {
                    auto bindingStr = arm.pattern.substr(parenPos + 1,
                                                          closePos - parenPos - 1);
                    size_t start = 0;
                    while (start < bindingStr.size()) {
                        auto commaPos = bindingStr.find(',', start);
                        std::string binding;
                        if (commaPos == std::string::npos) {
                            binding = bindingStr.substr(start);
                            start = bindingStr.size();
                        } else {
                            binding = bindingStr.substr(start, commaPos - start);
                            start = commaPos + 1;
                        }
                        // Trim whitespace
                        size_t b = binding.find_first_not_of(" \t");
                        size_t e = binding.find_last_not_of(" \t");
                        if (b != std::string::npos) {
                            auto name = binding.substr(b, e - b + 1);
                            Symbol sym;
                            sym.name = name;
                            sym.kind = Symbol::Kind::Variable;
                            sym.isMutable = false;
                            scopes_.declare(name, sym);
                        }
                    }
                }
            }
            visit(arm.body.get());
            scopes_.popScope();
        }
    }

    // Exhaustiveness check for enum matches without wildcard
    if (subjectEnum && !hasWildcard) {
        for (auto &c : subjectEnum->getCases()) {
            if (!coveredCases.count(c->getName())) {
                diag_.report(node->getStartLoc(), DiagID::err_nonexhaustive_match,
                             enumName + "." + c->getName());
            }
        }
    }

    // Check for Result type match exhaustiveness
    bool isResultMatch = false;
    for (auto &arm : node->getArms()) {
        if (arm.pattern.find("Result.Ok") != std::string::npos ||
            arm.pattern.find("Result.Err") != std::string::npos) {
            isResultMatch = true;
            break;
        }
    }
    if (isResultMatch && !hasWildcard) {
        bool hasOk = false, hasErr = false;
        for (auto &arm : node->getArms()) {
            if (arm.pattern.find("Result.Ok") != std::string::npos) hasOk = true;
            if (arm.pattern.find("Result.Err") != std::string::npos) hasErr = true;
        }
        if (!hasOk) diag_.report(node->getStartLoc(), DiagID::err_nonexhaustive_match, "Result.Ok");
        if (!hasErr) diag_.report(node->getStartLoc(), DiagID::err_nonexhaustive_match, "Result.Err");
    }
}

void TypeChecker::visitRangeExpr(RangeExpr *node) {
    visit(node->getStart());
    visit(node->getEnd());
}

void TypeChecker::visitUnwrapExpr(UnwrapExpr *node) {
    visit(node->getOperand());
}

void TypeChecker::visitTryExpr(TryExpr *node) {
    visit(const_cast<Expr *>(node->getOperand()));
}

void TypeChecker::visitClosureExpr(ClosureExpr *node) {
    scopes_.pushScope();

    for (auto &param : node->getParams()) {
        Symbol sym;
        sym.name = param.name;
        sym.kind = Symbol::Kind::Parameter;
        sym.type = param.type.get();
        scopes_.declare(param.name, sym);
    }

    auto *prevReturn = currentReturnType_;
    currentReturnType_ = node->getReturnType();

    if (node->getBody()) {
        visitBlockStmt(node->getBody());
    }

    currentReturnType_ = prevReturn;
    scopes_.popScope();
}

} // namespace liva
