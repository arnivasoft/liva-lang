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

    for (auto &name : {"abs", "min", "max", "sqrt", "pow", "floor", "ceil",
                        "log", "log10", "sin", "cos", "tan", "round"}) {
        Symbol sym;
        sym.name = name;
        sym.kind = Symbol::Kind::Function;
        scopes_.declare(name, sym);
    }

    Symbol readLineSym;
    readLineSym.name = "readLine";
    readLineSym.kind = Symbol::Kind::Function;
    scopes_.declare("readLine", readLineSym);

    Symbol formatSym;
    formatSym.name = "format";
    formatSym.kind = Symbol::Kind::Function;
    scopes_.declare("format", formatSym);

    Symbol fileSym;
    fileSym.name = "File";
    fileSym.kind = Symbol::Kind::StructType;
    scopes_.declare("File", fileSym);

    for (auto &name : {"parseInt", "parseInt64", "parseFloat"}) {
        Symbol sym;
        sym.name = name;
        sym.kind = Symbol::Kind::Function;
        scopes_.declare(name, sym);
    }

    // Stdlib: Random, Process/Env, Date/Time, Regex, Networking
    for (auto &name : {"randInt", "randFloat", "env", "exit", "args",
                        "clock", "clockMs", "sleep",
                        "regexMatch", "regexFind", "regexFindAll", "regexReplace",
                        "httpGet", "httpPost"}) {
        Symbol sym;
        sym.name = name;
        sym.kind = Symbol::Kind::Function;
        scopes_.declare(name, sym);
    }
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
            // Track async functions
            if (funcDecl->isAsync()) {
                asyncFuncNames_.insert(funcDecl->getName());
            }
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
        } else if (decl->getKind() == ASTNode::NodeKind::TypeAliasDecl) {
            auto *aliasDecl = static_cast<TypeAliasDecl *>(decl.get());
            Symbol sym;
            sym.name = aliasDecl->getName();
            sym.kind = Symbol::Kind::TypeAlias;
            sym.aliasTarget = aliasDecl->getTargetType();
            if (!scopes_.declare(sym.name, sym)) {
                diag_.report(decl->getStartLoc(), DiagID::err_redefinition,
                             aliasDecl->getName());
            }
            typeAliases_[aliasDecl->getName()] = aliasDecl->getTargetType();
        }
    }

    // Second pass: check declarations
    for (auto &decl : tu.getDeclarations()) {
        visit(decl.get());
    }
}

void TypeChecker::visitFuncDecl(FuncDecl *node) {
    scopes_.pushScope();

    // Save/restore async state
    bool prevIsAsync = currentIsAsync_;
    currentIsAsync_ = node->isAsync();

    // Register type parameters in scope
    for (const auto &tp : node->getTypeParams()) {
        Symbol sym;
        sym.name = tp;
        sym.kind = Symbol::Kind::TypeParam;
        scopes_.declare(tp, sym);
    }

    // Validate trait bounds reference real protocols
    for (auto &[paramName, boundProtos] : node->getTypeParamBounds()) {
        for (auto &boundProto : boundProtos) {
            auto *protoSym = scopes_.lookup(boundProto);
            if (!protoSym || protoSym->kind != Symbol::Kind::ProtocolType) {
                diag_.report(node->getStartLoc(), DiagID::err_undefined_protocol, boundProto);
            }
        }
    }

    // Validate variadic parameters
    bool seenVariadic = false;
    for (size_t pi = 0; pi < node->getParams().size(); ++pi) {
        auto &param = node->getParams()[pi];
        if (param.isVariadic) {
            if (seenVariadic) {
                diag_.report(param.location, DiagID::err_multiple_variadic, param.name);
            }
            seenVariadic = true;
            // Check it's the last non-self param
            bool isLast = true;
            for (size_t pj = pi + 1; pj < node->getParams().size(); ++pj) {
                if (!node->getParams()[pj].isSelf) {
                    isLast = false;
                    break;
                }
            }
            if (!isLast) {
                diag_.report(param.location, DiagID::err_variadic_not_last, param.name);
            }
        }
    }

    // Register parameters
    for (auto &param : node->getParams()) {
        Symbol sym;
        sym.name = param.name;
        sym.kind = Symbol::Kind::Parameter;
        if (param.isVariadic && param.type) {
            // Variadic param: type in scope is [T] (DynArray)
            auto arrType = std::make_unique<ArrayTypeRepr>(cloneTypeRepr(param.type.get()), -1);
            sym.type = arrType.get();
            variadicArrayTypes_.push_back(std::move(arrType));
        } else {
            sym.type = param.type.get();
        }
        sym.isMutable = param.isMutRef;
        scopes_.declare(sym.name, sym);
    }

    currentReturnType_ = node->getReturnType();

    if (node->getBody()) {
        visitBlockStmt(const_cast<BlockStmt *>(node->getBody()));
    }

    currentReturnType_ = nullptr;
    currentIsAsync_ = prevIsAsync;
    scopes_.popScope();
}

