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
            if (separateCompilation_) {
                declareExternFunction(funcDecl);
                continue;
            }
        }

        if (decl->getKind() == ASTNode::NodeKind::StructDecl) {
            auto *structDecl = static_cast<StructDecl *>(decl.get());
            if (structDecl->isGeneric()) {
                genericStructDecls_[structDecl->getName()] = structDecl;
                continue;
            }
            if (separateCompilation_) {
                declareExternStruct(structDecl);
                continue;
            }
        }

        if (decl->getKind() == ASTNode::NodeKind::ImplDecl) {
            auto *implDecl = static_cast<ImplDecl *>(decl.get());
            if (implDecl->isGeneric()) {
                genericImplDecls_[implDecl->getTypeName()] = implDecl;
                continue;
            }
            if (separateCompilation_) {
                declareExternImpl(implDecl);
                continue;
            }
        }

        // EnumDecl, ProtocolDecl, TypeAliasDecl, ImportDecl: always visit
        // (they produce only metadata, no code bodies)
        visit(decl.get());
    }

    return nullptr;
}

void IRGen::declareExternFunction(FuncDecl *funcDecl) {
    // Store for default arg lookup
    funcDecls_[funcDecl->getName()] = funcDecl;

    // Build function type
    std::vector<llvm::Type *> paramTypes;
    for (auto &param : funcDecl->getParams()) {
        if (param.isVariadic) {
            paramTypes.push_back(getDynArrayStructTy());
        } else if (param.type && param.type->getKind() == TypeRepr::Kind::DynProtocol) {
            paramTypes.push_back(getTraitObjectTy());
        } else if (param.isRef) {
            paramTypes.push_back(llvm::PointerType::getUnqual(*context_));
        } else {
            paramTypes.push_back(toLLVMType(param.type.get()));
        }
    }

    auto *returnType = toLLVMType(funcDecl->getReturnType());
    auto *ptrTy = llvm::PointerType::getUnqual(*context_);

    // Async functions return ptr (LivaTask*)
    if (funcDecl->isAsync()) {
        asyncFuncNames_.insert(funcDecl->getName());
        returnType = ptrTy;
    }

    auto *funcType = llvm::FunctionType::get(returnType, paramTypes, false);
    // Create declaration only (no body) with ExternalLinkage
    module_->getOrInsertFunction(funcDecl->getName(), funcType);
}

void IRGen::declareExternStruct(StructDecl *structDecl) {
    // Register struct type layout (same as visitStructDecl but no code gen)
    std::vector<llvm::Type *> fieldTypes;
    std::vector<std::string> fieldNames;
    std::vector<const TypeRepr *> fieldTypeReprs;
    for (auto &field : structDecl->getFields()) {
        auto *typeRepr = field->getType();
        if (typeRepr && typeRepr->getKind() == TypeRepr::Kind::Array) {
            auto *arrRepr = static_cast<const ArrayTypeRepr *>(typeRepr);
            if (arrRepr->isDynamic()) {
                fieldTypes.push_back(getDynArrayStructTy());
            } else {
                fieldTypes.push_back(toLLVMType(typeRepr));
            }
        } else {
            fieldTypes.push_back(toLLVMType(typeRepr));
        }
        fieldNames.push_back(field->getName());
        fieldTypeReprs.push_back(typeRepr);
    }

    auto *structType = llvm::StructType::create(*context_, fieldTypes, structDecl->getName());
    structTypes_[structDecl->getName()] = structType;
    structFieldNames_[structDecl->getName()] = std::move(fieldNames);
    structFieldTypeReprs_[structDecl->getName()] = std::move(fieldTypeReprs);
}

