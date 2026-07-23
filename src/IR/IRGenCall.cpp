#include "liva/IR/IRGen.h"

#ifdef LIVA_HAS_LLVM

#include <llvm/IR/Intrinsics.h>

namespace liva {

std::optional<IRGen::MemberDynArrayInfo>
IRGen::resolveMemberDynArray(MemberExpr *memberExpr) {
    // Pattern: identifier.field where field is a DynArray in the struct
    if (memberExpr->getObject()->getKind() != ASTNode::NodeKind::IdentifierExpr)
        return std::nullopt;

    auto *ident = static_cast<IdentifierExpr *>(memberExpr->getObject());
    const std::string &objName = ident->getName();
    const std::string &fieldName = memberExpr->getMember();

    // Find struct type name for the variable
    auto stIt = vars_.varStructTypes.find(objName);
    if (stIt == vars_.varStructTypes.end())
        return std::nullopt;
    const std::string &structTypeName = stIt->second;

    // Find the field's TypeRepr to check if it's a DynArray
    auto ftrIt = structFieldTypeReprs_.find(structTypeName);
    if (ftrIt == structFieldTypeReprs_.end())
        return std::nullopt;

    int idx = getStructFieldIndex(structTypeName, fieldName);
    if (idx < 0 || static_cast<size_t>(idx) >= ftrIt->second.size())
        return std::nullopt;

    auto *fieldTypeRepr = ftrIt->second[idx];
    if (!fieldTypeRepr || fieldTypeRepr->getKind() != TypeRepr::Kind::Array)
        return std::nullopt;

    auto *arrRepr = static_cast<const ArrayTypeRepr *>(fieldTypeRepr);
    if (!arrRepr->isDynamic())
        return std::nullopt;

    // It IS a DynArray field — compute GEP
    auto allocaIt = vars_.namedValues.find(objName);
    if (allocaIt == vars_.namedValues.end())
        return std::nullopt;

    auto *objAlloca = allocaIt->second;
    auto *structTy = structTypes_[structTypeName];

    // If the variable stores a pointer to the struct (e.g., self parameter)
    llvm::Value *basePtr = objAlloca;
    if (objAlloca->getAllocatedType()->isPointerTy()) {
        basePtr = builder_->CreateLoad(objAlloca->getAllocatedType(), objAlloca, objName + ".ptr");
    }

    auto *elemType = toLLVMType(arrRepr->getElement());
    const llvm::DataLayout &dl = module_->getDataLayout();
    uint64_t elemSize = dl.getTypeAllocSize(elemType);

    // GEP to the DynArray struct field within the parent struct
    auto *dynStructTy = getDynArrayStructTy();
    auto *fieldGEP = builder_->CreateStructGEP(structTy, basePtr, idx, fieldName + ".da");

    MemberDynArrayInfo info;
    info.arrGEP = fieldGEP;
    info.elementType = elemType;
    info.elemSize = elemSize;
    return info;
}

llvm::Value *IRGen::visitCallExpr(CallExpr *node) {
    if (diBuilder_) emitDebugLocation(node->getStartLoc());
    // Check for method call or enum case constructor: obj.method(args) / Shape.Circle(3.14)
    if (node->getCallee()->getKind() == ASTNode::NodeKind::MemberExpr) {
        if (auto r = tryEmitMethodCall(node)) return *r;
    }

    // Get function name
    std::string funcName;
    if (node->getCallee()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
        auto *ident = static_cast<IdentifierExpr *>(node->getCallee());
        funcName = ident->getName();
    }

    // Class constructor call: ClassName(args) → overload resolution on arg count
    if (classNames_.count(funcName)) {
        size_t argCount = node->getArgs().size();
        // Try ClassName_init (designated/first) if its arity matches
        llvm::Function *initFn = nullptr;
        auto *firstInit = module_->getFunction(funcName + "_init");
        if (firstInit && firstInit->arg_size() == argCount) {
            initFn = firstInit;
        } else {
            // Try ClassName_init<argCount> (convenience/overload)
            auto *overloaded = module_->getFunction(funcName + "_init" + std::to_string(argCount));
            if (overloaded) initFn = overloaded;
            else initFn = firstInit; // fallback
        }
        if (initFn) {
            std::vector<llvm::Value *> args;
            for (auto &arg : node->getArgs()) {
                auto *val = visit(arg.get());
                if (!val) return nullptr;
                args.push_back(val);
            }
            auto *obj = builder_->CreateCall(initFn, args, "class_obj");
            return obj;
        }
        return nullptr;
    }

    if (auto r = tryEmitCoreBuiltin(node, funcName)) return *r;

    if (auto r = tryEmitSysBuiltin(node, funcName)) return *r;

    if (auto r = tryEmitStringBuiltin(node, funcName)) return *r;

    if (auto r = tryEmitConcurrencyBuiltin(node, funcName)) return *r;

    if (auto r = tryEmitDataBuiltin(node, funcName)) return *r;

    if (auto r = tryEmitNetBuiltin(node, funcName)) return *r;

    if (auto r = tryEmitUIBuiltin(node, funcName)) return *r;

    // Check for indirect call through function-typed variable (closure object)
    auto funcIt = vars_.varFuncTypes.find(funcName);
    if (funcIt != vars_.varFuncTypes.end()) {
        auto namedIt = vars_.namedValues.find(funcName);
        if (namedIt != vars_.namedValues.end()) {
            auto *closureObjTy = getClosureObjTy();
            auto *ptrTy = llvm::PointerType::getUnqual(*context_);
            // Extract func_ptr and env_ptr from closure object
            auto *funcGEP = builder_->CreateStructGEP(closureObjTy, namedIt->second, 0);
            auto *funcPtr = builder_->CreateLoad(ptrTy, funcGEP, "func.ptr");
            auto *envGEP = builder_->CreateStructGEP(closureObjTy, namedIt->second, 1);
            auto *envPtr = builder_->CreateLoad(ptrTy, envGEP, "env.ptr");
            // Build args: env first, then user args
            std::vector<llvm::Value *> args;
            args.push_back(envPtr);
            for (auto &arg : node->getArgs()) {
                auto *val = visit(arg.get());
                if (!val) return nullptr;
                args.push_back(val);
            }
            if (funcIt->second->getReturnType()->isVoidTy())
                return builder_->CreateCall(funcIt->second, funcPtr, args);
            return builder_->CreateCall(funcIt->second, funcPtr, args, "indcalltmp");
        }
    }

    // Look up the function
    auto *callee = module_->getFunction(funcName);

    // Generic function check
    if (!callee) {
        auto gIt = genericFuncDecls_.find(funcName);
        if (gIt != genericFuncDecls_.end()) {
            const FuncDecl *genericFunc = gIt->second;

            // Evaluate arguments
            std::vector<llvm::Value *> argValues;
            for (auto &arg : node->getArgs()) {
                auto *val = visit(arg.get());
                if (!val) return nullptr;
                argValues.push_back(val);
            }

            // Infer type arguments from LLVM types
            const auto &typeParams = genericFunc->getTypeParams();
            std::unordered_map<std::string, const TypeRepr *> inferred;
            for (size_t i = 0; i < genericFunc->getParams().size() && i < argValues.size(); ++i) {
                const TypeRepr *paramType = genericFunc->getParams()[i].type.get();
                if (paramType && paramType->getKind() == TypeRepr::Kind::Named) {
                    auto *named = static_cast<const NamedTypeRepr *>(paramType);
                    for (const auto &tp : typeParams) {
                        if (named->getName() == tp && inferred.find(tp) == inferred.end()) {
                            llvm::Type *argTy = argValues[i]->getType();
                            const TypeRepr *inferredType = nullptr;
                            if (argTy->isIntegerTy(32)) {
                                inferredTypes_.push_back(makeI32Type());
                                inferredType = inferredTypes_.back().get();
                            } else if (argTy->isIntegerTy(64)) {
                                inferredTypes_.push_back(makeI64Type());
                                inferredType = inferredTypes_.back().get();
                            } else if (argTy->isDoubleTy()) {
                                inferredTypes_.push_back(makeF64Type());
                                inferredType = inferredTypes_.back().get();
                            } else if (argTy->isIntegerTy(1)) {
                                inferredTypes_.push_back(makeBoolType());
                                inferredType = inferredTypes_.back().get();
                            } else if (argTy->isPointerTy()) {
                                inferredTypes_.push_back(makeStringType());
                                inferredType = inferredTypes_.back().get();
                            } else if (argTy->isStructTy()) {
                                // DynArray [T]: infer as ArrayTypeRepr([T]) so that
                                // the monomorphized body uses the built-in iteration path.
                                if (argTy == getDynArrayStructTy() &&
                                    i < node->getArgs().size()) {
                                    // Recover element type from the argument variable name.
                                    if (node->getArgs()[i]->getKind() == ASTNode::NodeKind::IdentifierExpr) {
                                        auto *id = static_cast<const IdentifierExpr *>(node->getArgs()[i].get());
                                        auto daIt = vars_.varDynArrayTypes.find(id->getName());
                                        if (daIt != vars_.varDynArrayTypes.end()) {
                                            // Build [elemType] TypeRepr from the LLVM element type.
                                            // Map common LLVM types to TypeRepr primitives.
                                            auto *et = daIt->second.elementType;
                                            std::unique_ptr<TypeRepr> elemTR;
                                            if (et->isIntegerTy(32))       elemTR = makeI32Type();
                                            else if (et->isIntegerTy(64))  elemTR = makeI64Type();
                                            else if (et->isDoubleTy())     elemTR = makeF64Type();
                                            else if (et->isIntegerTy(1))   elemTR = makeBoolType();
                                            else                           elemTR = makeI32Type(); // fallback
                                            inferredTypes_.push_back(
                                                std::make_unique<ArrayTypeRepr>(std::move(elemTR), -1));
                                            inferredType = inferredTypes_.back().get();
                                        }
                                    }
                                }
                                // Infer concrete struct type: look up LLVM struct
                                // type against the known structTypes_ registry.
                                if (!inferredType) {
                                    for (auto &[sName, sTy] : structTypes_) {
                                        if (sTy == argTy) {
                                            inferredTypes_.push_back(
                                                std::make_unique<NamedTypeRepr>(sName));
                                            inferredType = inferredTypes_.back().get();
                                            break;
                                        }
                                    }
                                }
                            }
                            if (inferredType) inferred[tp] = inferredType;
                            break;
                        }
                    }
                }
            }

            // Build ordered type arguments vector
            std::vector<const TypeRepr *> typeArgs;
            for (const auto &tp : typeParams) {
                auto it = inferred.find(tp);
                if (it != inferred.end()) typeArgs.push_back(it->second);
            }

            // Monomorphize and call
            callee = monomorphize(genericFunc, typeArgs);
            if (!callee) {
                diag_.report(node->getStartLoc(), DiagID::err_irgen_call_callee_failed);
                return nullptr;
            }
            if (callee->getReturnType()->isVoidTy())
                return builder_->CreateCall(callee, argValues);
            return builder_->CreateCall(callee, argValues, "calltmp");
        }
        diag_.report(node->getStartLoc(), DiagID::err_irgen_unknown_function, funcName);
        return nullptr;
    }

    // Check if function has variadic param
    auto fdIt = funcDecls_.find(funcName);
    bool hasVariadic = false;
    size_t variadicIdx = 0;
    llvm::Type *variadicElemType = nullptr;
    if (fdIt != funcDecls_.end()) {
        const auto &params = fdIt->second->getParams();
        for (size_t vi = 0; vi < params.size(); ++vi) {
            if (params[vi].isVariadic) {
                hasVariadic = true;
                variadicIdx = vi;
                variadicElemType = toLLVMType(params[vi].type.get());
                break;
            }
        }
    }

    if (hasVariadic) {
        // Normal args (before variadic)
        std::vector<llvm::Value *> args;
        for (size_t ai = 0; ai < variadicIdx && ai < node->getArgs().size(); ++ai) {
            auto *val = visit(node->getArgs()[ai].get());
            if (!val) return nullptr;
            args.push_back(val);
        }

        // Pack variadic args into DynArray
        size_t numVariadicArgs = node->getArgs().size() > variadicIdx
            ? node->getArgs().size() - variadicIdx : 0;

        auto *curFunc = builder_->GetInsertBlock()->getParent();
        auto *structTy = getDynArrayStructTy();
        auto *daAlloca = createEntryBlockAlloca(curFunc, "varargs.da", structTy);

        if (numVariadicArgs > 0) {
            // Stack-allocate element array
            auto *arrType = llvm::ArrayType::get(variadicElemType, numVariadicArgs);
            auto *elemArray = createEntryBlockAlloca(curFunc, "varargs.elems", arrType);

            for (size_t ai = 0; ai < numVariadicArgs; ++ai) {
                auto *val = visit(node->getArgs()[variadicIdx + ai].get());
                if (!val) return nullptr;
                auto *gep = builder_->CreateConstInBoundsGEP2_64(
                    arrType, elemArray, 0, ai, "vararg." + std::to_string(ai));
                builder_->CreateStore(val, gep);
            }

            // Build DynArray struct
            auto *dataField = builder_->CreateStructGEP(structTy, daAlloca, 0);
            builder_->CreateStore(elemArray, dataField);
            auto *lenField = builder_->CreateStructGEP(structTy, daAlloca, 1);
            builder_->CreateStore(builder_->getInt64(numVariadicArgs), lenField);
            auto *capField = builder_->CreateStructGEP(structTy, daAlloca, 2);
            builder_->CreateStore(builder_->getInt64(numVariadicArgs), capField);
        } else {
            // Empty DynArray: {null, 0, 0}
            auto *ptrTy = llvm::PointerType::getUnqual(*context_);
            auto *dataField = builder_->CreateStructGEP(structTy, daAlloca, 0);
            builder_->CreateStore(llvm::ConstantPointerNull::get(ptrTy), dataField);
            auto *lenField = builder_->CreateStructGEP(structTy, daAlloca, 1);
            builder_->CreateStore(builder_->getInt64(0), lenField);
            auto *capField = builder_->CreateStructGEP(structTy, daAlloca, 2);
            builder_->CreateStore(builder_->getInt64(0), capField);
        }

        auto *daVal = builder_->CreateLoad(structTy, daAlloca, "varargs.val");
        args.push_back(daVal);

        if (callee->getReturnType()->isVoidTy())
            return builder_->CreateCall(callee, args);
        return builder_->CreateCall(callee, args, "calltmp");
    }

    std::vector<llvm::Value *> args;
    for (auto &arg : node->getArgs()) {
        auto *val = visit(arg.get());
        if (!val)
            return nullptr;
        args.push_back(val);
    }

    // Caller-side boxing: concrete struct → dyn Protocol fat pointer
    {
        auto fdIt3 = funcDecls_.find(funcName);
        if (fdIt3 != funcDecls_.end()) {
            const auto &params = fdIt3->second->getParams();
            for (size_t i = 0; i < args.size() && i < params.size(); ++i) {
                if (params[i].type && params[i].type->getKind() == TypeRepr::Kind::DynProtocol) {
                    auto *dynType = static_cast<const DynProtocolTypeRepr *>(params[i].type.get());
                    const std::string &protocolName = dynType->getProtocolName();

                    // Check if argument is already a trait object (passed through)
                    if (args[i]->getType() == getTraitObjectTy())
                        continue;

                    // Find concrete type from argument expression
                    std::string concreteType;
                    llvm::AllocaInst *dataAlloca = nullptr;
                    if (i < node->getArgs().size() &&
                        node->getArgs()[i]->getKind() == ASTNode::NodeKind::IdentifierExpr) {
                        auto *ident = static_cast<IdentifierExpr *>(node->getArgs()[i].get());
                        auto stIt = vars_.varStructTypes.find(ident->getName());
                        if (stIt != vars_.varStructTypes.end()) {
                            concreteType = stIt->second;
                            auto nvIt = vars_.namedValues.find(ident->getName());
                            if (nvIt != vars_.namedValues.end())
                                dataAlloca = nvIt->second;
                        }
                    }

                    if (!concreteType.empty() && dataAlloca) {
                        auto *curFunc = builder_->GetInsertBlock()->getParent();
                        auto *traitTy = getTraitObjectTy();
                        auto *traitAlloca = createEntryBlockAlloca(curFunc, "dyn.box", traitTy);

                        auto *dataGEP = builder_->CreateStructGEP(traitTy, traitAlloca, 0, "dyn.data");
                        builder_->CreateStore(dataAlloca, dataGEP);

                        auto *vtable = getOrCreateVtable(protocolName, concreteType);
                        auto *vtableGEP = builder_->CreateStructGEP(traitTy, traitAlloca, 1, "dyn.vtable");
                        builder_->CreateStore(vtable, vtableGEP);

                        auto *traitVal = builder_->CreateLoad(traitTy, traitAlloca, "dyn.val");
                        args[i] = traitVal;
                    }
                }
            }
        }
    }

    // Fill in default arguments for missing params
    if (args.size() < callee->arg_size()) {
        auto fdIt2 = funcDecls_.find(funcName);
        if (fdIt2 != funcDecls_.end()) {
            const auto &params = fdIt2->second->getParams();
            for (size_t i = args.size(); i < params.size(); ++i) {
                if (params[i].hasDefault()) {
                    auto *defVal = visit(const_cast<Expr *>(params[i].defaultValue.get()));
                    if (defVal) args.push_back(defVal);
                }
            }
        }
    }

    if (callee->getReturnType()->isVoidTy())
        return builder_->CreateCall(callee, args);
    return builder_->CreateCall(callee, args, "calltmp");
}

llvm::Value *IRGen::visitAssignExpr(AssignExpr *node) {
    if (diBuilder_) emitDebugLocation(node->getStartLoc());
    auto *val = visit(node->getValue());
    if (!val)
        return nullptr;

    // Handle member assignment: obj.field = value
    if (node->getTarget()->getKind() == ASTNode::NodeKind::MemberExpr) {
        auto *memberExpr = static_cast<MemberExpr *>(node->getTarget());
        std::string objName;
        llvm::AllocaInst *objAlloca = nullptr;
        std::string structTypeName;

        if (memberExpr->getObject()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
            auto *ident = static_cast<IdentifierExpr *>(memberExpr->getObject());
            objName = ident->getName();
            auto it = vars_.namedValues.find(objName);
            if (it != vars_.namedValues.end())
                objAlloca = it->second;
            auto stIt = vars_.varStructTypes.find(objName);
            if (stIt != vars_.varStructTypes.end())
                structTypeName = stIt->second;
        }

        // Static field assignment: ClassName.field = val → global store
        if (!objName.empty() && !objAlloca && classNames_.count(objName)) {
            std::string key = objName + "." + memberExpr->getMember();
            auto sfIt = classStaticFields_.find(key);
            if (sfIt != classStaticFields_.end()) {
                builder_->CreateStore(val, sfIt->second);
                return val;
            }
        }

        // Computed property setter: obj.computedField = val → call setter
        if (objAlloca && !objName.empty()) {
            auto clsIt2 = vars_.varClassTypes.find(objName);
            if (clsIt2 != vars_.varClassTypes.end()) {
                auto cpIt = classComputedFields_.find(clsIt2->second);
                if (cpIt != classComputedFields_.end() && cpIt->second.count(memberExpr->getMember())) {
                    std::string setterName = clsIt2->second + "_set_" + memberExpr->getMember();
                    auto *setter = module_->getFunction(setterName);
                    if (setter) {
                        auto *ptrTy = llvm::PointerType::getUnqual(*context_);
                        llvm::Value *selfVal = objAlloca;
                        if (objAlloca->getAllocatedType()->isPointerTy()) {
                            selfVal = builder_->CreateLoad(ptrTy, objAlloca, objName + ".ptr");
                        }
                        builder_->CreateCall(setter, {selfVal, val});
                        return val;
                    }
                }
            }
        }

        // Class field assignment: obj.field = val (or self.field = val)
        if (objAlloca && !objName.empty()) {
            auto clsIt = vars_.varClassTypes.find(objName);
            if (clsIt != vars_.varClassTypes.end()) {
                const std::string &clsTypeName = clsIt->second;
                auto ctIt = classTypes_.find(clsTypeName);
                if (ctIt != classTypes_.end()) {
                    auto *classTy = ctIt->second;
                    auto cfIt = classFieldNames_.find(clsTypeName);
                    if (cfIt != classFieldNames_.end()) {
                        int fieldIdx = -1;
                        for (size_t i = 0; i < cfIt->second.size(); ++i) {
                            if (cfIt->second[i] == memberExpr->getMember()) {
                                fieldIdx = static_cast<int>(i);
                                break;
                            }
                        }
                        if (fieldIdx >= 0) {
                            int structIdx = fieldIdx + 1; // +1 for vtable ptr
                            llvm::Value *basePtr = objAlloca;
                            if (objAlloca->getAllocatedType()->isPointerTy()) {
                                basePtr = builder_->CreateLoad(
                                    objAlloca->getAllocatedType(), objAlloca, objName + ".ptr");
                            }
                            auto *gep = builder_->CreateStructGEP(
                                classTy, basePtr, structIdx, memberExpr->getMember());
                            // Free old heap string before reassignment (conservative: only if tracked)
                            std::string compositeKey = objName + "." + memberExpr->getMember();
                            if (node->getOp() == AssignExpr::Op::Assign &&
                                val->getType()->isPointerTy() &&
                                vars_.heapStringVars.count(compositeKey)) {
                                auto *freeFn = module_->getFunction("free");
                                if (freeFn) {
                                    auto *ptrTy = llvm::PointerType::getUnqual(*context_);
                                    auto *oldStr = builder_->CreateLoad(ptrTy, gep, "old.cls.field.str");
                                    builder_->CreateCall(freeFn, {oldStr});
                                }
                            }
                            // Property observers: call willSet before, didSet after
                            bool hasObs = false;
                            auto obsIt = classObserverFields_.find(clsTypeName);
                            if (obsIt != classObserverFields_.end() &&
                                obsIt->second.count(memberExpr->getMember())) {
                                hasObs = true;
                            }
                            llvm::Value *oldValue = nullptr;
                            auto *ptrTy2 = llvm::PointerType::getUnqual(*context_);
                            llvm::Value *selfForObs = nullptr;
                            if (hasObs) {
                                // Load self ptr for observer calls
                                selfForObs = objAlloca;
                                if (objAlloca->getAllocatedType()->isPointerTy()) {
                                    selfForObs = builder_->CreateLoad(
                                        ptrTy2, objAlloca, objName + ".obs.self");
                                }
                                // Load old value for didSet
                                auto *didSetFn = module_->getFunction(
                                    clsTypeName + "_didSet_" + memberExpr->getMember());
                                if (didSetFn) {
                                    oldValue = builder_->CreateLoad(
                                        val->getType(), gep, "obs.oldValue");
                                }
                                // Call willSet(self, newValue)
                                auto *willSetFn = module_->getFunction(
                                    clsTypeName + "_willSet_" + memberExpr->getMember());
                                if (willSetFn) {
                                    builder_->CreateCall(willSetFn, {selfForObs, val});
                                }
                            }
                            builder_->CreateStore(val, gep);
                            if (hasObs) {
                                auto *didSetFn = module_->getFunction(
                                    clsTypeName + "_didSet_" + memberExpr->getMember());
                                if (didSetFn && oldValue) {
                                    builder_->CreateCall(didSetFn, {selfForObs, oldValue});
                                }
                            }
                            // Transfer ownership: string temp is now owned by the class field
                            if (val->getType()->isPointerTy())
                                transferStringOwnership(val, compositeKey);
                            return val;
                        }
                    }
                }
            }
        }

        if (objAlloca && !structTypeName.empty()) {
            auto stIt = structTypes_.find(structTypeName);
            if (stIt != structTypes_.end()) {
                auto *structTy = stIt->second;
                int idx = getStructFieldIndex(structTypeName, memberExpr->getMember());
                if (idx >= 0) {
                    llvm::Value *basePtr = objAlloca;
                    if (objAlloca->getAllocatedType()->isPointerTy()) {
                        basePtr = builder_->CreateLoad(objAlloca->getAllocatedType(),
                                                        objAlloca, objName);
                    }
                    auto *gep = builder_->CreateStructGEP(structTy, basePtr, idx,
                                                           memberExpr->getMember());
                    // Free old heap string before reassignment
                    if (node->getOp() == AssignExpr::Op::Assign && val->getType()->isPointerTy()) {
                        auto ftrIt = structFieldTypeReprs_.find(structTypeName);
                        if (ftrIt != structFieldTypeReprs_.end() &&
                            idx < static_cast<int>(ftrIt->second.size())) {
                            const TypeRepr *ft = ftrIt->second[idx];
                            if (isStringTypeRepr(ft)) {
                                auto *freeFn = module_->getFunction("free");
                                if (freeFn) {
                                    auto *ptrTy = llvm::PointerType::getUnqual(*context_);
                                    auto *oldStr = builder_->CreateLoad(ptrTy, gep, "old.field.str");
                                    builder_->CreateCall(freeFn, {oldStr});
                                }
                            }
                        }
                    }
                    builder_->CreateStore(val, gep);
                    // Transfer ownership: string temp is now owned by the struct field
                    if (val->getType()->isPointerTy())
                        transferStringOwnership(val, objName + "." + memberExpr->getMember());
                }
            }
        }
        return val;
    }

    // Handle index assignment: arr[i] = value
    if (node->getTarget()->getKind() == ASTNode::NodeKind::IndexExpr) {
        auto *indexExpr = static_cast<IndexExpr *>(node->getTarget());
        if (indexExpr->getBase()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
            auto *ident = static_cast<const IdentifierExpr *>(indexExpr->getBase());

            // Class subscript setter: obj[i] = val → ClassName_subscript_set(self, i, val)
            auto cvIt = vars_.varClassTypes.find(ident->getName());
            if (cvIt != vars_.varClassTypes.end()) {
                std::string setName = cvIt->second + "_subscript_set";
                auto *setFn = module_->getFunction(setName);
                if (setFn) {
                    auto *ptrTy = llvm::PointerType::getUnqual(*context_);
                    auto nvIt = vars_.namedValues.find(ident->getName());
                    if (nvIt != vars_.namedValues.end()) {
                        auto *selfAlloca = nvIt->second;
                        llvm::Value *selfLoaded = selfAlloca;
                        if (selfAlloca->getAllocatedType()->isPointerTy()) {
                            selfLoaded = builder_->CreateLoad(ptrTy, selfAlloca, "sub.set.self");
                        }
                        auto *idxVal = visit(const_cast<Expr *>(indexExpr->getIndex()));
                        if (idxVal) {
                            builder_->CreateCall(setFn, {selfLoaded, idxVal, val});
                            return val;
                        }
                    }
                }
            }

            // Dynamic array index assign: arr[i] = val
            auto daIt = vars_.varDynArrayTypes.find(ident->getName());
            if (daIt != vars_.varDynArrayTypes.end()) {
                auto allocaIt = vars_.namedValues.find(ident->getName());
                if (allocaIt != vars_.namedValues.end()) {
                    auto *arrAlloca = allocaIt->second;
                    auto *structTy = getDynArrayStructTy();
                    auto *dataField = builder_->CreateStructGEP(structTy, arrAlloca, 0);
                    auto *dataPtr = builder_->CreateLoad(
                        llvm::PointerType::getUnqual(*context_), dataField);
                    auto *indexVal = visit(const_cast<Expr *>(indexExpr->getIndex()));
                    if (!indexVal) return val;
                    if (indexVal->getType()->isIntegerTy(32))
                        indexVal = builder_->CreateSExt(indexVal, builder_->getInt64Ty());
                    auto *lenField = builder_->CreateStructGEP(structTy, arrAlloca, 1);
                    auto *lenVal = builder_->CreateLoad(builder_->getInt64Ty(), lenField, "darr.len");
                    emitBoundsCheck(indexVal, lenVal);
                    auto *elemPtr = builder_->CreateGEP(daIt->second.elementType, dataPtr, indexVal);
                    builder_->CreateStore(val, elemPtr);
                    // Ownership transfers to the array element — don't free as temp
                    if (val->getType()->isPointerTy())
                        removeFromTempStrings(val);
                }
                return val;
            }

            auto arrIt = vars_.varArrayTypes.find(ident->getName());
            auto allocaIt = vars_.namedValues.find(ident->getName());
            if (arrIt != vars_.varArrayTypes.end() && allocaIt != vars_.namedValues.end()) {
                auto *alloca = allocaIt->second;
                auto *arrayType = alloca->getAllocatedType();
                auto *indexVal = visit(const_cast<Expr *>(indexExpr->getIndex()));
                if (!indexVal) return val;
                if (indexVal->getType()->isIntegerTy(32))
                    indexVal = builder_->CreateSExt(indexVal, builder_->getInt64Ty(), "idx.ext");
                emitBoundsCheck(indexVal, builder_->getInt64(arrIt->second.size));
                auto *gep = builder_->CreateInBoundsGEP(
                    arrayType, alloca, {builder_->getInt64(0), indexVal},
                    ident->getName() + ".elem");
                builder_->CreateStore(val, gep);
                // Ownership transfers to the array element — don't free as temp
                if (val->getType()->isPointerTy())
                    removeFromTempStrings(val);
            }
        }
        return val;
    }

    if (node->getTarget()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
        auto *ident = static_cast<IdentifierExpr *>(node->getTarget());
        auto it = vars_.namedValues.find(ident->getName());

        // Handle optional variable assignment: x = 42, x = nil
        auto optIt = vars_.varOptionalTypes.find(ident->getName());
        if (optIt != vars_.varOptionalTypes.end() && it != vars_.namedValues.end()) {
            bool isNil = (node->getValue()->getKind() == ASTNode::NodeKind::NilLiteralExpr);
            auto *optStructTy = getOptionalType(optIt->second);
            auto *hasValPtr = builder_->CreateStructGEP(optStructTy, it->second, 0);
            auto *valPtr = builder_->CreateStructGEP(optStructTy, it->second, 1);
            if (isNil) {
                builder_->CreateStore(builder_->getFalse(), hasValPtr);
                builder_->CreateStore(llvm::Constant::getNullValue(optIt->second), valPtr);
            } else {
                builder_->CreateStore(builder_->getTrue(), hasValPtr);
                if (val) builder_->CreateStore(val, valPtr);
            }
            return val;
        }

        // Handle ref mut assignment: store through pointer
        auto refIt = vars_.varRefTypes.find(ident->getName());
        if (refIt != vars_.varRefTypes.end() && it != vars_.namedValues.end()) {
            auto *ptr = builder_->CreateLoad(
                llvm::PointerType::getUnqual(*context_), it->second,
                ident->getName() + ".ptr");
            if (node->getOp() != AssignExpr::Op::Assign) {
                auto *current = builder_->CreateLoad(refIt->second, ptr, ident->getName());
                switch (node->getOp()) {
                case AssignExpr::Op::AddAssign:
                    val = builder_->CreateAdd(current, val, "addtmp");
                    break;
                case AssignExpr::Op::SubAssign:
                    val = builder_->CreateSub(current, val, "subtmp");
                    break;
                case AssignExpr::Op::MulAssign:
                    val = builder_->CreateMul(current, val, "multmp");
                    break;
                case AssignExpr::Op::DivAssign:
                    val = builder_->CreateSDiv(current, val, "divtmp");
                    break;
                case AssignExpr::Op::ModAssign:
                    val = builder_->CreateSRem(current, val, "modtmp");
                    break;
                default:
                    break;
                }
            }
            builder_->CreateStore(val, ptr);
            return val;
        }

        if (it != vars_.namedValues.end()) {
            // Handle compound assignment
            if (node->getOp() != AssignExpr::Op::Assign) {
                auto *current = builder_->CreateLoad(it->second->getAllocatedType(),
                                                      it->second, ident->getName());
                switch (node->getOp()) {
                case AssignExpr::Op::AddAssign:
                    val = builder_->CreateAdd(current, val, "addtmp");
                    break;
                case AssignExpr::Op::SubAssign:
                    val = builder_->CreateSub(current, val, "subtmp");
                    break;
                case AssignExpr::Op::MulAssign:
                    val = builder_->CreateMul(current, val, "multmp");
                    break;
                case AssignExpr::Op::DivAssign:
                    val = builder_->CreateSDiv(current, val, "divtmp");
                    break;
                case AssignExpr::Op::ModAssign:
                    val = builder_->CreateSRem(current, val, "modtmp");
                    break;
                default:
                    break;
                }
            }

            // Free old heap string before reassignment
            if (node->getOp() == AssignExpr::Op::Assign &&
                vars_.heapStringVars.count(ident->getName())) {
                auto *freeFn = module_->getFunction("free");
                if (freeFn) {
                    auto *old = builder_->CreateLoad(
                        llvm::PointerType::getUnqual(*context_), it->second, "old.str");
                    builder_->CreateCall(freeFn, {old});
                }
                // Transfer ownership from vars_.tempStrings if applicable.
                // Storing a literal (Constant ptr) into a heap-tracked string
                // var: dup it first so the slot keeps holding heap memory.
                // Otherwise the next reassignment's free(old) would free a
                // string literal (heap corruption) and intervening reads
                // would alias the global.
                auto tIt = std::find(vars_.tempStrings.begin(), vars_.tempStrings.end(), val);
                if (tIt != vars_.tempStrings.end()) {
                    vars_.tempStrings.erase(tIt);
                    // keep in vars_.heapStringVars
                } else if (val && val->getType()->isPointerTy() &&
                           llvm::isa<llvm::Constant>(val) &&
                           it->second->getAllocatedType()->isPointerTy()) {
                    val = builder_->CreateCall(getOrPanic("liva_str_dup"),
                                                {val}, ident->getName() + ".own");
                    // current still in vars_.heapStringVars
                } else {
                    vars_.heapStringVars.erase(ident->getName());
                }
            } else if (node->getOp() == AssignExpr::Op::Assign &&
                       val && val->getType()->isPointerTy() &&
                       it->second->getAllocatedType()->isPointerTy()) {
                // First-time assignment of a heap string into a ptr-typed
                // variable that wasn't yet heap-tracked (e.g.
                // `var s: string = ""; s = strToUpper(x)`). Without this,
                // statement-end cleanup frees the temp while the variable
                // still references it → use-after-free on the next read.
                auto tIt = std::find(vars_.tempStrings.begin(), vars_.tempStrings.end(), val);
                if (tIt != vars_.tempStrings.end()) {
                    vars_.tempStrings.erase(tIt);
                    vars_.heapStringVars.insert(ident->getName());
                }
            }

            builder_->CreateStore(val, it->second);
        }
    }

    return val;
}

std::string IRGen::resolveExprClassTypeName(Expr *expr) {
    if (!expr) return {};

    // Fast path: use Sema's resolved type if available.
    if (auto *rt = expr->getResolvedType()) {
        if (rt->getKind() == TypeRepr::Kind::Named) {
            auto *named = static_cast<const NamedTypeRepr *>(rt);
            if (classTypes_.count(named->getName()))
                return named->getName();
        }
    }

    // Identifier: look up in varClassTypes (works for locals, params, 'self').
    if (expr->getKind() == ASTNode::NodeKind::IdentifierExpr) {
        auto *ident = static_cast<IdentifierExpr *>(expr);
        auto it = vars_.varClassTypes.find(ident->getName());
        if (it != vars_.varClassTypes.end())
            return it->second;
    }

    // MemberExpr obj.field: recursively resolve obj's class type, then look up
    // the field's declared type in classFieldTypeReprs_.
    if (expr->getKind() == ASTNode::NodeKind::MemberExpr) {
        auto *mem = static_cast<MemberExpr *>(expr);
        std::string objClass = resolveExprClassTypeName(mem->getObject());
        if (!objClass.empty()) {
            auto cfnIt = classFieldNames_.find(objClass);
            auto cftrIt = classFieldTypeReprs_.find(objClass);
            if (cfnIt != classFieldNames_.end() && cftrIt != classFieldTypeReprs_.end()) {
                for (size_t i = 0; i < cfnIt->second.size(); ++i) {
                    if (cfnIt->second[i] == mem->getMember()) {
                        if (i < cftrIt->second.size()) {
                            auto *fieldType = cftrIt->second[i];
                            if (fieldType && fieldType->getKind() == TypeRepr::Kind::Named) {
                                auto *named = static_cast<const NamedTypeRepr *>(fieldType);
                                if (classTypes_.count(named->getName()))
                                    return named->getName();
                            }
                        }
                        break;
                    }
                }
            }
        }
    }

    return {};
}

llvm::Value *IRGen::visitMemberExpr(MemberExpr *node) {
    if (diBuilder_) emitDebugLocation(node->getStartLoc());
    // Optional chaining: obj?.field
    if (node->isOptionalChain())
        return emitOptionalChainMember(node);

    // Tuple element access: pair.0, pair.1
    if (node->getObject()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
        auto *ident = static_cast<IdentifierExpr *>(node->getObject());
        auto tupleIt = vars_.varTupleTypes.find(ident->getName());
        if (tupleIt != vars_.varTupleTypes.end()) {
            const auto &member = node->getMember();
            bool isNumeric = !member.empty();
            for (char c : member) {
                if (c < '0' || c > '9') { isNumeric = false; break; }
            }
            if (isNumeric) {
                unsigned idx = (unsigned)strtol(member.c_str(), nullptr, 10);
                auto *baseAlloca = vars_.namedValues[ident->getName()];
                auto &ti = tupleIt->second;
                auto *tupleTy = llvm::StructType::get(*context_, ti.elementTypes);
                auto *gep = builder_->CreateStructGEP(tupleTy, baseAlloca, idx);
                return builder_->CreateLoad(ti.elementTypes[idx], gep, "tuple.elem");
            }
        }
    }

    // Check for string.length (UTF-8 code point count)
    if (node->getObject()->getResolvedType() &&
        node->getObject()->getResolvedType()->getKind() == TypeRepr::Kind::String &&
        node->getMember() == "length") {
        auto *obj = visit(node->getObject());
        if (!obj) return nullptr;
        auto *lenFn = getOrPanic("liva_str_length");
        return builder_->CreateCall(lenFn, {obj});
    }

    // Check for string.byteLength (byte count)
    if (node->getObject()->getResolvedType() &&
        node->getObject()->getResolvedType()->getKind() == TypeRepr::Kind::String &&
        node->getMember() == "byteLength") {
        auto *obj = visit(node->getObject());
        if (!obj) return nullptr;
        auto *byteLenFn = getOrPanic("liva_str_byte_length");
        return builder_->CreateCall(byteLenFn, {obj});
    }

    // Dynamic array properties: arr.length, arr.capacity, arr.isEmpty
    if (node->getObject()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
        auto *ident = static_cast<IdentifierExpr *>(node->getObject());
        auto daIt = vars_.varDynArrayTypes.find(ident->getName());
        if (daIt != vars_.varDynArrayTypes.end()) {
            auto allocaIt = vars_.namedValues.find(ident->getName());
            if (allocaIt != vars_.namedValues.end()) {
                auto *arrAlloca = allocaIt->second;
                auto *structTy = getDynArrayStructTy();

                if (node->getMember() == "length") {
                    auto *lenField = builder_->CreateStructGEP(structTy, arrAlloca, 1);
                    return builder_->CreateLoad(builder_->getInt64Ty(), lenField, "arr.len");
                }
                if (node->getMember() == "capacity") {
                    auto *capField = builder_->CreateStructGEP(structTy, arrAlloca, 2);
                    return builder_->CreateLoad(builder_->getInt64Ty(), capField, "arr.cap");
                }
                if (node->getMember() == "isEmpty") {
                    auto *lenField = builder_->CreateStructGEP(structTy, arrAlloca, 1);
                    auto *len = builder_->CreateLoad(builder_->getInt64Ty(), lenField);
                    return builder_->CreateICmpEQ(len, builder_->getInt64(0), "arr.empty");
                }
            }
        }
    }

    // DynArray properties on struct member field: self.grades.length, self.grades.isEmpty
    if (node->getObject()->getKind() == ASTNode::NodeKind::MemberExpr) {
        auto *innerMember = static_cast<MemberExpr *>(node->getObject());
        auto daInfo = resolveMemberDynArray(innerMember);
        if (daInfo) {
            auto *arrGEP = daInfo->arrGEP;
            auto *structTy = getDynArrayStructTy();

            if (node->getMember() == "length") {
                auto *lenField = builder_->CreateStructGEP(structTy, arrGEP, 1);
                return builder_->CreateLoad(builder_->getInt64Ty(), lenField, "mda.len");
            }
            if (node->getMember() == "capacity") {
                auto *capField = builder_->CreateStructGEP(structTy, arrGEP, 2);
                return builder_->CreateLoad(builder_->getInt64Ty(), capField, "mda.cap");
            }
            if (node->getMember() == "isEmpty") {
                auto *lenField = builder_->CreateStructGEP(structTy, arrGEP, 1);
                auto *len = builder_->CreateLoad(builder_->getInt64Ty(), lenField);
                return builder_->CreateICmpEQ(len, builder_->getInt64(0), "mda.empty");
            }
        }
    }

    // Map/Set properties: m.size, m.isEmpty
    if (node->getObject()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
        auto *ident = static_cast<IdentifierExpr *>(node->getObject());
        auto mapIt = vars_.varMapTypes.find(ident->getName());
        if (mapIt != vars_.varMapTypes.end()) {
            auto allocaIt = vars_.namedValues.find(ident->getName());
            if (allocaIt != vars_.namedValues.end()) {
                auto *mapAlloca = allocaIt->second;
                auto *structTy = getMapStructTy();
                if (node->getMember() == "size") {
                    auto *sizeField = builder_->CreateStructGEP(structTy, mapAlloca, 1);
                    return builder_->CreateLoad(builder_->getInt64Ty(), sizeField, "map.size");
                }
                if (node->getMember() == "isEmpty") {
                    auto *sizeField = builder_->CreateStructGEP(structTy, mapAlloca, 1);
                    auto *sz = builder_->CreateLoad(builder_->getInt64Ty(), sizeField);
                    return builder_->CreateICmpEQ(sz, builder_->getInt64(0), "map.empty");
                }
            }
        }
        auto setIt = vars_.varSetTypes.find(ident->getName());
        if (setIt != vars_.varSetTypes.end()) {
            auto allocaIt = vars_.namedValues.find(ident->getName());
            if (allocaIt != vars_.namedValues.end()) {
                auto *setAlloca = allocaIt->second;
                auto *structTy = getMapStructTy();
                if (node->getMember() == "size") {
                    auto *sizeField = builder_->CreateStructGEP(structTy, setAlloca, 1);
                    return builder_->CreateLoad(builder_->getInt64Ty(), sizeField, "set.size");
                }
                if (node->getMember() == "isEmpty") {
                    auto *sizeField = builder_->CreateStructGEP(structTy, setAlloca, 1);
                    auto *sz = builder_->CreateLoad(builder_->getInt64Ty(), sizeField);
                    return builder_->CreateICmpEQ(sz, builder_->getInt64(0), "set.empty");
                }
            }
        }
    }

    // Result properties: r.isOk, r.isErr
    if (node->getObject()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
        auto *ident = static_cast<IdentifierExpr *>(node->getObject());
        auto rtIt = vars_.varResultTypes.find(ident->getName());
        if (rtIt != vars_.varResultTypes.end()) {
            auto nvIt = vars_.namedValues.find(ident->getName());
            if (nvIt != vars_.namedValues.end()) {
                auto *resTy = getResultType(rtIt->second.okType, rtIt->second.errType);
                auto *tagPtr = builder_->CreateStructGEP(resTy, nvIt->second, 0, "res.tag");
                auto *tag = builder_->CreateLoad(builder_->getInt32Ty(), tagPtr, "res.tag.val");
                if (node->getMember() == "isOk")
                    return builder_->CreateICmpEQ(tag, builder_->getInt32(0), "res.isok");
                if (node->getMember() == "isErr")
                    return builder_->CreateICmpEQ(tag, builder_->getInt32(1), "res.iserr");
            }
        }
    }

    // Check for enum case reference: Color.Red
    if (node->getObject()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
        auto *ident = static_cast<IdentifierExpr *>(node->getObject());
        auto eIt = enumCases_.find(ident->getName());
        if (eIt != enumCases_.end()) {
            auto cIt = eIt->second.find(node->getMember());
            if (cIt != eIt->second.end())
                return builder_->getInt32(cIt->second);
            diag_.report(node->getStartLoc(), DiagID::err_irgen_unknown_enum_case,
                         ident->getName(), node->getMember());
            return nullptr;
        }
    }

    // Find the object's alloca and struct type
    std::string objName;
    llvm::AllocaInst *objAlloca = nullptr;
    std::string structTypeName;

    if (node->getObject()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
        auto *ident = static_cast<IdentifierExpr *>(node->getObject());
        objName = ident->getName();
        auto it = vars_.namedValues.find(objName);
        if (it != vars_.namedValues.end())
            objAlloca = it->second;
        auto stIt = vars_.varStructTypes.find(objName);
        if (stIt != vars_.varStructTypes.end())
            structTypeName = stIt->second;
    }

    // Static field read: ClassName.field → global variable load
    if (!objName.empty() && !objAlloca && classNames_.count(objName)) {
        std::string key = objName + "." + node->getMember();
        auto sfIt = classStaticFields_.find(key);
        if (sfIt != classStaticFields_.end()) {
            auto *gv = sfIt->second;
            return builder_->CreateLoad(gv->getValueType(), gv, node->getMember() + ".static");
        }
    }

    // Lazy field access: obj.lazyField → call ClassName_lazy_<field>(self)
    if (objAlloca && !objName.empty()) {
        auto clsIt3 = vars_.varClassTypes.find(objName);
        if (clsIt3 != vars_.varClassTypes.end()) {
            auto lazyIt = classLazyFields_.find(clsIt3->second);
            if (lazyIt != classLazyFields_.end() && lazyIt->second.count(node->getMember())) {
                std::string accName = clsIt3->second + "_lazy_" + node->getMember();
                auto *accFn = module_->getFunction(accName);
                if (accFn) {
                    auto *ptrTy = llvm::PointerType::getUnqual(*context_);
                    llvm::Value *selfVal = objAlloca;
                    if (objAlloca->getAllocatedType()->isPointerTy()) {
                        selfVal = builder_->CreateLoad(ptrTy, objAlloca, objName + ".ptr");
                    }
                    return builder_->CreateCall(accFn, {selfVal}, node->getMember() + ".lazy");
                }
            }
        }
    }

    // Computed property getter: obj.computedField → call getter function
    if (objAlloca && !objName.empty()) {
        auto clsIt2 = vars_.varClassTypes.find(objName);
        if (clsIt2 != vars_.varClassTypes.end()) {
            auto cpIt = classComputedFields_.find(clsIt2->second);
            if (cpIt != classComputedFields_.end() && cpIt->second.count(node->getMember())) {
                std::string getterName = clsIt2->second + "_get_" + node->getMember();
                auto *getter = module_->getFunction(getterName);
                if (getter) {
                    auto *ptrTy = llvm::PointerType::getUnqual(*context_);
                    llvm::Value *selfVal = objAlloca;
                    if (objAlloca->getAllocatedType()->isPointerTy()) {
                        selfVal = builder_->CreateLoad(ptrTy, objAlloca, objName + ".ptr");
                    }
                    return builder_->CreateCall(getter, {selfVal}, node->getMember() + ".get");
                }
            }
        }
    }

    // Class field access: obj.field where obj is a class instance
    if (objAlloca && !objName.empty()) {
        auto clsIt = vars_.varClassTypes.find(objName);
        if (clsIt != vars_.varClassTypes.end()) {
            const std::string &clsTypeName = clsIt->second;
            auto ctIt = classTypes_.find(clsTypeName);
            if (ctIt != classTypes_.end()) {
                auto *classTy = ctIt->second;
                auto cfIt = classFieldNames_.find(clsTypeName);
                if (cfIt != classFieldNames_.end()) {
                    int fieldIdx = -1;
                    for (size_t i = 0; i < cfIt->second.size(); ++i) {
                        if (cfIt->second[i] == node->getMember()) {
                            fieldIdx = static_cast<int>(i);
                            break;
                        }
                    }
                    if (fieldIdx >= 0) {
                        // field index in struct = fieldIdx + 1 (vtable ptr at 0)
                        int structIdx = fieldIdx + 1;
                        llvm::Value *basePtr = objAlloca;
                        if (objAlloca->getAllocatedType()->isPointerTy()) {
                            basePtr = builder_->CreateLoad(
                                objAlloca->getAllocatedType(), objAlloca, objName + ".ptr");
                        }
                        auto *gep = builder_->CreateStructGEP(
                            classTy, basePtr, structIdx, node->getMember());
                        return builder_->CreateLoad(
                            classTy->getElementType(structIdx), gep,
                            node->getMember() + ".val");
                    }
                }
            }
        }
    }

    if (!objAlloca || structTypeName.empty()) {
        // Fallback: resolve struct type from AST resolved type
        // Handles chained access (self.color.r) and unregistered local struct vars
        if (node->getObject()->getResolvedType()) {
            auto *resolvedType = node->getObject()->getResolvedType();
            if (resolvedType->getKind() == TypeRepr::Kind::Named) {
                auto *namedType = static_cast<const NamedTypeRepr *>(resolvedType);
                auto stIt = structTypes_.find(namedType->getName());
                if (stIt != structTypes_.end()) {
                    auto *structTy = stIt->second;
                    int idx = getStructFieldIndex(namedType->getName(), node->getMember());
                    if (idx >= 0) {
                        llvm::Value *basePtr = nullptr;
                        if (objAlloca) {
                            // Local var with alloca but not in vars_.varStructTypes
                            basePtr = objAlloca;
                            if (objAlloca->getAllocatedType()->isPointerTy()) {
                                basePtr = builder_->CreateLoad(
                                    objAlloca->getAllocatedType(), objAlloca, objName);
                            }
                        } else {
                            // Non-identifier object (e.g. self.color in self.color.r)
                            auto *objVal = visit(node->getObject());
                            if (!objVal) return nullptr;
                            auto *curFunc = builder_->GetInsertBlock()->getParent();
                            auto *tmpAlloca = createEntryBlockAlloca(
                                curFunc, "chain.tmp", structTy);
                            builder_->CreateStore(objVal, tmpAlloca);
                            basePtr = tmpAlloca;
                        }
                        auto *gep = builder_->CreateStructGEP(
                            structTy, basePtr, idx, node->getMember());
                        return builder_->CreateLoad(
                            structTy->getElementType(idx), gep,
                            node->getMember() + ".val");
                    }
                }
                // Class-typed object (e.g. chained access h.inner.v where
                // `h.inner` resolves to a class type). Class instances are
                // heap pointers, and the field layout has the vtable slot at
                // index 0, so the field is at getClassFieldIndex + 1.
                auto ctIt = classTypes_.find(namedType->getName());
                if (ctIt != classTypes_.end()) {
                    auto *classTy = ctIt->second;
                    auto cfIt = classFieldNames_.find(namedType->getName());
                    if (cfIt != classFieldNames_.end()) {
                        int fieldIdx = -1;
                        for (size_t i = 0; i < cfIt->second.size(); ++i) {
                            if (cfIt->second[i] == node->getMember()) {
                                fieldIdx = static_cast<int>(i);
                                break;
                            }
                        }
                        if (fieldIdx >= 0) {
                            int structIdx = fieldIdx + 1; // vtable ptr at 0
                            // The object value IS the class pointer. For an
                            // identifier with an alloca, load the pointer; for
                            // a chained/non-identifier object, visit it.
                            llvm::Value *basePtr = nullptr;
                            if (objAlloca &&
                                objAlloca->getAllocatedType()->isPointerTy()) {
                                basePtr = builder_->CreateLoad(
                                    objAlloca->getAllocatedType(), objAlloca,
                                    objName + ".ptr");
                            } else {
                                basePtr = visit(node->getObject());
                                if (!basePtr) return nullptr;
                            }
                            auto *gep = builder_->CreateStructGEP(
                                classTy, basePtr, structIdx, node->getMember());
                            return builder_->CreateLoad(
                                classTy->getElementType(structIdx), gep,
                                node->getMember() + ".val");
                        }
                    }
                }
            }
        }
        // Fallback for class field access when Sema did not set resolvedType on
        // the object expression (e.g. `self.inner.v` inside a class method body
        // where TypeChecker uses currentClassName_ rather than
        // currentImplTypeName_, so `self.inner` has no resolvedType).
        // Use resolveExprClassTypeName() to walk the maps directly.
        {
            std::string clsName = resolveExprClassTypeName(node->getObject());
            if (!clsName.empty()) {
                auto ctIt2 = classTypes_.find(clsName);
                auto cfIt2 = classFieldNames_.find(clsName);
                if (ctIt2 != classTypes_.end() && cfIt2 != classFieldNames_.end()) {
                    int fieldIdx = -1;
                    for (size_t i = 0; i < cfIt2->second.size(); ++i) {
                        if (cfIt2->second[i] == node->getMember()) {
                            fieldIdx = static_cast<int>(i);
                            break;
                        }
                    }
                    if (fieldIdx >= 0) {
                        int structIdx = fieldIdx + 1; // vtable ptr at index 0
                        auto *classTy = ctIt2->second;
                        llvm::Value *basePtr = visit(node->getObject());
                        if (!basePtr) return nullptr;
                        auto *gep = builder_->CreateStructGEP(
                            classTy, basePtr, structIdx, node->getMember());
                        return builder_->CreateLoad(
                            classTy->getElementType(structIdx), gep,
                            node->getMember() + ".val");
                    }
                }
            }
        }
        diag_.report(node->getStartLoc(), DiagID::err_irgen_member_resolve_failed, node->getMember());
        return nullptr;
    }

    auto stIt = structTypes_.find(structTypeName);
    if (stIt == structTypes_.end()) {
        diag_.report(node->getStartLoc(), DiagID::err_irgen_unknown_struct, structTypeName);
        return nullptr;
    }

    auto *structTy = stIt->second;
    int idx = getStructFieldIndex(structTypeName, node->getMember());
    if (idx < 0) {
        diag_.report(node->getStartLoc(), DiagID::err_irgen_unknown_field,
                     structTypeName, node->getMember());
        return nullptr;
    }

    // Check if the alloca holds a pointer to struct (self parameter case)
    llvm::Value *basePtr = objAlloca;
    if (objAlloca->getAllocatedType()->isPointerTy()) {
        basePtr = builder_->CreateLoad(objAlloca->getAllocatedType(), objAlloca, objName);
    }

    auto *gep = builder_->CreateStructGEP(structTy, basePtr, idx, node->getMember());
    return builder_->CreateLoad(structTy->getElementType(idx), gep,
                                 node->getMember() + ".val");
}

llvm::Value *IRGen::visitStructLiteralExpr(StructLiteralExpr *node) {
    // Standalone struct literal (not in VarDecl context)
    const auto &typeName = node->getTypeName();

    // Check for generic struct
    auto gsIt = genericStructDecls_.find(typeName);
    if (gsIt != genericStructDecls_.end()) {
        std::vector<llvm::Value *> fieldValues;
        for (auto &fieldInit : node->getFields()) {
            auto *val = visit(fieldInit.value.get());
            fieldValues.push_back(val);
        }

        // Resolve type args in priority order:
        //  1. Explicit turbofish on the literal: `Stream::<i64> { ... }`
        //  2. Surrounding monomorphization context (`return Stream { ... }`
        //     inside Stream_string_from has T pinned via currentTypeSubst_)
        //  3. Inference from field init values (only catches `var x: T`)
        const auto &typeParams = gsIt->second->getTypeParams();
        std::vector<const TypeRepr *> typeArgs;
        if (!node->getTypeArgs().empty()) {
            // Substitute type-param refs through the surrounding mono context
            // so that `return Stream<T> { ... }` inside Stream_string_from
            // produces Stream_string, not Stream_T.
            for (auto &ta : node->getTypeArgs())
                typeArgs.push_back(substituteTypeRepr(ta.get(), currentTypeSubst_));
        }
        if (typeArgs.size() != typeParams.size() && !currentTypeSubst_.empty()) {
            typeArgs.clear();
            for (const auto &tp : typeParams) {
                auto it = currentTypeSubst_.find(tp);
                if (it != currentTypeSubst_.end())
                    typeArgs.push_back(it->second);
            }
        }
        if (typeArgs.size() != typeParams.size()) {
            typeArgs = inferStructTypeArgs(gsIt->second, node->getFields(), fieldValues);
        }
        monomorphizeStruct(gsIt->second, typeArgs);
        std::string mangledName = mangleGenericStruct(typeName, typeArgs);

        auto *structTy = structTypes_[mangledName];
        auto *func = builder_->GetInsertBlock()->getParent();
        auto *alloca = createEntryBlockAlloca(func, mangledName + ".tmp", structTy);
        builder_->CreateStore(llvm::Constant::getNullValue(structTy), alloca);

        for (size_t i = 0; i < node->getFields().size(); ++i) {
            int idx = getStructFieldIndex(mangledName, node->getFields()[i].name);
            if (idx < 0 || !fieldValues[i])
                continue;
            auto *val = dupIfStringField(mangledName, idx, fieldValues[i]);
            val = cloneIfDynArrayField(mangledName, idx, val,
                                       node->getFields()[i].name);
            auto *gep = builder_->CreateStructGEP(structTy, alloca, idx, node->getFields()[i].name);
            builder_->CreateStore(val, gep);
        }

        return builder_->CreateLoad(structTy, alloca, mangledName + ".val");
    }

    auto stIt = structTypes_.find(typeName);
    if (stIt == structTypes_.end()) {
        diag_.report(node->getStartLoc(), DiagID::err_irgen_struct_literal_unknown, typeName);
        return nullptr;
    }

    auto *structTy = stIt->second;
    auto *func = builder_->GetInsertBlock()->getParent();
    auto *alloca = createEntryBlockAlloca(func, typeName + ".tmp", structTy);
    builder_->CreateStore(llvm::Constant::getNullValue(structTy), alloca);

    for (auto &fieldInit : node->getFields()) {
        int idx = getStructFieldIndex(typeName, fieldInit.name);
        if (idx < 0)
            continue;
        auto *val = visit(fieldInit.value.get());
        if (!val)
            continue;
        val = dupIfStringField(typeName, idx, val);
        val = cloneIfDynArrayField(typeName, idx, val, fieldInit.name);
        auto *gep = builder_->CreateStructGEP(structTy, alloca, idx, fieldInit.name);
        builder_->CreateStore(val, gep);
    }

    return builder_->CreateLoad(structTy, alloca, typeName + ".val");
}

llvm::Value *IRGen::emitEnumCaseConstruct(const std::string &enumName,
                                           const std::string &caseName, int tag,
                                           const std::vector<std::unique_ptr<Expr>> &args) {
    auto etIt = enumTypes_.find(enumName);
    if (etIt == enumTypes_.end()) {
        diag_.report(SourceLocation{}, DiagID::err_irgen_unknown_enum, enumName);
        return nullptr;
    }

    auto *enumStructTy = etIt->second;
    auto *func = builder_->GetInsertBlock()->getParent();
    auto *alloca = createEntryBlockAlloca(func, enumName + ".tmp", enumStructTy);

    // Store tag
    auto *tagPtr = builder_->CreateStructGEP(enumStructTy, alloca, 0, "tag.ptr");
    builder_->CreateStore(builder_->getInt32(tag), tagPtr);

    // Store payload
    if (!args.empty()) {
        auto *payloadPtr = builder_->CreateStructGEP(enumStructTy, alloca, 1, "payload.ptr");
        const llvm::DataLayout &dl = module_->getDataLayout();

        // Get the payload types for this case
        auto &payloadTypes = enumCasePayloads_[enumName][caseName];
        uint64_t offset = 0;
        for (size_t i = 0; i < args.size(); ++i) {
            auto *val = visit(args[i].get());
            if (!val)
                continue;
            auto *fieldPtr = builder_->CreateConstInBoundsGEP1_64(
                builder_->getInt8Ty(), payloadPtr, offset, "field." + std::to_string(i));
            builder_->CreateStore(val, fieldPtr);
            if (i < payloadTypes.size())
                offset += dl.getTypeAllocSize(payloadTypes[i]);
        }
    }

    return builder_->CreateLoad(enumStructTy, alloca, enumName + ".val");
}

llvm::Value *IRGen::visitMatchExpr(MatchExpr *node) {
    if (diBuilder_) emitDebugLocation(node->getStartLoc());
    auto *func = builder_->GetInsertBlock()->getParent();

    // Find enum type name from subject (if it's an identifier)
    std::string enumTypeName;
    std::string subjectVarName;
    bool isResultMatch = false;
    if (node->getSubject()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
        auto *ident = static_cast<const IdentifierExpr *>(node->getSubject());
        subjectVarName = ident->getName();
        auto it = vars_.varEnumTypes.find(subjectVarName);
        if (it != vars_.varEnumTypes.end())
            enumTypeName = it->second;

        // Check for Result type match
        auto rtIt = vars_.varResultTypes.find(subjectVarName);
        if (rtIt != vars_.varResultTypes.end()) {
            isResultMatch = true;
            enumTypeName = "Result";
            // Set up temporary enum infrastructure for Result
            enumCases_["Result"] = {{"Ok", 0}, {"Err", 1}};
            auto *resTy = getResultType(rtIt->second.okType, rtIt->second.errType);
            enumTypes_["Result"] = resTy;
            enumCasePayloads_["Result"]["Ok"] = {rtIt->second.okType};
            enumCasePayloads_["Result"]["Err"] = {rtIt->second.errType};
            vars_.varEnumTypes[subjectVarName] = "Result";
        }
    }

    // Determine if this is a payload enum match
    bool isPayloadEnum = !enumTypeName.empty() &&
                          enumTypes_.find(enumTypeName) != enumTypes_.end();

    llvm::Value *tagVal = nullptr;
    llvm::AllocaInst *subjectAlloca = nullptr;

    if (isPayloadEnum) {
        // Payload enum: load tag from struct field 0
        auto nIt = vars_.namedValues.find(subjectVarName);
        if (nIt == vars_.namedValues.end()) {
            diag_.report(node->getStartLoc(), DiagID::err_irgen_match_subject_failed, subjectVarName);
            return nullptr;
        }
        subjectAlloca = nIt->second;
        auto *enumStructTy = enumTypes_[enumTypeName];
        auto *tagPtr = builder_->CreateStructGEP(enumStructTy, subjectAlloca, 0, "tag.ptr");
        tagVal = builder_->CreateLoad(builder_->getInt32Ty(), tagPtr, "tag");
    } else {
        // Simple enum or integer: evaluate subject directly
        tagVal = visit(const_cast<Expr *>(node->getSubject()));
        if (!tagVal) {
            diag_.report(node->getStartLoc(), DiagID::err_irgen_match_subject_failed, "expression");
            return nullptr;
        }
    }

    auto *mergeBB = llvm::BasicBlock::Create(*context_, "match.end", func);

    // Parse all arm patterns
    struct ArmInfo {
        llvm::BasicBlock *bb;
        PatternInfo pat;
    };
    std::vector<ArmInfo> armInfos;
    int defaultIdx = -1;

    for (size_t i = 0; i < node->getArms().size(); ++i) {
        auto &arm = node->getArms()[i];
        auto *armBB = llvm::BasicBlock::Create(*context_,
            "match.arm." + std::to_string(i), func);
        auto pat = resolveMatchPattern(arm.patternNode.get(), enumTypeName);
        if (pat.isWildcard)
            defaultIdx = static_cast<int>(i);
        armInfos.push_back({armBB, std::move(pat)});
    }

    // Count non-default, non-wildcard cases. Uses hasTag (not `tag >= 0` —
    // see PatternInfo::hasTag) so a negative int-literal arm (e.g. `-1 =>`)
    // is correctly counted as a real switch case instead of being mistaken
    // for the "no tag" sentinel.
    unsigned numCases = 0;
    for (auto &info : armInfos) {
        if (!info.pat.isWildcard && info.pat.hasTag)
            ++numCases;
    }

    // Collect arm results for PHI node (match-as-expression)
    std::vector<std::pair<llvm::Value *, llvm::BasicBlock *>> armResults;

    // Pattern Types Faz B, Task 3: a range pattern (`lo..hi` / `lo..=hi`)
    // isn't a single discrete tag value CreateSwitch can dispatch on — it
    // needs a `sge` + `slt`/`sle` comparison pair — so any arm being a range
    // forces the whole match into if-else-chain mode, exactly like a
    // non-integer subject (string/float) already does below. DISPATCH-MODE
    // DECISION: force-the-whole-match-to-chain-mode was chosen over a
    // switch+chain hybrid (e.g. CreateSwitch for the tag-bearing arms with a
    // range-only fallback chain off the switch's default edge) because it
    // reuses the existing, already-correct if-else fallthrough/guard/PHI
    // machinery unchanged and keeps arm ORDER semantics identical to the
    // source text for every arm kind in the match, not just the range one —
    // correctness and a single code path over cleverness.
    bool hasRangePattern = false;
    for (auto &info : armInfos) {
        if (info.pat.isRange) {
            hasRangePattern = true;
            break;
        }
    }

    // Use if-else chain for non-integer types (float/double/pointer), when
    // all arms are guarded bindings (no concrete switch cases), or when any
    // arm is a range pattern.
    bool useIfElseChain = !tagVal->getType()->isIntegerTy() || numCases == 0 || hasRangePattern;

    // Create default block. Pattern Types Faz B, Task 2 REGRESSION FIX:
    // an earlier version of this fix routed the no-wildcard default
    // straight to a fresh `unreachable` block whenever in switch mode. That
    // broke a perfectly legal, Sema-accepted case: a NON-exhaustive
    // switch-mode match used as a STATEMENT (e.g. an int subject with only
    // some values covered and no wildcard — Sema only enforces
    // exhaustiveness for enum/Result subjects) now trapped at runtime
    // instead of benignly falling through to mergeBB. Reverted to the
    // original unconditional `mergeBB` default; see below for how the
    // original PHI-verifier problem (bool `true`/`false` exhaustive match
    // used as an EXPRESSION) is solved instead, without reintroducing the
    // trap.
    llvm::BasicBlock *defaultBB = (defaultIdx >= 0) ? armInfos[defaultIdx].bb : mergeBB;

    // Blocks that branch directly into mergeBB via the "no wildcard arm"
    // default path (only populated when defaultIdx < 0, i.e. defaultBB ==
    // mergeBB) — tracked so a match-as-expression's PHI node (built below)
    // can supply a placeholder incoming value for these predecessor edges.
    // Without this, an exhaustive-but-wildcard-free switch (the idiomatic
    // way to write a bool `true`/`false` match) still creates a live default
    // CFG edge straight into mergeBB even though it's never actually taken
    // at runtime, and a PHI node requires an entry for every predecessor —
    // "PHINode should have one entry for each predecessor" otherwise.
    std::vector<llvm::BasicBlock *> defaultEdgePreds;

    if (!useIfElseChain) {
        auto *switchOriginBB = builder_->GetInsertBlock();
        auto *switchInst = builder_->CreateSwitch(tagVal, defaultBB, numCases);
        // Case constant width must match tagVal's integer type — usually
        // i32 (enum tag / int-literal subject), but a bool subject's tagVal
        // is i1 (Pattern Types Faz B, Task 2: BoolLiteralPattern reuses this
        // switch path via tag/hasTag). A hardcoded getInt32() here would
        // build an i32 case constant against an i1 switch condition, which
        // LLVM rejects.
        auto *tagIntTy = llvm::cast<llvm::IntegerType>(tagVal->getType());
        for (auto &info : armInfos) {
            if (!info.pat.isWildcard && info.pat.hasTag) {
                auto *caseVal = llvm::ConstantInt::get(
                    tagIntTy, static_cast<uint64_t>(info.pat.tag), /*isSigned=*/true);
                switchInst->addCase(caseVal, info.bb);
            }
        }
        if (defaultIdx < 0)
            defaultEdgePreds.push_back(switchOriginBB);
    } else {
        // If-else chain: check guards in order, fall through to next arm
        // First arm gets control from current block
        if (!armInfos.empty()) {
            builder_->CreateBr(armInfos[0].bb);
        } else {
            builder_->CreateBr(mergeBB);
        }
    }

    // Generate arm bodies with binding extraction
    for (size_t i = 0; i < node->getArms().size(); ++i) {
        auto &arm = node->getArms()[i];
        auto &info = armInfos[i];
        builder_->SetInsertPoint(info.bb);

        // Save named values for binding scope
        auto guard = pushVarState();

        // Extract bindings for payload enum arms
        if (isPayloadEnum && !info.pat.bindings.empty() && subjectAlloca) {
            auto *enumStructTy = enumTypes_[enumTypeName];
            auto *payloadPtr = builder_->CreateStructGEP(enumStructTy, subjectAlloca, 1,
                                                          "payload.ptr");
            const llvm::DataLayout &dl = module_->getDataLayout();
            auto &payloadTypes = enumCasePayloads_[enumTypeName][info.pat.caseName];

            uint64_t offset = 0;
            for (size_t b = 0; b < info.pat.bindings.size() && b < payloadTypes.size(); ++b) {
                auto *fieldPtr = builder_->CreateConstInBoundsGEP1_64(
                    builder_->getInt8Ty(), payloadPtr, offset,
                    "bind." + std::to_string(b));

                if (b < info.pat.nestedPatterns.size() && info.pat.nestedPatterns[b].hasTag) {
                    // Nested enum pattern: check inner tag and extract inner bindings
                    emitNestedPatternMatch(fieldPtr, info.pat.nestedPatterns[b], defaultBB, func);
                } else if (!info.pat.bindings[b].empty()) {
                    auto *val = builder_->CreateLoad(payloadTypes[b], fieldPtr,
                                                      info.pat.bindings[b]);
                    auto *bindAlloca = createEntryBlockAlloca(func, info.pat.bindings[b],
                                                               payloadTypes[b]);
                    builder_->CreateStore(val, bindAlloca);
                    vars_.namedValues[info.pat.bindings[b]] = bindAlloca;
                }
                offset += dl.getTypeAllocSize(payloadTypes[b]);
            }
        }

        // Bind subject value for guarded binding patterns (s if s >= 90.0)
        if (useIfElseChain && !info.pat.bindings.empty() && !isPayloadEnum) {
            for (auto &bindName : info.pat.bindings) {
                if (!bindName.empty()) {
                    auto *bindAlloca = createEntryBlockAlloca(func, bindName,
                                                               tagVal->getType());
                    builder_->CreateStore(tagVal, bindAlloca);
                    vars_.namedValues[bindName] = bindAlloca;
                }
            }
        }

        // Pattern-level comparison for string/float literal patterns
        // (Pattern Types Faz B, Task 2): these always take the if-else-chain
        // path (their subject's LLVM type is never an integer eligible for
        // CreateSwitch — a pointer for string, a float/double for float), so
        // unlike the switch path, nothing has verified the pattern actually
        // matches yet. Bool literal patterns don't need this: they reuse
        // the tag/hasTag switch-case machinery above instead.
        llvm::Value *patternCond = nullptr;
        if (useIfElseChain) {
            if (info.pat.isFloatLiteral) {
                auto *litVal = llvm::ConstantFP::get(tagVal->getType(), info.pat.floatValue);
                patternCond = builder_->CreateFCmpOEQ(tagVal, litVal, "pat.feq");
            } else if (info.pat.isStringLiteral) {
                auto *litStr = builder_->CreateGlobalString(info.pat.stringValue, "pat.str");
                auto *equalFn = getOrPanic("liva_str_equal");
                auto *rawResult = builder_->CreateCall(equalFn, {tagVal, litStr}, "pat.streq.raw");
                patternCond = builder_->CreateICmpNE(rawResult, builder_->getInt32(0), "pat.streq");
            } else if (info.pat.isRange && tagVal->getType()->isIntegerTy()) {
                // Pattern Types Faz B, Task 3: `lo <= x && x < hi` (exclusive)
                // / `lo <= x && x <= hi` (inclusive). Sema's
                // err_pattern_type_mismatch rejects range arms on known
                // non-int scalar and known-enum subjects, but subjects Sema
                // cannot classify (struct/tuple/other Named) slip through —
                // the isIntegerTy() guard keeps those on the legacy
                // "patternCond stays null → arm matches unconditionally"
                // fallback instead of hitting an invalid ICmp (LLVM verify
                // failure). Same defense as the hasTag branch below.
                auto *tagIntTy = llvm::cast<llvm::IntegerType>(tagVal->getType());
                auto *loVal = llvm::ConstantInt::get(
                    tagIntTy, static_cast<uint64_t>(info.pat.rangeLo), /*isSigned=*/true);
                auto *hiVal = llvm::ConstantInt::get(
                    tagIntTy, static_cast<uint64_t>(info.pat.rangeHi), /*isSigned=*/true);
                auto *geLo = builder_->CreateICmpSGE(tagVal, loVal, "pat.range.ge");
                auto *cmpHi = info.pat.rangeInclusive
                                  ? builder_->CreateICmpSLE(tagVal, hiVal, "pat.range.le")
                                  : builder_->CreateICmpSLT(tagVal, hiVal, "pat.range.lt");
                patternCond = builder_->CreateAnd(geLo, cmpHi, "pat.range.and");
            } else if (info.pat.hasTag && !info.pat.isWildcard &&
                       tagVal->getType()->isIntegerTy()) {
                // Pattern Types Faz B, Task 3: a tag-bearing arm (plain
                // int-literal, or a bare/qualified no-payload enum case) that
                // ends up sharing a match with a range arm is now in
                // if-else-chain mode too (see hasRangePattern above), where
                // — unlike switch mode — nothing has verified its tag
                // actually equals the subject yet. Build that equality
                // check explicitly. Guarded-binding arms (Identifier
                // pattern, hasTag == false) are unaffected: they fall
                // through with patternCond == nullptr exactly as before,
                // i.e. always match (subject to any guard clause).
                //
                // REVIEW FIX: this branch is ALSO reachable via chain mode's
                // OTHER two triggers — a non-integer subject (float/string)
                // or numCases == 0 — not just hasRangePattern. Sema's
                // err_pattern_type_mismatch now rejects an int-literal arm
                // against a known non-int scalar subject at compile time
                // (root-cause fix, TypeChecker.cpp), but an unresolved/
                // generic subject that slips past that conservative check
                // must still not reach an unconditional
                // `cast<IntegerType>(tagVal->getType())` here — hence the
                // explicit `isIntegerTy()` guard (defense in depth): when
                // false, patternCond simply stays null, i.e. this arm falls
                // back to the pre-generalization "always matches" behavior
                // instead of crashing LLVM module verification.
                auto *tagIntTy = llvm::cast<llvm::IntegerType>(tagVal->getType());
                auto *litVal = llvm::ConstantInt::get(
                    tagIntTy, static_cast<uint64_t>(info.pat.tag), /*isSigned=*/true);
                patternCond = builder_->CreateICmpEQ(tagVal, litVal, "pat.tageq");
            }
        }

        // Guard clause, ANDed with any pattern-level comparison above: if
        // the combined condition is false, jump to next arm or default.
        llvm::Value *guardVal = arm.guard ? visit(arm.guard.get()) : nullptr;
        llvm::Value *branchCond = patternCond;
        if (guardVal) {
            branchCond = branchCond ? builder_->CreateAnd(branchCond, guardVal, "pat.guard.and")
                                     : guardVal;
        }
        if (branchCond) {
            auto *bodyBB = llvm::BasicBlock::Create(*context_,
                "match.arm." + std::to_string(i) + ".body", func);
            // Determine fallthrough: next arm's BB, or defaultBB
            llvm::BasicBlock *nextBB = defaultBB;
            if (useIfElseChain && i + 1 < armInfos.size()) {
                nextBB = armInfos[i + 1].bb;
            }
            auto *condBrOriginBB = builder_->GetInsertBlock();
            builder_->CreateCondBr(branchCond, bodyBB, nextBB);
            // This is the if-else-chain counterpart of the switch-mode
            // default edge tracked above: when there's no wildcard arm,
            // only the LAST arm's guard/pattern-comparison failure can
            // reach mergeBB directly (every earlier arm's failure falls to
            // the next arm instead) — record it for the PHI-placeholder
            // fixup below.
            if (defaultIdx < 0 && nextBB == defaultBB)
                defaultEdgePreds.push_back(condBrOriginBB);
            builder_->SetInsertPoint(bodyBB);
        }

        auto *armVal = visit(arm.body.get());
        auto *incomingBB = builder_->GetInsertBlock();
        bool terminated = incomingBB->getTerminator() != nullptr;
        if (!terminated)
            builder_->CreateBr(mergeBB);

        // Collect arm value and its block for PHI
        if (armVal && !terminated) {
            armResults.push_back({armVal, incomingBB});
        }

    }

    builder_->SetInsertPoint(mergeBB);

    // Build PHI node if arms produce values (match-as-expression)
    llvm::PHINode *phi = nullptr;
    if (!armResults.empty()) {
        auto *valType = armResults[0].first->getType();
        bool allSameType = true;
        for (auto &p : armResults) {
            if (p.first->getType() != valType) {
                allSameType = false;
                break;
            }
        }
        if (allSameType) {
            phi = builder_->CreatePHI(valType, armResults.size() + defaultEdgePreds.size(),
                                       "match.val");
            for (auto &p : armResults) {
                phi->addIncoming(p.first, p.second);
            }
            // Placeholder incoming value for the "no wildcard arm" default
            // edge(s) tracked above (Pattern Types Faz B, Task 2 regression
            // fix): these predecessor blocks branch into mergeBB without
            // ever going through an arm body, so they have no real value to
            // contribute. They are unreachable at runtime whenever the
            // match's patterns are truly exhaustive (e.g. bool
            // `true`/`false`); when they're NOT exhaustive and this edge IS
            // reachable, the match is being used as an expression with no
            // arm covering that case — already an existing, out-of-scope
            // gap (not newly introduced here) — an UndefValue keeps the IR
            // valid rather than crashing the verifier either way.
            for (auto *predBB : defaultEdgePreds) {
                phi->addIncoming(llvm::UndefValue::get(valType), predBB);
            }
        }
    }

    // Clean up temporary Result enum entries
    if (isResultMatch) {
        enumCases_.erase("Result");
        enumTypes_.erase("Result");
        enumCasePayloads_.erase("Result");
        vars_.varEnumTypes.erase(subjectVarName);
    }

    return phi;
}

/// Resolve one EnumCasePattern's subpattern slots into `info.bindings` /
/// `info.nestedPatterns` (index-aligned), mirroring the legacy string
/// splitter's per-slot rule: a nested EnumCasePattern recurses (with an
/// empty subjectEnumType, exactly like the old code's `parseMatchPattern(
/// trimmed, "")`); anything else contributes a binding name — an
/// IdentifierPattern's name, or "" for a WildcardPattern (ignored slot; the
/// legacy string form had no such concept and would instead have bound a
/// literal variable named "_", which is dead-in-practice, never referenced
/// by guard/body — see task-2-report.md).
void IRGen::resolvePatternSubs(const std::vector<std::unique_ptr<Pattern>> &subs,
                                PatternInfo &info) {
    for (auto &sub : subs) {
        switch (sub->getKind()) {
        case Pattern::Kind::EnumCase:
            info.bindings.push_back(""); // placeholder — no slot-local name
            info.nestedPatterns.push_back(resolveMatchPattern(sub.get(), ""));
            break;
        case Pattern::Kind::Identifier:
            info.bindings.push_back(static_cast<const IdentifierPattern *>(sub.get())->getName());
            info.nestedPatterns.push_back(IRGen::PatternInfo{}); // empty (tag=-1)
            break;
        case Pattern::Kind::Wildcard:
            info.bindings.push_back(""); // ignored slot
            info.nestedPatterns.push_back(IRGen::PatternInfo{});
            break;
        case Pattern::Kind::IntLiteral:
            // Not observed anywhere in the repo as a subslot: fall back to
            // its spelling (load-bearing identifier derivation, not
            // display — see Pattern::getSpelling()) as a (almost certainly
            // unused) binding name, matching the old code's blind "whatever
            // the slot text was" fallback.
            info.bindings.push_back(sub->getSpelling());
            info.nestedPatterns.push_back(IRGen::PatternInfo{});
            break;

        case Pattern::Kind::BoolLiteral:
        case Pattern::Kind::StringLiteral:
        case Pattern::Kind::FloatLiteral:
        case Pattern::Kind::Range:
            // Pattern Types Faz B, Task 2/3 subpattern decision: TypeChecker's
            // declarePatternSubBinding rejects these as Case(...) sub-slots
            // before IRGen ever runs (err_pattern_literal_subpattern_
            // unsupported) — this branch only keeps the switch exhaustive
            // for `-Wswitch`; it is never reached by a successfully
            // type-checked program.
            info.bindings.push_back("");
            info.nestedPatterns.push_back(IRGen::PatternInfo{});
            break;
        }
    }
}

IRGen::PatternInfo IRGen::resolveMatchPattern(const Pattern *pattern,
                                               const std::string &subjectEnumType) {
    PatternInfo info;
    if (!pattern)
        return info;

    switch (pattern->getKind()) {
    case Pattern::Kind::Wildcard:
        info.isWildcard = true;
        return info;

    case Pattern::Kind::IntLiteral: {
        auto *lit = static_cast<const IntLiteralPattern *>(pattern);
        info.tag = static_cast<int>(lit->getValue());
        info.hasTag = true; // real value, including negatives (e.g. -1)
        return info;
    }

    case Pattern::Kind::BoolLiteral: {
        // Reuses the tag/hasTag switch-case machinery: a bool subject's
        // tagVal is i1, which CreateSwitch accepts directly (see the
        // ConstantInt::get(tagVal->getType(), ...) fix in visitMatchExpr —
        // needed so the case constant's bit-width matches an i1 subject
        // instead of the enum/int path's hardcoded i32).
        auto *lit = static_cast<const BoolLiteralPattern *>(pattern);
        info.tag = lit->getValue() ? 1 : 0;
        info.hasTag = true;
        return info;
    }

    case Pattern::Kind::FloatLiteral: {
        auto *lit = static_cast<const FloatLiteralPattern *>(pattern);
        info.isFloatLiteral = true;
        info.floatValue = lit->getValue();
        return info;
    }

    case Pattern::Kind::StringLiteral: {
        auto *lit = static_cast<const StringLiteralPattern *>(pattern);
        info.isStringLiteral = true;
        info.stringValue = lit->getValue(); // unescaped
        return info;
    }

    case Pattern::Kind::Range: {
        auto *rp = static_cast<const RangePattern *>(pattern);
        info.isRange = true;
        info.rangeLo = rp->getLo().getValue();
        info.rangeHi = rp->getHi().getValue();
        info.rangeInclusive = rp->isInclusive();
        return info;
    }

    case Pattern::Kind::EnumCase: {
        auto *ec = static_cast<const EnumCasePattern *>(pattern);
        // Bare `Case(subs)` (no enum prefix): the legacy string parser mis-
        // handled this form (it never appears in the repo) by treating the
        // whole "Case(subs)" text as one broken binding name. Sane
        // deviation (per task brief): resolve it the same way as a bare
        // Case with an enum prefix, using the subject's own enum type —
        // i.e. identical to the `Enum.Case(subs)` path below.
        info.enumName = ec->getEnumName().empty() ? subjectEnumType : ec->getEnumName();
        info.caseName = ec->getCaseName();

        if (ec->hasParens())
            resolvePatternSubs(ec->getSubpatterns(), info);

        auto ecIt = enumCases_.find(info.enumName);
        if (ecIt != enumCases_.end()) {
            auto cIt = ecIt->second.find(info.caseName);
            if (cIt != ecIt->second.end()) {
                info.tag = cIt->second;
                info.hasTag = true;
            }
        }
        return info;
    }

    case Pattern::Kind::Identifier: {
        auto *idp = static_cast<const IdentifierPattern *>(pattern);
        const std::string &name = idp->getName();

        // Bare-case-vs-binding resolution: matches a case of the subject's
        // known enum type -> bare case pattern; otherwise a plain binding
        // that binds the whole subject value (always matches).
        if (!subjectEnumType.empty()) {
            auto ecIt = enumCases_.find(subjectEnumType);
            if (ecIt != enumCases_.end()) {
                auto cIt = ecIt->second.find(name);
                if (cIt != ecIt->second.end()) {
                    info.enumName = subjectEnumType;
                    info.caseName = name;
                    info.tag = cIt->second;
                    info.hasTag = true;
                    return info;
                }
            }
        }

        info.bindings.push_back(name);
        return info;
    }
    }

    return info;
}

void IRGen::emitNestedPatternMatch(llvm::Value *fieldPtr, const PatternInfo &nested,
                                    llvm::BasicBlock *failBB, llvm::Function *func) {
    auto etIt = enumTypes_.find(nested.enumName);
    if (etIt != enumTypes_.end()) {
        // Payload enum: struct {i32 tag, [payload bytes]}
        auto *enumStructTy = etIt->second;
        auto *innerTagPtr = builder_->CreateStructGEP(enumStructTy, fieldPtr, 0, "nested.tag.ptr");
        auto *innerTag = builder_->CreateLoad(builder_->getInt32Ty(), innerTagPtr, "nested.tag");

        auto *cmp = builder_->CreateICmpEQ(innerTag, builder_->getInt32(nested.tag), "nested.cmp");
        auto *matchBB = llvm::BasicBlock::Create(*context_, "nested.match", func);
        builder_->CreateCondBr(cmp, matchBB, failBB);
        builder_->SetInsertPoint(matchBB);

        // Extract inner bindings
        if (!nested.bindings.empty()) {
            auto *innerPayloadPtr = builder_->CreateStructGEP(enumStructTy, fieldPtr, 1, "nested.payload");
            auto cpIt = enumCasePayloads_.find(nested.enumName);
            if (cpIt != enumCasePayloads_.end()) {
                auto ccIt = cpIt->second.find(nested.caseName);
                if (ccIt != cpIt->second.end()) {
                    auto &innerPayloadTypes = ccIt->second;
                    const llvm::DataLayout &dl = module_->getDataLayout();
                    uint64_t offset = 0;
                    for (size_t b = 0; b < nested.bindings.size() && b < innerPayloadTypes.size(); ++b) {
                        auto *innerFieldPtr = builder_->CreateConstInBoundsGEP1_64(
                            builder_->getInt8Ty(), innerPayloadPtr, offset,
                            "nested.bind." + std::to_string(b));

                        if (b < nested.nestedPatterns.size() && nested.nestedPatterns[b].hasTag) {
                            // Deeper nesting — recurse
                            emitNestedPatternMatch(innerFieldPtr, nested.nestedPatterns[b], failBB, func);
                        } else if (!nested.bindings[b].empty()) {
                            auto *val = builder_->CreateLoad(innerPayloadTypes[b], innerFieldPtr, nested.bindings[b]);
                            auto *bindAlloca = createEntryBlockAlloca(func, nested.bindings[b], innerPayloadTypes[b]);
                            builder_->CreateStore(val, bindAlloca);
                            vars_.namedValues[nested.bindings[b]] = bindAlloca;
                        }
                        offset += dl.getTypeAllocSize(innerPayloadTypes[b]);
                    }
                }
            }
        }
    } else {
        // Simple enum (no payload): just an i32 tag value
        auto *innerTag = builder_->CreateLoad(builder_->getInt32Ty(), fieldPtr, "nested.tag");
        auto *cmp = builder_->CreateICmpEQ(innerTag, builder_->getInt32(nested.tag), "nested.cmp");
        auto *matchBB = llvm::BasicBlock::Create(*context_, "nested.match", func);
        builder_->CreateCondBr(cmp, matchBB, failBB);
        builder_->SetInsertPoint(matchBB);
    }
}

void IRGen::emitBoundsCheck(llvm::Value *indexVal, llvm::Value *sizeVal) {
    auto *func = builder_->GetInsertBlock()->getParent();
    if (indexVal->getType()->isIntegerTy(32))
        indexVal = builder_->CreateSExt(indexVal, builder_->getInt64Ty());
    if (sizeVal->getType()->isIntegerTy(32))
        sizeVal = builder_->CreateZExt(sizeVal, builder_->getInt64Ty());
    auto *cmp = builder_->CreateICmpUGE(indexVal, sizeVal, "bounds.check");
    auto *panicBB = llvm::BasicBlock::Create(*context_, "bounds.fail", func);
    auto *okBB = llvm::BasicBlock::Create(*context_, "bounds.ok", func);
    builder_->CreateCondBr(cmp, panicBB, okBB);
    builder_->SetInsertPoint(panicBB);
    auto *panicFn = getOrPanic("liva_panic");
    auto *msg = builder_->CreateGlobalString("index out of bounds");
    builder_->CreateCall(panicFn, {msg});
    builder_->CreateUnreachable();
    builder_->SetInsertPoint(okBB);
}


void IRGen::emitSliceBoundsCheck(llvm::Value *startVal, llvm::Value *endVal, llvm::Value *lenVal) {
    auto *func = builder_->GetInsertBlock()->getParent();
    auto *i64Ty = builder_->getInt64Ty();
    if (startVal->getType() != i64Ty)
        startVal = builder_->CreateSExt(startVal, i64Ty);
    if (endVal->getType() != i64Ty)
        endVal = builder_->CreateSExt(endVal, i64Ty);
    if (lenVal->getType() != i64Ty)
        lenVal = builder_->CreateZExt(lenVal, i64Ty);

    auto *zero = builder_->getInt64(0);
    auto *panicFn = getOrPanic("liva_panic");

    // Check 1: start < 0
    auto *startNeg = builder_->CreateICmpSLT(startVal, zero, "slice.start.neg");
    auto *failStartBB = llvm::BasicBlock::Create(*context_, "slice.start.fail", func);
    auto *okStartBB = llvm::BasicBlock::Create(*context_, "slice.start.ok", func);
    builder_->CreateCondBr(startNeg, failStartBB, okStartBB);
    builder_->SetInsertPoint(failStartBB);
    builder_->CreateCall(panicFn, {builder_->CreateGlobalString("slice start index out of bounds")});
    builder_->CreateUnreachable();
    builder_->SetInsertPoint(okStartBB);

    // Check 2: end < start
    auto *endLtStart = builder_->CreateICmpSLT(endVal, startVal, "slice.end.lt.start");
    auto *failEndBB = llvm::BasicBlock::Create(*context_, "slice.end.fail", func);
    auto *okEndBB = llvm::BasicBlock::Create(*context_, "slice.end.ok", func);
    builder_->CreateCondBr(endLtStart, failEndBB, okEndBB);
    builder_->SetInsertPoint(failEndBB);
    builder_->CreateCall(panicFn, {builder_->CreateGlobalString("slice end index less than start")});
    builder_->CreateUnreachable();
    builder_->SetInsertPoint(okEndBB);

    // Check 3: end > len
    auto *endGtLen = builder_->CreateICmpSGT(endVal, lenVal, "slice.end.gt.len");
    auto *failLenBB = llvm::BasicBlock::Create(*context_, "slice.len.fail", func);
    auto *okLenBB = llvm::BasicBlock::Create(*context_, "slice.len.ok", func);
    builder_->CreateCondBr(endGtLen, failLenBB, okLenBB);
    builder_->SetInsertPoint(failLenBB);
    builder_->CreateCall(panicFn, {builder_->CreateGlobalString("slice end index out of bounds")});
    builder_->CreateUnreachable();
    builder_->SetInsertPoint(okLenBB);
}

} // namespace liva

#endif // LIVA_HAS_LLVM