void TypeChecker::visitVarDecl(VarDecl *node) {
    // Const declaration: compile-time constant
    if (node->isConst()) {
        if (!node->hasInit()) {
            diag_.report(node->getStartLoc(), DiagID::err_const_requires_init);
            return;
        }
        visit(const_cast<Expr *>(node->getInit()));
        auto constVal = evaluateConstExpr(node->getInit());
        if (!constVal) {
            diag_.report(node->getStartLoc(), DiagID::err_const_init_not_constant);
            return;
        }
        constValues_[node->getName()] = *constVal;

        Symbol sym;
        sym.name = node->getName();
        sym.kind = Symbol::Kind::Variable;
        sym.type = node->getType();
        sym.isMutable = false;
        sym.isConstant = true;
        if ((!sym.type || sym.type->isInferred()) && node->getInit()->getResolvedType()) {
            sym.type = node->getInit()->getResolvedType();
        }
        if (!scopes_.declare(sym.name, sym)) {
            diag_.report(node->getStartLoc(), DiagID::err_redefinition, node->getName());
        }
        return;
    }

    // Tuple destructuring: let (x, y) = expr
    if (node->isDestructured()) {
        if (node->hasInit()) {
            visit(const_cast<Expr *>(node->getInit()));
            auto *initType = node->getInit()->getResolvedType();
            if (initType && initType->getKind() == TypeRepr::Kind::Tuple) {
                auto *tupleType = static_cast<const TupleTypeRepr *>(initType);
                if (tupleType->getArity() == node->getDestructuredNames().size()) {
                    for (size_t i = 0; i < node->getDestructuredNames().size(); ++i) {
                        Symbol sym;
                        sym.name = node->getDestructuredNames()[i];
                        sym.kind = Symbol::Kind::Variable;
                        sym.isMutable = node->isMutable();
                        sym.type = tupleType->getElements()[i].get();
                        scopes_.declare(sym.name, sym);
                    }
                } else {
                    diag_.report(node->getStartLoc(), DiagID::err_tuple_arity_mismatch,
                                 std::to_string(tupleType->getArity()),
                                 std::to_string(node->getDestructuredNames().size()));
                }
            }
        }
        return;
    }

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

    // Track File-typed variables for method resolution
    if (sym.type && sym.type->getKind() == TypeRepr::Kind::Named) {
        auto *named = static_cast<const NamedTypeRepr *>(sym.type);
        if (named->getName() == "File") {
            fileVariables_.insert(node->getName());
        }
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
    for (auto &[paramName, boundProtos] : node->getTypeParamBounds()) {
        for (auto &boundProto : boundProtos) {
            auto *protoSym = scopes_.lookup(boundProto);
            if (!protoSym || protoSym->kind != Symbol::Kind::ProtocolType) {
                diag_.report(node->getStartLoc(), DiagID::err_undefined_protocol, boundProto);
            }
        }
    }
    for (auto &field : node->getFields()) {
        visitFieldDecl(field.get());
    }
    scopes_.popScope();
}

void TypeChecker::visitTypeAliasDecl(TypeAliasDecl *node) {
    // Validate that target type is a known type
    auto *target = node->getTargetType();
    if (target && target->getKind() == TypeRepr::Kind::Named) {
        auto *named = static_cast<const NamedTypeRepr *>(target);
        auto *sym = scopes_.lookup(named->getName());
        if (!sym) {
            diag_.report(node->getStartLoc(), DiagID::err_undefined_type, named->getName());
        }
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
            // Check associated types are provided
            auto assocIt = protocolAssociatedTypes_.find(node->getProtocolName());
            if (assocIt != protocolAssociatedTypes_.end()) {
                for (auto &assocTypeName : assocIt->second) {
                    auto &implAssocTypes = node->getAssociatedTypes();
                    if (implAssocTypes.find(assocTypeName) == implAssocTypes.end()) {
                        diag_.report(node->getStartLoc(),
                                     DiagID::err_missing_associated_type,
                                     node->getTypeName(), assocTypeName,
                                     node->getProtocolName());
                    }
                }
            }
            // Record successful conformance
            protocolConformances_[node->getProtocolName()].push_back(node->getTypeName());

            // Iter protocol: extract element type from next() -> T?
            if (node->getProtocolName() == "Iter") {
                for (auto &method : node->getMethods()) {
                    if (method->getName() == "next") {
                        auto *retType = method->getReturnType();
                        if (retType && retType->getKind() == TypeRepr::Kind::Optional) {
                            auto *optType = static_cast<const OptionalTypeRepr *>(retType);
                            iteratorElementTypes_[node->getTypeName()] = optType->getInner();
                        }
                        break;
                    }
                }
            }

            // Drop protocol validation
            if (node->getProtocolName() == "Drop") {
                bool validDrop = false;
                for (auto &method : node->getMethods()) {
                    if (method->getName() == "drop" && method->isMethod() &&
                        method->getParams().size() == 1 &&
                        (!method->getReturnType() || method->getReturnType()->isVoid())) {
                        validDrop = true;
                    }
                }
                if (!validDrop && !node->getMethods().empty()) {
                    diag_.report(node->getStartLoc(), DiagID::err_drop_method_signature);
                }
            }
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
    for (auto &[paramName, boundProtos] : node->getTypeParamBounds()) {
        for (auto &boundProto : boundProtos) {
            auto *protoSym = scopes_.lookup(boundProto);
            if (!protoSym || protoSym->kind != Symbol::Kind::ProtocolType) {
                diag_.report(node->getStartLoc(), DiagID::err_undefined_protocol, boundProto);
            }
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

    // Record associated type names for this protocol
    if (!node->getAssociatedTypes().empty()) {
        protocolAssociatedTypes_[node->getName()] = node->getAssociatedTypes();
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

void TypeChecker::visitIfLetStmt(IfLetStmt *node) {
    visit(node->getOptionalExpr());

    scopes_.pushScope();
    Symbol sym;
    sym.name = node->getBindingName();
    sym.kind = Symbol::Kind::Variable;
    sym.isMutable = false;

    // If unwrapping File.open() result, mark binding as File-typed
    if (node->getOptionalExpr()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
        auto *ident = static_cast<IdentifierExpr *>(node->getOptionalExpr());
        auto *optSym = scopes_.lookup(ident->getName());
        if (optSym && optSym->type &&
            optSym->type->getKind() == TypeRepr::Kind::Optional) {
            auto *optTy = static_cast<const OptionalTypeRepr *>(optSym->type);
            if (optTy->getInner()->getKind() == TypeRepr::Kind::Named) {
                auto *namedInner = static_cast<const NamedTypeRepr *>(optTy->getInner());
                if (namedInner->getName() == "File") {
                    sym.type = optTy->getInner();
                    fileVariables_.insert(node->getBindingName());
                }
            }
        }
    }
    // Also handle direct File.open() call: if let file = File.open(...)
    if (node->getOptionalExpr()->getKind() == ASTNode::NodeKind::CallExpr) {
        auto *callExpr = static_cast<CallExpr *>(node->getOptionalExpr());
        if (callExpr->getCallee()->getKind() == ASTNode::NodeKind::MemberExpr) {
            auto *me = static_cast<MemberExpr *>(callExpr->getCallee());
            if (me->getObject()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
                auto *id = static_cast<IdentifierExpr *>(me->getObject());
                if (id->getName() == "File" && me->getMember() == "open") {
                    fileVariables_.insert(node->getBindingName());
                }
            }
        }
    }

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

void TypeChecker::visitWhileLetStmt(WhileLetStmt *node) {
    visit(node->getOptionalExpr());

    scopes_.pushScope();
    Symbol sym;
    sym.name = node->getBindingName();
    sym.kind = Symbol::Kind::Variable;
    sym.isMutable = false;

    // Unwrap optional type for binding — look up from scope, not resolvedType
    if (node->getOptionalExpr()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
        auto *ident = static_cast<IdentifierExpr *>(node->getOptionalExpr());
        auto *optSym = scopes_.lookup(ident->getName());
        if (optSym && optSym->type &&
            optSym->type->getKind() == TypeRepr::Kind::Optional) {
            auto *optTy = static_cast<const OptionalTypeRepr *>(optSym->type);
            sym.type = optTy->getInner();
        }
    }

    scopes_.declare(sym.name, sym);
    ++loopDepth_;
    visitBlockStmt(node->getBody());
    --loopDepth_;
    scopes_.popScope();
}

void TypeChecker::visitForStmt(ForStmt *node) {
    visit(const_cast<Expr *>(node->getIterable()));

    scopes_.pushScope();

    // Determine element type from iterable
    const TypeRepr *iterableType = nullptr;

    // If iterable is an identifier, look up its declared type
    if (node->getIterable()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
        auto *ident = static_cast<const IdentifierExpr *>(node->getIterable());
        auto *sym = scopes_.lookup(ident->getName());
        if (sym && sym->type) {
            iterableType = sym->type;
        }
    }
    // Also check resolved type from visit
    if (!iterableType && node->getIterable()->getResolvedType()) {
        iterableType = node->getIterable()->getResolvedType();
    }

    if (node->hasTuplePattern()) {
        // Tuple pattern: (k, v) in map — must be Map<K,V>
        bool isMap = false;
        if (iterableType && iterableType->getKind() == TypeRepr::Kind::Generic) {
            auto *genType = static_cast<const GenericTypeRepr *>(iterableType);
            if (genType->getBaseName() == "Map" && genType->getTypeArgs().size() >= 2) {
                isMap = true;
                Symbol sym1;
                sym1.name = node->getVarName();
                sym1.kind = Symbol::Kind::Variable;
                sym1.isMutable = false;
                sym1.type = genType->getTypeArgs()[0].get();
                scopes_.declare(sym1.name, sym1);

                Symbol sym2;
                sym2.name = node->getVarName2();
                sym2.kind = Symbol::Kind::Variable;
                sym2.isMutable = false;
                sym2.type = genType->getTypeArgs()[1].get();
                scopes_.declare(sym2.name, sym2);
            }
        }
        if (!isMap) {
            diag_.report(node->getStartLoc(), DiagID::err_tuple_for_requires_map);
            // Still declare variables to avoid cascading errors
            Symbol sym1;
            sym1.name = node->getVarName();
            sym1.kind = Symbol::Kind::Variable;
            sym1.isMutable = false;
            scopes_.declare(sym1.name, sym1);
            Symbol sym2;
            sym2.name = node->getVarName2();
            sym2.kind = Symbol::Kind::Variable;
            sym2.isMutable = false;
            scopes_.declare(sym2.name, sym2);
        }
    } else {
        // Single variable pattern
        Symbol sym;
        sym.name = node->getVarName();
        sym.kind = Symbol::Kind::Variable;
        sym.isMutable = false;

        if (iterableType) {
            // Dynamic array [T] → element type T
            if (iterableType->getKind() == TypeRepr::Kind::Array) {
                auto *arrType = static_cast<const ArrayTypeRepr *>(iterableType);
                if (arrType->isDynamic()) {
                    sym.type = arrType->getElement();
                }
            }
            // Map<K,V> → key type K
            else if (iterableType->getKind() == TypeRepr::Kind::Generic) {
                auto *genType = static_cast<const GenericTypeRepr *>(iterableType);
                if (genType->getBaseName() == "Map" && genType->getTypeArgs().size() >= 1) {
                    sym.type = genType->getTypeArgs()[0].get();
                } else if (genType->getBaseName() == "Set" && genType->getTypeArgs().size() >= 1) {
                    sym.type = genType->getTypeArgs()[0].get();
                }
            }
            // Custom Iterator (Iter protocol): Named type → check conformance
            else if (iterableType->getKind() == TypeRepr::Kind::Named) {
                auto *namedType = static_cast<const NamedTypeRepr *>(iterableType);
                const std::string &typeName = namedType->getName();
                auto confIt = protocolConformances_.find("Iter");
                if (confIt != protocolConformances_.end()) {
                    for (auto &t : confIt->second) {
                        if (t == typeName) {
                            auto elemIt = iteratorElementTypes_.find(typeName);
                            if (elemIt != iteratorElementTypes_.end()) {
                                sym.type = elemIt->second;
                            }
                            break;
                        }
                    }
                }
            }
        }

        scopes_.declare(sym.name, sym);
    }

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
        } else if (sym->type->getKind() == TypeRepr::Kind::Tuple ||
                   sym->type->getKind() == TypeRepr::Kind::Optional ||
                   sym->type->getKind() == TypeRepr::Kind::Array ||
                   sym->type->getKind() == TypeRepr::Kind::Function ||
                   sym->type->getKind() == TypeRepr::Kind::Result ||
                   sym->type->getKind() == TypeRepr::Kind::Reference) {
            node->setResolvedType(cloneTypeRepr(sym->type));
        } else {
            node->setResolvedType(makePrimitiveType(sym->type->getKind()));
        }
    }
}

namespace {
struct OpProtoInfo { const char *proto; const char *method; };
const OpProtoInfo *getOpProto(BinaryExpr::Op op) {
    static const std::pair<BinaryExpr::Op, OpProtoInfo> table[] = {
        {BinaryExpr::Op::Add,       {"Add", "add"}},
        {BinaryExpr::Op::Sub,       {"Sub", "sub"}},
        {BinaryExpr::Op::Mul,       {"Mul", "mul"}},
        {BinaryExpr::Op::Div,       {"Div", "div"}},
        {BinaryExpr::Op::Mod,       {"Mod", "mod"}},
        {BinaryExpr::Op::Eq,        {"Eq", "eq"}},
        {BinaryExpr::Op::NotEq,     {"Eq", "eq"}},
        {BinaryExpr::Op::Less,      {"Less", "less"}},
        {BinaryExpr::Op::LessEq,    {"Less", "less"}},
        {BinaryExpr::Op::Greater,   {"Less", "less"}},
        {BinaryExpr::Op::GreaterEq, {"Less", "less"}},
    };
    for (auto &[o, info] : table)
        if (o == op) return &info;
    return nullptr;
}
} // anon

void TypeChecker::visitBinaryExpr(BinaryExpr *node) {
    visit(node->getLHS());
    visit(node->getRHS());

    // Struct operator overload dispatch
    auto *lhsType = node->getLHS()->getResolvedType();
    if (lhsType && lhsType->getKind() == TypeRepr::Kind::Named) {
        auto *named = static_cast<const NamedTypeRepr *>(lhsType);
        const std::string &typeName = named->getName();
        auto *info = getOpProto(node->getOp());
        if (info) {
            bool conforms = false;
            auto confIt = protocolConformances_.find(std::string(info->proto));
            if (confIt != protocolConformances_.end()) {
                for (auto &t : confIt->second)
                    if (t == typeName) { conforms = true; break; }
            }
            if (conforms) {
                switch (node->getOp()) {
                case BinaryExpr::Op::Eq: case BinaryExpr::Op::NotEq:
                case BinaryExpr::Op::Less: case BinaryExpr::Op::LessEq:
                case BinaryExpr::Op::Greater: case BinaryExpr::Op::GreaterEq:
                    node->setResolvedType(makeBoolType());
                    break;
                default:
                    node->setResolvedType(makeNamedType(typeName));
                    break;
                }
                return;
            }
            diag_.report(node->getStartLoc(), DiagID::err_binary_op_on_struct,
                         node->getOpSpelling(), typeName, info->proto);
            return;
        }
    }

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

    // Propagate DynArray element type to closure params for map/filter/forEach
    if (node->getCallee()->getKind() == ASTNode::NodeKind::MemberExpr) {
        auto *memberExpr = static_cast<MemberExpr *>(node->getCallee());
        const std::string &methodName = memberExpr->getMember();
        if ((methodName == "map" || methodName == "filter" || methodName == "forEach") &&
            !node->getArgs().empty() &&
            node->getArgs()[0]->getKind() == ASTNode::NodeKind::ClosureExpr) {
            // Look up array element type
            if (memberExpr->getObject()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
                auto *ident = static_cast<IdentifierExpr *>(memberExpr->getObject());
                auto *sym = scopes_.lookup(ident->getName());
                if (sym && sym->type && sym->type->getKind() == TypeRepr::Kind::Array) {
                    auto *arrType = static_cast<const ArrayTypeRepr *>(sym->type);
                    if (arrType->isDynamic() && arrType->getElement()) {
                        auto *closure = static_cast<ClosureExpr *>(node->getArgs()[0].get());
                        if (!closure->getParams().empty() && !closure->getParams()[0].type) {
                            closure->setParamType(0, cloneTypeRepr(arrType->getElement()));
                        }
                    }
                }
            }
        }
        // reduce(init, |acc, x| -> T { ... }) — set x param type from element type
        if (methodName == "reduce" && node->getArgs().size() >= 2 &&
            node->getArgs()[1]->getKind() == ASTNode::NodeKind::ClosureExpr) {
            if (memberExpr->getObject()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
                auto *ident = static_cast<IdentifierExpr *>(memberExpr->getObject());
                auto *sym = scopes_.lookup(ident->getName());
                if (sym && sym->type && sym->type->getKind() == TypeRepr::Kind::Array) {
                    auto *arrType = static_cast<const ArrayTypeRepr *>(sym->type);
                    if (arrType->isDynamic() && arrType->getElement()) {
                        auto *closure = static_cast<ClosureExpr *>(node->getArgs()[1].get());
                        // param 1 (x) = element type
                        if (closure->getParams().size() >= 2 && !closure->getParams()[1].type) {
                            closure->setParamType(1, cloneTypeRepr(arrType->getElement()));
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

        if (ident->getName() == "readLine") {
            node->setResolvedType(makeStringType());
        } else if (ident->getName() == "format") {
            node->setResolvedType(makeStringType());
        } else if (ident->getName() == "len") {
            node->setResolvedType(makeI64Type());
        } else if (ident->getName() == "toString") {
            node->setResolvedType(makeStringType());
        } else if (ident->getName() == "abs" || ident->getName() == "min" ||
                   ident->getName() == "max") {
            // abs/min/max return the same type as their first argument
            if (!node->getArgs().empty() && node->getArgs()[0]->getResolvedType()) {
                node->setResolvedType(
                    makePrimitiveType(node->getArgs()[0]->getResolvedType()->getKind()));
            }
        } else if (ident->getName() == "sqrt" || ident->getName() == "pow" ||
                   ident->getName() == "floor" || ident->getName() == "ceil" ||
                   ident->getName() == "log" || ident->getName() == "log10" ||
                   ident->getName() == "sin" || ident->getName() == "cos" ||
                   ident->getName() == "tan" || ident->getName() == "round") {
            // sqrt/pow/floor/ceil/log/log10/sin/cos/tan/round always return f64
            node->setResolvedType(makeF64Type());
        } else if (ident->getName() == "parseInt") {
            auto optType = std::make_unique<OptionalTypeRepr>(makeI32Type());
            node->setResolvedType(std::move(optType));
        } else if (ident->getName() == "parseInt64") {
            auto optType = std::make_unique<OptionalTypeRepr>(makeI64Type());
            node->setResolvedType(std::move(optType));
        } else if (ident->getName() == "parseFloat") {
            auto optType = std::make_unique<OptionalTypeRepr>(makeF64Type());
            node->setResolvedType(std::move(optType));
        // Stdlib: Random
        } else if (ident->getName() == "randInt") {
            node->setResolvedType(makeI32Type());
        } else if (ident->getName() == "randFloat") {
            node->setResolvedType(makeF64Type());
        // Stdlib: Process/Env
        } else if (ident->getName() == "env") {
            auto optType = std::make_unique<OptionalTypeRepr>(makeStringType());
            node->setResolvedType(std::move(optType));
        } else if (ident->getName() == "exit") {
            // void — no resolved type needed
        } else if (ident->getName() == "args") {
            auto arrType = std::make_unique<ArrayTypeRepr>(makeStringType(), true);
            node->setResolvedType(std::move(arrType));
        // Stdlib: Date/Time
        } else if (ident->getName() == "clock") {
            node->setResolvedType(makeF64Type());
        } else if (ident->getName() == "clockMs") {
            node->setResolvedType(makeI64Type());
        } else if (ident->getName() == "sleep") {
            // void — no resolved type needed
        // Stdlib: Regex
        } else if (ident->getName() == "regexMatch") {
            node->setResolvedType(makeBoolType());
        } else if (ident->getName() == "regexFind") {
            auto optType = std::make_unique<OptionalTypeRepr>(makeStringType());
            node->setResolvedType(std::move(optType));
        } else if (ident->getName() == "regexFindAll") {
            auto arrType = std::make_unique<ArrayTypeRepr>(makeStringType(), true);
            node->setResolvedType(std::move(arrType));
        } else if (ident->getName() == "regexReplace") {
            node->setResolvedType(makeStringType());
        // Stdlib: Networking
        } else if (ident->getName() == "httpGet" || ident->getName() == "httpPost") {
            auto optType = std::make_unique<OptionalTypeRepr>(makeStringType());
            node->setResolvedType(std::move(optType));
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
                    for (auto &[pName, boundProtos] : bounds) {
                        auto bindIt = typeBindings.find(pName);
                        if (bindIt == typeBindings.end()) continue;
                        std::string concreteName = typeToString(bindIt->second);
                        for (auto &boundProto : boundProtos) {
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

                    // Resolve return type
                    const TypeRepr *retType = resolveAlias(sym->funcDecl->getReturnType());
                    if (retType && retType->getKind() == TypeRepr::Kind::Named) {
                        auto *named = static_cast<const NamedTypeRepr *>(retType);
                        auto it = typeBindings.find(named->getName());
                        if (it != typeBindings.end()) {
                            node->setResolvedType(makePrimitiveType(it->second->getKind()));
                        } else {
                            node->setResolvedType(makeNamedType(named->getName()));
                        }
                    } else if (retType && !retType->isVoid()) {
                        node->setResolvedType(cloneTypeRepr(retType));
                    }
                } else if (sym->funcDecl->getReturnType()) {
                    const TypeRepr *retType = resolveAlias(sym->funcDecl->getReturnType());
                    node->setResolvedType(cloneTypeRepr(retType));
                }
            }
        }

        // Wrap return type in Task<T> for async function calls
        if (asyncFuncNames_.count(ident->getName()) && node->getResolvedType()) {
            std::vector<std::unique_ptr<TypeRepr>> taskArgs;
            taskArgs.push_back(cloneTypeRepr(node->getResolvedType()));
            node->setResolvedType(std::make_unique<GenericTypeRepr>("Task", std::move(taskArgs)));
        }
    }

    // Map/Set method call resolution: m.insert(), m.get(), m.contains(), m.remove()
    if (node->getCallee()->getKind() == ASTNode::NodeKind::MemberExpr) {
        auto *memberExpr = static_cast<MemberExpr *>(node->getCallee());
        const std::string &methodName = memberExpr->getMember();

        if (memberExpr->getObject()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
            auto *ident = static_cast<IdentifierExpr *>(memberExpr->getObject());
            auto *sym = scopes_.lookup(ident->getName());
            if (sym && sym->type && sym->type->getKind() == TypeRepr::Kind::Generic) {
                auto *genType = static_cast<const GenericTypeRepr *>(sym->type);
                if (genType->getBaseName() == "Map") {
                    if (methodName == "get" && genType->getTypeArgs().size() >= 2) {
                        // get() returns Optional<V>
                        auto optType = std::make_unique<OptionalTypeRepr>(
                            cloneTypeRepr(genType->getTypeArgs()[1].get()));
                        node->setResolvedType(std::move(optType));
                    } else if (methodName == "contains" || methodName == "remove") {
                        node->setResolvedType(makeBoolType());
                    }
                    // insert() returns void — no resolved type needed
                } else if (genType->getBaseName() == "Set") {
                    if (methodName == "contains" || methodName == "remove") {
                        node->setResolvedType(makeBoolType());
                    }
                    // insert() returns void
                }
            }

            // DynArray method calls: arr.contains(), arr.indexOf(), arr.reverse()
            if (sym && sym->type && sym->type->getKind() == TypeRepr::Kind::Array) {
                auto *arrType = static_cast<const ArrayTypeRepr *>(sym->type);
                if (arrType->isDynamic()) {
                    if (methodName == "contains") {
                        node->setResolvedType(makeBoolType());
                    } else if (methodName == "indexOf") {
                        node->setResolvedType(makeI64Type());
                    } else if (methodName == "filter") {
                        // filter returns same type array [T]
                        auto filterArr = std::make_unique<ArrayTypeRepr>(
                            cloneTypeRepr(arrType->getElement()));
                        node->setResolvedType(std::move(filterArr));
                    } else if (methodName == "map" && !node->getArgs().empty()) {
                        // map returns [ClosureReturnType]
                        auto *closureArg = node->getArgs()[0].get();
                        if (closureArg->getKind() == ASTNode::NodeKind::ClosureExpr) {
                            auto *closure = static_cast<ClosureExpr *>(closureArg);
                            if (closure->getReturnType()) {
                                auto mapArr = std::make_unique<ArrayTypeRepr>(
                                    cloneTypeRepr(closure->getReturnType()));
                                node->setResolvedType(std::move(mapArr));
                            }
                        }
                    } else if (methodName == "reduce" && node->getArgs().size() >= 2) {
                        // reduce(init, closure) returns init's type
                        if (node->getArgs()[0]->getResolvedType()) {
                            node->setResolvedType(makePrimitiveType(
                                node->getArgs()[0]->getResolvedType()->getKind()));
                        }
                    }
                    // push(), pop(), reverse(), forEach() return void
                }
            }

            // File.open() static method
            if (ident->getName() == "File" && methodName == "open") {
                auto optType = std::make_unique<OptionalTypeRepr>(makeNamedType("File"));
                node->setResolvedType(std::move(optType));
            }

            // File instance methods
            if (fileVariables_.count(ident->getName())) {
                if (methodName == "readLine") {
                    auto optType = std::make_unique<OptionalTypeRepr>(makeStringType());
                    node->setResolvedType(std::move(optType));
                } else if (methodName == "readAll") {
                    node->setResolvedType(makeStringType());
                }
                // write, writeLine, close → void (no resolved type)
            }

            // String method calls: s.contains(), s.startsWith(), etc.
            if (sym && sym->type && sym->type->getKind() == TypeRepr::Kind::String) {
                if (methodName == "contains" || methodName == "startsWith" ||
                    methodName == "endsWith") {
                    node->setResolvedType(makeBoolType());
                } else if (methodName == "indexOf") {
                    node->setResolvedType(makeI64Type());
                } else if (methodName == "substring" || methodName == "trim" ||
                           methodName == "toUpper" || methodName == "toLower" ||
                           methodName == "replace") {
                    node->setResolvedType(makeStringType());
                }
                else if (methodName == "split") {
                    // split() returns [string] — dynamic array
                    auto arrType = std::make_unique<ArrayTypeRepr>(makeStringType());
                    node->setResolvedType(std::move(arrType));
                }
            }
        }

        // String method calls on expressions with resolved string type
        if (memberExpr->getObject()->getResolvedType() &&
            memberExpr->getObject()->getResolvedType()->getKind() == TypeRepr::Kind::String) {
            if (methodName == "contains" || methodName == "startsWith" ||
                methodName == "endsWith") {
                node->setResolvedType(makeBoolType());
            } else if (methodName == "indexOf") {
                node->setResolvedType(makeI64Type());
            } else if (methodName == "substring" || methodName == "trim" ||
                       methodName == "toUpper" || methodName == "toLower" ||
                       methodName == "replace") {
                node->setResolvedType(makeStringType());
            } else if (methodName == "split") {
                auto arrType = std::make_unique<ArrayTypeRepr>(makeStringType());
                node->setResolvedType(std::move(arrType));
            }
        }
    }
}

void TypeChecker::visitMemberExpr(MemberExpr *node) {
    visit(node->getObject());

    // Tuple element access: tuple.0, tuple.1
    auto *baseType = node->getObject()->getResolvedType();
    if (baseType && baseType->getKind() == TypeRepr::Kind::Tuple) {
        auto *tupleType = static_cast<const TupleTypeRepr *>(baseType);
        const auto &member = node->getMember();
        // Check if member is a numeric index
        bool isNumeric = !member.empty();
        for (char c : member) {
            if (c < '0' || c > '9') { isNumeric = false; break; }
        }
        if (isNumeric) {
            long idx = strtol(member.c_str(), nullptr, 10);
            if (idx >= 0 && (size_t)idx < tupleType->getArity()) {
                node->setResolvedType(cloneTypeRepr(tupleType->getElements()[idx].get()));
            } else {
                diag_.report(node->getStartLoc(), DiagID::err_tuple_index_out_of_range,
                             member, std::to_string(tupleType->getArity()));
            }
            return;
        }
    }

    // string.length → i64
    if (node->getObject()->getResolvedType() &&
        node->getObject()->getResolvedType()->getKind() == TypeRepr::Kind::String &&
        node->getMember() == "length") {
        node->setResolvedType(makeI64Type());
    }

    // DynArray .length → i64, .isEmpty → bool
    if (node->getObject()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
        auto *ident = static_cast<IdentifierExpr *>(node->getObject());
        auto *sym = scopes_.lookup(ident->getName());
        if (sym && sym->type && sym->type->getKind() == TypeRepr::Kind::Array) {
            auto *arrType = static_cast<const ArrayTypeRepr *>(sym->type);
            if (arrType->isDynamic()) {
                if (node->getMember() == "length") {
                    node->setResolvedType(makeI64Type());
                } else if (node->getMember() == "isEmpty") {
                    node->setResolvedType(makeBoolType());
                }
            }
        }
    }

    // Map/Set .size → i64, .isEmpty → bool
    if (node->getObject()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
        auto *ident = static_cast<IdentifierExpr *>(node->getObject());
        auto *sym = scopes_.lookup(ident->getName());
        if (sym && sym->type && sym->type->getKind() == TypeRepr::Kind::Generic) {
            auto *genType = static_cast<const GenericTypeRepr *>(sym->type);
            if (genType->getBaseName() == "Map" || genType->getBaseName() == "Set") {
                if (node->getMember() == "size") {
                    node->setResolvedType(makeI64Type());
                } else if (node->getMember() == "isEmpty") {
                    node->setResolvedType(makeBoolType());
                }
            }
        }
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

    // String indexing/slicing: s[i] -> string, s[1..3] -> string
    if (node->getBase()->getResolvedType() &&
        node->getBase()->getResolvedType()->getKind() == TypeRepr::Kind::String) {
        node->setResolvedType(makePrimitiveType(TypeRepr::Kind::String));
    }

    // Array slicing: arr[1..3] -> same array type
    if (node->getIndex()->getKind() == ASTNode::NodeKind::RangeExpr &&
        node->getBase()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
        auto *ident = static_cast<IdentifierExpr *>(const_cast<Expr *>(node->getBase()));
        auto *sym = scopes_.lookup(ident->getName());
        if (sym && sym->type && sym->type->getKind() == TypeRepr::Kind::Array) {
            node->setResolvedType(cloneTypeRepr(sym->type));
        }
    }
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
    // Resolve type alias to underlying struct
    if (sym && sym->kind == Symbol::Kind::TypeAlias && sym->aliasTarget) {
        if (sym->aliasTarget->getKind() == TypeRepr::Kind::Named) {
            auto *named = static_cast<const NamedTypeRepr *>(sym->aliasTarget);
            sym = scopes_.lookup(named->getName());
        }
    }
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
            for (auto &[pName, boundProtos] : bounds) {
                auto bindIt = typeBindings.find(pName);
                if (bindIt == typeBindings.end()) continue;
                std::string concreteName = typeToString(bindIt->second);
                for (auto &boundProto : boundProtos) {
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
    }

    node->setResolvedType(makeNamedType(node->getTypeName()));
}

void TypeChecker::visitArrayLiteralExpr(ArrayLiteralExpr *node) {
    for (auto &elem : node->getElements()) {
        visit(elem.get());
    }
}

void TypeChecker::visitTupleLiteralExpr(TupleLiteralExpr *node) {
    std::vector<std::unique_ptr<TypeRepr>> elemTypes;
    for (auto &elem : node->getElements()) {
        visit(elem.get());
        if (elem->getResolvedType())
            elemTypes.push_back(cloneTypeRepr(elem->getResolvedType()));
        else
            elemTypes.push_back(makeInferredType());
    }
    node->setResolvedType(std::make_unique<TupleTypeRepr>(std::move(elemTypes)));
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
    auto *exp = resolveAlias(expected);
    auto *act = resolveAlias(actual);
    if (exp->getKind() != act->getKind())
        return false;
    // Deep compare for tuples
    if (exp->getKind() == TypeRepr::Kind::Tuple) {
        auto *expTuple = static_cast<const TupleTypeRepr *>(exp);
        auto *actTuple = static_cast<const TupleTypeRepr *>(act);
        if (expTuple->getArity() != actTuple->getArity()) return false;
        for (size_t i = 0; i < expTuple->getArity(); ++i) {
            if (!typesCompatible(expTuple->getElements()[i].get(),
                                 actTuple->getElements()[i].get()))
                return false;
        }
    }
    return true;
}

std::string TypeChecker::typeToString(const TypeRepr *type) const {
    if (!type)
        return "<unknown>";
    return type->toString();
}

const TypeRepr *TypeChecker::resolveAlias(const TypeRepr *type) const {
    if (!type || type->getKind() != TypeRepr::Kind::Named)
        return type;
    auto *named = static_cast<const NamedTypeRepr *>(type);
    auto it = typeAliases_.find(named->getName());
    if (it != typeAliases_.end())
        return it->second;
    return type;
}

const TypeRepr *TypeChecker::resolveExprType(Expr *expr) {
    visit(expr);
    return expr->getResolvedType();
}

void TypeChecker::extractPatternBindings(const std::string &pattern) {
    auto parenPos = pattern.find('(');
    if (parenPos == std::string::npos) return;

    // Find matching closing paren (depth-aware for nested patterns)
    int depth = 0;
    size_t closePos = std::string::npos;
    for (size_t i = parenPos; i < pattern.size(); ++i) {
        if (pattern[i] == '(') depth++;
        else if (pattern[i] == ')') {
            depth--;
            if (depth == 0) { closePos = i; break; }
        }
    }
    if (closePos == std::string::npos) return;

    auto innerStr = pattern.substr(parenPos + 1, closePos - parenPos - 1);

    // Split by top-level commas (respecting nested parens)
    std::vector<std::string> slots;
    int slotDepth = 0;
    size_t start = 0;
    for (size_t i = 0; i < innerStr.size(); ++i) {
        if (innerStr[i] == '(') slotDepth++;
        else if (innerStr[i] == ')') slotDepth--;
        else if (innerStr[i] == ',' && slotDepth == 0) {
            slots.push_back(innerStr.substr(start, i - start));
            start = i + 1;
        }
    }
    if (start < innerStr.size())
        slots.push_back(innerStr.substr(start));

    for (auto &slot : slots) {
        // Trim whitespace
        size_t b = slot.find_first_not_of(" \t");
        if (b == std::string::npos) continue;
        size_t e = slot.find_last_not_of(" \t");
        auto trimmed = slot.substr(b, e - b + 1);

        if (trimmed.find('.') != std::string::npos) {
            // Nested enum pattern — recursively extract leaf bindings
            extractPatternBindings(trimmed);
        } else if (!trimmed.empty()) {
            Symbol sym;
            sym.name = trimmed;
            sym.kind = Symbol::Kind::Variable;
            sym.isMutable = false;
            scopes_.declare(trimmed, sym);
        }
    }
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
                    if (!arm.guard) {
                        diag_.report(node->getStartLoc(), DiagID::warn_unreachable_match_arm,
                                     arm.pattern);
                    }
                } else if (!arm.guard) {
                    coveredCases.insert(caseName);
                }
            }
        }

        if (arm.body) {
            // Extract bindings from pattern (supports nested patterns)
            scopes_.pushScope();
            extractPatternBindings(arm.pattern);
            if (arm.guard) {
                visit(arm.guard.get());
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

void TypeChecker::visitTernaryExpr(TernaryExpr *node) {
    visit(node->getCondition());
    visit(node->getThenExpr());
    visit(node->getElseExpr());
    // Propagate then-branch type as result type
    if (node->getThenExpr()->getResolvedType()) {
        node->setResolvedType(cloneTypeRepr(node->getThenExpr()->getResolvedType()));
    }
}

void TypeChecker::visitAwaitExpr(AwaitExpr *node) {
    if (!currentIsAsync_) {
        diag_.report(node->getStartLoc(), DiagID::err_await_outside_async);
    }
    visit(node->getOperand());
    // Unwrap Task<T> to T
    auto *operandType = node->getOperand()->getResolvedType();
    if (operandType && operandType->getKind() == TypeRepr::Kind::Generic) {
        auto *genType = static_cast<const GenericTypeRepr *>(operandType);
        if (genType->getBaseName() == "Task" && !genType->getTypeArgs().empty()) {
            node->setResolvedType(cloneTypeRepr(genType->getTypeArgs()[0].get()));
            return;
        }
    }
    // If not a Task type, propagate operand type (tolerant for non-async calls)
    if (operandType) {
        node->setResolvedType(cloneTypeRepr(operandType));
    }
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

std::optional<TypeChecker::ConstValue> TypeChecker::evaluateConstExpr(const Expr *expr) {
    if (!expr) return std::nullopt;

    switch (expr->getKind()) {
    case ASTNode::NodeKind::IntegerLiteralExpr: {
        auto *lit = static_cast<const IntegerLiteralExpr *>(expr);
        ConstValue v;
        v.kind = ConstValue::Integer;
        v.intVal = lit->getValue();
        return v;
    }
    case ASTNode::NodeKind::FloatLiteralExpr: {
        auto *lit = static_cast<const FloatLiteralExpr *>(expr);
        ConstValue v;
        v.kind = ConstValue::Float;
        v.floatVal = lit->getValue();
        return v;
    }
    case ASTNode::NodeKind::BoolLiteralExpr: {
        auto *lit = static_cast<const BoolLiteralExpr *>(expr);
        ConstValue v;
        v.kind = ConstValue::Bool;
        v.boolVal = lit->getValue();
        return v;
    }
    case ASTNode::NodeKind::StringLiteralExpr: {
        auto *lit = static_cast<const StringLiteralExpr *>(expr);
        ConstValue v;
        v.kind = ConstValue::String;
        v.strVal = lit->getValue();
        return v;
    }
    case ASTNode::NodeKind::IdentifierExpr: {
        auto *ident = static_cast<const IdentifierExpr *>(expr);
        auto it = constValues_.find(ident->getName());
        if (it != constValues_.end()) return it->second;
        return std::nullopt;
    }
    case ASTNode::NodeKind::GroupExpr: {
        auto *group = static_cast<const GroupExpr *>(expr);
        return evaluateConstExpr(group->getExpr());
    }
    case ASTNode::NodeKind::UnaryExpr: {
        auto *unary = static_cast<const UnaryExpr *>(expr);
        auto operand = evaluateConstExpr(unary->getOperand());
        if (!operand) return std::nullopt;
        switch (unary->getOp()) {
        case UnaryExpr::Op::Negate:
            if (operand->kind == ConstValue::Integer) {
                operand->intVal = -operand->intVal;
                return operand;
            }
            if (operand->kind == ConstValue::Float) {
                operand->floatVal = -operand->floatVal;
                return operand;
            }
            return std::nullopt;
        case UnaryExpr::Op::Not:
            if (operand->kind == ConstValue::Bool) {
                operand->boolVal = !operand->boolVal;
                return operand;
            }
            return std::nullopt;
        case UnaryExpr::Op::BitNot:
            if (operand->kind == ConstValue::Integer) {
                operand->intVal = ~operand->intVal;
                return operand;
            }
            return std::nullopt;
        }
        return std::nullopt;
    }
    case ASTNode::NodeKind::BinaryExpr: {
        auto *bin = static_cast<const BinaryExpr *>(expr);
        auto lhs = evaluateConstExpr(bin->getLHS());
        auto rhs = evaluateConstExpr(bin->getRHS());
        if (!lhs || !rhs) return std::nullopt;

        // Integer arithmetic
        if (lhs->kind == ConstValue::Integer && rhs->kind == ConstValue::Integer) {
            ConstValue v;
            v.kind = ConstValue::Integer;
            switch (bin->getOp()) {
            case BinaryExpr::Op::Add: v.intVal = lhs->intVal + rhs->intVal; return v;
            case BinaryExpr::Op::Sub: v.intVal = lhs->intVal - rhs->intVal; return v;
            case BinaryExpr::Op::Mul: v.intVal = lhs->intVal * rhs->intVal; return v;
            case BinaryExpr::Op::Div:
                if (rhs->intVal == 0) return std::nullopt;
                v.intVal = lhs->intVal / rhs->intVal; return v;
            case BinaryExpr::Op::Mod:
                if (rhs->intVal == 0) return std::nullopt;
                v.intVal = lhs->intVal % rhs->intVal; return v;
            case BinaryExpr::Op::BitAnd: v.intVal = lhs->intVal & rhs->intVal; return v;
            case BinaryExpr::Op::BitOr:  v.intVal = lhs->intVal | rhs->intVal; return v;
            case BinaryExpr::Op::BitXor: v.intVal = lhs->intVal ^ rhs->intVal; return v;
            case BinaryExpr::Op::Shl:    v.intVal = lhs->intVal << rhs->intVal; return v;
            case BinaryExpr::Op::Shr:    v.intVal = lhs->intVal >> rhs->intVal; return v;
            case BinaryExpr::Op::Eq:  v.kind = ConstValue::Bool; v.boolVal = (lhs->intVal == rhs->intVal); return v;
            case BinaryExpr::Op::NotEq: v.kind = ConstValue::Bool; v.boolVal = (lhs->intVal != rhs->intVal); return v;
            case BinaryExpr::Op::Less: v.kind = ConstValue::Bool; v.boolVal = (lhs->intVal < rhs->intVal); return v;
            case BinaryExpr::Op::LessEq: v.kind = ConstValue::Bool; v.boolVal = (lhs->intVal <= rhs->intVal); return v;
            case BinaryExpr::Op::Greater: v.kind = ConstValue::Bool; v.boolVal = (lhs->intVal > rhs->intVal); return v;
            case BinaryExpr::Op::GreaterEq: v.kind = ConstValue::Bool; v.boolVal = (lhs->intVal >= rhs->intVal); return v;
            default: return std::nullopt;
            }
        }

        // Float arithmetic
        if (lhs->kind == ConstValue::Float && rhs->kind == ConstValue::Float) {
            ConstValue v;
            v.kind = ConstValue::Float;
            switch (bin->getOp()) {
            case BinaryExpr::Op::Add: v.floatVal = lhs->floatVal + rhs->floatVal; return v;
            case BinaryExpr::Op::Sub: v.floatVal = lhs->floatVal - rhs->floatVal; return v;
            case BinaryExpr::Op::Mul: v.floatVal = lhs->floatVal * rhs->floatVal; return v;
            case BinaryExpr::Op::Div:
                if (rhs->floatVal == 0.0) return std::nullopt;
                v.floatVal = lhs->floatVal / rhs->floatVal; return v;
            case BinaryExpr::Op::Eq:  v.kind = ConstValue::Bool; v.boolVal = (lhs->floatVal == rhs->floatVal); return v;
            case BinaryExpr::Op::NotEq: v.kind = ConstValue::Bool; v.boolVal = (lhs->floatVal != rhs->floatVal); return v;
            case BinaryExpr::Op::Less: v.kind = ConstValue::Bool; v.boolVal = (lhs->floatVal < rhs->floatVal); return v;
            case BinaryExpr::Op::LessEq: v.kind = ConstValue::Bool; v.boolVal = (lhs->floatVal <= rhs->floatVal); return v;
            case BinaryExpr::Op::Greater: v.kind = ConstValue::Bool; v.boolVal = (lhs->floatVal > rhs->floatVal); return v;
            case BinaryExpr::Op::GreaterEq: v.kind = ConstValue::Bool; v.boolVal = (lhs->floatVal >= rhs->floatVal); return v;
            default: return std::nullopt;
            }
        }

        // Bool logic
        if (lhs->kind == ConstValue::Bool && rhs->kind == ConstValue::Bool) {
            ConstValue v;
            v.kind = ConstValue::Bool;
            switch (bin->getOp()) {
            case BinaryExpr::Op::And: v.boolVal = lhs->boolVal && rhs->boolVal; return v;
            case BinaryExpr::Op::Or:  v.boolVal = lhs->boolVal || rhs->boolVal; return v;
            case BinaryExpr::Op::Eq:  v.boolVal = (lhs->boolVal == rhs->boolVal); return v;
            case BinaryExpr::Op::NotEq: v.boolVal = (lhs->boolVal != rhs->boolVal); return v;
            default: return std::nullopt;
            }
        }

        // String concatenation
        if (lhs->kind == ConstValue::String && rhs->kind == ConstValue::String) {
            if (bin->getOp() == BinaryExpr::Op::Add) {
                ConstValue v;
                v.kind = ConstValue::String;
                v.strVal = lhs->strVal + rhs->strVal;
                return v;
            }
        }

        return std::nullopt;
    }
    case ASTNode::NodeKind::TernaryExpr: {
        auto *ternary = static_cast<const TernaryExpr *>(expr);
        auto cond = evaluateConstExpr(ternary->getCondition());
        auto then = evaluateConstExpr(ternary->getThenExpr());
        auto els = evaluateConstExpr(ternary->getElseExpr());
        if (!cond || !then || !els) return std::nullopt;
        if (cond->kind != ConstValue::Bool) return std::nullopt;
        return cond->boolVal ? then : els;
    }
    case ASTNode::NodeKind::CastExpr: {
        auto *cast = static_cast<const CastExpr *>(expr);
        auto operand = evaluateConstExpr(cast->getExpr());
        if (!operand) return std::nullopt;
        auto *targetType = cast->getTargetType();
        if (!targetType) return std::nullopt;

        auto kind = targetType->getKind();
        bool isIntTarget = (kind == TypeRepr::Kind::I8 || kind == TypeRepr::Kind::I16 ||
                            kind == TypeRepr::Kind::I32 || kind == TypeRepr::Kind::I64 ||
                            kind == TypeRepr::Kind::U8 || kind == TypeRepr::Kind::U16 ||
                            kind == TypeRepr::Kind::U32 || kind == TypeRepr::Kind::U64);
        bool isFloatTarget = (kind == TypeRepr::Kind::F32 || kind == TypeRepr::Kind::F64);

        if (operand->kind == ConstValue::Integer) {
            if (isIntTarget) return operand;
            if (isFloatTarget) {
                ConstValue v;
                v.kind = ConstValue::Float;
                v.floatVal = static_cast<double>(operand->intVal);
                return v;
            }
        }
        if (operand->kind == ConstValue::Float) {
            if (isIntTarget) {
                ConstValue v;
                v.kind = ConstValue::Integer;
                v.intVal = static_cast<int64_t>(operand->floatVal);
                return v;
            }
            if (isFloatTarget) return operand;
        }
        return std::nullopt;
    }
    default:
        return std::nullopt;
    }
}

} // namespace liva
