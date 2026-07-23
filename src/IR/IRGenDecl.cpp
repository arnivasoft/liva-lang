#include "liva/IR/IRGen.h"

#ifdef LIVA_HAS_LLVM

namespace liva {

llvm::Value *IRGen::visitImportDecl(ImportDecl *node) {
    if (!moduleLoader_) {
        diag_.report(node->getStartLoc(), DiagID::err_irgen_import_no_loader);
        return nullptr;
    }

    std::string moduleName = node->getPathString();

    // Don't process the same module twice
    if (processedModules_.count(moduleName)) return nullptr;
    processedModules_.insert(moduleName);

    auto *mod = moduleLoader_->getLoadedModule(moduleName);
    if (!mod) {
        diag_.report(node->getStartLoc(), DiagID::err_irgen_module_load_failed, moduleName);
        return nullptr;
    }

    // Builtin modules (std::math, std::ui, etc.) have no AST —
    // their functions are declared via createRuntimeDecls()
    if (!mod->tu)
        return nullptr;

    // Class pre-pass for the imported module (phase 1), mirroring the main-TU
    // pre-pass in generate(): index every ClassDecl, then pre-declare each
    // (types + prototypes, no bodies) so classes in this module can
    // forward-reference each other (e.g. Window.setMenuBar(mb: MenuBar) where
    // MenuBar is declared later in the same module).
    if (!separateCompilation_) {
        // Enum pre-pass: register enum case/payload maps BEFORE class method
        // prototypes are built, so a method param typed by an enum declared in
        // this same module (e.g. Control.setAlign(a: Align)) lowers correctly
        // (simple enum → i32) regardless of declaration order.
        for (auto &decl : mod->tu->getDeclarations()) {
            if (decl->getKind() == ASTNode::NodeKind::EnumDecl) {
                auto *enumDecl = static_cast<EnumDecl *>(decl.get());
                if (preDeclaredEnums_.insert(enumDecl->getName()).second)
                    visitEnumDecl(enumDecl);
            }
        }
        for (auto &decl : mod->tu->getDeclarations()) {
            if (decl->getKind() == ASTNode::NodeKind::ClassDecl) {
                auto *classDecl = static_cast<ClassDecl *>(decl.get());
                classDecls_[classDecl->getName()] = classDecl;
            }
        }
        for (auto &decl : mod->tu->getDeclarations()) {
            if (decl->getKind() == ASTNode::NodeKind::ClassDecl)
                preDeclareClass(static_cast<ClassDecl *>(decl.get()));
        }
    }

    // Process all declarations from the imported module
    for (auto &decl : mod->tu->getDeclarations()) {
        if (decl->getKind() == ASTNode::NodeKind::FuncDecl) {
            auto *funcDecl = static_cast<FuncDecl *>(decl.get());
            if (funcDecl->isGeneric()) {
                genericFuncDecls_[funcDecl->getName()] = funcDecl;
                continue;
            }
            if (separateCompilation_) {
                // Skip non-pub functions — they have internal linkage
                if (funcDecl->isPublic())
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

        // Enums already registered in the enum pre-pass above — don't re-emit
        // (would recreate the named tagged-union struct type).
        if (decl->getKind() == ASTNode::NodeKind::EnumDecl) {
            auto *enumDecl = static_cast<EnumDecl *>(decl.get());
            if (preDeclaredEnums_.count(enumDecl->getName()))
                continue;
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
        } else if (param.type && param.type->getKind() == TypeRepr::Kind::Array) {
            auto *arrTy = static_cast<const ArrayTypeRepr *>(param.type.get());
            if (arrTy->isDynamic()) {
                paramTypes.push_back(getDynArrayStructTy());
            } else {
                paramTypes.push_back(toLLVMType(param.type.get()));
            }
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

    // Dynamic arrays are passed/returned as DynArray struct values, not ptr.
    if (funcDecl->getReturnType() &&
        funcDecl->getReturnType()->getKind() == TypeRepr::Kind::Array) {
        auto *arrRet = static_cast<const ArrayTypeRepr *>(funcDecl->getReturnType());
        if (arrRet->isDynamic())
            returnType = getDynArrayStructTy();
    }

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

    // Register function-typed fields for closure dispatch
    for (size_t i = 0; i < structDecl->getFields().size(); ++i) {
        auto *typeRepr = structDecl->getFields()[i]->getType();
        if (typeRepr && typeRepr->getKind() == TypeRepr::Kind::Function) {
            auto *ftr = static_cast<const FunctionTypeRepr *>(typeRepr);
            std::vector<llvm::Type *> fParams;
            fParams.push_back(llvm::PointerType::getUnqual(*context_)); // hidden env
            for (auto &p : ftr->getParams())
                fParams.push_back(toLLVMType(p.get()));
            auto *fRetTy = ftr->getReturnType()
                ? toLLVMType(ftr->getReturnType()) : builder_->getVoidTy();
            structFieldFuncTypes_[structDecl->getName()][structDecl->getFields()[i]->getName()] =
                llvm::FunctionType::get(fRetTy, fParams, false);
        }
    }
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
        // Dynamic arrays: pass/return as DynArray struct value, not ptr.
        if (method->getReturnType() &&
            method->getReturnType()->getKind() == TypeRepr::Kind::Array) {
            auto *arrRet = static_cast<const ArrayTypeRepr *>(method->getReturnType());
            if (arrRet->isDynamic())
                returnType = getDynArrayStructTy();
        }
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
        } else if (param.type && param.type->getKind() == TypeRepr::Kind::Array) {
            auto *arrTy = static_cast<const ArrayTypeRepr *>(param.type.get());
            if (arrTy->isDynamic()) {
                paramTypes.push_back(getDynArrayStructTy());
            } else {
                paramTypes.push_back(toLLVMType(param.type.get()));
            }
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

    // Dynamic arrays are passed/returned as DynArray struct values, not ptr.
    if (node->getReturnType() &&
        node->getReturnType()->getKind() == TypeRepr::Kind::Array) {
        auto *arrRet = static_cast<const ArrayTypeRepr *>(node->getReturnType());
        if (arrRet->isDynamic())
            returnType = getDynArrayStructTy();
    }

    // Async functions: return ptr (LivaTask*) instead of {i1, T}
    llvm::Type *asyncInnerRetType = nullptr;
    bool isAsyncMain = false;
    if (node->isAsync() || node->isGenerator()) {
        asyncFuncNames_.insert(node->getName());
        asyncInnerRetType = returnType;
        if (returnType->isVoidTy()) {
            // Generator functions auto-detected from yield don't carry a
            // declared return type — default the yielded value to i32 (Liva's
            // default integer width) so the promise slot matches the value
            // pushed by `yield expr`.
            asyncInnerRetType = node->isGenerator()
                ? (llvm::Type *)builder_->getInt32Ty()
                : (llvm::Type *)builder_->getInt1Ty();
        }
        if (node->isGenerator()) {
            generatorFuncs_[node->getName()] = asyncInnerRetType;
        }
        returnType = ptrTy;  // Phase 2: returns LivaTask* (or generator handle)
        if (node->getName() == "main" && node->isAsync()) {
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

    auto *funcType = llvm::FunctionType::get(returnType, paramTypes, node->isCVarargs());
    // Non-pub functions get internal linkage in separate compilation mode
    // to avoid duplicate symbol errors across translation units
    auto linkage = (separateCompilation_ && !node->isPublic() && !isMain)
        ? llvm::Function::InternalLinkage
        : llvm::Function::ExternalLinkage;
    auto *func = llvm::Function::Create(
        funcType, linkage, funcName, *module_);

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
        auto *funcDbgType = createFunctionDebugType(node);
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
    auto oldNamedValues = vars_.namedValues;
    auto oldVarStructTypes = vars_.varStructTypes;
    auto oldVarEnumTypes = vars_.varEnumTypes;
    auto oldVarArrayTypes = vars_.varArrayTypes;
    auto oldVarDynArrayTypes = vars_.varDynArrayTypes;
    auto oldVarDynArrayProtocol = vars_.varDynArrayProtocol;
    auto oldVarMapTypes = vars_.varMapTypes;
    auto oldVarSetTypes = vars_.varSetTypes;
    auto oldVarOptionalTypes = vars_.varOptionalTypes;
    auto oldVarFuncTypes = vars_.varFuncTypes;
    auto oldVarProtocolTypes = vars_.varProtocolTypes;
    auto oldVarConcreteProtocolTypes = vars_.varConcreteProtocolTypes;
    auto oldVarResultTypes = vars_.varResultTypes;
    auto oldVarRefTypes = vars_.varRefTypes;
    auto oldVarFileTypes = vars_.varFileTypes;
    auto oldVarFileOptionalTypes = vars_.varFileOptionalTypes;
    auto oldVarTupleTypes = vars_.varTupleTypes;
    auto oldMovedVars = vars_.movedVars;
    auto oldHeapStringVars = vars_.heapStringVars;
    auto oldHeapOptionalStringVars = vars_.heapOptionalStringVars;
    auto oldTempStrings = vars_.tempStrings;
    auto *oldFuncResultInfo = vars_.currentFuncResultInfo;
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

    currentIsAsync_ = node->isAsync() || node->isGenerator();
    asyncDeclaredRetType_ = currentIsAsync_ ? asyncInnerRetType : nullptr;
    currentCoroTask_ = nullptr;
    currentCoroHandle_ = nullptr;
    currentCoroId_ = nullptr;
    currentCoroPromise_ = nullptr;
    currentCoroFinalBB_ = nullptr;
    currentCoroCleanupBB_ = nullptr;
    currentCoroSuspendBB_ = nullptr;
    vars_.namedValues.clear();
    vars_.varStructTypes.clear();
    vars_.varEnumTypes.clear();
    vars_.varArrayTypes.clear();
    vars_.varDynArrayTypes.clear();
    vars_.varDynArrayProtocol.clear();
    vars_.varMapTypes.clear();
    vars_.varSetTypes.clear();
    vars_.varOptionalTypes.clear();
    vars_.varFuncTypes.clear();
    vars_.varProtocolTypes.clear();
    vars_.varConcreteProtocolTypes.clear();
    vars_.varResultTypes.clear();
    vars_.varRefTypes.clear();
    vars_.varFileTypes.clear();
    vars_.varFileOptionalTypes.clear();
    vars_.varTupleTypes.clear();
    vars_.movedVars.clear();
    vars_.heapStringVars.clear();
    vars_.heapOptionalStringVars.clear();
    vars_.tempStrings.clear();
    vars_.currentFuncResultInfo = nullptr;
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
        vars_.currentFuncResultInfo = &currentFuncResultInfoStorage_;
    }

    // === Phase 2 Coroutine Ramp Setup (for async/generator functions) ===
    //
    // INVARIANT: async and generators share an EAGER ramp.
    // The body runs immediately on call, up to the first suspend point
    // (first `yield` for generators, first `await` for async). For generators,
    // this produces semantically lazy iteration "by accident" — the first
    // `yield` in the body suspends with the first value already in the promise,
    // so the for-in loop can read coro.promise without an initial coro.resume.
    //
    // DO NOT add a "lazy initial suspend" here without ALSO updating the
    // for-in dispatch in IRGenStmt.cpp::visitForStmt to issue an initial
    // coro.resume before the first coro.promise read. Touching either site
    // alone will break every generator runtime test.
    //
    // See: PresplitCoroutine attribute below (CoroSplit gating);
    //      IRGenStmt.cpp::visitForStmt (the consumer of this ramp);
    //      docs/superpowers/plans/2026-05-02-generator-yield-runtime.md (Task 4
    //      diagnosis + Task 6 outcome that established this invariant).
    if (node->isAsync() || node->isGenerator()) {
        // Mark this function as a presplit coroutine so the CoroSplit pass
        // will identify and split it. Without this attribute,
        // Function::isPresplitCoroutine() returns false in CoroSplit and our
        // coroutine intrinsics (coro.suspend, coro.end, coro.promise) survive
        // into codegen, where DAG ISel cannot lower them and aborts with
        // "Do not know how to promote this operator's operand!".
        func->addFnAttr(llvm::Attribute::PresplitCoroutine);

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
            vars_.namedValues[std::string(arg.getName())] = alloca;
            // Register struct/enum-typed parameters for member access
            unsigned argIdx = arg.getArgNo();
            if (argIdx < node->getParams().size()) {
                auto &pd = node->getParams()[argIdx];
                const TypeRepr *pt = pd.type.get();
                if (pt && pt->getKind() == TypeRepr::Kind::Reference)
                    pt = static_cast<const ReferenceTypeRepr *>(pt)->getInner();
                if (pt && pt->getKind() == TypeRepr::Kind::Named) {
                    auto *named = static_cast<const NamedTypeRepr *>(pt);
                    if (structTypes_.count(named->getName()))
                        vars_.varStructTypes[pd.name] = named->getName();
                    else if (classTypes_.count(named->getName()))
                        vars_.varClassTypes[pd.name] = named->getName();
                    else if (enumTypes_.count(named->getName()))
                        vars_.varEnumTypes[pd.name] = named->getName();
                }
            }
            if (diBuilder_) {
                unsigned argIdx2 = arg.getArgNo();
                if (argIdx2 < node->getParams().size()) {
                    auto &pd = node->getParams()[argIdx2];
                    auto *diTy = toDIType(pd.type.get());
                    emitParamDebugInfo(pd.name, argIdx2 + 1, alloca, diTy, pd.location);
                }
            }
        }
    }

    // Register variadic params as DynArray variables
    for (auto &param : node->getParams()) {
        if (param.isVariadic && param.type) {
            auto *elemType = toLLVMType(param.type.get());
            auto &DL = module_->getDataLayout();
            uint64_t elemSize = DL.getTypeAllocSize(elemType);
            vars_.varDynArrayTypes[param.name] = {elemType, elemSize};
        }
    }

    // Register [T] (DynArray) typed parameters
    for (auto &param : node->getParams()) {
        if (!param.isVariadic && param.type &&
            param.type->getKind() == TypeRepr::Kind::Array) {
            auto *arrType = static_cast<const ArrayTypeRepr *>(param.type.get());
            if (arrType->isDynamic()) {
                auto *elemType = toLLVMType(arrType->getElement());
                auto &DL = module_->getDataLayout();
                uint64_t elemSize = DL.getTypeAllocSize(elemType);
                vars_.varDynArrayTypes[param.name] = {elemType, elemSize};
                // Params are borrowed: the caller owns the backing buffer.
                // Skip them in emitScopeCleanup to avoid double-free.
                vars_.movedVars.insert(param.name);
                // Track dyn Protocol element type
                if (arrType->getElement() &&
                    arrType->getElement()->getKind() == TypeRepr::Kind::DynProtocol) {
                    auto *dynP = static_cast<const DynProtocolTypeRepr *>(arrType->getElement());
                    vars_.varDynArrayProtocol[param.name] = dynP->getProtocolName();
                }
            }
        }
    }

    // Populate vars_.varRefTypes for ref parameters
    for (auto &param : node->getParams()) {
        if (param.isRef && param.type) {
            llvm::Type *innerTy = toLLVMType(param.type.get());
            if (param.type->getKind() == TypeRepr::Kind::Reference) {
                auto *refTy = static_cast<const ReferenceTypeRepr *>(param.type.get());
                innerTy = toLLVMType(refTy->getInner());
            }
            vars_.varRefTypes[param.name] = innerTy;
        }
    }

    // Populate vars_.varFuncTypes for function-typed parameters
    for (auto &param : node->getParams()) {
        if (param.type && param.type->getKind() == TypeRepr::Kind::Function) {
            auto *ftr = static_cast<const FunctionTypeRepr *>(param.type.get());
            std::vector<llvm::Type *> fParamTypes;
            fParamTypes.push_back(llvm::PointerType::getUnqual(*context_)); // hidden env
            for (auto &p : ftr->getParams())
                fParamTypes.push_back(toLLVMType(p.get()));
            llvm::Type *fRetTy = ftr->getReturnType()
                ? toLLVMType(ftr->getReturnType()) : builder_->getVoidTy();
            vars_.varFuncTypes[param.name] = llvm::FunctionType::get(fRetTy, fParamTypes, false);
        }
    }

    // Populate vars_.varProtocolTypes for dyn Protocol parameters
    for (auto &param : node->getParams()) {
        if (param.type && param.type->getKind() == TypeRepr::Kind::DynProtocol) {
            auto *dynType = static_cast<const DynProtocolTypeRepr *>(param.type.get());
            vars_.varProtocolTypes[param.name] = dynType->getProtocolName();
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
    if ((node->isAsync() || node->isGenerator()) && currentCoroFinalBB_) {
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
    vars_.namedValues = oldNamedValues;
    vars_.varStructTypes = oldVarStructTypes;
    vars_.varEnumTypes = oldVarEnumTypes;
    vars_.varArrayTypes = oldVarArrayTypes;
    vars_.varDynArrayTypes = oldVarDynArrayTypes;
    vars_.varDynArrayProtocol = oldVarDynArrayProtocol;
    vars_.varMapTypes = oldVarMapTypes;
    vars_.varSetTypes = oldVarSetTypes;
    vars_.varOptionalTypes = oldVarOptionalTypes;
    vars_.varFuncTypes = oldVarFuncTypes;
    vars_.varProtocolTypes = oldVarProtocolTypes;
    vars_.varConcreteProtocolTypes = oldVarConcreteProtocolTypes;
    vars_.varResultTypes = oldVarResultTypes;
    vars_.varRefTypes = oldVarRefTypes;
    vars_.varFileTypes = oldVarFileTypes;
    vars_.varFileOptionalTypes = oldVarFileOptionalTypes;
    vars_.varTupleTypes = oldVarTupleTypes;
    vars_.movedVars = oldMovedVars;
    vars_.heapStringVars = oldHeapStringVars;
    vars_.heapOptionalStringVars = oldHeapOptionalStringVars;
    vars_.tempStrings = oldTempStrings;
    vars_.currentFuncResultInfo = oldFuncResultInfo;
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
        // Attach debug info subprogram to async main wrapper
        if (diBuilder_) {
            auto *funcDbgType = createFunctionDebugType();
            unsigned lineNo = node->getStartLoc().isValid() ? node->getStartLoc().line : 0;
            auto *sp = diBuilder_->createFunction(
                diFile_, "main", "main", diFile_, lineNo,
                funcDbgType, lineNo,
                llvm::DINode::FlagPrototyped,
                llvm::DISubprogram::SPFlagDefinition);
            mainFunc->setSubprogram(sp);
        }
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
        if (!node->hasInit()) {
            diag_.report(node->getStartLoc(), DiagID::err_irgen_tuple_type_failed);
            return nullptr;
        }
        auto *initVal = visit(const_cast<Expr *>(node->getInit()));
        if (!initVal) return nullptr;

        // Get tuple element types from init's resolved type
        auto *initType = node->getInit()->getResolvedType();
        if (!initType || initType->getKind() != TypeRepr::Kind::Tuple) {
            diag_.report(node->getStartLoc(), DiagID::err_irgen_tuple_type_failed);
            return nullptr;
        }

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
            vars_.namedValues[node->getDestructuredNames()[i]] = elemAlloca;
        }
        return tupleAlloca;
    }

    // Redeclaration hygiene: vars_.varOptionalTypes/varStructTypes/movedVars/
    // heapOptionalStringVars/varFileOptionalTypes are name-keyed and
    // function-flat (no lexical-scope save/restore for plain `if`/loop
    // blocks — only closures/monomorphization/if-let/while-let snapshot
    // vars_ via VarStateGuard or manual saves). If a name is shadowed by a
    // LATER VarDecl of a DIFFERENT shape (e.g. `if true { let x: Res? = ...
    // }` followed by a sibling `let x = Res{...}`), the earlier
    // declaration's per-name classification entries would otherwise survive
    // untouched — since the new declaration's branch only writes the maps
    // ITS OWN shape needs, any leftover entry from the old shape is stale
    // and can cause type confusion at scope-cleanup time (e.g. treating a
    // plain struct alloca as an Optional wrapper, GEP'ing past its bounds).
    // Clear every per-name classification entry for this name up front so
    // each branch below starts from a clean slate and only re-establishes
    // what its own shape actually needs.
    {
        const std::string &declName = node->getName();
        vars_.varOptionalTypes.erase(declName);
        vars_.varStructTypes.erase(declName);
        vars_.movedVars.erase(declName);
        vars_.heapOptionalStringVars.erase(declName);
        vars_.varFileOptionalTypes.erase(declName);
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
                auto stIt = vars_.varStructTypes.find(ident->getName());
                if (stIt != vars_.varStructTypes.end()) {
                    concreteType = stIt->second;
                    auto nvIt = vars_.namedValues.find(ident->getName());
                    if (nvIt != vars_.namedValues.end())
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

                vars_.namedValues[node->getName()] = alloca;
                vars_.varProtocolTypes[node->getName()] = protocolName;
                if (!node->isMutable()) {
                    vars_.varConcreteProtocolTypes[node->getName()] = concreteType;
                }
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
                    auto stIt = vars_.varStructTypes.find(ident->getName());
                    if (stIt != vars_.varStructTypes.end()) {
                        concreteType = stIt->second;
                        auto nvIt = vars_.namedValues.find(ident->getName());
                        if (nvIt != vars_.namedValues.end())
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

                    vars_.namedValues[node->getName()] = alloca;
                    vars_.varProtocolTypes[node->getName()] = protocolName;
                    if (!node->isMutable()) {
                        vars_.varConcreteProtocolTypes[node->getName()] = concreteType;
                    }
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
            // Temporarily set vars_.currentFuncResultInfo so emitResultOk/Err can be used from visitCallExpr
            ResultInfo ri = {okLLVM, errLLVM};
            auto *oldRI = vars_.currentFuncResultInfo;
            vars_.currentFuncResultInfo = &ri;
            auto *initVal = visit(const_cast<Expr *>(node->getInit()));
            vars_.currentFuncResultInfo = oldRI;
            if (initVal) builder_->CreateStore(initVal, alloca);
        }

        vars_.namedValues[node->getName()] = alloca;
        vars_.varResultTypes[node->getName()] = {okLLVM, errLLVM};
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
                    vars_.namedValues[node->getName()] = alloca;
                    vars_.varOptionalTypes[node->getName()] = ptrTy;
                    vars_.varFileOptionalTypes.insert(node->getName());
                    return alloca;
                }
            }
        }
    }

    // Optional variable: let x: i32? = 42 / let x: i32? = nil
    if (node->hasTypeAnnotation() && node->getType() &&
        node->getType()->getKind() == TypeRepr::Kind::Optional) {
        auto *optTypeRepr = static_cast<const OptionalTypeRepr *>(node->getType());
        // Special-case: Optional<[T]> stores the DynArray struct value
        // inline, not a ptr (toLLVMType maps Array→ptr by default).
        llvm::Type *innerLLVM = nullptr;
        if (optTypeRepr->getInner() &&
            optTypeRepr->getInner()->getKind() == TypeRepr::Kind::Array) {
            auto *arrInner = static_cast<const ArrayTypeRepr *>(optTypeRepr->getInner());
            if (arrInner->isDynamic())
                innerLLVM = getDynArrayStructTy();
        }
        if (!innerLLVM) innerLLVM = toLLVMType(optTypeRepr->getInner());
        auto *optStructTy = getOptionalType(innerLLVM);
        auto *alloca = createEntryBlockAlloca(func, node->getName(), optStructTy);
        auto *hasValPtr = builder_->CreateStructGEP(optStructTy, alloca, 0, "opt.hasval");
        auto *valPtr = builder_->CreateStructGEP(optStructTy, alloca, 1, "opt.val");

        if (node->hasInit() &&
            node->getInit()->getKind() == ASTNode::NodeKind::NilLiteralExpr) {
            builder_->CreateStore(builder_->getFalse(), hasValPtr);
            builder_->CreateStore(llvm::Constant::getNullValue(innerLLVM), valPtr);
        } else if (node->hasInit()) {
            // Snapshot vars_.tempStrings to detect inner-string ownership transfer
            // for Optional<string>-returning builtins (hexDecode, tomlGetString,
            // base64UrlDecode, isoParse, ...). Their codegen calls
            // trackStringTemp on the inner ptr; without ownership transfer the
            // raw ptr is freed at the end of this statement, leaving the var
            // holding a dangling pointer.
            size_t tempsBeforeInit = vars_.tempStrings.size();
            auto *initVal = visit(const_cast<Expr *>(node->getInit()));
            if (initVal && initVal->getType() == optStructTy) {
                // RHS is already an Optional<T> matching the declared type;
                // store it as a single value rather than wrapping in Some(...)
                builder_->CreateStore(initVal, alloca);
                if (innerLLVM->isPointerTy() &&
                    vars_.tempStrings.size() > tempsBeforeInit) {
                    // Transfer ownership: drop the wrapping call's temp and
                    // mark this variable for conditional cleanup.
                    vars_.tempStrings.pop_back();
                    vars_.heapOptionalStringVars.insert(node->getName());
                }
            } else {
                // RHS is a plain T value → wrap as Some(value)
                builder_->CreateStore(builder_->getTrue(), hasValPtr);
                if (initVal) builder_->CreateStore(initVal, valPtr);
                if (innerLLVM->isPointerTy() && initVal &&
                    vars_.tempStrings.size() > tempsBeforeInit) {
                    vars_.tempStrings.pop_back();
                    vars_.heapOptionalStringVars.insert(node->getName());
                }
            }
        } else {
            builder_->CreateStore(builder_->getFalse(), hasValPtr);
            builder_->CreateStore(llvm::Constant::getNullValue(innerLLVM), valPtr);
        }
        vars_.namedValues[node->getName()] = alloca;
        vars_.varOptionalTypes[node->getName()] = innerLLVM;
        if (diBuilder_) {
            auto *diTy = toDIType(optTypeRepr->getInner());
            emitLocalVarDebugInfo(node->getName(), alloca, diTy, node->getStartLoc());
        }
        // Register struct type name for optional chaining support
        if (optTypeRepr->getInner()->getKind() == TypeRepr::Kind::Named) {
            auto *namedInner = static_cast<const NamedTypeRepr *>(optTypeRepr->getInner());
            vars_.varStructTypes[node->getName()] = namedInner->getName();
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

            // Resolve type args (priority: explicit turbofish, surrounding
            // monomorphization context, inference from field values).
            const auto &typeParams = gsIt->second->getTypeParams();
            std::vector<const TypeRepr *> typeArgs;
            if (!structLit->getTypeArgs().empty()) {
                for (auto &ta : structLit->getTypeArgs())
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
                typeArgs = inferStructTypeArgs(gsIt->second, structLit->getFields(), fieldValues);
            }

            // Monomorphize the struct
            monomorphizeStruct(gsIt->second, typeArgs);
            std::string mangledName = mangleGenericStruct(typeName, typeArgs);

            auto *structTy = structTypes_[mangledName];
            auto *alloca = createEntryBlockAlloca(func, node->getName(), structTy);
            // Zero-initialize so unset fields (empty arrays, etc.) are safe to cleanup
            builder_->CreateStore(llvm::Constant::getNullValue(structTy), alloca);

            for (size_t i = 0; i < structLit->getFields().size(); ++i) {
                int idx = getStructFieldIndex(mangledName, structLit->getFields()[i].name);
                if (idx < 0 || !fieldValues[i])
                    continue;
                auto *val = dupIfStringField(mangledName, idx, fieldValues[i]);
                val = cloneIfDynArrayField(mangledName, idx, val,
                                           structLit->getFields()[i].name);
                auto *gep = builder_->CreateStructGEP(structTy, alloca, idx,
                                                       structLit->getFields()[i].name);
                builder_->CreateStore(val, gep);
            }

            vars_.namedValues[node->getName()] = alloca;
            vars_.varStructTypes[node->getName()] = mangledName;
            return alloca;
        }

        auto stIt = structTypes_.find(typeName);
        if (stIt != structTypes_.end()) {
            auto *structTy = stIt->second;
            auto *alloca = createEntryBlockAlloca(func, node->getName(), structTy);
            // Zero-initialize so unset fields (empty arrays, etc.) are safe to cleanup
            builder_->CreateStore(llvm::Constant::getNullValue(structTy), alloca);

            // Store each field
            for (auto &fieldInit : structLit->getFields()) {
                int idx = getStructFieldIndex(typeName, fieldInit.name);
                if (idx < 0)
                    continue;
                auto *val = visit(fieldInit.value.get());
                if (!val)
                    continue;
                // Dup string fields to ensure ownership for safe cleanup
                val = dupIfStringField(typeName, idx, val);
                val = cloneIfDynArrayField(typeName, idx, val, fieldInit.name);
                auto *gep = builder_->CreateStructGEP(structTy, alloca, idx,
                                                       fieldInit.name);
                builder_->CreateStore(val, gep);
            }

            vars_.namedValues[node->getName()] = alloca;
            vars_.varStructTypes[node->getName()] = typeName;
            if (diBuilder_) {
                auto *diTy = getOrCreateStructDIType(typeName);
                emitLocalVarDebugInfo(node->getName(), alloca, diTy, node->getStartLoc());
            }
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
                    vars_.namedValues[node->getName()] = alloca;
                    vars_.varClassTypes[node->getName()] = ident->getName();
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
                    auto *initVal = visit(const_cast<Expr *>(node->getInit()));
                    if (initVal) {
                        // Use the call's *actual* return type — failable
                        // factories like `Foo.tryNew(...) -> Foo?` return
                        // Optional<Foo>, not Foo. Allocating a plain Foo slot
                        // would silently truncate the Optional discriminant.
                        auto *resultTy = initVal->getType();
                        auto *alloca = createEntryBlockAlloca(func, node->getName(), resultTy);
                        builder_->CreateStore(initVal, alloca);
                        vars_.namedValues[node->getName()] = alloca;
                        if (resultTy->isStructTy()) {
                            auto *st = llvm::cast<llvm::StructType>(resultTy);
                            if (st->getNumElements() == 2 &&
                                st->getElementType(0)->isIntegerTy(1)) {
                                // Optional<T>: register inner type for if-let
                                vars_.varOptionalTypes[node->getName()] = st->getElementType(1);
                                if (st->getElementType(1) == stIt->second) {
                                    // Optional chaining still wants the
                                    // underlying struct name available.
                                    vars_.varStructTypes[node->getName()] = ident->getName();
                                }
                            } else {
                                // The method may return a DIFFERENT struct type than the
                                // receiver (e.g. Json::parse -> JsonValue, not Json).
                                // Use the LLVM struct type's name when it differs.
                                std::string structName = ident->getName();
                                if (!st->getName().empty() &&
                                    st->getName() != ident->getName()) {
                                    structName = st->getName().str();
                                }
                                vars_.varStructTypes[node->getName()] = structName;
                            }
                        } else {
                            vars_.varStructTypes[node->getName()] = ident->getName();
                        }
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
                    vars_.namedValues[node->getName()] = alloca;
                    vars_.varEnumTypes[node->getName()] = ident->getName();
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
                        vars_.namedValues[node->getName()] = alloca;
                        vars_.varEnumTypes[node->getName()] = ident->getName();
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
                vars_.namedValues[node->getName()] = alloca;
                vars_.varEnumTypes[node->getName()] = ident->getName();
                return alloca;
            }
        }
    }

    // Dynamic array: var arr: [i32] = [1, 2, 3] or var arr: [i32] = []
    // (only handles array-literal initializers; for `let arr: [T] = call()`
    // we fall through to the inferred-init DynArray branch below so the
    // call's struct value is actually stored — otherwise this branch silently
    // discards the init and allocates an empty array.)
    bool initIsArrayLit = node->hasInit() &&
        node->getInit()->getKind() == ASTNode::NodeKind::ArrayLiteralExpr;
    if (node->hasTypeAnnotation() && node->getType() &&
        node->getType()->getKind() == TypeRepr::Kind::Array &&
        (!node->hasInit() || initIsArrayLit)) {
        auto *arrTypeRepr = static_cast<const ArrayTypeRepr *>(node->getType());
        if (arrTypeRepr->isDynamic()) {
            auto *elemType = toLLVMType(arrTypeRepr->getElement());
            const llvm::DataLayout &dl = module_->getDataLayout();
            uint64_t elemSize = dl.getTypeAllocSize(elemType);
            auto *structTy = getDynArrayStructTy();
            auto *alloca = createEntryBlockAlloca(func, node->getName(), structTy);

            // Check if element type is dyn Protocol (for boxing)
            bool isDynProtoElem = arrTypeRepr->getElement() &&
                arrTypeRepr->getElement()->getKind() == TypeRepr::Kind::DynProtocol;
            std::string protoName;
            if (isDynProtoElem) {
                auto *dp = static_cast<const DynProtocolTypeRepr *>(arrTypeRepr->getElement());
                protoName = dp->getProtocolName();
            }

            // Collect init elements
            std::vector<llvm::Value *> initVals;
            if (node->hasInit() &&
                node->getInit()->getKind() == ASTNode::NodeKind::ArrayLiteralExpr) {
                auto *arrayLit = static_cast<ArrayLiteralExpr *>(
                    const_cast<Expr *>(node->getInit()));
                for (auto &elem : arrayLit->getElements()) {
                    // Box elements as trait objects for [dyn Protocol] arrays
                    if (isDynProtoElem && elem->getKind() == ASTNode::NodeKind::IdentifierExpr) {
                        auto *ident = static_cast<IdentifierExpr *>(elem.get());
                        auto stIt = vars_.varStructTypes.find(ident->getName());
                        if (stIt != vars_.varStructTypes.end()) {
                            auto *traitTy = getTraitObjectTy();
                            auto *traitAlloca = createEntryBlockAlloca(func, "dyn.arr.box", traitTy);
                            auto *dataGEP = builder_->CreateStructGEP(traitTy, traitAlloca, 0, "dyn.data");
                            builder_->CreateStore(vars_.namedValues[ident->getName()], dataGEP);
                            auto *vtable = getOrCreateVtable(protoName, stIt->second);
                            auto *vtGEP = builder_->CreateStructGEP(traitTy, traitAlloca, 1, "dyn.vtable");
                            builder_->CreateStore(vtable, vtGEP);
                            auto *boxed = builder_->CreateLoad(traitTy, traitAlloca, "dyn.boxed");
                            initVals.push_back(boxed);
                            continue;
                        }
                    }
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

            vars_.namedValues[node->getName()] = alloca;
            vars_.varDynArrayTypes[node->getName()] = {elemType, elemSize};
            // Track dyn Protocol element type for for-in loop dispatch
            if (isDynProtoElem) {
                vars_.varDynArrayProtocol[node->getName()] = protoName;
            }
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

            vars_.namedValues[node->getName()] = alloca;
            vars_.varMapTypes[node->getName()] = {keyType, valType, keySize, valSize, keyKind};
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

            vars_.namedValues[node->getName()] = alloca;
            vars_.varSetTypes[node->getName()] = {elemType, elemSize, keyKind};
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
            vars_.namedValues[node->getName()] = alloca;
            vars_.varArrayTypes[node->getName()] = {elemType, numElements};
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
        vars_.namedValues[node->getName()] = alloca;
        vars_.varFuncTypes[node->getName()] = llvmFuncTy;
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
        vars_.namedValues[node->getName()] = alloca;
        TupleInfo ti;
        for (auto &e : tupleTypeRepr->getElements())
            ti.elementTypes.push_back(toLLVMType(e.get()));
        vars_.varTupleTypes[node->getName()] = ti;
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
        vars_.namedValues[node->getName()] = alloca;
        TupleInfo ti;
        for (auto &e : tupleTypeRepr->getElements())
            ti.elementTypes.push_back(toLLVMType(e.get()));
        vars_.varTupleTypes[node->getName()] = ti;
        return alloca;
    }

    // Init returns DynArray (map/filter/split, or `let p: [T] = call(...)`):
    // prefer the annotation when present (more precise element type), else
    // fall back to the call's resolved type.
    {
        const ArrayTypeRepr *arrReprType = nullptr;
        if (node->hasTypeAnnotation() && node->getType() &&
            node->getType()->getKind() == TypeRepr::Kind::Array) {
            arrReprType = static_cast<const ArrayTypeRepr *>(node->getType());
        } else if (node->hasInit() && node->getInit()->getResolvedType() &&
                   node->getInit()->getResolvedType()->getKind() == TypeRepr::Kind::Array) {
            arrReprType = static_cast<const ArrayTypeRepr *>(node->getInit()->getResolvedType());
        }
        if (arrReprType && arrReprType->isDynamic() && node->hasInit()) {
            auto *elemType = toLLVMType(arrReprType->getElement());
            const llvm::DataLayout &dl = module_->getDataLayout();
            uint64_t elemSize = dl.getTypeAllocSize(elemType);
            auto *structTy = getDynArrayStructTy();
            auto *alloca = createEntryBlockAlloca(func, node->getName(), structTy);
            auto *initVal = visit(const_cast<Expr *>(node->getInit()));
            if (initVal) builder_->CreateStore(initVal, alloca);
            vars_.namedValues[node->getName()] = alloca;
            vars_.varDynArrayTypes[node->getName()] = {elemType, elemSize};
            return alloca;
        }
    }

    // Fallback: visit init first to determine correct type for inferred vars
    if (node->hasInit()) {
        // Snapshot vars_.tempStrings before init so we can identify any temp strings
        // produced by the init expression — needed for Optional<string>
        // ownership transfer below.
        size_t tempsBeforeInit = vars_.tempStrings.size();
        auto *initVal = visit(const_cast<Expr *>(node->getInit()));
        auto *type = toLLVMType(node->getType());
        // Use init value's type when annotation is absent/inferred.
        // Covers two cases:
        //   1. Annotation says i32 but init has a richer type (struct copy, etc.)
        //   2. No annotation at all → toLLVMType(nullptr) returns voidTy, which
        //      is unusable as an alloca type — substitute the init value's type.
        if (initVal && !initVal->getType()->isVoidTy() &&
            (type == builder_->getInt32Ty() || type->isVoidTy()) &&
            initVal->getType() != builder_->getInt32Ty()) {
            type = initVal->getType();
        }
        auto *alloca = createEntryBlockAlloca(func, node->getName(), type);
        if (initVal)
            builder_->CreateStore(initVal, alloca);
        vars_.namedValues[node->getName()] = alloca;
        if (diBuilder_) {
            auto *diTy = toDIType(node->getType());
            emitLocalVarDebugInfo(node->getName(), alloca, diTy, node->getStartLoc());
        }

        // Track heap string ownership (explicit String type or inferred from string init)
        if (initVal && type->isPointerTy()) {
            bool isString = (node->getType() &&
                node->getType()->getKind() == TypeRepr::Kind::String);
            if (!isString && node->getInit() && node->getInit()->getResolvedType() &&
                node->getInit()->getResolvedType()->getKind() == TypeRepr::Kind::String) {
                isString = true;
            }
            if (isString) {
                // For mutable string vars, ensure the slot always holds a
                // heap-allocated pointer so reassignments can free the old
                // value uniformly. If init is a string literal (not tracked),
                // dup it; otherwise transfer the temp's ownership.
                auto tIt = std::find(vars_.tempStrings.begin(), vars_.tempStrings.end(), initVal);
                if (tIt != vars_.tempStrings.end()) {
                    transferStringOwnership(initVal, node->getName());
                } else if (node->isMutable()) {
                    // Literal or borrowed init — replace stored value with a
                    // heap copy and mark as heap-owned.
                    auto *dup = builder_->CreateCall(getOrPanic("liva_str_dup"),
                                                     {initVal}, node->getName() + ".own");
                    builder_->CreateStore(dup, alloca);
                    vars_.heapStringVars.insert(node->getName());
                }
            }
        }

        // Register Optional type for if-let support when type is inferred
        if (initVal && initVal->getType()->isStructTy()) {
            auto *structTy = llvm::cast<llvm::StructType>(initVal->getType());
            if (structTy->getNumElements() == 2 &&
                structTy->getElementType(0)->isIntegerTy(1)) {
                vars_.varOptionalTypes[node->getName()] = structTy->getElementType(1);

                // Optional<string> ownership transfer: builtins like hexDecode
                // and tomlGetString call trackStringTemp on their inner pointer
                // so the temp gets freed at the end of the producing statement.
                // For `let opt: string? = call()` that means the inner ptr is
                // freed before the variable is used → use-after-free.
                //
                // Detect the inner-ptr temp added during this init and hand
                // ownership to the variable; emitScopeCleanup will free it
                // conditionally (only when hasVal is true) at scope end.
                if (structTy->getElementType(1)->isPointerTy() &&
                    vars_.tempStrings.size() > tempsBeforeInit) {
                    // Take the LAST entry — that's the wrapping call's result.
                    // Earlier temps in the same statement are unrelated nested
                    // calls and continue along the normal cleanup path.
                    vars_.tempStrings.pop_back();
                    vars_.heapOptionalStringVars.insert(node->getName());
                }
            }
        }

        // Register struct/enum type from init's resolved type or LLVM type
        // Always update (no guard) — handles same-name vars in different scopes
        {
            bool registered = false;
            // Try Sema resolved type first
            if (node->getInit() && node->getInit()->getResolvedType()) {
                auto *resolvedType = node->getInit()->getResolvedType();
                if (resolvedType->getKind() == TypeRepr::Kind::Named) {
                    auto *namedRepr = static_cast<const NamedTypeRepr *>(resolvedType);
                    const std::string &typeName = namedRepr->getName();
                    if (structTypes_.count(typeName)) {
                        vars_.varStructTypes[node->getName()] = typeName;
                        registered = true;
                    } else if (enumTypes_.count(typeName)) {
                        vars_.varEnumTypes[node->getName()] = typeName;
                        registered = true;
                    }
                }
            }
            // Fallback: match LLVM type against known struct/enum types
            if (!registered && initVal) {
                auto *valTy = initVal->getType();
                for (auto &[name, ty] : structTypes_) {
                    if (ty == valTy) {
                        vars_.varStructTypes[node->getName()] = name;
                        registered = true;
                        break;
                    }
                }
                if (!registered) {
                    for (auto &[name, ty] : enumTypes_) {
                        if (ty == valTy) {
                            vars_.varEnumTypes[node->getName()] = name;
                            registered = true;
                            break;
                        }
                    }
                }
            }
            // Annotation-based fallback (let x: Color = ...)
            if (!registered && node->getType() &&
                node->getType()->getKind() == TypeRepr::Kind::Named) {
                auto *namedRepr = static_cast<const NamedTypeRepr *>(node->getType());
                const std::string &typeName = namedRepr->getName();
                if (structTypes_.count(typeName))
                    vars_.varStructTypes[node->getName()] = typeName;
                else if (enumTypes_.count(typeName))
                    vars_.varEnumTypes[node->getName()] = typeName;
            }
        }

        // Move semantics: `let b = a` where `a` is a Drop-conforming struct
        // moves `a` — mark the SOURCE identifier as moved so emitScopeCleanup
        // skips it (only `b` gets dropped, closing the double-drop). Scope is
        // conservative: only Drop-conforming structs are affected (guarded by
        // dropImplementors_), matching OwnershipChecker's isDropType gate.
        if (node->getInit() &&
            node->getInit()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
            auto *initIdent = static_cast<const IdentifierExpr *>(node->getInit());
            auto srcStIt = vars_.varStructTypes.find(initIdent->getName());
            if (srcStIt != vars_.varStructTypes.end() &&
                dropImplementors_.count(srcStIt->second)) {
                vars_.movedVars.insert(initIdent->getName());
            }
        }

        return alloca;
    }

    auto *type = toLLVMType(node->getType());
    auto *alloca = createEntryBlockAlloca(func, node->getName(), type);
    vars_.namedValues[node->getName()] = alloca;
    if (diBuilder_) {
        auto *diTy = toDIType(node->getType());
        emitLocalVarDebugInfo(node->getName(), alloca, diTy, node->getStartLoc());
    }
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

    // Register function-typed fields for closure dispatch
    for (size_t i = 0; i < node->getFields().size(); ++i) {
        auto *typeRepr = node->getFields()[i]->getType();
        if (typeRepr && typeRepr->getKind() == TypeRepr::Kind::Function) {
            auto *ftr = static_cast<const FunctionTypeRepr *>(typeRepr);
            std::vector<llvm::Type *> fParams;
            fParams.push_back(llvm::PointerType::getUnqual(*context_)); // hidden env
            for (auto &p : ftr->getParams())
                fParams.push_back(toLLVMType(p.get()));
            auto *fRetTy = ftr->getReturnType()
                ? toLLVMType(ftr->getReturnType()) : builder_->getVoidTy();
            structFieldFuncTypes_[node->getName()][node->getFields()[i]->getName()] =
                llvm::FunctionType::get(fRetTy, fParams, false);
        }
    }

    if (diBuilder_) {
        getOrCreateStructDIType(node->getName());
    }
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
                paramTypes.push_back(llvm::PointerType::getUnqual(*context_));
            } else if (param.type && param.type->getKind() == TypeRepr::Kind::Array) {
                auto *arrTy = static_cast<const ArrayTypeRepr *>(param.type.get());
                if (arrTy->isDynamic()) {
                    paramTypes.push_back(getDynArrayStructTy());
                } else {
                    paramTypes.push_back(toLLVMType(param.type.get()));
                }
            } else if (param.type && param.type->getKind() == TypeRepr::Kind::DynProtocol) {
                paramTypes.push_back(getTraitObjectTy());
            } else {
                paramTypes.push_back(toLLVMType(param.type.get()));
            }
        }

        auto *returnType = toLLVMType(method->getReturnType());
        // Dynamic arrays: pass/return as DynArray struct value, not ptr.
        if (method->getReturnType() &&
            method->getReturnType()->getKind() == TypeRepr::Kind::Array) {
            auto *arrRet = static_cast<const ArrayTypeRepr *>(method->getReturnType());
            if (arrRet->isDynamic())
                returnType = getDynArrayStructTy();
        }
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

        // Attach debug info subprogram to impl method
        if (diBuilder_) {
            auto *funcDbgType = createFunctionDebugType(method.get());
            unsigned lineNo = method->getStartLoc().isValid() ? method->getStartLoc().line : 0;
            auto *sp = diBuilder_->createFunction(
                diFile_, mangledName, mangledName, diFile_, lineNo,
                funcDbgType, lineNo,
                llvm::DINode::FlagPrototyped,
                llvm::DISubprogram::SPFlagDefinition);
            func->setSubprogram(sp);
        }

        auto *entryBB = llvm::BasicBlock::Create(*context_, "entry", func);
        builder_->SetInsertPoint(entryBB);

        if (diBuilder_) {
            emitDebugLocation(method->getStartLoc());
        }

        // Save and clear scope
        auto oldNamedValues = vars_.namedValues;
        auto oldVarStructTypes = vars_.varStructTypes;
        auto oldVarClassTypes = vars_.varClassTypes;
        auto oldVarEnumTypes = vars_.varEnumTypes;
        auto oldVarArrayTypes = vars_.varArrayTypes;
        auto oldVarDynArrayTypes = vars_.varDynArrayTypes;
        auto oldVarDynArrayProtocol = vars_.varDynArrayProtocol;
        auto oldVarMapTypes = vars_.varMapTypes;
        auto oldVarSetTypes = vars_.varSetTypes;
        auto oldVarOptionalTypes = vars_.varOptionalTypes;
        auto oldVarFuncTypes = vars_.varFuncTypes;
        auto oldVarProtocolTypes = vars_.varProtocolTypes;
        auto oldVarConcreteProtocolTypes = vars_.varConcreteProtocolTypes;
        auto oldVarResultTypes = vars_.varResultTypes;
        auto *oldFuncRI = vars_.currentFuncResultInfo;
        auto *oldFuncOptInner = currentFuncOptionalInner_;
        auto oldMovedVars = vars_.movedVars;
        auto oldHeapStringVars = vars_.heapStringVars;
        auto oldTempStrings = vars_.tempStrings;
        vars_.namedValues.clear();
        vars_.varStructTypes.clear();
        vars_.varClassTypes.clear();
        vars_.varEnumTypes.clear();
        vars_.varArrayTypes.clear();
        vars_.varDynArrayTypes.clear();
        vars_.varDynArrayProtocol.clear();
        vars_.varMapTypes.clear();
        vars_.varSetTypes.clear();
        vars_.varOptionalTypes.clear();
        vars_.varFuncTypes.clear();
        vars_.varProtocolTypes.clear();
        vars_.varConcreteProtocolTypes.clear();
        vars_.varResultTypes.clear();
        vars_.varFileTypes.clear();
        vars_.currentFuncResultInfo = nullptr;
        currentFuncOptionalInner_ = nullptr;
        vars_.movedVars.clear();
        vars_.heapStringVars.clear();
        vars_.tempStrings.clear();

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
            vars_.namedValues[std::string(arg.getName())] = alloca;
            if (method->getParams()[i].isSelf) {
                // Register self as struct, enum, or class type
                auto etIt = enumTypes_.find(typeName);
                if (etIt != enumTypes_.end()) {
                    vars_.varEnumTypes["self"] = typeName;
                } else if (classTypes_.find(typeName) != classTypes_.end()) {
                    vars_.varClassTypes["self"] = typeName;
                } else {
                    vars_.varStructTypes["self"] = typeName;
                }
            } else {
                // Register struct/enum-typed parameters for member access
                auto &pd = method->getParams()[i];
                const TypeRepr *pt = pd.type.get();
                if (pt && pt->getKind() == TypeRepr::Kind::Reference)
                    pt = static_cast<const ReferenceTypeRepr *>(pt)->getInner();
                if (pt && pt->getKind() == TypeRepr::Kind::Named) {
                    auto *named = static_cast<const NamedTypeRepr *>(pt);
                    if (structTypes_.count(named->getName()))
                        vars_.varStructTypes[pd.name] = named->getName();
                    else if (classTypes_.count(named->getName()))
                        vars_.varClassTypes[pd.name] = named->getName();
                    else if (enumTypes_.count(named->getName()))
                        vars_.varEnumTypes[pd.name] = named->getName();
                }
            }
            if (diBuilder_) {
                auto &pd = method->getParams()[i];
                auto *diTy = toDIType(pd.type.get());
                emitParamDebugInfo(pd.name, static_cast<unsigned>(i) + 1, alloca, diTy, pd.location);
            }
            ++i;
        }

        // Register [T] (DynArray) typed method parameters
        for (auto &param : method->getParams()) {
            if (!param.isSelf && !param.isVariadic && param.type &&
                param.type->getKind() == TypeRepr::Kind::Array) {
                auto *arrType = static_cast<const ArrayTypeRepr *>(param.type.get());
                if (arrType->isDynamic()) {
                    auto *elemType = toLLVMType(arrType->getElement());
                    auto &DL = module_->getDataLayout();
                    uint64_t elemSize = DL.getTypeAllocSize(elemType);
                    vars_.varDynArrayTypes[param.name] = {elemType, elemSize};
                    // Borrowed: skip in cleanup (caller owns the buffer).
                    vars_.movedVars.insert(param.name);
                    if (arrType->getElement() &&
                        arrType->getElement()->getKind() == TypeRepr::Kind::DynProtocol) {
                        auto *dynP = static_cast<const DynProtocolTypeRepr *>(arrType->getElement());
                        vars_.varDynArrayProtocol[param.name] = dynP->getProtocolName();
                    }
                }
            }
        }

        // Register dyn Protocol typed method parameters
        for (auto &param : method->getParams()) {
            if (!param.isSelf && param.type &&
                param.type->getKind() == TypeRepr::Kind::DynProtocol) {
                auto *dynType = static_cast<const DynProtocolTypeRepr *>(param.type.get());
                vars_.varProtocolTypes[param.name] = dynType->getProtocolName();
            }
        }

        // Register function-typed method parameters in vars_.varFuncTypes so that
        // calls like `fn(x)` inside the method body resolve as indirect calls
        // through the closure object rather than as undefined function names.
        for (auto &param : method->getParams()) {
            if (!param.isSelf && param.type &&
                param.type->getKind() == TypeRepr::Kind::Function) {
                auto *ftr = static_cast<const FunctionTypeRepr *>(param.type.get());
                std::vector<llvm::Type *> fParamTypes;
                fParamTypes.push_back(llvm::PointerType::getUnqual(*context_)); // hidden env
                for (auto &p : ftr->getParams())
                    fParamTypes.push_back(toLLVMType(p.get()));
                llvm::Type *fRetTy = ftr->getReturnType()
                    ? toLLVMType(ftr->getReturnType()) : builder_->getVoidTy();
                vars_.varFuncTypes[param.name] =
                    llvm::FunctionType::get(fRetTy, fParamTypes, false);
            }
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
        vars_.namedValues = oldNamedValues;
        vars_.varStructTypes = oldVarStructTypes;
        vars_.varClassTypes = oldVarClassTypes;
        vars_.varEnumTypes = oldVarEnumTypes;
        vars_.varArrayTypes = oldVarArrayTypes;
        vars_.varDynArrayTypes = oldVarDynArrayTypes;
        vars_.varDynArrayProtocol = oldVarDynArrayProtocol;
        vars_.varMapTypes = oldVarMapTypes;
        vars_.varSetTypes = oldVarSetTypes;
        vars_.varOptionalTypes = oldVarOptionalTypes;
        vars_.varFuncTypes = oldVarFuncTypes;
        vars_.varProtocolTypes = oldVarProtocolTypes;
        vars_.varConcreteProtocolTypes = oldVarConcreteProtocolTypes;
        vars_.varResultTypes = oldVarResultTypes;
        vars_.currentFuncResultInfo = oldFuncRI;
        currentFuncOptionalInner_ = oldFuncOptInner;
        vars_.movedVars = oldMovedVars;
        vars_.heapStringVars = oldHeapStringVars;
        vars_.tempStrings = oldTempStrings;
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
                    } else if (param.type && param.type->getKind() == TypeRepr::Kind::Array) {
                        auto *arrTy = static_cast<const ArrayTypeRepr *>(param.type.get());
                        if (arrTy->isDynamic()) {
                            paramTypes.push_back(getDynArrayStructTy());
                        } else {
                            paramTypes.push_back(toLLVMType(param.type.get()));
                        }
                    } else if (param.type && param.type->getKind() == TypeRepr::Kind::DynProtocol) {
                        paramTypes.push_back(getTraitObjectTy());
                    } else {
                        paramTypes.push_back(toLLVMType(param.type.get()));
                    }
                }

                auto *returnType = toLLVMType(protoMethod->getReturnType());
                auto *funcType = llvm::FunctionType::get(returnType, paramTypes, false);
                auto *func = llvm::Function::Create(
                    funcType, llvm::Function::ExternalLinkage, mangledName, *module_);

                // Attach debug info subprogram to protocol thunk
                if (diBuilder_) {
                    auto *funcDbgType = createFunctionDebugType();
                    auto *sp = diBuilder_->createFunction(
                        diFile_, mangledName, mangledName, diFile_, 0,
                        funcDbgType, 0,
                        llvm::DINode::FlagPrototyped,
                        llvm::DISubprogram::SPFlagDefinition);
                    func->setSubprogram(sp);
                }

                size_t i = 0;
                for (auto &arg : func->args()) {
                    arg.setName(protoMethod->getParams()[i].name);
                    ++i;
                }

                auto *entryBB = llvm::BasicBlock::Create(*context_, "entry", func);
                builder_->SetInsertPoint(entryBB);

                auto oldNamedValues = vars_.namedValues;
                auto oldVarStructTypes = vars_.varStructTypes;
                auto oldVarEnumTypes = vars_.varEnumTypes;
                auto oldVarArrayTypes = vars_.varArrayTypes;
                auto oldVarDynArrayTypes = vars_.varDynArrayTypes;
                auto oldVarDynArrayProtocol = vars_.varDynArrayProtocol;
                auto oldVarMapTypes = vars_.varMapTypes;
                auto oldVarSetTypes = vars_.varSetTypes;
                auto oldVarOptionalTypes = vars_.varOptionalTypes;
                auto oldVarFuncTypes = vars_.varFuncTypes;
                auto oldVarProtocolTypes = vars_.varProtocolTypes;
                auto oldVarConcreteProtocolTypes = vars_.varConcreteProtocolTypes;
                auto oldVarResultTypes = vars_.varResultTypes;
                auto *oldFuncRI = vars_.currentFuncResultInfo;
                auto *oldFuncOptInner2 = currentFuncOptionalInner_;
                auto oldMovedVars2 = vars_.movedVars;
                auto oldHeapStringVars2 = vars_.heapStringVars;
                auto oldTempStrings2 = vars_.tempStrings;
                vars_.namedValues.clear();
                vars_.varStructTypes.clear();
                vars_.varEnumTypes.clear();
                vars_.varArrayTypes.clear();
                vars_.varDynArrayTypes.clear();
                vars_.varDynArrayProtocol.clear();
                vars_.varMapTypes.clear();
                vars_.varSetTypes.clear();
                vars_.varOptionalTypes.clear();
                vars_.varFuncTypes.clear();
                vars_.varProtocolTypes.clear();
                vars_.varConcreteProtocolTypes.clear();
                vars_.varResultTypes.clear();
                vars_.varFileTypes.clear();
                vars_.currentFuncResultInfo = nullptr;
                currentFuncOptionalInner_ = nullptr;
                vars_.movedVars.clear();
                vars_.heapStringVars.clear();
                vars_.tempStrings.clear();

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
                    vars_.namedValues[std::string(arg.getName())] = alloca;
                    if (protoMethod->getParams()[i].isSelf) {
                        if (classTypes_.find(typeName) != classTypes_.end()) {
                            vars_.varClassTypes["self"] = typeName;
                        } else {
                            vars_.varStructTypes["self"] = typeName;
                        }
                    } else {
                        // Register struct/enum-typed parameters for member access
                        auto &pd = protoMethod->getParams()[i];
                        const TypeRepr *pt = pd.type.get();
                        if (pt && pt->getKind() == TypeRepr::Kind::Reference)
                            pt = static_cast<const ReferenceTypeRepr *>(pt)->getInner();
                        if (pt && pt->getKind() == TypeRepr::Kind::Named) {
                            auto *named = static_cast<const NamedTypeRepr *>(pt);
                            if (structTypes_.count(named->getName()))
                                vars_.varStructTypes[pd.name] = named->getName();
                            else if (classTypes_.count(named->getName()))
                                vars_.varClassTypes[pd.name] = named->getName();
                            else if (enumTypes_.count(named->getName()))
                                vars_.varEnumTypes[pd.name] = named->getName();
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

                vars_.namedValues = oldNamedValues;
                vars_.varStructTypes = oldVarStructTypes;
                vars_.varEnumTypes = oldVarEnumTypes;
                vars_.varArrayTypes = oldVarArrayTypes;
                vars_.varDynArrayTypes = oldVarDynArrayTypes;
                vars_.varDynArrayProtocol = oldVarDynArrayProtocol;
                vars_.varMapTypes = oldVarMapTypes;
                vars_.varSetTypes = oldVarSetTypes;
                vars_.varOptionalTypes = oldVarOptionalTypes;
                vars_.varFuncTypes = oldVarFuncTypes;
                vars_.varProtocolTypes = oldVarProtocolTypes;
                vars_.varConcreteProtocolTypes = oldVarConcreteProtocolTypes;
                vars_.varResultTypes = oldVarResultTypes;
                vars_.currentFuncResultInfo = oldFuncRI;
                currentFuncOptionalInner_ = oldFuncOptInner2;
                vars_.movedVars = oldMovedVars2;
                vars_.heapStringVars = oldHeapStringVars2;
                vars_.tempStrings = oldTempStrings2;
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
        if (c->hasDiscriminant()) {
            tag = static_cast<int>(c->getDiscriminant());
        }
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

void IRGen::preDeclareClass(ClassDecl *node) {
    const std::string &className = node->getName();
    // Idempotency / cycle guard.
    if (preDeclared_.count(className)) return;
    preDeclared_.insert(className);

    // Ensure parent class is pre-declared first so field/vtable inheritance reads
    // the parent's already-populated maps regardless of source order.
    if (node->hasParentClass()) {
        auto pdit = classDecls_.find(node->getParentClass());
        if (pdit != classDecls_.end())
            preDeclareClass(pdit->second);
    }

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

    // Add own fields (skip computed properties and static fields — no instance storage)
    // For lazy fields with initializer: add a companion bool "__lazy_init_<name>" field
    for (auto *field : node->getFields()) {
        if (field->isComputed()) {
            classComputedFields_[className].insert(field->getName());
            continue;
        }
        if (node->isStaticMember(field->getName())) {
            auto *fieldTy = toLLVMType(field->getType());
            std::string gvName = className + "_" + field->getName();
            auto *gv = new llvm::GlobalVariable(
                *module_, fieldTy, false,
                llvm::GlobalValue::ExternalLinkage,
                llvm::Constant::getNullValue(fieldTy),
                gvName);
            classStaticFields_[className + "." + field->getName()] = gv;
            continue;
        }
        allFieldNames.push_back(field->getName());
        allFieldTypeReprs.push_back(field->getType());
        allFieldLLVMTypes.push_back(toLLVMType(field->getType()));

        // Companion flag for lazy fields with initializer
        if (field->hasLazyInit()) {
            classLazyFields_[className].insert(field->getName());
            allFieldNames.push_back("__lazy_init_" + field->getName());
            allFieldTypeReprs.push_back(field->getType()); // placeholder (not used for access)
            allFieldLLVMTypes.push_back(llvm::Type::getInt1Ty(*context_));
        }
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

    // Add own methods (or override slots) — skip static methods and subscript
    for (auto *method : node->getMethods()) {
        if (node->isStaticMember(method->getName())) continue;
        if (method->getName() == "subscript") continue;
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

    // Generate methods — self is implicit first parameter (except static)
    for (auto *method : node->getMethods()) {
        bool isStatic = node->isStaticMember(method->getName());
        std::string mangledName = className + "_" + method->getName();

        std::vector<llvm::Type *> paramTypes;
        if (!isStatic) {
            paramTypes.push_back(llvm::PointerType::getUnqual(*context_)); // implicit self
        }
        for (auto &param : method->getParams()) {
            paramTypes.push_back(toLLVMType(param.type.get()));
        }

        auto *returnType = toLLVMType(method->getReturnType());
        // Dynamic arrays: pass/return as DynArray struct value, not ptr.
        if (method->getReturnType() &&
            method->getReturnType()->getKind() == TypeRepr::Kind::Array) {
            auto *arrRet = static_cast<const ArrayTypeRepr *>(method->getReturnType());
            if (arrRet->isDynamic())
                returnType = getDynArrayStructTy();
        }
        auto *funcType = llvm::FunctionType::get(returnType, paramTypes, false);
        auto *func = llvm::Function::Create(
            funcType, llvm::Function::ExternalLinkage, mangledName, *module_);

        // Attach debug info subprogram to class method
        if (diBuilder_) {
            auto *funcDbgType = createFunctionDebugType(method);
            unsigned lineNo = method->getStartLoc().isValid() ? method->getStartLoc().line : 0;
            auto *sp = diBuilder_->createFunction(
                diFile_, mangledName, mangledName, diFile_, lineNo,
                funcDbgType, lineNo,
                llvm::DINode::FlagPrototyped,
                llvm::DISubprogram::SPFlagDefinition);
            func->setSubprogram(sp);
        }

        // Set arg names
        auto argIt = func->arg_begin();
        if (!isStatic) {
            argIt->setName("self");
            ++argIt;
        }
        for (auto &param : method->getParams()) {
            argIt->setName(param.name);
            ++argIt;
        }
        // Body emitted in phase 2 (visitClassDecl).
    }

    // Generate computed property getter/setter prototypes
    for (auto *field : node->getFields()) {
        if (!field->isComputed()) continue;
        auto *ptrTy = llvm::PointerType::getUnqual(*context_);

        // Getter: ClassName_get_fieldName(self) → fieldType
        if (field->getGetter()) {
            std::string getterName = className + "_get_" + field->getName();
            auto *retTy = toLLVMType(field->getType());
            auto *funcType = llvm::FunctionType::get(retTy, {ptrTy}, false);
            auto *func = llvm::Function::Create(
                funcType, llvm::Function::ExternalLinkage, getterName, *module_);
            func->arg_begin()->setName("self");
        }

        // Setter: ClassName_set_fieldName(self, newValue) → void
        if (field->getSetter()) {
            std::string setterName = className + "_set_" + field->getName();
            auto *valTy = toLLVMType(field->getType());
            auto *funcType = llvm::FunctionType::get(
                llvm::Type::getVoidTy(*context_), {ptrTy, valTy}, false);
            auto *func = llvm::Function::Create(
                funcType, llvm::Function::ExternalLinkage, setterName, *module_);
            auto argIt = func->arg_begin();
            argIt->setName("self");
            ++argIt;
            argIt->setName("newValue");
        }
    }

    // Generate property observer (willSet/didSet) prototypes for stored fields
    for (auto *field : node->getFields()) {
        if (field->isComputed()) continue;
        if (!field->hasObservers()) continue;
        auto *ptrTy = llvm::PointerType::getUnqual(*context_);
        auto *valTy = toLLVMType(field->getType());
        auto *voidTy = llvm::Type::getVoidTy(*context_);

        // willSet: ClassName_willSet_fieldName(self, newValue) → void
        if (field->getWillSet()) {
            std::string fname = className + "_willSet_" + field->getName();
            auto *funcType = llvm::FunctionType::get(voidTy, {ptrTy, valTy}, false);
            auto *func = llvm::Function::Create(
                funcType, llvm::Function::ExternalLinkage, fname, *module_);
            auto argIt = func->arg_begin();
            argIt->setName("self"); ++argIt;
            argIt->setName("newValue");
        }

        // didSet: ClassName_didSet_fieldName(self, oldValue) → void
        if (field->getDidSet()) {
            std::string fname = className + "_didSet_" + field->getName();
            auto *funcType = llvm::FunctionType::get(voidTy, {ptrTy, valTy}, false);
            auto *func = llvm::Function::Create(
                funcType, llvm::Function::ExternalLinkage, fname, *module_);
            auto argIt = func->arg_begin();
            argIt->setName("self"); ++argIt;
            argIt->setName("oldValue");
        }
    }

    // Track which fields have observers (for assignment codegen)
    for (auto *field : node->getFields()) {
        if (field->hasObservers()) {
            classObserverFields_[className].insert(field->getName());
        }
    }

    // Generate lazy accessor prototypes: ClassName_lazy_<field>(self) → fieldType
    for (auto *field : node->getFields()) {
        if (!field->hasLazyInit()) continue;
        auto *ptrTy = llvm::PointerType::getUnqual(*context_);
        auto *fieldLLVMTy = toLLVMType(field->getType());
        std::string fname = className + "_lazy_" + field->getName();
        auto *funcType = llvm::FunctionType::get(fieldLLVMTy, {ptrTy}, false);
        auto *func = llvm::Function::Create(
            funcType, llvm::Function::ExternalLinkage, fname, *module_);
        func->arg_begin()->setName("self");
    }

    // Generate init(s) prototypes: for each init (designated + convenience), a separate function
    // Mangled name: first = ClassName_init, others = ClassName_init<argCount>
    auto allInits = node->getInits();
    bool hasAnyInit = !allInits.empty();
    // Fallback: synthesize a zero-arg init if none declared (for vtable-only classes)
    std::vector<const FuncDecl *> initsToGen;
    for (auto *it : allInits) initsToGen.push_back(it);
    if (initsToGen.empty()) initsToGen.push_back(nullptr); // sentinel for default init

    bool firstInit = true;
    for (auto *initDecl : initsToGen) {
        // Mangled name
        std::string initName = className + "_init";
        if (!firstInit && initDecl) {
            size_t argCount = 0;
            for (auto &p : initDecl->getParams()) if (!p.isSelf) argCount++;
            initName += std::to_string(argCount);
        }
        firstInit = false;

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

        if (diBuilder_) {
            auto *funcDbgType = createFunctionDebugType();
            auto *sp = diBuilder_->createFunction(
                diFile_, initName, initName, diFile_, 0,
                funcDbgType, 0,
                llvm::DINode::FlagPrototyped,
                llvm::DISubprogram::SPFlagDefinition);
            initFunc->setSubprogram(sp);
        }

        // Set arg names; body emitted in phase 2 (visitClassDecl).
        if (initDecl) {
            size_t argIdx = 0;
            for (auto &arg : initFunc->args()) {
                if (argIdx < initDecl->getParams().size())
                    arg.setName(initDecl->getParams()[argIdx].name);
                ++argIdx;
            }
        }
    }

    // Generate init_fields prototype: uses designated (first) init's signature.
    // Used by child class super.init() and convenience self.init delegation.
    {
        auto *designatedInit = hasAnyInit ? allInits.front() : nullptr;
        std::string initFieldsName = className + "_init_fields";

        std::vector<llvm::Type *> paramTypes;
        paramTypes.push_back(llvm::PointerType::getUnqual(*context_));
        if (designatedInit) {
            for (auto &param : designatedInit->getParams()) {
                paramTypes.push_back(toLLVMType(param.type.get()));
            }
        }

        auto *voidTy = llvm::Type::getVoidTy(*context_);
        auto *funcType = llvm::FunctionType::get(voidTy, paramTypes, false);
        auto *func = llvm::Function::Create(
            funcType, llvm::Function::ExternalLinkage, initFieldsName, *module_);

        if (diBuilder_) {
            auto *funcDbgType = createFunctionDebugType();
            auto *sp = diBuilder_->createFunction(
                diFile_, initFieldsName, initFieldsName, diFile_, 0,
                funcDbgType, 0,
                llvm::DINode::FlagPrototyped,
                llvm::DISubprogram::SPFlagDefinition);
            func->setSubprogram(sp);
        }

        // Set arg names; body emitted in phase 2 (visitClassDecl).
        auto argIt = func->arg_begin();
        argIt->setName("self");
        ++argIt;
        if (designatedInit) {
            for (auto &param : designatedInit->getParams()) {
                if (argIt != func->arg_end()) {
                    argIt->setName(param.name);
                    ++argIt;
                }
            }
        }
    }

    // Generate deinit prototype: ClassName_deinit(self) → void
    {
        std::string deinitName = className + "_deinit";

        auto *ptrTy = llvm::PointerType::getUnqual(*context_);
        auto *voidTy = llvm::Type::getVoidTy(*context_);
        auto *funcType = llvm::FunctionType::get(voidTy, {ptrTy}, false);
        auto *func = llvm::Function::Create(
            funcType, llvm::Function::ExternalLinkage, deinitName, *module_);

        // Attach debug info subprogram to class deinit
        if (diBuilder_) {
            auto *funcDbgType = createFunctionDebugType();
            auto *sp = diBuilder_->createFunction(
                diFile_, deinitName, deinitName, diFile_, 0,
                funcDbgType, 0,
                llvm::DINode::FlagPrototyped,
                llvm::DISubprogram::SPFlagDefinition);
            func->setSubprogram(sp);
        }

        // Set arg name; body emitted in phase 2 (visitClassDecl).
        func->arg_begin()->setName("self");
    }

    currentClassContext_ = prevClassCtx;
}

// Phase 2: emit class function bodies and build the vtable global. All prototypes
// were created in preDeclareClass; here we look them up via module_->getFunction.
llvm::Value *IRGen::visitClassDecl(ClassDecl *node) {
    const std::string &className = node->getName();

    // Safety net: if pre-declare pass didn't run for this class, do it now.
    preDeclareClass(node);

    auto *classType = classTypes_[className];
    auto *ptrTy = llvm::PointerType::getUnqual(*context_);

    std::string prevClassCtx = currentClassContext_;
    currentClassContext_ = className;

    // --- Method bodies ---
    for (auto *method : node->getMethods()) {
        if (!method->hasBody()) continue;
        std::string mangledName = className + "_" + method->getName();
        auto *func = module_->getFunction(mangledName);
        if (!func) continue;
        auto *returnType = func->getReturnType();

        auto *entryBB = llvm::BasicBlock::Create(*context_, "entry", func);
        builder_->SetInsertPoint(entryBB);

        auto guard = pushVarState();
        vars_ = VarState{};

        for (auto &arg : func->args()) {
            auto *alloca = createEntryBlockAlloca(func, std::string(arg.getName()), arg.getType());
            builder_->CreateStore(&arg, alloca);
            std::string argName = std::string(arg.getName());
            vars_.namedValues[argName] = alloca;
            if (argName == "self") {
                vars_.varClassTypes["self"] = className;
                continue;
            }
            // Register struct/class/enum-typed params for member access
            for (auto &pd : method->getParams()) {
                if (pd.isSelf || pd.name != argName) continue;
                const TypeRepr *pt = pd.type.get();
                if (pt && pt->getKind() == TypeRepr::Kind::Reference)
                    pt = static_cast<const ReferenceTypeRepr *>(pt)->getInner();
                if (pt && pt->getKind() == TypeRepr::Kind::Named) {
                    auto *named = static_cast<const NamedTypeRepr *>(pt);
                    if (structTypes_.count(named->getName()))
                        vars_.varStructTypes[pd.name] = named->getName();
                    else if (classTypes_.count(named->getName()))
                        vars_.varClassTypes[pd.name] = named->getName();
                    else if (enumTypes_.count(named->getName()))
                        vars_.varEnumTypes[pd.name] = named->getName();
                }
                break;
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
    }

    // --- Computed property getter/setter bodies ---
    for (auto *field : node->getFields()) {
        if (!field->isComputed()) continue;

        if (field->getGetter()) {
            auto *func = module_->getFunction(className + "_get_" + field->getName());
            if (func) {
                auto *retTy = func->getReturnType();
                auto *entryBB = llvm::BasicBlock::Create(*context_, "entry", func);
                builder_->SetInsertPoint(entryBB);
                auto guard = pushVarState();
                vars_ = VarState{};
                auto *selfAlloca = createEntryBlockAlloca(func, "self", ptrTy);
                builder_->CreateStore(&*func->arg_begin(), selfAlloca);
                vars_.namedValues["self"] = selfAlloca;
                vars_.varClassTypes["self"] = className;
                visit(const_cast<BlockStmt *>(field->getGetter()));
                if (!builder_->GetInsertBlock()->getTerminator()) {
                    builder_->CreateRet(llvm::Constant::getNullValue(retTy));
                }
            }
        }

        if (field->getSetter()) {
            auto *func = module_->getFunction(className + "_set_" + field->getName());
            if (func) {
                auto *valTy = toLLVMType(field->getType());
                auto *entryBB = llvm::BasicBlock::Create(*context_, "entry", func);
                builder_->SetInsertPoint(entryBB);
                auto guard = pushVarState();
                vars_ = VarState{};
                auto *selfAlloca = createEntryBlockAlloca(func, "self", ptrTy);
                builder_->CreateStore(&*func->arg_begin(), selfAlloca);
                vars_.namedValues["self"] = selfAlloca;
                vars_.varClassTypes["self"] = className;
                auto setArgIt = func->arg_begin(); ++setArgIt;
                auto *nvAlloca = createEntryBlockAlloca(func, "newValue", valTy);
                builder_->CreateStore(&*setArgIt, nvAlloca);
                vars_.namedValues["newValue"] = nvAlloca;
                visit(const_cast<BlockStmt *>(field->getSetter()));
                if (!builder_->GetInsertBlock()->getTerminator()) {
                    builder_->CreateRetVoid();
                }
            }
        }
    }

    // --- Property observer (willSet/didSet) bodies ---
    for (auto *field : node->getFields()) {
        if (field->isComputed()) continue;
        if (!field->hasObservers()) continue;
        auto *valTy = toLLVMType(field->getType());

        if (field->getWillSet()) {
            auto *func = module_->getFunction(className + "_willSet_" + field->getName());
            if (func) {
                auto *entryBB = llvm::BasicBlock::Create(*context_, "entry", func);
                builder_->SetInsertPoint(entryBB);
                auto guard = pushVarState();
                vars_ = VarState{};
                auto *selfAlloca = createEntryBlockAlloca(func, "self", ptrTy);
                builder_->CreateStore(&*func->arg_begin(), selfAlloca);
                vars_.namedValues["self"] = selfAlloca;
                vars_.varClassTypes["self"] = className;
                auto wsIt = func->arg_begin(); ++wsIt;
                auto *nvAlloca = createEntryBlockAlloca(func, "newValue", valTy);
                builder_->CreateStore(&*wsIt, nvAlloca);
                vars_.namedValues["newValue"] = nvAlloca;
                visit(const_cast<BlockStmt *>(field->getWillSet()));
                if (!builder_->GetInsertBlock()->getTerminator()) {
                    builder_->CreateRetVoid();
                }
            }
        }

        if (field->getDidSet()) {
            auto *func = module_->getFunction(className + "_didSet_" + field->getName());
            if (func) {
                auto *entryBB = llvm::BasicBlock::Create(*context_, "entry", func);
                builder_->SetInsertPoint(entryBB);
                auto guard = pushVarState();
                vars_ = VarState{};
                auto *selfAlloca = createEntryBlockAlloca(func, "self", ptrTy);
                builder_->CreateStore(&*func->arg_begin(), selfAlloca);
                vars_.namedValues["self"] = selfAlloca;
                vars_.varClassTypes["self"] = className;
                auto dsIt = func->arg_begin(); ++dsIt;
                auto *ovAlloca = createEntryBlockAlloca(func, "oldValue", valTy);
                builder_->CreateStore(&*dsIt, ovAlloca);
                vars_.namedValues["oldValue"] = ovAlloca;
                visit(const_cast<BlockStmt *>(field->getDidSet()));
                if (!builder_->GetInsertBlock()->getTerminator()) {
                    builder_->CreateRetVoid();
                }
            }
        }
    }

    // --- Lazy accessor bodies ---
    for (auto *field : node->getFields()) {
        if (!field->hasLazyInit()) continue;
        auto *func = module_->getFunction(className + "_lazy_" + field->getName());
        if (!func) continue;
        auto cfIt = classFieldNames_.find(className);
        if (cfIt == classFieldNames_.end()) continue;
        int fieldIdx = -1, flagIdx = -1;
        for (size_t i = 0; i < cfIt->second.size(); ++i) {
            if (cfIt->second[i] == field->getName()) fieldIdx = (int)i;
            if (cfIt->second[i] == "__lazy_init_" + field->getName()) flagIdx = (int)i;
        }
        if (fieldIdx < 0 || flagIdx < 0) continue;

        auto *fieldLLVMTy = toLLVMType(field->getType());
        auto *entryBB = llvm::BasicBlock::Create(*context_, "entry", func);
        auto *initBB = llvm::BasicBlock::Create(*context_, "lazy.init", func);
        auto *loadBB = llvm::BasicBlock::Create(*context_, "lazy.load", func);

        builder_->SetInsertPoint(entryBB);
        auto guard = pushVarState();
        vars_ = VarState{};

        auto *selfAlloca = createEntryBlockAlloca(func, "self", ptrTy);
        builder_->CreateStore(&*func->arg_begin(), selfAlloca);
        vars_.namedValues["self"] = selfAlloca;
        vars_.varClassTypes["self"] = className;

        auto *selfV = builder_->CreateLoad(ptrTy, selfAlloca, "self.v");
        auto *flagGEP = builder_->CreateStructGEP(classType, selfV, flagIdx + 1, "flag.gep");
        auto *flagV = builder_->CreateLoad(llvm::Type::getInt1Ty(*context_), flagGEP, "lazy.flag");
        builder_->CreateCondBr(flagV, loadBB, initBB);

        builder_->SetInsertPoint(initBB);
        auto *initVal = visit(const_cast<Expr *>(field->getLazyInit()));
        auto *selfV2 = builder_->CreateLoad(ptrTy, selfAlloca, "self.v2");
        auto *fieldGEP = builder_->CreateStructGEP(classType, selfV2, fieldIdx + 1, "fld.gep");
        if (initVal) builder_->CreateStore(initVal, fieldGEP);
        auto *flagGEP2 = builder_->CreateStructGEP(classType, selfV2, flagIdx + 1, "flag.gep2");
        builder_->CreateStore(llvm::ConstantInt::getTrue(*context_), flagGEP2);
        builder_->CreateBr(loadBB);

        builder_->SetInsertPoint(loadBB);
        auto *selfV3 = builder_->CreateLoad(ptrTy, selfAlloca, "self.v3");
        auto *fieldGEP2 = builder_->CreateStructGEP(classType, selfV3, fieldIdx + 1, "fld.gep2");
        auto *retVal = builder_->CreateLoad(fieldLLVMTy, fieldGEP2, "lazy.val");
        builder_->CreateRet(retVal);
    }

    // --- init(s) bodies ---
    auto allInits = node->getInits();
    bool hasAnyInit = !allInits.empty();
    std::vector<const FuncDecl *> initsToGen;
    for (auto *it : allInits) initsToGen.push_back(it);
    if (initsToGen.empty()) initsToGen.push_back(nullptr);

    bool firstInit = true;
    for (auto *initDecl : initsToGen) {
        std::string initName = className + "_init";
        if (!firstInit && initDecl) {
            size_t argCount = 0;
            for (auto &p : initDecl->getParams()) if (!p.isSelf) argCount++;
            initName += std::to_string(argCount);
        }
        firstInit = false;

        auto *initFunc = module_->getFunction(initName);
        if (!initFunc) continue;

        auto *entryBB = llvm::BasicBlock::Create(*context_, "entry", initFunc);
        builder_->SetInsertPoint(entryBB);

        auto guard = pushVarState();
        vars_ = VarState{};

        if (initDecl) {
            size_t argIdx = 0;
            for (auto &arg : initFunc->args()) {
                if (argIdx < initDecl->getParams().size()) {
                    auto *alloca = createEntryBlockAlloca(
                        initFunc, std::string(arg.getName()), arg.getType());
                    builder_->CreateStore(&arg, alloca);
                    vars_.namedValues[std::string(arg.getName())] = alloca;
                    auto &pd = initDecl->getParams()[argIdx];
                    const TypeRepr *pt = pd.type.get();
                    if (pt && pt->getKind() == TypeRepr::Kind::Reference)
                        pt = static_cast<const ReferenceTypeRepr *>(pt)->getInner();
                    if (pt && pt->getKind() == TypeRepr::Kind::Named) {
                        auto *named = static_cast<const NamedTypeRepr *>(pt);
                        if (structTypes_.count(named->getName()))
                            vars_.varStructTypes[pd.name] = named->getName();
                        else if (classTypes_.count(named->getName()))
                            vars_.varClassTypes[pd.name] = named->getName();
                        else if (enumTypes_.count(named->getName()))
                            vars_.varEnumTypes[pd.name] = named->getName();
                    }
                }
                ++argIdx;
            }
        }

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

        auto *vtableGEP = builder_->CreateStructGEP(classType, objPtr, 0, "vtable_ptr");
        (void)vtableGEP;
        auto *selfAlloca = createEntryBlockAlloca(initFunc, "self", ptrTy);
        builder_->CreateStore(objPtr, selfAlloca);
        vars_.namedValues["self"] = selfAlloca;
        vars_.varClassTypes["self"] = className;

        bool savedIsClassInit = currentIsClassInit_;
        currentIsClassInit_ = true;
        if (initDecl && initDecl->getBody()) {
            visit(const_cast<BlockStmt *>(initDecl->getBody()));
        }
        currentIsClassInit_ = savedIsClassInit;

        if (!builder_->GetInsertBlock()->getTerminator()) {
            auto *selfVal = builder_->CreateLoad(ptrTy, selfAlloca, "self_val");
            auto *vtableGEP2 = builder_->CreateStructGEP(classType, selfVal, 0, "vtable_slot");
            builder_->CreateStore(llvm::ConstantPointerNull::get(ptrTy), vtableGEP2);
            builder_->CreateRet(selfVal);
        }
    }

    // --- init_fields body ---
    {
        auto *designatedInit = hasAnyInit ? allInits.front() : nullptr;
        auto *func = module_->getFunction(className + "_init_fields");
        if (func) {
            auto *entryBB = llvm::BasicBlock::Create(*context_, "entry", func);
            builder_->SetInsertPoint(entryBB);

            auto guard = pushVarState();
            vars_ = VarState{};

            auto argIt = func->arg_begin();
            auto *selfAlloca = createEntryBlockAlloca(func, "self", ptrTy);
            builder_->CreateStore(&*argIt, selfAlloca);
            vars_.namedValues["self"] = selfAlloca;
            vars_.varClassTypes["self"] = className;
            ++argIt;

            if (designatedInit) {
                for (auto &param : designatedInit->getParams()) {
                    if (argIt != func->arg_end()) {
                        auto *alloca = createEntryBlockAlloca(func, param.name, argIt->getType());
                        builder_->CreateStore(&*argIt, alloca);
                        vars_.namedValues[param.name] = alloca;
                        const TypeRepr *pt = param.type.get();
                        if (pt && pt->getKind() == TypeRepr::Kind::Reference)
                            pt = static_cast<const ReferenceTypeRepr *>(pt)->getInner();
                        if (pt && pt->getKind() == TypeRepr::Kind::Named) {
                            auto *named = static_cast<const NamedTypeRepr *>(pt);
                            if (structTypes_.count(named->getName()))
                                vars_.varStructTypes[param.name] = named->getName();
                            else if (classTypes_.count(named->getName()))
                                vars_.varClassTypes[param.name] = named->getName();
                            else if (enumTypes_.count(named->getName()))
                                vars_.varEnumTypes[param.name] = named->getName();
                        }
                        ++argIt;
                    }
                }
            }

            if (designatedInit && designatedInit->getBody()) {
                visit(const_cast<BlockStmt *>(designatedInit->getBody()));
            }

            if (!builder_->GetInsertBlock()->getTerminator()) {
                builder_->CreateRetVoid();
            }
        }
    }

    // --- deinit body ---
    {
        auto *voidTy = llvm::Type::getVoidTy(*context_);
        auto *func = module_->getFunction(className + "_deinit");
        if (func) {
            auto *entryBB = llvm::BasicBlock::Create(*context_, "entry", func);
            builder_->SetInsertPoint(entryBB);

            auto guard = pushVarState();
            vars_ = VarState{};

            auto &selfArg = *func->arg_begin();
            auto *selfAlloca = createEntryBlockAlloca(func, "self", ptrTy);
            builder_->CreateStore(&selfArg, selfAlloca);
            vars_.namedValues["self"] = selfAlloca;
            vars_.varClassTypes["self"] = className;

            auto *deinitDecl = node->getDeinit();
            if (deinitDecl && deinitDecl->getBody()) {
                visit(const_cast<BlockStmt *>(deinitDecl->getBody()));
            }

            if (node->hasParentClass()) {
                std::string parentDeinit = node->getParentClass() + "_deinit";
                auto *parentFn = module_->getFunction(parentDeinit);
                if (parentFn) {
                    auto *selfVal = builder_->CreateLoad(ptrTy, selfAlloca, "self_val");
                    builder_->CreateCall(parentFn, {selfVal});
                }
            }

            auto *freeFn = module_->getFunction("free");
            if (!freeFn) {
                auto *freeTy = llvm::FunctionType::get(voidTy, {ptrTy}, false);
                freeFn = llvm::Function::Create(
                    freeTy, llvm::Function::ExternalLinkage, "free", *module_);
            }
            auto *selfVal = builder_->CreateLoad(ptrTy, selfAlloca, "self_for_free");
            builder_->CreateCall(freeFn, {selfVal});

            if (!builder_->GetInsertBlock()->getTerminator()) {
                builder_->CreateRetVoid();
            }
        }
    }

    // --- Create vtable global ---
    {
        auto &methods = classVtableMethods_[className];
        std::vector<llvm::Constant *> vtableEntries;

        for (auto &methodName : methods) {
            std::string mangledName;
            auto *fn = module_->getFunction(className + "_" + methodName);
            if (fn) {
                mangledName = className + "_" + methodName;
            } else {
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

        // Fix up all init functions' vtable store (designated + convenience)
        std::vector<llvm::Function *> initFns;
        initFns.push_back(module_->getFunction(className + "_init"));
        for (auto *it : node->getInits()) {
            size_t argCount = 0;
            for (auto &p : it->getParams()) if (!p.isSelf) argCount++;
            auto *fn = module_->getFunction(className + "_init" + std::to_string(argCount));
            if (fn) initFns.push_back(fn);
        }
        for (auto *initFn : initFns) {
            if (!initFn) continue;
            for (auto &BB : *initFn) {
                for (auto &I : BB) {
                    if (auto *store = llvm::dyn_cast<llvm::StoreInst>(&I)) {
                        if (llvm::isa<llvm::ConstantPointerNull>(store->getValueOperand())) {
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

llvm::Value *IRGen::visitTestDecl(TestDecl *node) {
    // Create a void() function: __liva_test_N
    std::string funcName = "__liva_test_" + std::to_string(testCounter_++);
    auto *funcTy = llvm::FunctionType::get(builder_->getVoidTy(), {}, false);
    auto *func = llvm::Function::Create(funcTy, llvm::Function::InternalLinkage,
                                         funcName, module_.get());

    // Attach debug info subprogram to test function
    if (diBuilder_) {
        auto *funcDbgType = createFunctionDebugType();
        unsigned lineNo = node->getStartLoc().isValid() ? node->getStartLoc().line : 0;
        auto *sp = diBuilder_->createFunction(
            diFile_, funcName, funcName, diFile_, lineNo,
            funcDbgType, lineNo,
            llvm::DINode::FlagPrototyped,
            llvm::DISubprogram::SPFlagDefinition);
        func->setSubprogram(sp);
    }

    auto *entry = llvm::BasicBlock::Create(*context_, "entry", func);
    auto *prevBB = builder_->GetInsertBlock();
    auto *prevFn = prevBB ? prevBB->getParent() : nullptr;

    builder_->SetInsertPoint(entry);

    // Save/restore named values
    auto guard = pushVarState();

    if (node->getBody()) {
        for (auto &stmt : node->getBody()->getStatements()) {
            visit(stmt.get());
            if (builder_->GetInsertBlock()->getTerminator())
                break;
        }
    }

    // Add return if no terminator
    if (!builder_->GetInsertBlock()->getTerminator()) {
        builder_->CreateRetVoid();
    }

    // Restore insert point
    if (prevBB)
        builder_->SetInsertPoint(prevBB);

    testEntries_.push_back({node->getName(), func});
    return nullptr;
}

void IRGen::generateTestMain() {
    auto *i32Ty = builder_->getInt32Ty();
    auto *mainTy = llvm::FunctionType::get(i32Ty, {}, false);
    auto *mainFn = llvm::Function::Create(mainTy, llvm::Function::ExternalLinkage,
                                            "main", module_.get());
    // Attach debug info subprogram to test main
    if (diBuilder_) {
        auto *funcDbgType = createFunctionDebugType();
        auto *sp = diBuilder_->createFunction(
            diFile_, "main", "main", diFile_, 0,
            funcDbgType, 0,
            llvm::DINode::FlagPrototyped,
            llvm::DISubprogram::SPFlagDefinition);
        mainFn->setSubprogram(sp);
    }
    auto *entry = llvm::BasicBlock::Create(*context_, "entry", mainFn);
    builder_->SetInsertPoint(entry);

    // Call liva_test_begin()
    builder_->CreateCall(getOrPanic("liva_test_begin"));

    // Call liva_test_run(name, fn_ptr) for each test
    auto *testRunFn = getOrPanic("liva_test_run");
    for (auto &te : testEntries_) {
        auto *nameStr = builder_->CreateGlobalString(te.name);
        builder_->CreateCall(testRunFn, {nameStr, te.func});
    }

    // return liva_test_end()
    auto *result = builder_->CreateCall(getOrPanic("liva_test_end"));
    builder_->CreateRet(result);
}

} // namespace liva

#endif // LIVA_HAS_LLVM
