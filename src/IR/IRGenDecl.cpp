#include "liva/IR/IRGen.h"

#ifdef LIVA_HAS_LLVM

namespace liva {

llvm::Value *IRGen::visitImportDecl(ImportDecl *node) {
    if (!moduleLoader_) return nullptr;

    std::string moduleName = node->getPathString();

    // Don't process the same module twice
    if (processedModules_.count(moduleName)) return nullptr;
    processedModules_.insert(moduleName);

    auto *mod = moduleLoader_->getLoadedModule(moduleName);
    if (!mod || !mod->tu) return nullptr;

    // Process all declarations from the imported module
    for (auto &decl : mod->tu->getDeclarations()) {
        if (decl->getKind() == ASTNode::NodeKind::FuncDecl) {
            auto *funcDecl = static_cast<FuncDecl *>(decl.get());
            if (funcDecl->isGeneric()) {
                genericFuncDecls_[funcDecl->getName()] = funcDecl;
                continue;
            }
        }

        if (decl->getKind() == ASTNode::NodeKind::StructDecl) {
            auto *structDecl = static_cast<StructDecl *>(decl.get());
            if (structDecl->isGeneric()) {
                genericStructDecls_[structDecl->getName()] = structDecl;
                continue;
            }
        }

        if (decl->getKind() == ASTNode::NodeKind::ImplDecl) {
            auto *implDecl = static_cast<ImplDecl *>(decl.get());
            if (implDecl->isGeneric()) {
                genericImplDecls_[implDecl->getTypeName()] = implDecl;
                continue;
            }
        }

        visit(decl.get());
    }

    return nullptr;
}

llvm::Value *IRGen::visitProtocolDecl(ProtocolDecl *node) {
    std::vector<std::string> methodNames;
    std::unordered_map<std::string, int> methodIndices;
    int idx = 0;
    for (auto &method : node->getMethods()) {
        methodNames.push_back(method->getName());
        methodIndices[method->getName()] = idx++;
    }
    protocolMethodNames_[node->getName()] = std::move(methodNames);
    protocolMethodIndices_[node->getName()] = std::move(methodIndices);
    protocolDecls_[node->getName()] = node;
    return nullptr;
}

llvm::Value *IRGen::visitFuncDecl(FuncDecl *node) {
    // Store FuncDecl for default arg lookup
    funcDecls_[node->getName()] = node;

    // Build function type
    std::vector<llvm::Type *> paramTypes;
    for (auto &param : node->getParams()) {
        if (param.isRef) {
            paramTypes.push_back(llvm::PointerType::getUnqual(*context_));
        } else {
            paramTypes.push_back(toLLVMType(param.type.get()));
        }
    }

    auto *returnType = toLLVMType(node->getReturnType());

    // Async functions: wrap return type in Task<T>
    llvm::Type *asyncInnerRetType = nullptr;
    if (node->isAsync()) {
        asyncFuncNames_.insert(node->getName());
        asyncInnerRetType = returnType;
        if (returnType->isVoidTy()) {
            asyncInnerRetType = builder_->getInt1Ty(); // placeholder for Void tasks
        }
        returnType = getTaskType(asyncInnerRetType);
    }

    // C ABI: main must return i32
    bool isMain = (node->getName() == "main");
    if (isMain && returnType->isVoidTy()) {
        returnType = builder_->getInt32Ty();
    }

    auto *funcType = llvm::FunctionType::get(returnType, paramTypes, false);
    auto *func = llvm::Function::Create(
        funcType, llvm::Function::ExternalLinkage, node->getName(), *module_);

    // Set parameter names
    size_t i = 0;
    for (auto &arg : func->args()) {
        arg.setName(node->getParams()[i].name);
        ++i;
    }

    if (!node->hasBody())
        return func;

    // Create entry block
    auto *entryBB = llvm::BasicBlock::Create(*context_, "entry", func);
    builder_->SetInsertPoint(entryBB);

    // Save old named values and create new scope
    auto oldNamedValues = namedValues_;
    auto oldVarStructTypes = varStructTypes_;
    auto oldVarEnumTypes = varEnumTypes_;
    auto oldVarArrayTypes = varArrayTypes_;
    auto oldVarDynArrayTypes = varDynArrayTypes_;
    auto oldVarMapTypes = varMapTypes_;
    auto oldVarSetTypes = varSetTypes_;
    auto oldVarOptionalTypes = varOptionalTypes_;
    auto oldVarFuncTypes = varFuncTypes_;
    auto oldVarProtocolTypes = varProtocolTypes_;
    auto oldVarResultTypes = varResultTypes_;
    auto oldVarRefTypes = varRefTypes_;
    auto oldVarFileTypes = varFileTypes_;
    auto oldVarFileOptionalTypes = varFileOptionalTypes_;
    auto oldVarTupleTypes = varTupleTypes_;
    auto oldMovedVars = movedVars_;
    auto *oldFuncResultInfo = currentFuncResultInfo_;
    bool oldIsAsync = currentIsAsync_;
    auto *oldAsyncRetType = asyncDeclaredRetType_;
    currentIsAsync_ = node->isAsync();
    asyncDeclaredRetType_ = node->isAsync() ? asyncInnerRetType : nullptr;
    namedValues_.clear();
    varStructTypes_.clear();
    varEnumTypes_.clear();
    varArrayTypes_.clear();
    varDynArrayTypes_.clear();
    varMapTypes_.clear();
    varSetTypes_.clear();
    varOptionalTypes_.clear();
    varFuncTypes_.clear();
    varProtocolTypes_.clear();
    varResultTypes_.clear();
    varRefTypes_.clear();
    varFileTypes_.clear();
    varFileOptionalTypes_.clear();
    varTupleTypes_.clear();
    movedVars_.clear();
    currentFuncResultInfo_ = nullptr;

    // Track Result return type for try expressions (visitFuncDecl)
    if (node->getReturnType() &&
        node->getReturnType()->getKind() == TypeRepr::Kind::Result) {
        auto *rt = static_cast<const ResultTypeRepr *>(node->getReturnType());
        currentFuncResultInfoStorage_ = {toLLVMType(rt->getOkType()), toLLVMType(rt->getErrType())};
        currentFuncResultInfo_ = &currentFuncResultInfoStorage_;
    }

    // Create allocas for parameters
    i = 0;
    for (auto &arg : func->args()) {
        auto *alloca = createEntryBlockAlloca(func, std::string(arg.getName()), arg.getType());
        builder_->CreateStore(&arg, alloca);
        namedValues_[std::string(arg.getName())] = alloca;
        ++i;
    }

    // Populate varRefTypes_ for ref parameters
    for (auto &param : node->getParams()) {
        if (param.isRef && param.type) {
            llvm::Type *innerTy = toLLVMType(param.type.get());
            if (param.type->getKind() == TypeRepr::Kind::Reference) {
                auto *refTy = static_cast<const ReferenceTypeRepr *>(param.type.get());
                innerTy = toLLVMType(refTy->getInner());
            }
            varRefTypes_[param.name] = innerTy;
        }
    }

    // Populate varFuncTypes_ for function-typed parameters
    for (auto &param : node->getParams()) {
        if (param.type && param.type->getKind() == TypeRepr::Kind::Function) {
            auto *ftr = static_cast<const FunctionTypeRepr *>(param.type.get());
            std::vector<llvm::Type *> fParamTypes;
            fParamTypes.push_back(llvm::PointerType::getUnqual(*context_)); // hidden env
            for (auto &p : ftr->getParams())
                fParamTypes.push_back(toLLVMType(p.get()));
            llvm::Type *fRetTy = ftr->getReturnType()
                ? toLLVMType(ftr->getReturnType()) : builder_->getVoidTy();
            varFuncTypes_[param.name] = llvm::FunctionType::get(fRetTy, fParamTypes, false);
        }
    }

    // Generate body
    visitBlockStmt(const_cast<BlockStmt *>(node->getBody()));

    // Add implicit return if needed
    if (!builder_->GetInsertBlock()->getTerminator()) {
        emitScopeCleanup();
        if (isMain) {
            // main always returns i32 0 implicitly
            builder_->CreateRet(builder_->getInt32(0));
        } else if (currentIsAsync_) {
            // Async func: return Task { done: true, result: nullValue }
            auto *taskTy = getTaskType(asyncDeclaredRetType_);
            llvm::Value *taskVal = llvm::UndefValue::get(taskTy);
            taskVal = builder_->CreateInsertValue(taskVal, builder_->getTrue(), 0, "task.done");
            taskVal = builder_->CreateInsertValue(taskVal,
                llvm::Constant::getNullValue(asyncDeclaredRetType_), 1, "task.result");
            builder_->CreateRet(taskVal);
        } else if (returnType->isVoidTy()) {
            builder_->CreateRetVoid();
        } else {
            builder_->CreateRet(llvm::Constant::getNullValue(returnType));
        }
    }

    // Restore named values
    namedValues_ = oldNamedValues;
    varStructTypes_ = oldVarStructTypes;
    varEnumTypes_ = oldVarEnumTypes;
    varArrayTypes_ = oldVarArrayTypes;
    varDynArrayTypes_ = oldVarDynArrayTypes;
    varMapTypes_ = oldVarMapTypes;
    varSetTypes_ = oldVarSetTypes;
    varOptionalTypes_ = oldVarOptionalTypes;
    varFuncTypes_ = oldVarFuncTypes;
    varProtocolTypes_ = oldVarProtocolTypes;
    varResultTypes_ = oldVarResultTypes;
    varRefTypes_ = oldVarRefTypes;
    varFileTypes_ = oldVarFileTypes;
    varFileOptionalTypes_ = oldVarFileOptionalTypes;
    varTupleTypes_ = oldVarTupleTypes;
    movedVars_ = oldMovedVars;
    currentFuncResultInfo_ = oldFuncResultInfo;
    currentIsAsync_ = oldIsAsync;
    asyncDeclaredRetType_ = oldAsyncRetType;

    return func;
}

llvm::Value *IRGen::visitVarDecl(VarDecl *node) {
    auto *func = builder_->GetInsertBlock()->getParent();

    // Tuple destructuring: let (x, y) = expr
    if (node->isDestructured()) {
        if (!node->hasInit()) return nullptr;
        auto *initVal = visit(const_cast<Expr *>(node->getInit()));
        if (!initVal) return nullptr;

        // Get tuple element types from init's resolved type
        auto *initType = node->getInit()->getResolvedType();
        if (!initType || initType->getKind() != TypeRepr::Kind::Tuple)
            return nullptr;

        auto *tupleTypeRepr = static_cast<const TupleTypeRepr *>(initType);
        auto *tupleTy = toLLVMType(initType);

        // Store the whole tuple to a temp alloca
        auto *tupleAlloca = createEntryBlockAlloca(func, "tuple.dest", tupleTy);
        builder_->CreateStore(initVal, tupleAlloca);

        // Extract each element and assign to named variables
        for (size_t i = 0; i < node->getDestructuredNames().size(); ++i) {
            auto *elemTy = toLLVMType(tupleTypeRepr->getElements()[i].get());
            auto *gep = builder_->CreateStructGEP(tupleTy, tupleAlloca, i);
            auto *val = builder_->CreateLoad(elemTy, gep);

            auto *elemAlloca = createEntryBlockAlloca(func, node->getDestructuredNames()[i], elemTy);
            builder_->CreateStore(val, elemAlloca);
            namedValues_[node->getDestructuredNames()[i]] = elemAlloca;
        }
        return tupleAlloca;
    }

    // Protocol trait object: let s: ref Shape = circle
    if (node->hasTypeAnnotation() && node->getType() &&
        node->getType()->getKind() == TypeRepr::Kind::Reference) {
        auto *refType = static_cast<const ReferenceTypeRepr *>(node->getType());
        if (refType->getInner() && refType->getInner()->getKind() == TypeRepr::Kind::Named) {
            auto *namedInner = static_cast<const NamedTypeRepr *>(refType->getInner());
            const std::string &protocolName = namedInner->getName();
            auto pmIt = protocolMethodNames_.find(protocolName);
            if (pmIt != protocolMethodNames_.end()) {
                // This is a protocol trait object variable
                auto *traitTy = getTraitObjectTy();
                auto *alloca = createEntryBlockAlloca(func, node->getName(), traitTy);

                // Get concrete type from init expression
                std::string concreteType;
                llvm::AllocaInst *dataAlloca = nullptr;
                if (node->hasInit() &&
                    node->getInit()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
                    auto *ident = static_cast<IdentifierExpr *>(
                        const_cast<Expr *>(node->getInit()));
                    auto stIt = varStructTypes_.find(ident->getName());
                    if (stIt != varStructTypes_.end()) {
                        concreteType = stIt->second;
                        auto nvIt = namedValues_.find(ident->getName());
                        if (nvIt != namedValues_.end())
                            dataAlloca = nvIt->second;
                    }
                }

                if (!concreteType.empty() && dataAlloca) {
                    auto *ptrTy = llvm::PointerType::getUnqual(*context_);

                    // Store data pointer (pointer to concrete struct)
                    auto *dataGEP = builder_->CreateStructGEP(traitTy, alloca, 0, "trait.data");
                    builder_->CreateStore(dataAlloca, dataGEP);

                    // Get or create vtable and store pointer
                    auto *vtable = getOrCreateVtable(protocolName, concreteType);
                    auto *vtableGEP = builder_->CreateStructGEP(traitTy, alloca, 1, "trait.vtable");
                    builder_->CreateStore(vtable, vtableGEP);

                    namedValues_[node->getName()] = alloca;
                    varProtocolTypes_[node->getName()] = protocolName;
                    return alloca;
                }
            }
        }
    }

    // Result variable: let r: Result<i32, string> = Result.ok(42)
    if (node->hasTypeAnnotation() && node->getType() &&
        node->getType()->getKind() == TypeRepr::Kind::Result) {
        auto *rtRepr = static_cast<const ResultTypeRepr *>(node->getType());
        auto *okLLVM = toLLVMType(rtRepr->getOkType());
        auto *errLLVM = toLLVMType(rtRepr->getErrType());
        auto *resTy = getResultType(okLLVM, errLLVM);
        auto *alloca = createEntryBlockAlloca(func, node->getName(), resTy);

        if (node->hasInit()) {
            // Temporarily set currentFuncResultInfo_ so emitResultOk/Err can be used from visitCallExpr
            ResultInfo ri = {okLLVM, errLLVM};
            auto *oldRI = currentFuncResultInfo_;
            currentFuncResultInfo_ = &ri;
            auto *initVal = visit(const_cast<Expr *>(node->getInit()));
            currentFuncResultInfo_ = oldRI;
            if (initVal) builder_->CreateStore(initVal, alloca);
        }

        namedValues_[node->getName()] = alloca;
        varResultTypes_[node->getName()] = {okLLVM, errLLVM};
        return alloca;
    }

    // File.open() init: let f = File.open("path", "mode") → Optional<ptr>
    if (node->hasInit() &&
        node->getInit()->getKind() == ASTNode::NodeKind::CallExpr) {
        auto *callInit = static_cast<CallExpr *>(const_cast<Expr *>(node->getInit()));
        if (callInit->getCallee()->getKind() == ASTNode::NodeKind::MemberExpr) {
            auto *me = static_cast<MemberExpr *>(callInit->getCallee());
            if (me->getObject()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
                auto *id = static_cast<IdentifierExpr *>(me->getObject());
                if (id->getName() == "File" && me->getMember() == "open") {
                    auto *ptrTy = llvm::PointerType::getUnqual(*context_);
                    auto *optTy = getOptionalType(ptrTy);
                    auto *alloca = createEntryBlockAlloca(func, node->getName(), optTy);
                    auto *initVal = visit(const_cast<Expr *>(node->getInit()));
                    if (initVal) builder_->CreateStore(initVal, alloca);
                    namedValues_[node->getName()] = alloca;
                    varOptionalTypes_[node->getName()] = ptrTy;
                    varFileOptionalTypes_.insert(node->getName());
                    return alloca;
                }
            }
        }
    }

    // Optional variable: let x: i32? = 42 / let x: i32? = nil
    if (node->hasTypeAnnotation() && node->getType() &&
        node->getType()->getKind() == TypeRepr::Kind::Optional) {
        auto *optTypeRepr = static_cast<const OptionalTypeRepr *>(node->getType());
        auto *innerLLVM = toLLVMType(optTypeRepr->getInner());
        auto *optStructTy = getOptionalType(innerLLVM);
        auto *alloca = createEntryBlockAlloca(func, node->getName(), optStructTy);
        auto *hasValPtr = builder_->CreateStructGEP(optStructTy, alloca, 0, "opt.hasval");
        auto *valPtr = builder_->CreateStructGEP(optStructTy, alloca, 1, "opt.val");

        if (node->hasInit() &&
            node->getInit()->getKind() == ASTNode::NodeKind::NilLiteralExpr) {
            builder_->CreateStore(builder_->getFalse(), hasValPtr);
            builder_->CreateStore(llvm::Constant::getNullValue(innerLLVM), valPtr);
        } else if (node->hasInit()) {
            auto *initVal = visit(const_cast<Expr *>(node->getInit()));
            builder_->CreateStore(builder_->getTrue(), hasValPtr);
            if (initVal) builder_->CreateStore(initVal, valPtr);
        } else {
            builder_->CreateStore(builder_->getFalse(), hasValPtr);
            builder_->CreateStore(llvm::Constant::getNullValue(innerLLVM), valPtr);
        }
        namedValues_[node->getName()] = alloca;
        varOptionalTypes_[node->getName()] = innerLLVM;
        // Register struct type name for optional chaining support
        if (optTypeRepr->getInner()->getKind() == TypeRepr::Kind::Named) {
            auto *namedInner = static_cast<const NamedTypeRepr *>(optTypeRepr->getInner());
            varStructTypes_[node->getName()] = namedInner->getName();
        }
        return alloca;
    }

    // Check if init is a struct literal - reuse its alloca
    if (node->hasInit() &&
        node->getInit()->getKind() == ASTNode::NodeKind::StructLiteralExpr) {
        auto *structLit = static_cast<StructLiteralExpr *>(
            const_cast<Expr *>(node->getInit()));
        const auto &typeName = structLit->getTypeName();

        // Check for generic struct
        auto gsIt = genericStructDecls_.find(typeName);
        if (gsIt != genericStructDecls_.end()) {
            // Evaluate all field values first
            std::vector<llvm::Value *> fieldValues;
            for (auto &fieldInit : structLit->getFields()) {
                auto *val = visit(fieldInit.value.get());
                fieldValues.push_back(val);
            }

            // Infer type arguments from field values
            auto typeArgs = inferStructTypeArgs(gsIt->second, structLit->getFields(), fieldValues);

            // Monomorphize the struct
            monomorphizeStruct(gsIt->second, typeArgs);
            std::string mangledName = mangleGenericStruct(typeName, typeArgs);

            auto *structTy = structTypes_[mangledName];
            auto *alloca = createEntryBlockAlloca(func, node->getName(), structTy);

            for (size_t i = 0; i < structLit->getFields().size(); ++i) {
                int idx = getStructFieldIndex(mangledName, structLit->getFields()[i].name);
                if (idx < 0 || !fieldValues[i])
                    continue;
                auto *gep = builder_->CreateStructGEP(structTy, alloca, idx,
                                                       structLit->getFields()[i].name);
                builder_->CreateStore(fieldValues[i], gep);
            }

            namedValues_[node->getName()] = alloca;
            varStructTypes_[node->getName()] = mangledName;
            return alloca;
        }

        auto stIt = structTypes_.find(typeName);
        if (stIt != structTypes_.end()) {
            auto *structTy = stIt->second;
            auto *alloca = createEntryBlockAlloca(func, node->getName(), structTy);

            // Store each field
            for (auto &fieldInit : structLit->getFields()) {
                int idx = getStructFieldIndex(typeName, fieldInit.name);
                if (idx < 0)
                    continue;
                auto *val = visit(fieldInit.value.get());
                if (!val)
                    continue;
                auto *gep = builder_->CreateStructGEP(structTy, alloca, idx,
                                                       fieldInit.name);
                builder_->CreateStore(val, gep);
            }

            namedValues_[node->getName()] = alloca;
            varStructTypes_[node->getName()] = typeName;
            return alloca;
        }
    }

    // Check if init is a payload enum constructor: let s = Shape.Circle(3.14)
    if (node->hasInit() &&
        node->getInit()->getKind() == ASTNode::NodeKind::CallExpr) {
        auto *callExpr = static_cast<CallExpr *>(
            const_cast<Expr *>(node->getInit()));
        if (callExpr->getCallee()->getKind() == ASTNode::NodeKind::MemberExpr) {
            auto *memberExpr = static_cast<MemberExpr *>(callExpr->getCallee());
            if (memberExpr->getObject()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
                auto *ident = static_cast<IdentifierExpr *>(memberExpr->getObject());
                auto etIt = enumTypes_.find(ident->getName());
                if (etIt != enumTypes_.end()) {
                    auto *enumStructTy = etIt->second;
                    auto *alloca = createEntryBlockAlloca(func, node->getName(), enumStructTy);
                    auto *initVal = visit(const_cast<Expr *>(node->getInit()));
                    if (initVal)
                        builder_->CreateStore(initVal, alloca);
                    namedValues_[node->getName()] = alloca;
                    varEnumTypes_[node->getName()] = ident->getName();
                    return alloca;
                }
            }
        }
    }

    // Check if init is an enum case reference: let c = Color.Green (or Shape.Empty in payload enum)
    if (node->hasInit() &&
        node->getInit()->getKind() == ASTNode::NodeKind::MemberExpr) {
        auto *memberExpr = static_cast<MemberExpr *>(
            const_cast<Expr *>(node->getInit()));
        if (memberExpr->getObject()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
            auto *ident = static_cast<IdentifierExpr *>(memberExpr->getObject());

            // Check for payload enum: Shape.Empty (no-arg case in a payload enum)
            auto etIt = enumTypes_.find(ident->getName());
            if (etIt != enumTypes_.end()) {
                auto *enumStructTy = etIt->second;
                auto ecIt = enumCases_.find(ident->getName());
                if (ecIt != enumCases_.end()) {
                    auto cIt = ecIt->second.find(memberExpr->getMember());
                    if (cIt != ecIt->second.end()) {
                        auto *alloca = createEntryBlockAlloca(func, node->getName(), enumStructTy);
                        // Store tag only
                        auto *tagPtr = builder_->CreateStructGEP(enumStructTy, alloca, 0, "tag.ptr");
                        builder_->CreateStore(builder_->getInt32(cIt->second), tagPtr);
                        namedValues_[node->getName()] = alloca;
                        varEnumTypes_[node->getName()] = ident->getName();
                        return alloca;
                    }
                }
            }

            // Simple enum (no payload): let c = Color.Green
            auto eIt = enumCases_.find(ident->getName());
            if (eIt != enumCases_.end()) {
                auto *alloca = createEntryBlockAlloca(func, node->getName(),
                                                       builder_->getInt32Ty());
                auto *initVal = visit(const_cast<Expr *>(node->getInit()));
                if (initVal)
                    builder_->CreateStore(initVal, alloca);
                namedValues_[node->getName()] = alloca;
                varEnumTypes_[node->getName()] = ident->getName();
                return alloca;
            }
        }
    }

    // Dynamic array: var arr: [i32] = [1, 2, 3] or var arr: [i32] = []
    if (node->hasTypeAnnotation() && node->getType() &&
        node->getType()->getKind() == TypeRepr::Kind::Array) {
        auto *arrTypeRepr = static_cast<const ArrayTypeRepr *>(node->getType());
        if (arrTypeRepr->isDynamic()) {
            auto *elemType = toLLVMType(arrTypeRepr->getElement());
            const llvm::DataLayout &dl = module_->getDataLayout();
            uint64_t elemSize = dl.getTypeAllocSize(elemType);
            auto *structTy = getDynArrayStructTy();
            auto *alloca = createEntryBlockAlloca(func, node->getName(), structTy);

            // Collect init elements
            std::vector<llvm::Value *> initVals;
            if (node->hasInit() &&
                node->getInit()->getKind() == ASTNode::NodeKind::ArrayLiteralExpr) {
                auto *arrayLit = static_cast<ArrayLiteralExpr *>(
                    const_cast<Expr *>(node->getInit()));
                for (auto &elem : arrayLit->getElements()) {
                    auto *val = visit(elem.get());
                    if (val) initVals.push_back(val);
                }
            }

            uint64_t initLen = initVals.size();
            uint64_t initCap = initLen > 0 ? initLen : 8;

            // liva_array_new(elem_size, capacity)
            auto *newFn = module_->getFunction("liva_array_new");
            auto *dataPtr = builder_->CreateCall(newFn,
                {builder_->getInt64(elemSize), builder_->getInt64(initCap)}, "arr.data");

            // Store initial elements
            for (uint64_t i = 0; i < initLen; ++i) {
                auto *elemPtr = builder_->CreateGEP(elemType, dataPtr,
                    builder_->getInt64(i), "arr.init." + std::to_string(i));
                builder_->CreateStore(initVals[i], elemPtr);
            }

            // Fill struct fields: {data, length, capacity}
            auto *dataField = builder_->CreateStructGEP(structTy, alloca, 0, "arr.data.ptr");
            builder_->CreateStore(dataPtr, dataField);
            auto *lenField = builder_->CreateStructGEP(structTy, alloca, 1, "arr.len.ptr");
            builder_->CreateStore(builder_->getInt64(initLen), lenField);
            auto *capField = builder_->CreateStructGEP(structTy, alloca, 2, "arr.cap.ptr");
            builder_->CreateStore(builder_->getInt64(initCap), capField);

            namedValues_[node->getName()] = alloca;
            varDynArrayTypes_[node->getName()] = {elemType, elemSize};
            return alloca;
        }
    }

    // Map declaration: var m: Map<K, V>
    if (node->hasTypeAnnotation() && node->getType() &&
        node->getType()->getKind() == TypeRepr::Kind::Generic) {
        auto *genType = static_cast<const GenericTypeRepr *>(node->getType());
        if (genType->getBaseName() == "Map" && genType->getTypeArgs().size() >= 2) {
            auto *keyType = toLLVMType(genType->getTypeArgs()[0].get());
            auto *valType = toLLVMType(genType->getTypeArgs()[1].get());
            const llvm::DataLayout &dl = module_->getDataLayout();
            uint64_t keySize = dl.getTypeAllocSize(keyType);
            uint64_t valSize = dl.getTypeAllocSize(valType);
            int8_t keyKind = (genType->getTypeArgs()[0]->getKind() == TypeRepr::Kind::String) ? 1 : 0;

            int64_t initCap = 16;
            int64_t stride = 9 + (int64_t)keySize + (int64_t)valSize;

            auto *structTy = getMapStructTy();
            auto *alloca = createEntryBlockAlloca(func, node->getName(), structTy);

            auto *newFn = module_->getFunction("liva_map_new");
            auto *dataPtr = builder_->CreateCall(newFn,
                {builder_->getInt64(initCap), builder_->getInt64(stride)}, "map.entries");

            auto *dataField = builder_->CreateStructGEP(structTy, alloca, 0, "map.entries.ptr");
            builder_->CreateStore(dataPtr, dataField);
            auto *sizeField = builder_->CreateStructGEP(structTy, alloca, 1, "map.size.ptr");
            builder_->CreateStore(builder_->getInt64(0), sizeField);
            auto *capField = builder_->CreateStructGEP(structTy, alloca, 2, "map.cap.ptr");
            builder_->CreateStore(builder_->getInt64(initCap), capField);

            namedValues_[node->getName()] = alloca;
            varMapTypes_[node->getName()] = {keyType, valType, keySize, valSize, keyKind};
            return alloca;
        }

        if (genType->getBaseName() == "Set" && !genType->getTypeArgs().empty()) {
            auto *elemType = toLLVMType(genType->getTypeArgs()[0].get());
            const llvm::DataLayout &dl = module_->getDataLayout();
            uint64_t elemSize = dl.getTypeAllocSize(elemType);
            int8_t keyKind = (genType->getTypeArgs()[0]->getKind() == TypeRepr::Kind::String) ? 1 : 0;

            int64_t initCap = 16;
            int64_t stride = 9 + (int64_t)elemSize;  // val_size=0 for sets

            auto *structTy = getMapStructTy();
            auto *alloca = createEntryBlockAlloca(func, node->getName(), structTy);

            auto *newFn = module_->getFunction("liva_set_new");
            auto *dataPtr = builder_->CreateCall(newFn,
                {builder_->getInt64(initCap), builder_->getInt64(stride)}, "set.entries");

            auto *dataField = builder_->CreateStructGEP(structTy, alloca, 0, "set.entries.ptr");
            builder_->CreateStore(dataPtr, dataField);
            auto *sizeField = builder_->CreateStructGEP(structTy, alloca, 1, "set.size.ptr");
            builder_->CreateStore(builder_->getInt64(0), sizeField);
            auto *capField = builder_->CreateStructGEP(structTy, alloca, 2, "set.cap.ptr");
            builder_->CreateStore(builder_->getInt64(initCap), capField);

            namedValues_[node->getName()] = alloca;
            varSetTypes_[node->getName()] = {elemType, elemSize, keyKind};
            return alloca;
        }
    }

    // Array literal init: let arr = [10, 20, 30]
    if (node->hasInit() &&
        node->getInit()->getKind() == ASTNode::NodeKind::ArrayLiteralExpr) {
        auto *arrayLit = static_cast<ArrayLiteralExpr *>(
            const_cast<Expr *>(node->getInit()));
        auto &elements = arrayLit->getElements();
        if (!elements.empty()) {
            auto *firstVal = visit(elements[0].get());
            if (!firstVal) return nullptr;
            auto *elemType = firstVal->getType();
            uint64_t numElements = elements.size();
            auto *arrayType = llvm::ArrayType::get(elemType, numElements);
            auto *alloca = createEntryBlockAlloca(func, node->getName(), arrayType);
            auto *gep0 = builder_->CreateConstInBoundsGEP2_64(arrayType, alloca, 0, 0, "arr.elem.0");
            builder_->CreateStore(firstVal, gep0);
            for (uint64_t i = 1; i < numElements; ++i) {
                auto *val = visit(elements[i].get());
                if (!val) continue;
                auto *gep = builder_->CreateConstInBoundsGEP2_64(
                    arrayType, alloca, 0, i, "arr.elem." + std::to_string(i));
                builder_->CreateStore(val, gep);
            }
            namedValues_[node->getName()] = alloca;
            varArrayTypes_[node->getName()] = {elemType, numElements};
            return alloca;
        }
    }

    // Function-typed variable: let f: (i32) -> i32 = |x: i32| -> i32 { ... }
    if (node->hasTypeAnnotation() && node->getType() &&
        node->getType()->getKind() == TypeRepr::Kind::Function) {
        auto *funcTypeRepr = static_cast<const FunctionTypeRepr *>(node->getType());
        std::vector<llvm::Type *> paramTypes;
        paramTypes.push_back(llvm::PointerType::getUnqual(*context_)); // hidden env
        for (auto &p : funcTypeRepr->getParams())
            paramTypes.push_back(toLLVMType(p.get()));
        llvm::Type *retTy = funcTypeRepr->getReturnType()
            ? toLLVMType(funcTypeRepr->getReturnType())
            : builder_->getVoidTy();
        auto *llvmFuncTy = llvm::FunctionType::get(retTy, paramTypes, false);
        auto *closureObjTy = getClosureObjTy();
        auto *alloca = createEntryBlockAlloca(func, node->getName(), closureObjTy);
        if (node->hasInit()) {
            auto *initVal = visit(const_cast<Expr *>(node->getInit()));
            if (initVal) builder_->CreateStore(initVal, alloca);
        }
        namedValues_[node->getName()] = alloca;
        varFuncTypes_[node->getName()] = llvmFuncTy;
        return alloca;
    }

    // Tuple variable: let pair = (1, "hello") or let r = divmod(10, 3)
    if (node->hasInit() && node->getInit()->getResolvedType() &&
        node->getInit()->getResolvedType()->getKind() == TypeRepr::Kind::Tuple) {
        auto *tupleTypeRepr = static_cast<const TupleTypeRepr *>(node->getInit()->getResolvedType());
        auto *tupleTy = toLLVMType(tupleTypeRepr);
        auto *alloca = createEntryBlockAlloca(func, node->getName(), tupleTy);
        auto *initVal = visit(const_cast<Expr *>(node->getInit()));
        if (initVal) builder_->CreateStore(initVal, alloca);
        namedValues_[node->getName()] = alloca;
        TupleInfo ti;
        for (auto &e : tupleTypeRepr->getElements())
            ti.elementTypes.push_back(toLLVMType(e.get()));
        varTupleTypes_[node->getName()] = ti;
        return alloca;
    }

    // Also handle tuple type annotation: let x: (i32, string) = ...
    if (node->hasTypeAnnotation() && node->getType() &&
        node->getType()->getKind() == TypeRepr::Kind::Tuple) {
        auto *tupleTypeRepr = static_cast<const TupleTypeRepr *>(node->getType());
        auto *tupleTy = toLLVMType(tupleTypeRepr);
        auto *alloca = createEntryBlockAlloca(func, node->getName(), tupleTy);
        if (node->hasInit()) {
            auto *initVal = visit(const_cast<Expr *>(node->getInit()));
            if (initVal) builder_->CreateStore(initVal, alloca);
        }
        namedValues_[node->getName()] = alloca;
        TupleInfo ti;
        for (auto &e : tupleTypeRepr->getElements())
            ti.elementTypes.push_back(toLLVMType(e.get()));
        varTupleTypes_[node->getName()] = ti;
        return alloca;
    }

    // Init returns DynArray (map/filter/split): let doubled = arr.map(...)
    if (node->hasInit() && node->getInit()->getResolvedType() &&
        node->getInit()->getResolvedType()->getKind() == TypeRepr::Kind::Array) {
        auto *arrReprType = static_cast<const ArrayTypeRepr *>(node->getInit()->getResolvedType());
        if (arrReprType->isDynamic()) {
            auto *elemType = toLLVMType(arrReprType->getElement());
            const llvm::DataLayout &dl = module_->getDataLayout();
            uint64_t elemSize = dl.getTypeAllocSize(elemType);
            auto *structTy = getDynArrayStructTy();
            auto *alloca = createEntryBlockAlloca(func, node->getName(), structTy);
            auto *initVal = visit(const_cast<Expr *>(node->getInit()));
            if (initVal) builder_->CreateStore(initVal, alloca);
            namedValues_[node->getName()] = alloca;
            varDynArrayTypes_[node->getName()] = {elemType, elemSize};
            return alloca;
        }
    }

    // Fallback: visit init first to determine correct type for inferred vars
    if (node->hasInit()) {
        auto *initVal = visit(const_cast<Expr *>(node->getInit()));
        auto *type = toLLVMType(node->getType());
        // Use init value's type when annotation is absent/inferred (avoids i32 default)
        if (initVal && type == builder_->getInt32Ty() &&
            initVal->getType() != builder_->getInt32Ty()) {
            type = initVal->getType();
        }
        auto *alloca = createEntryBlockAlloca(func, node->getName(), type);
        if (initVal)
            builder_->CreateStore(initVal, alloca);
        namedValues_[node->getName()] = alloca;
        return alloca;
    }

    auto *type = toLLVMType(node->getType());
    auto *alloca = createEntryBlockAlloca(func, node->getName(), type);
    namedValues_[node->getName()] = alloca;
    return alloca;
}

llvm::Value *IRGen::visitStructDecl(StructDecl *node) {
    std::vector<llvm::Type *> fieldTypes;
    std::vector<std::string> fieldNames;
    for (auto &field : node->getFields()) {
        fieldTypes.push_back(toLLVMType(field->getType()));
        fieldNames.push_back(field->getName());
    }

    auto *structType = llvm::StructType::create(*context_, fieldTypes, node->getName());
    structTypes_[node->getName()] = structType;
    structFieldNames_[node->getName()] = std::move(fieldNames);
    return nullptr;
}

int IRGen::getStructFieldIndex(const std::string &structName, const std::string &fieldName) {
    auto it = structFieldNames_.find(structName);
    if (it == structFieldNames_.end())
        return -1;
    for (size_t i = 0; i < it->second.size(); ++i) {
        if (it->second[i] == fieldName)
            return static_cast<int>(i);
    }
    return -1;
}

llvm::Value *IRGen::visitImplDecl(ImplDecl *node) {
    const auto &typeName = node->getTypeName();

    // Record protocol conformance for vtable creation
    if (node->hasProtocol()) {
        protocolConformances_[node->getProtocolName()].push_back(typeName);
    }

    for (auto &method : node->getMethods()) {
        // Mangled name: TypeName_methodName
        std::string mangledName = typeName + "_" + method->getName();

        // Build param types: self is a pointer to struct
        std::vector<llvm::Type *> paramTypes;
        for (auto &param : method->getParams()) {
            if (param.isSelf) {
                auto stIt = structTypes_.find(typeName);
                if (stIt != structTypes_.end())
                    paramTypes.push_back(llvm::PointerType::getUnqual(*context_));
                else
                    paramTypes.push_back(llvm::PointerType::getUnqual(*context_));
            } else {
                paramTypes.push_back(toLLVMType(param.type.get()));
            }
        }

        auto *returnType = toLLVMType(method->getReturnType());
        auto *funcType = llvm::FunctionType::get(returnType, paramTypes, false);
        auto *func = llvm::Function::Create(
            funcType, llvm::Function::ExternalLinkage, mangledName, *module_);

        // Set parameter names
        size_t i = 0;
        for (auto &arg : func->args()) {
            arg.setName(method->getParams()[i].name);
            ++i;
        }

        if (!method->hasBody())
            continue;

        auto *entryBB = llvm::BasicBlock::Create(*context_, "entry", func);
        builder_->SetInsertPoint(entryBB);

        // Save and clear scope
        auto oldNamedValues = namedValues_;
        auto oldVarStructTypes = varStructTypes_;
        auto oldVarEnumTypes = varEnumTypes_;
        auto oldVarArrayTypes = varArrayTypes_;
        auto oldVarDynArrayTypes = varDynArrayTypes_;
        auto oldVarMapTypes = varMapTypes_;
        auto oldVarSetTypes = varSetTypes_;
        auto oldVarOptionalTypes = varOptionalTypes_;
        auto oldVarFuncTypes = varFuncTypes_;
        auto oldVarProtocolTypes = varProtocolTypes_;
        auto oldVarResultTypes = varResultTypes_;
        auto *oldFuncRI = currentFuncResultInfo_;
        namedValues_.clear();
        varStructTypes_.clear();
        varEnumTypes_.clear();
        varArrayTypes_.clear();
        varDynArrayTypes_.clear();
        varMapTypes_.clear();
        varSetTypes_.clear();
        varOptionalTypes_.clear();
        varFuncTypes_.clear();
        varProtocolTypes_.clear();
        varResultTypes_.clear();
        varFileTypes_.clear();
        currentFuncResultInfo_ = nullptr;

        // Create allocas for parameters
        i = 0;
        for (auto &arg : func->args()) {
            auto *alloca = createEntryBlockAlloca(func, std::string(arg.getName()),
                                                   arg.getType());
            builder_->CreateStore(&arg, alloca);
            namedValues_[std::string(arg.getName())] = alloca;
            if (method->getParams()[i].isSelf) {
                // Register self as struct or enum type
                auto etIt = enumTypes_.find(typeName);
                if (etIt != enumTypes_.end()) {
                    varEnumTypes_["self"] = typeName;
                } else {
                    varStructTypes_["self"] = typeName;
                }
            }
            ++i;
        }

        visitBlockStmt(const_cast<BlockStmt *>(method->getBody()));

        // Add implicit return if needed
        if (!builder_->GetInsertBlock()->getTerminator()) {
            if (returnType->isVoidTy()) {
                builder_->CreateRetVoid();
            } else {
                builder_->CreateRet(llvm::Constant::getNullValue(returnType));
            }
        }

        // Restore scope
        namedValues_ = oldNamedValues;
        varStructTypes_ = oldVarStructTypes;
        varEnumTypes_ = oldVarEnumTypes;
        varArrayTypes_ = oldVarArrayTypes;
        varDynArrayTypes_ = oldVarDynArrayTypes;
        varMapTypes_ = oldVarMapTypes;
        varSetTypes_ = oldVarSetTypes;
        varOptionalTypes_ = oldVarOptionalTypes;
        varFuncTypes_ = oldVarFuncTypes;
        varProtocolTypes_ = oldVarProtocolTypes;
        varResultTypes_ = oldVarResultTypes;
        currentFuncResultInfo_ = oldFuncRI;
    }

    // Generate default method implementations from protocol
    if (node->hasProtocol()) {
        auto pdIt = protocolDecls_.find(node->getProtocolName());
        if (pdIt != protocolDecls_.end()) {
            // Collect implemented method names
            std::set<std::string> implMethods;
            for (auto &m : node->getMethods())
                implMethods.insert(m->getName());

            for (auto &protoMethod : pdIt->second->getMethods()) {
                if (implMethods.count(protoMethod->getName()) || !protoMethod->hasBody())
                    continue;

                std::string mangledName = typeName + "_" + protoMethod->getName();

                std::vector<llvm::Type *> paramTypes;
                for (auto &param : protoMethod->getParams()) {
                    if (param.isSelf) {
                        paramTypes.push_back(llvm::PointerType::getUnqual(*context_));
                    } else {
                        paramTypes.push_back(toLLVMType(param.type.get()));
                    }
                }

                auto *returnType = toLLVMType(protoMethod->getReturnType());
                auto *funcType = llvm::FunctionType::get(returnType, paramTypes, false);
                auto *func = llvm::Function::Create(
                    funcType, llvm::Function::ExternalLinkage, mangledName, *module_);

                size_t i = 0;
                for (auto &arg : func->args()) {
                    arg.setName(protoMethod->getParams()[i].name);
                    ++i;
                }

                auto *entryBB = llvm::BasicBlock::Create(*context_, "entry", func);
                builder_->SetInsertPoint(entryBB);

                auto oldNamedValues = namedValues_;
                auto oldVarStructTypes = varStructTypes_;
                auto oldVarEnumTypes = varEnumTypes_;
                auto oldVarArrayTypes = varArrayTypes_;
                auto oldVarDynArrayTypes = varDynArrayTypes_;
                auto oldVarMapTypes = varMapTypes_;
                auto oldVarSetTypes = varSetTypes_;
                auto oldVarOptionalTypes = varOptionalTypes_;
                auto oldVarFuncTypes = varFuncTypes_;
                auto oldVarProtocolTypes = varProtocolTypes_;
                auto oldVarResultTypes = varResultTypes_;
                auto *oldFuncRI = currentFuncResultInfo_;
                namedValues_.clear();
                varStructTypes_.clear();
                varEnumTypes_.clear();
                varArrayTypes_.clear();
                varDynArrayTypes_.clear();
                varMapTypes_.clear();
                varSetTypes_.clear();
                varOptionalTypes_.clear();
                varFuncTypes_.clear();
                varProtocolTypes_.clear();
                varResultTypes_.clear();
                varFileTypes_.clear();
                currentFuncResultInfo_ = nullptr;

                i = 0;
                for (auto &arg : func->args()) {
                    auto *alloca = createEntryBlockAlloca(func, std::string(arg.getName()),
                                                           arg.getType());
                    builder_->CreateStore(&arg, alloca);
                    namedValues_[std::string(arg.getName())] = alloca;
                    if (protoMethod->getParams()[i].isSelf) {
                        varStructTypes_["self"] = typeName;
                    }
                    ++i;
                }

                visitBlockStmt(const_cast<BlockStmt *>(protoMethod->getBody()));

                if (!builder_->GetInsertBlock()->getTerminator()) {
                    if (returnType->isVoidTy()) {
                        builder_->CreateRetVoid();
                    } else {
                        builder_->CreateRet(llvm::Constant::getNullValue(returnType));
                    }
                }

                namedValues_ = oldNamedValues;
                varStructTypes_ = oldVarStructTypes;
                varEnumTypes_ = oldVarEnumTypes;
                varArrayTypes_ = oldVarArrayTypes;
                varDynArrayTypes_ = oldVarDynArrayTypes;
                varMapTypes_ = oldVarMapTypes;
                varSetTypes_ = oldVarSetTypes;
                varOptionalTypes_ = oldVarOptionalTypes;
                varFuncTypes_ = oldVarFuncTypes;
                varProtocolTypes_ = oldVarProtocolTypes;
                varResultTypes_ = oldVarResultTypes;
                currentFuncResultInfo_ = oldFuncRI;
            }
        }
    }

    return nullptr;
}

llvm::Value *IRGen::visitEnumDecl(EnumDecl *node) {
    const auto &enumName = node->getName();
    auto &caseMap = enumCases_[enumName];
    int tag = 0;
    bool hasPayload = false;

    for (auto &c : node->getCases()) {
        caseMap[c->getName()] = tag++;
        if (c->hasAssociatedValues())
            hasPayload = true;

        // Record payload types for each case
        std::vector<llvm::Type *> payloadTypes;
        for (auto &assocType : c->getAssociatedTypes()) {
            payloadTypes.push_back(toLLVMType(assocType.get()));
        }
        enumCasePayloads_[enumName][c->getName()] = std::move(payloadTypes);
    }

    if (hasPayload) {
        // Compute max payload size across all cases
        const llvm::DataLayout &dl = module_->getDataLayout();
        uint64_t maxPayloadSize = 0;
        for (auto &c : node->getCases()) {
            uint64_t caseSize = 0;
            for (auto &assocType : c->getAssociatedTypes()) {
                auto *llvmTy = toLLVMType(assocType.get());
                caseSize += dl.getTypeAllocSize(llvmTy);
            }
            if (caseSize > maxPayloadSize)
                maxPayloadSize = caseSize;
        }

        // Create tagged union: { i32 tag, [maxPayloadSize x i8] payload }
        auto *payloadArrayTy = llvm::ArrayType::get(builder_->getInt8Ty(), maxPayloadSize);
        auto *enumStructTy = llvm::StructType::create(
            *context_, {builder_->getInt32Ty(), payloadArrayTy}, enumName);
        enumTypes_[enumName] = enumStructTy;
    }

    return nullptr;
}

llvm::Value *IRGen::visitEnumCaseDecl(EnumCaseDecl *) {
    // Handled by visitEnumDecl
    return nullptr;
}

llvm::Value *IRGen::visitTypeAliasDecl(TypeAliasDecl *node) {
    // Record alias for toLLVMType resolution
    typeAliases_[node->getName()] = node->getTargetType();
    return nullptr;
}

} // namespace liva

#endif // LIVA_HAS_LLVM