void IRGen::declareExternImpl(ImplDecl *implDecl) {
    const auto &typeName = implDecl->getTypeName();

    // Record protocol conformance
    if (implDecl->hasProtocol()) {
        protocolConformances_[implDecl->getProtocolName()].push_back(typeName);
        if (implDecl->getProtocolName() == "Drop") {
            dropImplementors_.insert(typeName);
        }
    }

    // Declare each method as extern (no body)
    for (auto &method : implDecl->getMethods()) {
        std::string mangledName = typeName + "_" + method->getName();

        std::vector<llvm::Type *> paramTypes;
        for (auto &param : method->getParams()) {
            if (param.isSelf) {
                paramTypes.push_back(llvm::PointerType::getUnqual(*context_));
            } else {
                paramTypes.push_back(toLLVMType(param.type.get()));
            }
        }

        auto *returnType = toLLVMType(method->getReturnType());
        auto *funcType = llvm::FunctionType::get(returnType, paramTypes, false);
        module_->getOrInsertFunction(mangledName, funcType);
    }
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
        if (param.isVariadic) {
            paramTypes.push_back(getDynArrayStructTy());
        } else if (param.type && param.type->getKind() == TypeRepr::Kind::DynProtocol) {
            paramTypes.push_back(getTraitObjectTy());
        } else if (param.isRef) {
            paramTypes.push_back(llvm::PointerType::getUnqual(*context_));
        } else {
            paramTypes.push_back(toLLVMType(param.type.get()));
        }
    }

    auto *returnType = toLLVMType(node->getReturnType());
    auto *ptrTy = llvm::PointerType::getUnqual(*context_);

    // Async functions: return ptr (LivaTask*) instead of {i1, T}
    llvm::Type *asyncInnerRetType = nullptr;
    bool isAsyncMain = false;
    if (node->isAsync()) {
        asyncFuncNames_.insert(node->getName());
        asyncInnerRetType = returnType;
        if (returnType->isVoidTy()) {
            asyncInnerRetType = builder_->getInt1Ty(); // placeholder for Void tasks
        }
        returnType = ptrTy;  // Phase 2: returns LivaTask*
        if (node->getName() == "main") {
            isAsyncMain = true;
        }
    }

    // C ABI: main must return i32 (non-async main)
    bool isMain = (node->getName() == "main");
    if (isMain && !node->isAsync() && returnType->isVoidTy()) {
        returnType = builder_->getInt32Ty();
    }

    // C ABI: main gets (int argc, char **argv) on all platforms
    if (isMain && !node->isAsync()) {
        paramTypes.clear();
        paramTypes.push_back(builder_->getInt32Ty());
        paramTypes.push_back(llvm::PointerType::getUnqual(*context_));
    }

    // For async main, the coroutine is named "liva_async_main"
    std::string funcName = isAsyncMain ? "liva_async_main" : node->getName();

    auto *funcType = llvm::FunctionType::get(returnType, paramTypes, false);
    auto *func = llvm::Function::Create(
        funcType, llvm::Function::ExternalLinkage, funcName, *module_);

    // Set parameter names
    if (isMain && !node->isAsync()) {
        func->getArg(0)->setName("argc");
        func->getArg(1)->setName("argv");
    } else {
        size_t i = 0;
        for (auto &arg : func->args()) {
            arg.setName(node->getParams()[i].name);
            ++i;
        }
    }

    if (!node->hasBody())
        return func;

    // Attach debug info subprogram to the function
    if (diBuilder_) {
        auto *funcDbgType = createFunctionDebugType();
        unsigned lineNo = node->getStartLoc().isValid() ? node->getStartLoc().line : 0;
        auto *sp = diBuilder_->createFunction(
            diFile_,                           // scope
            func->getName(),                   // name
            func->getName(),                   // linkage name
            diFile_,                           // file
            lineNo,                            // line number
            funcDbgType,                       // subroutine type
            lineNo,                            // scope line
            llvm::DINode::FlagPrototyped,      // flags
            llvm::DISubprogram::SPFlagDefinition  // SPFlags
        );
        func->setSubprogram(sp);
    }

    // Create entry block
    auto *entryBB = llvm::BasicBlock::Create(*context_, "entry", func);
    builder_->SetInsertPoint(entryBB);

    // Set initial debug location for function body
    if (diBuilder_) {
        emitDebugLocation(node->getStartLoc());
    }

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
    auto oldHeapStringVars = heapStringVars_;
    auto oldTempStrings = tempStrings_;
    auto *oldFuncResultInfo = currentFuncResultInfo_;
    auto *oldFuncOptInner = currentFuncOptionalInner_;
    bool oldIsAsync = currentIsAsync_;
    auto *oldAsyncRetType = asyncDeclaredRetType_;
    // Save coroutine state
    auto *oldCoroTask = currentCoroTask_;
    auto *oldCoroHandle = currentCoroHandle_;
    auto *oldCoroId = currentCoroId_;
    auto *oldCoroPromise = currentCoroPromise_;
    auto *oldCoroFinalBB = currentCoroFinalBB_;
    auto *oldCoroCleanupBB = currentCoroCleanupBB_;
    auto *oldCoroSuspendBB = currentCoroSuspendBB_;

    currentIsAsync_ = node->isAsync();
    asyncDeclaredRetType_ = node->isAsync() ? asyncInnerRetType : nullptr;
    currentCoroTask_ = nullptr;
    currentCoroHandle_ = nullptr;
    currentCoroId_ = nullptr;
    currentCoroPromise_ = nullptr;
    currentCoroFinalBB_ = nullptr;
    currentCoroCleanupBB_ = nullptr;
    currentCoroSuspendBB_ = nullptr;
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
    heapStringVars_.clear();
    tempStrings_.clear();
    currentFuncResultInfo_ = nullptr;
    currentFuncOptionalInner_ = nullptr;

    // Track Optional return type for return-nil / return-value wrapping
    if (node->getReturnType() &&
        node->getReturnType()->getKind() == TypeRepr::Kind::Optional) {
        auto *opt = static_cast<const OptionalTypeRepr *>(node->getReturnType());
        currentFuncOptionalInner_ = toLLVMType(opt->getInner());
    }

    // Track Result return type for try expressions (visitFuncDecl)
    if (node->getReturnType() &&
        node->getReturnType()->getKind() == TypeRepr::Kind::Result) {
        auto *rt = static_cast<const ResultTypeRepr *>(node->getReturnType());
        currentFuncResultInfoStorage_ = {toLLVMType(rt->getOkType()), toLLVMType(rt->getErrType())};
        currentFuncResultInfo_ = &currentFuncResultInfoStorage_;
    }

    // === Phase 2 Coroutine Ramp Setup (for async functions) ===
    if (node->isAsync()) {
        auto *i8Ty = builder_->getInt8Ty();
        auto *i32Ty = builder_->getInt32Ty();
        auto *i64Ty = builder_->getInt64Ty();
        auto *i1Ty = builder_->getInt1Ty();
        auto *tokenTy = llvm::Type::getTokenTy(*context_);

        // Promise alloca — stores the return value
        currentCoroPromise_ = createEntryBlockAlloca(func, "coro.promise", asyncInnerRetType);
        builder_->CreateStore(llvm::Constant::getNullValue(asyncInnerRetType), currentCoroPromise_);

        // Task alloca — stores LivaTask* pointer
        currentCoroTask_ = createEntryBlockAlloca(func, "coro.task.alloca", ptrTy);

        // @llvm.coro.id(i32 0, ptr %promise, ptr null, ptr null)
        auto *coroIdFn = llvm::Intrinsic::getOrInsertDeclaration(module_.get(),
            llvm::Intrinsic::coro_id);
        currentCoroId_ = builder_->CreateCall(coroIdFn, {
            i32Ty->isIntegerTy() ? builder_->getInt32(0) : builder_->getInt32(0),
            currentCoroPromise_,
            llvm::ConstantPointerNull::get(ptrTy),
            llvm::ConstantPointerNull::get(ptrTy)
        }, "coro.id");

        // @llvm.coro.alloc(token %id)
        auto *coroAllocFn = llvm::Intrinsic::getOrInsertDeclaration(module_.get(),
            llvm::Intrinsic::coro_alloc);
        auto *needAlloc = builder_->CreateCall(coroAllocFn, {currentCoroId_}, "need.alloc");

        auto *coroAllocBB = llvm::BasicBlock::Create(*context_, "coro.alloc", func);
        auto *coroInitBB = llvm::BasicBlock::Create(*context_, "coro.init", func);
        builder_->CreateCondBr(needAlloc, coroAllocBB, coroInitBB);

        // coro.alloc: allocate frame
        builder_->SetInsertPoint(coroAllocBB);
        auto *coroSizeFn = llvm::Intrinsic::getOrInsertDeclaration(module_.get(),
            llvm::Intrinsic::coro_size, {i64Ty});
        auto *frameSize = builder_->CreateCall(coroSizeFn, {}, "coro.size");
        auto *mallocFn = module_->getFunction("malloc");
        auto *frameMem = builder_->CreateCall(mallocFn, {frameSize}, "coro.mem");
        builder_->CreateBr(coroInitBB);

        // coro.init: phi for memory, then coro.begin
        builder_->SetInsertPoint(coroInitBB);
        auto *phiMem = builder_->CreatePHI(ptrTy, 2, "rawmem");
        phiMem->addIncoming(frameMem, coroAllocBB);
        phiMem->addIncoming(llvm::ConstantPointerNull::get(ptrTy), entryBB);

        auto *coroBeginFn = llvm::Intrinsic::getOrInsertDeclaration(module_.get(),
            llvm::Intrinsic::coro_begin);
        currentCoroHandle_ = builder_->CreateCall(coroBeginFn,
            {currentCoroId_, phiMem}, "coro.hdl");

        // Create LivaTask
        auto *taskCreateFn = getOrPanic("liva_task_create");
        auto *task = builder_->CreateCall(taskCreateFn, {currentCoroHandle_}, "task");
        builder_->CreateStore(task, currentCoroTask_);

        // Create the coro.final, coro.cleanup, coro.suspend blocks (will be filled later)
        currentCoroFinalBB_ = llvm::BasicBlock::Create(*context_, "coro.final", func);
        currentCoroCleanupBB_ = llvm::BasicBlock::Create(*context_, "coro.cleanup", func);
        currentCoroSuspendBB_ = llvm::BasicBlock::Create(*context_, "coro.suspend", func);

        // Create body block and branch to it
        auto *bodyBB = llvm::BasicBlock::Create(*context_, "coro.body", func);
        builder_->CreateBr(bodyBB);
        builder_->SetInsertPoint(bodyBB);
    }

    // Create allocas for parameters
    if (isMain && !isAsyncMain) {
        // C ABI main: call liva_init_args(argc, argv) for cross-platform args
        auto *initArgsFn = getOrPanic("liva_init_args");
        builder_->CreateCall(initArgsFn, {func->getArg(0), func->getArg(1)});
    } else {
        for (auto &arg : func->args()) {
            auto *alloca = createEntryBlockAlloca(func, std::string(arg.getName()), arg.getType());
            builder_->CreateStore(&arg, alloca);
            namedValues_[std::string(arg.getName())] = alloca;
        }
    }

    // Register variadic params as DynArray variables
    for (auto &param : node->getParams()) {
        if (param.isVariadic && param.type) {
            auto *elemType = toLLVMType(param.type.get());
            auto &DL = module_->getDataLayout();
            uint64_t elemSize = DL.getTypeAllocSize(elemType);
            varDynArrayTypes_[param.name] = {elemType, elemSize};
        }
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

    // Populate varProtocolTypes_ for dyn Protocol parameters
    for (auto &param : node->getParams()) {
        if (param.type && param.type->getKind() == TypeRepr::Kind::DynProtocol) {
            auto *dynType = static_cast<const DynProtocolTypeRepr *>(param.type.get());
            varProtocolTypes_[param.name] = dynType->getProtocolName();
        }
    }

    // Generate body
    visitBlockStmt(const_cast<BlockStmt *>(node->getBody()));

    // Add implicit return / terminator
    if (!builder_->GetInsertBlock()->getTerminator()) {
        emitScopeCleanup();
        if (currentIsAsync_ && currentCoroFinalBB_) {
            // Async: store null to promise and branch to coro.final
            if (currentCoroPromise_) {
                builder_->CreateStore(
                    llvm::Constant::getNullValue(asyncDeclaredRetType_), currentCoroPromise_);
            }
            builder_->CreateBr(currentCoroFinalBB_);
        } else if (isMain && !isAsyncMain) {
            builder_->CreateRet(builder_->getInt32(0));
        } else if (returnType->isVoidTy()) {
            builder_->CreateRetVoid();
        } else {
            builder_->CreateRet(llvm::Constant::getNullValue(returnType));
        }
    }

    // === Phase 2 Coroutine Final/Cleanup/Suspend blocks ===
    if (node->isAsync() && currentCoroFinalBB_) {
        auto *i8Ty = builder_->getInt8Ty();
        auto *tokenTy = llvm::Type::getTokenTy(*context_);

        // coro.final: mark task complete + final suspend
        builder_->SetInsertPoint(currentCoroFinalBB_);
        auto *taskLoad = builder_->CreateLoad(ptrTy, currentCoroTask_, "task.final");
        auto *completeFn = getOrPanic("liva_task_complete");
        builder_->CreateCall(completeFn, {taskLoad});

        auto *coroSuspendFn = llvm::Intrinsic::getOrInsertDeclaration(module_.get(),
            llvm::Intrinsic::coro_suspend);
        auto *noneToken = llvm::ConstantTokenNone::get(*context_);
        auto *finalSuspend = builder_->CreateCall(coroSuspendFn,
            {noneToken, builder_->getTrue()}, "fs");
        auto *sw = builder_->CreateSwitch(finalSuspend, currentCoroSuspendBB_, 2);
        sw->addCase(builder_->getInt8(0), currentCoroSuspendBB_);
        sw->addCase(builder_->getInt8(1), currentCoroCleanupBB_);

        // coro.cleanup: free coroutine frame
        builder_->SetInsertPoint(currentCoroCleanupBB_);
        auto *coroFreeFn = llvm::Intrinsic::getOrInsertDeclaration(module_.get(),
            llvm::Intrinsic::coro_free);
        auto *freeMem = builder_->CreateCall(coroFreeFn,
            {currentCoroId_, currentCoroHandle_}, "coro.free.mem");
        auto *freeFn = module_->getFunction("free");
        builder_->CreateCall(freeFn, {freeMem});
        builder_->CreateBr(currentCoroSuspendBB_);

        // coro.suspend: coro.end + return task
        builder_->SetInsertPoint(currentCoroSuspendBB_);
        auto *coroEndFn = llvm::Intrinsic::getOrInsertDeclaration(module_.get(),
            llvm::Intrinsic::coro_end);
        auto *noneToken2 = llvm::ConstantTokenNone::get(*context_);
        builder_->CreateCall(coroEndFn,
            {currentCoroHandle_, builder_->getFalse(), noneToken2});
        auto *taskRet = builder_->CreateLoad(ptrTy, currentCoroTask_, "task.ret");
        builder_->CreateRet(taskRet);
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
    heapStringVars_ = oldHeapStringVars;
    tempStrings_ = oldTempStrings;
    currentFuncResultInfo_ = oldFuncResultInfo;
    currentFuncOptionalInner_ = oldFuncOptInner;
    currentIsAsync_ = oldIsAsync;
    asyncDeclaredRetType_ = oldAsyncRetType;
    // Restore coroutine state
    currentCoroTask_ = oldCoroTask;
    currentCoroHandle_ = oldCoroHandle;
    currentCoroId_ = oldCoroId;
    currentCoroPromise_ = oldCoroPromise;
    currentCoroFinalBB_ = oldCoroFinalBB;
    currentCoroCleanupBB_ = oldCoroCleanupBB;
    currentCoroSuspendBB_ = oldCoroSuspendBB;

    // === Async Main Wrapper ===
    if (isAsyncMain) {
        // Generate: i32 @main(i32 %argc, ptr %argv) { init_args; call @liva_async_main(); ... }
        auto *i32Ty = builder_->getInt32Ty();
        auto *mainFuncType = llvm::FunctionType::get(i32Ty,
            {i32Ty, llvm::PointerType::getUnqual(*context_)}, false);
        auto *mainFunc = llvm::Function::Create(
            mainFuncType, llvm::Function::ExternalLinkage, "main", *module_);
        mainFunc->getArg(0)->setName("argc");
        mainFunc->getArg(1)->setName("argv");
        auto *mainEntry = llvm::BasicBlock::Create(*context_, "entry", mainFunc);
        builder_->SetInsertPoint(mainEntry);

        // Initialize cross-platform args
        auto *initArgsFn = getOrPanic("liva_init_args");
        builder_->CreateCall(initArgsFn, {mainFunc->getArg(0), mainFunc->getArg(1)});

        // Call liva_async_main()
        auto *asyncTask = builder_->CreateCall(func, {}, "root.task");

        // Run scheduler
        auto *schedulerFn = getOrPanic("liva_scheduler_run");
        builder_->CreateCall(schedulerFn, {asyncTask});

        // Get result from promise if non-void return type
        auto *declaredRet = toLLVMType(node->getReturnType());
        if (declaredRet->isIntegerTy(32)) {
            // Get handle, then use coro.promise to read result
            auto *getHandleFn = getOrPanic("liva_task_get_handle");
            auto *hdl = builder_->CreateCall(getHandleFn, {asyncTask}, "root.hdl");

            auto *coroPromiseFn = llvm::Intrinsic::getOrInsertDeclaration(module_.get(),
                llvm::Intrinsic::coro_promise);
            auto *promisePtr = builder_->CreateCall(coroPromiseFn,
                {hdl, builder_->getInt32(4), builder_->getFalse()}, "promise.ptr");
            auto *result = builder_->CreateLoad(i32Ty, promisePtr, "main.result");

            // Cleanup
            auto *coroDestroyFn = getOrPanic("liva_coro_destroy");
            builder_->CreateCall(coroDestroyFn, {hdl});
            auto *taskDestroyFn = getOrPanic("liva_task_destroy");
            builder_->CreateCall(taskDestroyFn, {asyncTask});

            builder_->CreateRet(result);
        } else {
            // Void or other: just cleanup and return 0
            auto *getHandleFn = getOrPanic("liva_task_get_handle");
            auto *hdl = builder_->CreateCall(getHandleFn, {asyncTask}, "root.hdl");
            auto *coroDestroyFn = getOrPanic("liva_coro_destroy");
            builder_->CreateCall(coroDestroyFn, {hdl});
            auto *taskDestroyFn = getOrPanic("liva_task_destroy");
            builder_->CreateCall(taskDestroyFn, {asyncTask});

            builder_->CreateRet(builder_->getInt32(0));
        }
    }

    return func;
}

llvm::Value *IRGen::visitVarDecl(VarDecl *node) {
    // Const variable: no alloca, cache constant value
    if (node->isConst() && node->hasInit()) {
        auto *val = visit(const_cast<Expr *>(node->getInit()));
        if (auto *constVal = llvm::dyn_cast<llvm::Constant>(val)) {
            constValues_[node->getName()] = constVal;
        }
        return nullptr;
    }

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

    // Dyn protocol trait object: let s: dyn Shape = circle
    if (node->hasTypeAnnotation() && node->getType() &&
        node->getType()->getKind() == TypeRepr::Kind::DynProtocol) {
        auto *dynType = static_cast<const DynProtocolTypeRepr *>(node->getType());
        const std::string &protocolName = dynType->getProtocolName();
        auto pmIt = protocolMethodNames_.find(protocolName);
        if (pmIt != protocolMethodNames_.end()) {
            auto *traitTy = getTraitObjectTy();
            auto *alloca = createEntryBlockAlloca(func, node->getName(), traitTy);

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

                auto *dataGEP = builder_->CreateStructGEP(traitTy, alloca, 0, "dyn.data");
                builder_->CreateStore(dataAlloca, dataGEP);

                auto *vtable = getOrCreateVtable(protocolName, concreteType);
                auto *vtableGEP = builder_->CreateStructGEP(traitTy, alloca, 1, "dyn.vtable");
                builder_->CreateStore(vtable, vtableGEP);

                namedValues_[node->getName()] = alloca;
                varProtocolTypes_[node->getName()] = protocolName;
                return alloca;
            }
        }
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

    // Check if init is a class constructor call: var x = ClassName(args)
    if (node->hasInit() &&
        node->getInit()->getKind() == ASTNode::NodeKind::CallExpr) {
        auto *callExpr = static_cast<CallExpr *>(
            const_cast<Expr *>(node->getInit()));
        if (callExpr->getCallee()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
            auto *ident = static_cast<IdentifierExpr *>(callExpr->getCallee());
            if (classNames_.count(ident->getName())) {
                auto *initVal = visit(const_cast<Expr *>(node->getInit()));
                if (initVal) {
                    auto *ptrTy = llvm::PointerType::getUnqual(*context_);
                    auto *alloca = createEntryBlockAlloca(func, node->getName(), ptrTy);
                    builder_->CreateStore(initVal, alloca);
                    namedValues_[node->getName()] = alloca;
                    varClassTypes_[node->getName()] = ident->getName();
                    return alloca;
                }
            }
        }
    }

    // Check if init is a static struct method call: var s = Student.new("Alice")
    if (node->hasInit() &&
        node->getInit()->getKind() == ASTNode::NodeKind::CallExpr) {
        auto *callExpr = static_cast<CallExpr *>(
            const_cast<Expr *>(node->getInit()));
        if (callExpr->getCallee()->getKind() == ASTNode::NodeKind::MemberExpr) {
            auto *memberExpr = static_cast<MemberExpr *>(callExpr->getCallee());
            if (memberExpr->getObject()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
                auto *ident = static_cast<IdentifierExpr *>(memberExpr->getObject());
                auto stIt = structTypes_.find(ident->getName());
                if (stIt != structTypes_.end()) {
                    auto *structTy = stIt->second;
                    auto *initVal = visit(const_cast<Expr *>(node->getInit()));
                    if (initVal) {
                        auto *alloca = createEntryBlockAlloca(func, node->getName(), structTy);
                        builder_->CreateStore(initVal, alloca);
                        namedValues_[node->getName()] = alloca;
                        varStructTypes_[node->getName()] = ident->getName();
                        return alloca;
                    }
                }
            }
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
            auto *newFn = getOrPanic("liva_array_new");
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

            auto *newFn = getOrPanic("liva_map_new");
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

            auto *newFn = getOrPanic("liva_set_new");
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

        // Track heap string ownership (explicit String type or inferred from string init)
        if (initVal && type->isPointerTy()) {
            bool isString = (node->getType() &&
                node->getType()->getKind() == TypeRepr::Kind::String);
            if (!isString && node->getInit() && node->getInit()->getResolvedType() &&
                node->getInit()->getResolvedType()->getKind() == TypeRepr::Kind::String) {
                isString = true;
            }
            if (isString) {
                transferStringOwnership(initVal, node->getName());
            }
        }

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
    std::vector<const TypeRepr *> fieldTypeReprs;
    for (auto &field : node->getFields()) {
        auto *typeRepr = field->getType();
        if (typeRepr && typeRepr->getKind() == TypeRepr::Kind::Array) {
            auto *arrRepr = static_cast<const ArrayTypeRepr *>(typeRepr);
            if (arrRepr->isDynamic()) {
                fieldTypes.push_back(getDynArrayStructTy());
            } else {
                fieldTypes.push_back(toLLVMType(typeRepr));
            }
        } else {
            fieldTypes.push_back(toLLVMType(typeRepr));
        }
        fieldNames.push_back(field->getName());
        fieldTypeReprs.push_back(typeRepr);
    }

    auto *structType = llvm::StructType::create(*context_, fieldTypes, node->getName());
    structTypes_[node->getName()] = structType;
    structFieldNames_[node->getName()] = std::move(fieldNames);
    structFieldTypeReprs_[node->getName()] = std::move(fieldTypeReprs);
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
        if (node->getProtocolName() == "Drop") {
            dropImplementors_.insert(typeName);
        }
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
        auto oldVarClassTypes = varClassTypes_;
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
        auto *oldFuncOptInner = currentFuncOptionalInner_;
        auto oldMovedVars = movedVars_;
        auto oldHeapStringVars = heapStringVars_;
        auto oldTempStrings = tempStrings_;
        namedValues_.clear();
        varStructTypes_.clear();
        varClassTypes_.clear();
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
        currentFuncOptionalInner_ = nullptr;
        movedVars_.clear();
        heapStringVars_.clear();
        tempStrings_.clear();

        // Track Optional return type for methods
        if (method->getReturnType() &&
            method->getReturnType()->getKind() == TypeRepr::Kind::Optional) {
            auto *opt = static_cast<const OptionalTypeRepr *>(method->getReturnType());
            currentFuncOptionalInner_ = toLLVMType(opt->getInner());
        }

        // Create allocas for parameters
        i = 0;
        for (auto &arg : func->args()) {
            auto *alloca = createEntryBlockAlloca(func, std::string(arg.getName()),
                                                   arg.getType());
            builder_->CreateStore(&arg, alloca);
            namedValues_[std::string(arg.getName())] = alloca;
            if (method->getParams()[i].isSelf) {
                // Register self as struct, enum, or class type
                auto etIt = enumTypes_.find(typeName);
                if (etIt != enumTypes_.end()) {
                    varEnumTypes_["self"] = typeName;
                } else if (classTypes_.find(typeName) != classTypes_.end()) {
                    varClassTypes_["self"] = typeName;
                } else {
                    varStructTypes_["self"] = typeName;
                }
            }
            ++i;
        }

        visitBlockStmt(const_cast<BlockStmt *>(method->getBody()));

        // Add implicit return if needed
        if (!builder_->GetInsertBlock()->getTerminator()) {
            emitScopeCleanup();
            if (returnType->isVoidTy()) {
                builder_->CreateRetVoid();
            } else {
                builder_->CreateRet(llvm::Constant::getNullValue(returnType));
            }
        }

        // Restore scope
        namedValues_ = oldNamedValues;
        varStructTypes_ = oldVarStructTypes;
        varClassTypes_ = oldVarClassTypes;
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
        currentFuncOptionalInner_ = oldFuncOptInner;
        movedVars_ = oldMovedVars;
        heapStringVars_ = oldHeapStringVars;
        tempStrings_ = oldTempStrings;
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
                auto *oldFuncOptInner2 = currentFuncOptionalInner_;
                auto oldMovedVars2 = movedVars_;
                auto oldHeapStringVars2 = heapStringVars_;
                auto oldTempStrings2 = tempStrings_;
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
                currentFuncOptionalInner_ = nullptr;
                movedVars_.clear();
                heapStringVars_.clear();
                tempStrings_.clear();

                // Track Optional return type for protocol default methods
                if (protoMethod->getReturnType() &&
                    protoMethod->getReturnType()->getKind() == TypeRepr::Kind::Optional) {
                    auto *opt = static_cast<const OptionalTypeRepr *>(protoMethod->getReturnType());
                    currentFuncOptionalInner_ = toLLVMType(opt->getInner());
                }

                i = 0;
                for (auto &arg : func->args()) {
                    auto *alloca = createEntryBlockAlloca(func, std::string(arg.getName()),
                                                           arg.getType());
                    builder_->CreateStore(&arg, alloca);
                    namedValues_[std::string(arg.getName())] = alloca;
                    if (protoMethod->getParams()[i].isSelf) {
                        if (classTypes_.find(typeName) != classTypes_.end()) {
                            varClassTypes_["self"] = typeName;
                        } else {
                            varStructTypes_["self"] = typeName;
                        }
                    }
                    ++i;
                }

                visitBlockStmt(const_cast<BlockStmt *>(protoMethod->getBody()));

                if (!builder_->GetInsertBlock()->getTerminator()) {
                    emitScopeCleanup();
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
                currentFuncOptionalInner_ = oldFuncOptInner2;
                movedVars_ = oldMovedVars2;
                heapStringVars_ = oldHeapStringVars2;
                tempStrings_ = oldTempStrings2;
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

llvm::Value *IRGen::visitClassDecl(ClassDecl *node) {
    const std::string &className = node->getName();
    classNames_.insert(className);

    // Record parent class (if any — protocols were reclassified by TypeChecker)
    if (node->hasParentClass()) {
        classParent_[className] = node->getParentClass();
    }

    // Record protocol conformances (from class declaration)
    for (auto &protoName : node->getProtocols()) {
        protocolConformances_[protoName].push_back(className);
    }

    // Collect all fields (parent chain + own)
    std::vector<std::string> allFieldNames;
    std::vector<const TypeRepr *> allFieldTypeReprs;
    std::vector<llvm::Type *> allFieldLLVMTypes;

    // Walk parent chain root → child
    std::vector<std::string> parentChain;
    {
        std::string cur = className;
        while (true) {
            auto pit = classParent_.find(cur);
            if (pit != classParent_.end()) {
                parentChain.push_back(pit->second);
                cur = pit->second;
            } else {
                break;
            }
        }
        std::reverse(parentChain.begin(), parentChain.end());
    }

    // Add parent fields
    for (auto &parent : parentChain) {
        auto fit = classFieldNames_.find(parent);
        if (fit != classFieldNames_.end()) {
            auto tit = classFieldTypeReprs_.find(parent);
            for (size_t i = 0; i < fit->second.size(); ++i) {
                allFieldNames.push_back(fit->second[i]);
                allFieldTypeReprs.push_back(tit->second[i]);
                allFieldLLVMTypes.push_back(toLLVMType(tit->second[i]));
            }
        }
    }

    // Add own fields
    for (auto *field : node->getFields()) {
        allFieldNames.push_back(field->getName());
        allFieldTypeReprs.push_back(field->getType());
        allFieldLLVMTypes.push_back(toLLVMType(field->getType()));
    }

    classFieldNames_[className] = allFieldNames;
    classFieldTypeReprs_[className] = allFieldTypeReprs;

    // LLVM struct: { ptr vtable, fields... }
    std::vector<llvm::Type *> structFields;
    structFields.push_back(llvm::PointerType::getUnqual(*context_)); // vtable ptr
    for (auto *ft : allFieldLLVMTypes) {
        structFields.push_back(ft);
    }
    auto *classType = llvm::StructType::create(*context_, structFields, className);
    classTypes_[className] = classType;

    // Collect vtable methods: deinit (index 0) + virtual methods (parent chain + own)
    std::vector<std::string> vtableMethods;
    vtableMethods.push_back("deinit"); // always index 0

    // Inherit parent vtable methods
    if (!parentChain.empty()) {
        auto &rootParent = parentChain.front();
        // Walk from root to immediate parent
        for (auto &parent : parentChain) {
            auto vit = classVtableMethods_.find(parent);
            if (vit != classVtableMethods_.end()) {
                vtableMethods = vit->second;
                break;
            }
        }
    }

    // Add own methods (or override slots)
    for (auto *method : node->getMethods()) {
        bool found = false;
        for (size_t i = 0; i < vtableMethods.size(); ++i) {
            if (vtableMethods[i] == method->getName()) {
                found = true;
                break;
            }
        }
        if (!found) {
            vtableMethods.push_back(method->getName());
        }
    }

    classVtableMethods_[className] = vtableMethods;

    // Record method indices
    for (size_t i = 0; i < vtableMethods.size(); ++i) {
        classMethodIndices_[className][vtableMethods[i]] = static_cast<int>(i);
    }

    std::string prevClassCtx = currentClassContext_;
    currentClassContext_ = className;

    // Generate methods — self is implicit first parameter
    for (auto *method : node->getMethods()) {
        std::string mangledName = className + "_" + method->getName();

        // Always add implicit self (ptr) as first param
        std::vector<llvm::Type *> paramTypes;
        paramTypes.push_back(llvm::PointerType::getUnqual(*context_)); // implicit self
        for (auto &param : method->getParams()) {
            paramTypes.push_back(toLLVMType(param.type.get()));
        }

        auto *returnType = toLLVMType(method->getReturnType());
        auto *funcType = llvm::FunctionType::get(returnType, paramTypes, false);
        auto *func = llvm::Function::Create(
            funcType, llvm::Function::ExternalLinkage, mangledName, *module_);

        // Set arg names: self + user params
        auto argIt = func->arg_begin();
        argIt->setName("self");
        ++argIt;
        for (auto &param : method->getParams()) {
            argIt->setName(param.name);
            ++argIt;
        }

        if (!method->hasBody()) continue;

        auto *entryBB = llvm::BasicBlock::Create(*context_, "entry", func);
        builder_->SetInsertPoint(entryBB);

        auto savedValues = namedValues_;
        auto savedVarStructTypes = varStructTypes_;
        auto savedVarClassTypes = varClassTypes_;
        namedValues_.clear();

        for (auto &arg : func->args()) {
            auto *alloca = createEntryBlockAlloca(func, std::string(arg.getName()), arg.getType());
            builder_->CreateStore(&arg, alloca);
            namedValues_[std::string(arg.getName())] = alloca;
            if (std::string(arg.getName()) == "self") {
                varClassTypes_["self"] = className;
            }
        }

        visit(const_cast<BlockStmt *>(method->getBody()));

        // Ensure terminator
        auto *lastBB = builder_->GetInsertBlock();
        if (!lastBB->getTerminator()) {
            if (returnType->isVoidTy()) {
                builder_->CreateRetVoid();
            } else {
                builder_->CreateRet(llvm::Constant::getNullValue(returnType));
            }
        }

        namedValues_ = savedValues;
        varStructTypes_ = savedVarStructTypes;
        varClassTypes_ = savedVarClassTypes;
    }

    // Generate init: ClassName_init(args) → ptr
    auto *initDecl = node->getInit();
    {
        std::string initName = className + "_init";

        std::vector<llvm::Type *> initParamTypes;
        if (initDecl) {
            for (auto &param : initDecl->getParams()) {
                initParamTypes.push_back(toLLVMType(param.type.get()));
            }
        }

        auto *ptrTy = llvm::PointerType::getUnqual(*context_);
        auto *initFuncType = llvm::FunctionType::get(ptrTy, initParamTypes, false);
        auto *initFunc = llvm::Function::Create(
            initFuncType, llvm::Function::ExternalLinkage, initName, *module_);

        auto *entryBB = llvm::BasicBlock::Create(*context_, "entry", initFunc);
        builder_->SetInsertPoint(entryBB);

        auto savedValues = namedValues_;
        namedValues_.clear();

        // Set parameter names — no self in param list (implicit)
        if (initDecl) {
            size_t argIdx = 0;
            for (auto &arg : initFunc->args()) {
                if (argIdx < initDecl->getParams().size()) {
                    arg.setName(initDecl->getParams()[argIdx].name);
                    auto *alloca = createEntryBlockAlloca(
                        initFunc, std::string(arg.getName()), arg.getType());
                    builder_->CreateStore(&arg, alloca);
                    namedValues_[std::string(arg.getName())] = alloca;
                }
                ++argIdx;
            }
        }

        // malloc(sizeof(ClassType))
        auto *mallocFn = module_->getFunction("malloc");
        if (!mallocFn) {
            auto *mallocTy = llvm::FunctionType::get(
                ptrTy, {llvm::Type::getInt64Ty(*context_)}, false);
            mallocFn = llvm::Function::Create(
                mallocTy, llvm::Function::ExternalLinkage, "malloc", *module_);
        }
        const llvm::DataLayout &dataLayout = module_->getDataLayout();
        uint64_t classSize = dataLayout.getTypeAllocSize(classType);
        auto *sizeVal = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context_), classSize);
        auto *objPtr = builder_->CreateCall(mallocFn, {sizeVal}, "obj");

        // Store vtable pointer at index 0
        auto *vtableGEP = builder_->CreateStructGEP(classType, objPtr, 0, "vtable_ptr");
        // Vtable will be created below, use forward reference
        // For now create a placeholder that will be resolved
        auto *selfAlloca = createEntryBlockAlloca(initFunc, "self", ptrTy);
        builder_->CreateStore(objPtr, selfAlloca);
        namedValues_["self"] = selfAlloca;
        varClassTypes_["self"] = className;

        // Execute init body (user code initializes fields via self.field = ...)
        if (initDecl && initDecl->getBody()) {
            visit(const_cast<BlockStmt *>(initDecl->getBody()));
        }

        // Set vtable pointer (after body so we can reference the vtable global)
        auto *selfVal = builder_->CreateLoad(ptrTy, selfAlloca, "self_val");
        auto *vtableGEP2 = builder_->CreateStructGEP(classType, selfVal, 0, "vtable_slot");
        // Vtable global will be created below
        // Store nullptr for now; fixed up after vtable creation
        builder_->CreateStore(llvm::ConstantPointerNull::get(ptrTy), vtableGEP2);

        builder_->CreateRet(selfVal);
        namedValues_ = savedValues;
    }

    // Generate init_fields: ClassName_init_fields(self, args) → void
    // Used by child class super.init() — doesn't malloc, takes self ptr
    {
        std::string initFieldsName = className + "_init_fields";

        std::vector<llvm::Type *> paramTypes;
        paramTypes.push_back(llvm::PointerType::getUnqual(*context_)); // implicit self
        if (initDecl) {
            for (auto &param : initDecl->getParams()) {
                paramTypes.push_back(toLLVMType(param.type.get()));
            }
        }

        auto *voidTy = llvm::Type::getVoidTy(*context_);
        auto *funcType = llvm::FunctionType::get(voidTy, paramTypes, false);
        auto *func = llvm::Function::Create(
            funcType, llvm::Function::ExternalLinkage, initFieldsName, *module_);

        auto *entryBB = llvm::BasicBlock::Create(*context_, "entry", func);
        builder_->SetInsertPoint(entryBB);

        auto savedValues = namedValues_;
        namedValues_.clear();

        auto argIt = func->arg_begin();
        argIt->setName("self");
        auto *selfAlloca = createEntryBlockAlloca(
            func, "self", llvm::PointerType::getUnqual(*context_));
        builder_->CreateStore(&*argIt, selfAlloca);
        namedValues_["self"] = selfAlloca;
        varClassTypes_["self"] = className;
        ++argIt;

        if (initDecl) {
            for (auto &param : initDecl->getParams()) {
                if (argIt != func->arg_end()) {
                    argIt->setName(param.name);
                    auto *alloca = createEntryBlockAlloca(func, param.name, argIt->getType());
                    builder_->CreateStore(&*argIt, alloca);
                    namedValues_[param.name] = alloca;
                    ++argIt;
                }
            }
        }

        // Execute init body
        if (initDecl && initDecl->getBody()) {
            visit(const_cast<BlockStmt *>(initDecl->getBody()));
        }

        if (!builder_->GetInsertBlock()->getTerminator()) {
            builder_->CreateRetVoid();
        }

        namedValues_ = savedValues;
    }

    // Generate deinit: ClassName_deinit(self) → void
    {
        std::string deinitName = className + "_deinit";

        auto *ptrTy = llvm::PointerType::getUnqual(*context_);
        auto *voidTy = llvm::Type::getVoidTy(*context_);
        auto *funcType = llvm::FunctionType::get(voidTy, {ptrTy}, false);
        auto *func = llvm::Function::Create(
            funcType, llvm::Function::ExternalLinkage, deinitName, *module_);

        auto *entryBB = llvm::BasicBlock::Create(*context_, "entry", func);
        builder_->SetInsertPoint(entryBB);

        auto savedValues = namedValues_;
        namedValues_.clear();

        auto &selfArg = *func->arg_begin();
        selfArg.setName("self");
        auto *selfAlloca = createEntryBlockAlloca(func, "self", ptrTy);
        builder_->CreateStore(&selfArg, selfAlloca);
        namedValues_["self"] = selfAlloca;
        varClassTypes_["self"] = className;

        // User deinit body
        auto *deinitDecl = node->getDeinit();
        if (deinitDecl && deinitDecl->getBody()) {
            visit(const_cast<BlockStmt *>(deinitDecl->getBody()));
        }

        // Call parent deinit
        if (node->hasParentClass()) {
            std::string parentDeinit = node->getParentClass() + "_deinit";
            auto *parentFn = module_->getFunction(parentDeinit);
            if (parentFn) {
                auto *selfVal = builder_->CreateLoad(ptrTy, selfAlloca, "self_val");
                builder_->CreateCall(parentFn, {selfVal});
            }
        }

        // free(self)
        auto *freeFn = module_->getFunction("free");
        if (!freeFn) {
            auto *freeTy = llvm::FunctionType::get(
                voidTy, {ptrTy}, false);
            freeFn = llvm::Function::Create(
                freeTy, llvm::Function::ExternalLinkage, "free", *module_);
        }
        auto *selfVal = builder_->CreateLoad(ptrTy, selfAlloca, "self_for_free");
        builder_->CreateCall(freeFn, {selfVal});

        if (!builder_->GetInsertBlock()->getTerminator()) {
            builder_->CreateRetVoid();
        }

        namedValues_ = savedValues;
    }

    // Create vtable global
    {
        auto *ptrTy = llvm::PointerType::getUnqual(*context_);
        auto &methods = classVtableMethods_[className];
        std::vector<llvm::Constant *> vtableEntries;

        for (auto &methodName : methods) {
            // Look for method: own → parent chain
            std::string mangledName;
            // Check own class first
            auto *fn = module_->getFunction(className + "_" + methodName);
            if (fn) {
                mangledName = className + "_" + methodName;
            } else {
                // Walk parent chain
                std::string cur = className;
                while (!mangledName.empty() || cur == className) {
                    if (cur != className) {
                        fn = module_->getFunction(cur + "_" + methodName);
                        if (fn) {
                            mangledName = cur + "_" + methodName;
                            break;
                        }
                    }
                    auto pit = classParent_.find(cur);
                    if (pit != classParent_.end())
                        cur = pit->second;
                    else
                        break;
                }
                if (mangledName.empty()) {
                    // Search parent chain from parent
                    std::string pc = node->hasParentClass() ? node->getParentClass() : "";
                    while (!pc.empty()) {
                        fn = module_->getFunction(pc + "_" + methodName);
                        if (fn) {
                            mangledName = pc + "_" + methodName;
                            break;
                        }
                        auto pit2 = classParent_.find(pc);
                        pc = (pit2 != classParent_.end()) ? pit2->second : "";
                    }
                }
            }

            if (fn) {
                vtableEntries.push_back(fn);
            } else {
                vtableEntries.push_back(llvm::ConstantPointerNull::get(ptrTy));
            }
        }

        auto *vtableArrTy = llvm::ArrayType::get(ptrTy, vtableEntries.size());
        auto *vtableInit = llvm::ConstantArray::get(vtableArrTy, vtableEntries);
        auto *vtableGlobal = new llvm::GlobalVariable(
            *module_, vtableArrTy, true, llvm::GlobalValue::LinkOnceODRLinkage,
            vtableInit, "vtable_" + className);
        classVtables_[className] = vtableGlobal;

        // Fix up init function's vtable store
        auto *initFn = module_->getFunction(className + "_init");
        if (initFn) {
            // Walk through init function to find the null store to vtable_slot
            for (auto &BB : *initFn) {
                for (auto &I : BB) {
                    if (auto *store = llvm::dyn_cast<llvm::StoreInst>(&I)) {
                        if (auto *nullVal = llvm::dyn_cast<llvm::ConstantPointerNull>(store->getValueOperand())) {
                            if (auto *gep = llvm::dyn_cast<llvm::GetElementPtrInst>(store->getPointerOperand())) {
                                if (gep->getName() == "vtable_slot") {
                                    store->setOperand(0, vtableGlobal);
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    currentClassContext_ = prevClassCtx;
    return nullptr;
}

} // namespace liva

#endif // LIVA_HAS_LLVM
