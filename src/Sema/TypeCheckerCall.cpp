#include "liva/Sema/TypeChecker.h"

namespace liva {

void TypeChecker::propagateClosureParamTypes(CallExpr *node) {
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
}

void TypeChecker::propagateDynArrayClosureTypes(CallExpr *node) {
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
}

void TypeChecker::checkCallArgCount(CallExpr *node) {
    // Check argument count for user-defined functions / class constructors
    if (node->getCallee()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
        auto *identChk = static_cast<IdentifierExpr *>(node->getCallee());
        auto *symChk = scopes_.lookup(identChk->getName());
        // Class constructor call: ClassName(args) → overload resolution on arg count
        if (symChk && symChk->kind == Symbol::Kind::ClassType && symChk->classDecl) {
            auto inits = symChk->classDecl->getInits();
            size_t actualArgs = node->getArgs().size();
            const FuncDecl *matchedInit = nullptr;
            if (!inits.empty()) {
                // Find init matching actual arg count (respecting defaults)
                for (auto *it : inits) {
                    size_t minReq = 0, maxP = 0;
                    for (auto &p : it->getParams()) {
                        if (p.isSelf) continue;
                        maxP++;
                        if (!p.hasDefault()) minReq++;
                    }
                    if (actualArgs >= minReq && actualArgs <= maxP) {
                        matchedInit = it;
                        break;
                    }
                }
                if (!matchedInit) {
                    // Report with first init's signature
                    auto *first = inits.front();
                    size_t minReq = 0;
                    for (auto &p : first->getParams()) {
                        if (p.isSelf) continue;
                        if (!p.hasDefault()) minReq++;
                    }
                    diag_.report(node->getStartLoc(), DiagID::err_wrong_arg_count,
                                 identChk->getName(),
                                 std::to_string(minReq),
                                 std::to_string(actualArgs));
                }
            }
            // Set result type: NamedTypeRepr(className), wrapped in Optional if failable
            auto classType = std::make_unique<NamedTypeRepr>(symChk->classDecl->getName());
            if (symChk->classDecl->hasFailableInit()) {
                auto optType = std::make_unique<OptionalTypeRepr>(std::move(classType));
                node->setResolvedType(std::move(optType));
            } else {
                node->setResolvedType(std::move(classType));
            }
        }
        if (symChk && symChk->funcDecl) {
            const auto &params = symChk->funcDecl->getParams();
            size_t actualArgs = node->getArgs().size();
            // Count required params (no default value, not variadic, not self)
            size_t requiredParams = 0;
            size_t maxParams = 0;
            bool hasVariadic = false;
            for (const auto &p : params) {
                if (p.isSelf) continue;
                maxParams++;
                if (p.isVariadic) { hasVariadic = true; continue; }
                if (!p.hasDefault()) requiredParams++;
            }
            if (!hasVariadic && (actualArgs < requiredParams || actualArgs > maxParams)) {
                diag_.report(node->getStartLoc(), DiagID::err_wrong_arg_count,
                             identChk->getName(),
                             std::to_string(requiredParams == maxParams
                                            ? requiredParams
                                            : requiredParams),
                             std::to_string(actualArgs));
            }
        }
    }
}

} // namespace liva
