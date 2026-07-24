#include "liva/IR/IRGen.h"

#ifdef LIVA_HAS_LLVM

namespace liva {

llvm::Value *IRGen::visitBlockStmt(BlockStmt *node) {
    llvm::Value *last = nullptr;
    for (auto &stmt : node->getStatements()) {
        if (diBuilder_) emitDebugLocation(stmt->getStartLoc());
        last = visit(stmt.get());
        // Free string temporaries created during this statement
        emitTempStringCleanup();
        // Stop if we hit a terminator
        if (builder_->GetInsertBlock()->getTerminator())
            break;
    }
    return last;
}

void IRGen::emitTempStringCleanup() {
    if (vars_.tempStrings.empty()) return;
    auto *freeFn = module_->getFunction("free");
    if (!freeFn) return;
    for (auto *val : vars_.tempStrings) {
        builder_->CreateCall(freeFn, {val});
    }
    vars_.tempStrings.clear();
}

void IRGen::emitScopeCleanup() {
    auto *ptrTy = llvm::PointerType::getUnqual(*context_);

    // Free DynArrays
    for (auto &[name, info] : vars_.varDynArrayTypes) {
        if (vars_.movedVars.count(name)) continue;
        auto it = vars_.namedValues.find(name);
        if (it == vars_.namedValues.end()) continue;
        auto *structAlloca = it->second;
        auto *dataGEP = builder_->CreateStructGEP(getDynArrayStructTy(), structAlloca, 0,
                                                   name + ".data.drop");
        auto *dataPtr = builder_->CreateLoad(ptrTy, dataGEP, name + ".ptr.drop");
        builder_->CreateCall(getOrPanic("liva_array_free"), {dataPtr});
    }

    // Free Maps
    for (auto &[name, info] : vars_.varMapTypes) {
        if (vars_.movedVars.count(name)) continue;
        auto it = vars_.namedValues.find(name);
        if (it == vars_.namedValues.end()) continue;
        auto *structAlloca = it->second;
        auto *entriesGEP = builder_->CreateStructGEP(getMapStructTy(), structAlloca, 0,
                                                      name + ".entries.drop");
        auto *entriesPtr = builder_->CreateLoad(ptrTy, entriesGEP, name + ".ptr.drop");
        builder_->CreateCall(getOrPanic("liva_map_free"), {entriesPtr});
    }

    // Free Sets
    for (auto &[name, info] : vars_.varSetTypes) {
        if (vars_.movedVars.count(name)) continue;
        auto it = vars_.namedValues.find(name);
        if (it == vars_.namedValues.end()) continue;
        auto *structAlloca = it->second;
        auto *entriesGEP = builder_->CreateStructGEP(getMapStructTy(), structAlloca, 0,
                                                      name + ".entries.drop");
        auto *entriesPtr = builder_->CreateLoad(ptrTy, entriesGEP, name + ".ptr.drop");
        builder_->CreateCall(getOrPanic("liva_set_free"), {entriesPtr});
    }

    // Call drop() for struct variables implementing Drop protocol,
    // or auto-cleanup heap fields for structs without Drop.
    //
    // Optional<Named> variables are ALSO registered in varStructTypes (see
    // IRGenDecl.cpp's Optional-typed VarDecl branch) so that optional
    // chaining / member access can resolve the payload's struct type — but
    // `it->second` for such a name is the Optional WRAPPER's alloca
    // ({i1 hasVal, T payload}), not a T*. Calling T_drop or
    // emitStructFieldCleanup on it directly reads/writes at the WRONG
    // offset (T's own field-0 offset instead of the wrapper's payload
    // offset, which is generally different because of the leading hasVal
    // field) — for Drop types this fires T_drop with a garbage self
    // pointer unconditionally (even when nil); for non-Drop types with
    // heap-owning fields (String/DynArray) this can read a garbage pointer
    // and call free() on it, corrupting the heap. Optional-registered names
    // are excluded here and handled correctly by the conditional
    // has-value-gated block below instead.
    for (auto &[name, structTypeName] : vars_.varStructTypes) {
        if (name == "self") continue; // self is borrowed (ref/ref mut), not owned
        if (vars_.movedVars.count(name)) continue;

        // Optional<Named> exclusion: only defer to the conditional block
        // below if the alloca's ACTUAL LLVM type matches the expected
        // Optional wrapper shape for the registered inner type. Trusting
        // varOptionalTypes co-presence alone is unsafe: these per-name maps
        // are function-flat (no lexical-scope save/restore for plain `if`/
        // loop blocks), so a shadowed prior declaration's stale
        // varOptionalTypes entry could otherwise survive a later,
        // differently-shaped redeclaration of the same name (hygiene fix:
        // IRGenDecl.cpp's VarDecl entry clears these on every redeclaration
        // — this check is defense in depth for anything that still slips
        // through). A stale entry diverting a PLAIN struct alloca into the
        // wrapper-typed conditional path below would GEP it out of bounds.
        auto optIt = vars_.varOptionalTypes.find(name);
        if (optIt != vars_.varOptionalTypes.end()) {
            auto nvIt = vars_.namedValues.find(name);
            if (nvIt != vars_.namedValues.end() &&
                nvIt->second->getAllocatedType() == getOptionalType(optIt->second)) {
                continue; // genuinely an Optional wrapper — handled below
            }
            // Type mismatch: stale/unrelated entry — fall through and treat
            // `name` as the plain NAMED struct its alloca actually is.
        }

        if (dropImplementors_.count(structTypeName)) {
            auto it = vars_.namedValues.find(name);
            if (it == vars_.namedValues.end()) continue;

            std::string dropFnName = structTypeName + "_drop";
            auto *dropFn = module_->getFunction(dropFnName);
            if (!dropFn) continue;

            builder_->CreateCall(dropFn, {it->second});
        } else {
            // Auto-cleanup heap fields for structs without Drop
            emitStructFieldCleanup(name, structTypeName);
        }
    }

    // Conditionally drop Optional<Drop-struct> payloads (has-value guard) —
    // the corrected counterpart to the loop above for Optional-registered
    // names. Non-Drop Optional<Named> payloads get NO drop/cleanup at all
    // here (matches spec: unchanged/no-op for non-Drop Optionals — the
    // wrong-pointer field-cleanup call is simply removed, not replaced).
    for (auto &[name, structTypeName] : vars_.varStructTypes) {
        if (name == "self") continue;
        if (!vars_.varOptionalTypes.count(name)) continue; // not an Optional<Named>
        if (vars_.movedVars.count(name)) continue;
        if (!dropImplementors_.count(structTypeName)) continue; // non-Drop payload: no cleanup

        auto it = vars_.namedValues.find(name);
        if (it == vars_.namedValues.end()) continue;
        auto optIt = vars_.varOptionalTypes.find(name);
        if (optIt == vars_.varOptionalTypes.end()) continue;

        auto *innerTy = optIt->second;
        auto *optTy = getOptionalType(innerTy);

        // Defense in depth (matching check in the loop above): verify the
        // alloca's actual type is really the Optional wrapper shape before
        // GEP'ing into it as one. A stale entry here would otherwise GEP a
        // plain (smaller) struct alloca as if it had a leading hasVal field
        // plus this payload field — out-of-bounds and type-confused.
        if (it->second->getAllocatedType() != optTy) continue;

        std::string dropFnName = structTypeName + "_drop";
        auto *dropFn = module_->getFunction(dropFnName);
        if (!dropFn) continue;

        auto *func = builder_->GetInsertBlock()->getParent();

        auto *hasValGEP = builder_->CreateStructGEP(optTy, it->second, 0,
            name + ".opt.has");
        auto *hasVal = builder_->CreateLoad(builder_->getInt1Ty(), hasValGEP,
            name + ".opt.has.val");
        auto *thenBB = llvm::BasicBlock::Create(*context_, name + ".opt.drop", func);
        auto *contBB = llvm::BasicBlock::Create(*context_, name + ".opt.dropcont", func);
        builder_->CreateCondBr(hasVal, thenBB, contBB);

        builder_->SetInsertPoint(thenBB);
        // Inner payload is stored inline (by value) inside the Optional
        // struct — unlike heapOptionalStringVars (a char* payload that must
        // be LOADED before use), the GEP here already IS a T*, so no load
        // is needed: pass it directly to T_drop.
        auto *innerGEP = builder_->CreateStructGEP(optTy, it->second, 1,
            name + ".opt.payload");
        builder_->CreateCall(dropFn, {innerGEP});
        builder_->CreateBr(contBB);

        builder_->SetInsertPoint(contBB);
    }

    // Call deinit for class instances (virtual dispatch through vtable)
    for (auto &[name, clsTypeName] : vars_.varClassTypes) {
        if (name == "self") continue; // self is not owned by current scope
        if (vars_.movedVars.count(name)) continue;
        auto it = vars_.namedValues.find(name);
        if (it == vars_.namedValues.end()) continue;

        auto ctIt = classTypes_.find(clsTypeName);
        if (ctIt == classTypes_.end()) continue;
        auto *classTy = ctIt->second;

        // Load obj ptr
        llvm::Value *objPtr = it->second;
        if (it->second->getAllocatedType()->isPointerTy()) {
            objPtr = builder_->CreateLoad(ptrTy, it->second, name + ".cls.drop");
        }

        // Load vtable, call deinit (index 0)
        auto vtIt = classVtableMethods_.find(clsTypeName);
        if (vtIt != classVtableMethods_.end() && !vtIt->second.empty()) {
            auto *vtableGEP = builder_->CreateStructGEP(classTy, objPtr, 0, name + ".vt.gep");
            auto *vtableArrTy = llvm::ArrayType::get(ptrTy, vtIt->second.size());
            auto *vtablePtr = builder_->CreateLoad(ptrTy, vtableGEP, name + ".vt.ptr");
            auto *deinitGEP = builder_->CreateGEP(
                vtableArrTy, vtablePtr,
                {builder_->getInt32(0), builder_->getInt32(0)},
                name + ".deinit.gep");
            auto *deinitPtr = builder_->CreateLoad(ptrTy, deinitGEP, name + ".deinit.ptr");
            auto *voidTy = llvm::Type::getVoidTy(*context_);
            auto *deinitFnTy = llvm::FunctionType::get(voidTy, {ptrTy}, false);
            builder_->CreateCall(deinitFnTy, deinitPtr, {objPtr});
        }
    }

    // Free heap-allocated string variables
    auto *freeFn = module_->getFunction("free");
    if (freeFn) {
        for (auto &name : vars_.heapStringVars) {
            if (vars_.movedVars.count(name)) continue;
            auto it = vars_.namedValues.find(name);
            if (it == vars_.namedValues.end()) continue;
            auto *strPtr = builder_->CreateLoad(ptrTy, it->second, name + ".str.drop");
            builder_->CreateCall(freeFn, {strPtr});
        }
        // Conditionally free Optional<string> inner ptrs (only if hasVal).
        for (auto &name : vars_.heapOptionalStringVars) {
            if (vars_.movedVars.count(name)) continue;
            auto it = vars_.namedValues.find(name);
            if (it == vars_.namedValues.end()) continue;
            auto optIt = vars_.varOptionalTypes.find(name);
            if (optIt == vars_.varOptionalTypes.end()) continue;
            auto *innerTy = optIt->second;
            auto *optTy = getOptionalType(innerTy);
            auto *func = builder_->GetInsertBlock()->getParent();

            auto *hasValGEP = builder_->CreateStructGEP(optTy, it->second, 0,
                name + ".opt.has");
            auto *hasVal = builder_->CreateLoad(builder_->getInt1Ty(), hasValGEP,
                name + ".opt.has.val");
            auto *thenBB = llvm::BasicBlock::Create(*context_, name + ".opt.free", func);
            auto *contBB = llvm::BasicBlock::Create(*context_, name + ".opt.cont", func);
            builder_->CreateCondBr(hasVal, thenBB, contBB);

            builder_->SetInsertPoint(thenBB);
            auto *innerGEP = builder_->CreateStructGEP(optTy, it->second, 1,
                name + ".opt.inner");
            auto *innerPtr = builder_->CreateLoad(ptrTy, innerGEP,
                name + ".opt.inner.val");
            builder_->CreateCall(freeFn, {innerPtr});
            builder_->CreateBr(contBB);

            builder_->SetInsertPoint(contBB);
        }
    }
}

void IRGen::emitStructFieldCleanup(const std::string &varName,
                                    const std::string &structTypeName) {
    auto *ptrTy = llvm::PointerType::getUnqual(*context_);

    auto ftrIt = structFieldTypeReprs_.find(structTypeName);
    if (ftrIt == structFieldTypeReprs_.end()) return;
    auto fnIt = structFieldNames_.find(structTypeName);
    if (fnIt == structFieldNames_.end()) return;
    auto stIt = structTypes_.find(structTypeName);
    if (stIt == structTypes_.end()) return;
    auto namedIt = vars_.namedValues.find(varName);
    if (namedIt == vars_.namedValues.end()) return;

    auto *structAlloca = namedIt->second;
    auto *structTy = stIt->second;
    const auto &fieldTypeReprs = ftrIt->second;

    for (size_t i = 0; i < fieldTypeReprs.size(); ++i) {
        const TypeRepr *ft = fieldTypeReprs[i];
        if (!ft) continue;

        if (ft->getKind() == TypeRepr::Kind::Array) {
            auto *arrRepr = static_cast<const ArrayTypeRepr *>(ft);
            if (arrRepr->isDynamic()) {
                // DynArray field -> liva_array_free(data_ptr)
                auto *fieldGEP = builder_->CreateStructGEP(structTy, structAlloca, i);
                auto *dataGEP = builder_->CreateStructGEP(getDynArrayStructTy(), fieldGEP, 0);
                auto *dataPtr = builder_->CreateLoad(ptrTy, dataGEP);
                builder_->CreateCall(getOrPanic("liva_array_free"), {dataPtr});
            }
        } else if (isStringTypeRepr(ft)) {
            // String field -> free(str_ptr)
            auto *fieldGEP = builder_->CreateStructGEP(structTy, structAlloca, i);
            auto *strPtr = builder_->CreateLoad(ptrTy, fieldGEP);
            auto *freeFn = module_->getFunction("free");
            if (freeFn) builder_->CreateCall(freeFn, {strPtr});
        } else if (ft->getKind() == TypeRepr::Kind::Named) {
            auto *named = static_cast<const NamedTypeRepr *>(ft);
            if (named->getName() == "Map") {
                auto *fieldGEP = builder_->CreateStructGEP(structTy, structAlloca, i);
                auto *entriesGEP = builder_->CreateStructGEP(getMapStructTy(), fieldGEP, 0);
                auto *entriesPtr = builder_->CreateLoad(ptrTy, entriesGEP);
                builder_->CreateCall(getOrPanic("liva_map_free"), {entriesPtr});
            } else if (named->getName() == "Set") {
                auto *fieldGEP = builder_->CreateStructGEP(structTy, structAlloca, i);
                auto *entriesGEP = builder_->CreateStructGEP(getMapStructTy(), fieldGEP, 0);
                auto *entriesPtr = builder_->CreateLoad(ptrTy, entriesGEP);
                builder_->CreateCall(getOrPanic("liva_set_free"), {entriesPtr});
            }
        }
    }
}

llvm::Value *IRGen::dupIfStringField(const std::string &structName,
                                      int idx, llvm::Value *val) {
    if (!val || idx < 0) return val;
    auto ftrIt = structFieldTypeReprs_.find(structName);
    if (ftrIt == structFieldTypeReprs_.end()) return val;
    if (idx >= static_cast<int>(ftrIt->second.size())) return val;
    const TypeRepr *ft = ftrIt->second[idx];
    if (isStringTypeRepr(ft)) {
        return builder_->CreateCall(getOrPanic("liva_str_dup"), {val}, "str.own");
    }
    return val;
}

llvm::Value *IRGen::cloneIfDynArrayField(const std::string &structName, int idx,
                                          llvm::Value *val,
                                          const std::string &nameHint) {
    if (!val || idx < 0) return val;
    if (!val->getType()->isStructTy()) return val;
    auto ftrIt = structFieldTypeReprs_.find(structName);
    if (ftrIt == structFieldTypeReprs_.end()) return val;
    if (idx >= static_cast<int>(ftrIt->second.size())) return val;
    const TypeRepr *ft = ftrIt->second[idx];
    if (!ft || ft->getKind() != TypeRepr::Kind::Array) return val;
    auto *arrRepr = static_cast<const ArrayTypeRepr *>(ft);
    if (!arrRepr->isDynamic()) return val;

    auto *daTy = getDynArrayStructTy();
    auto *funcCur = builder_->GetInsertBlock()->getParent();
    auto *srcAlloca = createEntryBlockAlloca(funcCur, nameHint + ".src", daTy);
    builder_->CreateStore(val, srcAlloca);
    auto *dataGEP = builder_->CreateStructGEP(daTy, srcAlloca, 0);
    auto *lenGEP = builder_->CreateStructGEP(daTy, srcAlloca, 1);
    auto *ptrTy = llvm::PointerType::getUnqual(*context_);
    auto *srcData = builder_->CreateLoad(ptrTy, dataGEP);
    auto *srcLen = builder_->CreateLoad(builder_->getInt64Ty(), lenGEP);
    auto *elemLLVMTy = toLLVMType(arrRepr->getElement());
    uint64_t elemSize = module_->getDataLayout().getTypeAllocSize(elemLLVMTy);
    auto *cloned = builder_->CreateCall(getOrPanic("liva_array_clone"),
        {srcData, srcLen, builder_->getInt64(elemSize)}, nameHint + ".clone");
    auto *newAlloca = createEntryBlockAlloca(funcCur, nameHint + ".cloned.da", daTy);
    builder_->CreateStore(cloned,
        builder_->CreateStructGEP(daTy, newAlloca, 0));
    builder_->CreateStore(srcLen,
        builder_->CreateStructGEP(daTy, newAlloca, 1));
    auto *eight = builder_->getInt64(8);
    auto *capVal = builder_->CreateSelect(
        builder_->CreateICmpSGT(srcLen, eight), srcLen, eight);
    builder_->CreateStore(capVal,
        builder_->CreateStructGEP(daTy, newAlloca, 2));
    return builder_->CreateLoad(daTy, newAlloca, nameHint + ".cloned.val");
}

llvm::Value *IRGen::visitReturnStmt(ReturnStmt *node) {
    if (diBuilder_) emitDebugLocation(node->getStartLoc());
    if (node->hasValue()) {
        // Check for 'return nil' in optional-returning function
        bool isReturnNil = node->getValue()->getKind() ==
                           ASTNode::NodeKind::NilLiteralExpr;

        if (isReturnNil && currentFuncOptionalInner_) {
            // return nil → return Optional { hasVal=false, val=zeroinit }
            emitScopeCleanup();
            auto *optTy = getOptionalType(currentFuncOptionalInner_);
            llvm::Value *optVal = llvm::Constant::getNullValue(optTy);
            if (currentIsAsync_ && currentCoroPromise_) {
                builder_->CreateStore(optVal, currentCoroPromise_);
                builder_->CreateBr(currentCoroFinalBB_);
                return nullptr;
            }
            return builder_->CreateRet(optVal);
        }

        // Class init 'return nil' → return nullptr (failable init)
        if (isReturnNil && currentIsClassInit_) {
            emitScopeCleanup();
            auto *ptrTy = llvm::PointerType::getUnqual(*context_);
            return builder_->CreateRet(llvm::ConstantPointerNull::get(ptrTy));
        }

        auto *val = visit(node->getValue());

        // Protect return value from cleanup
        // If returning a temp string, remove from vars_.tempStrings
        auto tIt = std::find(vars_.tempStrings.begin(), vars_.tempStrings.end(), val);
        if (tIt != vars_.tempStrings.end()) vars_.tempStrings.erase(tIt);
        // If returning a heap string variable, remove from vars_.heapStringVars
        // Same for Optional<string> variables that own their inner pointer.
        // Same for DynArray variables: returning the struct hands ownership
        // to the caller, so we must NOT free its backing buffer here.
        // We piggyback on vars_.movedVars which emitScopeCleanup already honours
        // to skip both vars_.heapStringVars and vars_.varDynArrayTypes/vars_.varMapTypes.
        if (node->getValue()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
            auto *retIdent = static_cast<IdentifierExpr *>(node->getValue());
            vars_.heapStringVars.erase(retIdent->getName());
            vars_.heapOptionalStringVars.erase(retIdent->getName());
            vars_.movedVars.insert(retIdent->getName());
        }
        // When the return expression wraps a temp string in Optional<string>
        // (e.g. `return convert::toStr(...)`), the raw pointer is still in
        // vars_.tempStrings but is now logically owned by the Optional we return.
        // Clear the list to avoid emitting `free(raw)` *after* our ret in the
        // surrounding visitBlockStmt's per-statement cleanup pass.
        if (currentFuncOptionalInner_ && val &&
            val->getType() == getOptionalType(currentFuncOptionalInner_)) {
            vars_.tempStrings.clear();
        }

        // Free remaining tracked temp strings BEFORE the terminator. Otherwise
        // visitBlockStmt's per-statement cleanup runs after CreateRet and
        // produces "Terminator in middle of basic block" verifier errors —
        // common pattern: `return Foo { s: strToUpper(x) }` where the temp is
        // duplicated into the struct field but the original still needs to
        // be freed.
        emitTempStringCleanup();

        emitScopeCleanup();

        // Wrap plain value in Optional if the function returns T?.
        // If val is already an Optional<T> matching the return type (e.g. when
        // forwarding the result of another optional-returning call), leave it
        // alone — wrapping again would store the whole Optional struct in the
        // value slot.
        if (currentFuncOptionalInner_ && val) {
            auto *optTy = getOptionalType(currentFuncOptionalInner_);
            if (val->getType() != optTy) {
                llvm::Value *optVal = llvm::UndefValue::get(optTy);
                optVal = builder_->CreateInsertValue(optVal, builder_->getTrue(), 0);
                optVal = builder_->CreateInsertValue(optVal, val, 1);
                val = optVal;
            }
        }

        if (currentIsAsync_ && currentCoroPromise_) {
            // Phase 2: store to promise and branch to coro.final
            builder_->CreateStore(val, currentCoroPromise_);
            builder_->CreateBr(currentCoroFinalBB_);
            return nullptr;
        }
        return builder_->CreateRet(val);
    }
    emitScopeCleanup();
    if (currentIsAsync_ && currentCoroPromise_) {
        // Phase 2: void return — just branch to coro.final
        builder_->CreateStore(
            llvm::Constant::getNullValue(asyncDeclaredRetType_), currentCoroPromise_);
        builder_->CreateBr(currentCoroFinalBB_);
        return nullptr;
    }
    return builder_->CreateRetVoid();
}

llvm::Value *IRGen::visitIfStmt(IfStmt *node) {
    if (diBuilder_) emitDebugLocation(node->getStartLoc());
    auto *condVal = visit(const_cast<Expr *>(node->getCondition()));
    if (!condVal)
        return nullptr;

    auto *func = builder_->GetInsertBlock()->getParent();

    auto *thenBB = llvm::BasicBlock::Create(*context_, "then", func);
    auto *elseBB = llvm::BasicBlock::Create(*context_, "else", func);
    auto *mergeBB = llvm::BasicBlock::Create(*context_, "merge", func);

    builder_->CreateCondBr(condVal, thenBB, node->hasElse() ? elseBB : mergeBB);

    // Then block
    builder_->SetInsertPoint(thenBB);
    visit(node->getThenBody());
    if (!builder_->GetInsertBlock()->getTerminator())
        builder_->CreateBr(mergeBB);

    // Else block
    builder_->SetInsertPoint(elseBB);
    if (node->hasElse()) {
        visit(node->getElseBody());
    }
    if (!builder_->GetInsertBlock()->getTerminator())
        builder_->CreateBr(mergeBB);

    // Merge block
    builder_->SetInsertPoint(mergeBB);
    return nullptr;
}

llvm::Value *IRGen::visitIfLetStmt(IfLetStmt *node) {
    if (diBuilder_) emitDebugLocation(node->getStartLoc());
    llvm::AllocaInst *optAlloca = nullptr;
    llvm::Type *innerType = nullptr;
    if (node->getOptionalExpr()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
        auto *ident = static_cast<IdentifierExpr *>(node->getOptionalExpr());
        auto it = vars_.namedValues.find(ident->getName());
        if (it != vars_.namedValues.end()) optAlloca = it->second;
        auto optIt = vars_.varOptionalTypes.find(ident->getName());
        if (optIt != vars_.varOptionalTypes.end()) innerType = optIt->second;
    }
    // Try to recover innerType from the alloca's struct type when vars_.varOptionalTypes miss
    if (optAlloca && !innerType) {
        auto *allocTy = optAlloca->getAllocatedType();
        if (allocTy->isStructTy()) {
            auto *st = llvm::cast<llvm::StructType>(allocTy);
            if (st->getNumElements() == 2 && st->getElementType(0)->isIntegerTy(1)) {
                innerType = st->getElementType(1);
            }
        }
    }

    // Handle arbitrary expressions returning Optional (e.g., method calls)
    if (!optAlloca || !innerType) {
        auto *exprVal = visit(node->getOptionalExpr());
        if (exprVal) {
            auto *structTy = llvm::dyn_cast<llvm::StructType>(exprVal->getType());
            if (structTy && structTy->getNumElements() == 2 &&
                structTy->getElementType(0)->isIntegerTy(1)) {
                innerType = structTy->getElementType(1);
                auto *func = builder_->GetInsertBlock()->getParent();
                optAlloca = createEntryBlockAlloca(func, "iflet.opt.tmp", structTy);
                builder_->CreateStore(exprVal, optAlloca);
            }
        }
    }
    if (!optAlloca || !innerType) {
        diag_.report(node->getStartLoc(), DiagID::err_irgen_if_let_not_optional);
        return nullptr;
    }

    // Task 2 (Drop/Move Tracking): if-let ownership transfer. If the
    // Optional's payload is a Drop-conforming NAMED struct, the binding
    // takes over ownership — it gets registered like an ordinary NAMED
    // struct var (dropped normally at its own scope exit) and the SOURCE
    // (if an identifier) is marked moved so emitScopeCleanup's new
    // conditional Optional-drop block skips it. Single drop per value path.
    std::string dropStructName;
    if (innerType->isStructTy()) {
        if (node->getOptionalExpr()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
            auto *srcIdent = static_cast<IdentifierExpr *>(node->getOptionalExpr());
            auto stIt = vars_.varStructTypes.find(srcIdent->getName());
            if (stIt != vars_.varStructTypes.end()) dropStructName = stIt->second;
        }
        if (dropStructName.empty()) {
            // Fallback: match innerType against known struct LLVM types by
            // identity (covers non-identifier sources, e.g. method calls).
            // Maintainer note: this is an O(n) scan over ALL declared struct
            // types, run once per if-let AST node during codegen (NOT per
            // runtime iteration/call) — negligible for realistic program
            // sizes today. If `structTypes_` ever grows large enough for
            // this to matter, consider a reverse llvm::Type* -> name index
            // built once and reused, rather than optimizing this call site
            // in isolation.
            for (auto &[nm, ty] : structTypes_) {
                if (ty == innerType) { dropStructName = nm; break; }
            }
        }
    }
    bool bindingOwnsDrop = !dropStructName.empty() &&
        dropImplementors_.count(dropStructName) > 0;

    if (bindingOwnsDrop &&
        node->getOptionalExpr()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
        auto *srcIdent = static_cast<IdentifierExpr *>(node->getOptionalExpr());
        vars_.movedVars.insert(srcIdent->getName());
    }

    auto *optStructTy = getOptionalType(innerType);
    auto *hasValPtr = builder_->CreateStructGEP(optStructTy, optAlloca, 0);
    auto *hasVal = builder_->CreateLoad(builder_->getInt1Ty(), hasValPtr, "iflet.hasval");
    auto *func = builder_->GetInsertBlock()->getParent();
    auto *thenBB = llvm::BasicBlock::Create(*context_, "iflet.then", func);
    auto *elseBB = llvm::BasicBlock::Create(*context_, "iflet.else", func);
    auto *mergeBB2 = llvm::BasicBlock::Create(*context_, "iflet.merge", func);
    builder_->CreateCondBr(hasVal, thenBB, node->hasElse() ? elseBB : mergeBB2);

    // Then: unwrap + bind
    builder_->SetInsertPoint(thenBB);
    auto *valPtr = builder_->CreateStructGEP(optStructTy, optAlloca, 1);
    auto *unwrapped = builder_->CreateLoad(innerType, valPtr, "iflet.val");
    auto *bindAlloca = createEntryBlockAlloca(func, node->getBindingName(), innerType);
    builder_->CreateStore(unwrapped, bindAlloca);
    // CRITICAL 3 fix (final whole-branch review): make the ownership
    // transfer runtime-real, not just compile-time. Previously only the
    // COMPILE-TIME movedVars marker suppressed the source's scope-exit
    // drop; the source's runtime hasVal flag was never cleared, so re-
    // entering this same if-let at runtime over the SAME identifier source
    // (e.g. nested inside a plain `while` loop) re-read and re-dropped the
    // same payload every pass. Storing false here makes a later hasVal
    // check on this identifier (another if-let, or a surrounding loop's
    // next pass) correctly see "no value". Only for identifier sources that
    // truly take ownership (bindingOwnsDrop) — a call-expr source has no
    // persistent variable to clear, and non-Drop payloads keep Copy
    // semantics (must remain readable through the source afterward).
    if (bindingOwnsDrop &&
        node->getOptionalExpr()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
        builder_->CreateStore(builder_->getFalse(), hasValPtr);
    }
    auto saved = vars_.namedValues;
    auto savedFileTypes = vars_.varFileTypes;
    auto savedDynArrayTypes = vars_.varDynArrayTypes;
    auto savedStructTypesForBinding = vars_.varStructTypes;
    // CRITICAL 2 fix (final whole-branch review): snapshot movedVars too.
    // VarDecl hygiene (IRGenDecl.cpp) erases movedVars[name] on every
    // redeclaration of `name` — a `let a = ...` INSIDE this then-body that
    // reuses an OUTER, already-moved-from name `a` would otherwise erase
    // the outer suppression permanently; once namedValues/varStructTypes are
    // restored below to refer to the outer `a` again, the erased movedVars
    // entry would no longer suppress its drop — resurrecting it for a
    // double-drop. Restored below by UNION (saved ∪ post-body): re-adds any
    // name suppressed before the body that got erased inside it, while
    // KEEPING whatever the body itself newly inserted (e.g. a same-scope
    // `outer1 = outer2` move that happens to occur inside the body) — a
    // blanket restore (`vars_.movedVars = savedMovedVars`) would incorrectly
    // un-suppress those.
    auto savedMovedVars = vars_.movedVars;
    vars_.namedValues[node->getBindingName()] = bindAlloca;
    if (bindingOwnsDrop) {
        // Register the binding like an ordinary NAMED struct var: an early
        // return inside the then-body will get it dropped correctly via
        // emitScopeCleanup's existing varStructTypes loop (its namedValues
        // entry is still live at that point — the restore below runs only
        // AFTER visit(thenBody) returns).
        vars_.varStructTypes[node->getBindingName()] = dropStructName;
        // IMPORTANT 5 fix: clear any stale moved-marker under the BINDING's
        // own name (symmetric with VarDecl's redeclaration hygiene). Without
        // this, an outer variable moved-from under the same name as this
        // binding would wrongly suppress the binding's own fallthrough drop
        // below (a leak) — see the fallthrough-drop guard's comment.
        // Composed with the union-restore above: erasing here only affects
        // movedVars for the DURATION of the body; if the outer name really
        // was moved, savedMovedVars still has it and the union-restore after
        // the body re-establishes that suppression once namedValues/
        // varStructTypes flip back to the outer variable.
        vars_.movedVars.erase(node->getBindingName());
    }
    // If the optional inner is a DynArray (`if let b = someOpt: [T]?`),
    // register the binding so `b.length`, `b[i]`, and for-iteration work
    // the same way they do for a let-bound `[T]` variable. Element type
    // comes from one of: the source variable's declared type (Sema),
    // the source variable's vars_.varDynArrayTypes entry from a prior
    // ownership transfer, or — for inline `if let b = call()` — the
    // call's resolved type.
    if (innerType == getDynArrayStructTy()) {
        llvm::Type *elemLLVM = nullptr;
        uint64_t elemSize = 0;
        auto deriveFromArrayRepr = [&](const ArrayTypeRepr *arrRepr) {
            if (!arrRepr || !arrRepr->isDynamic()) return;
            elemLLVM = toLLVMType(arrRepr->getElement());
            elemSize = module_->getDataLayout().getTypeAllocSize(elemLLVM);
        };
        if (auto *resolved = node->getOptionalExpr()->getResolvedType()) {
            if (resolved->getKind() == TypeRepr::Kind::Optional) {
                auto *optRepr = static_cast<const OptionalTypeRepr *>(resolved);
                if (optRepr->getInner() &&
                    optRepr->getInner()->getKind() == TypeRepr::Kind::Array) {
                    deriveFromArrayRepr(static_cast<const ArrayTypeRepr *>(
                        optRepr->getInner()));
                }
            } else if (resolved->getKind() == TypeRepr::Kind::Array) {
                // Some sites resolve to the inner directly.
                deriveFromArrayRepr(static_cast<const ArrayTypeRepr *>(resolved));
            }
        }
        if (!elemLLVM &&
            node->getOptionalExpr()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
            // Fall back to vars_.varDynArrayTypes on the source variable.
            auto *srcId = static_cast<IdentifierExpr *>(node->getOptionalExpr());
            auto daIt = vars_.varDynArrayTypes.find(srcId->getName());
            if (daIt != vars_.varDynArrayTypes.end()) {
                elemLLVM = daIt->second.elementType;
                elemSize = daIt->second.elemSize;
            }
        }
        if (elemLLVM) {
            vars_.varDynArrayTypes[node->getBindingName()] = {elemLLVM, elemSize};
            // Borrowed from the Optional storage: skip cleanup to avoid
            // double-freeing the buffer when the surrounding scope drops.
            vars_.movedVars.insert(node->getBindingName());
        }
    }
    // If the optional expression was File.open, the unwrapped value is a File ptr
    if (node->getOptionalExpr()->getKind() == ASTNode::NodeKind::CallExpr) {
        auto *callExpr = static_cast<CallExpr *>(node->getOptionalExpr());
        if (callExpr->getCallee()->getKind() == ASTNode::NodeKind::MemberExpr) {
            auto *me = static_cast<MemberExpr *>(callExpr->getCallee());
            if (me->getObject()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
                auto *id = static_cast<IdentifierExpr *>(me->getObject());
                if (id->getName() == "File" && me->getMember() == "open") {
                    vars_.varFileTypes.insert(node->getBindingName());
                }
            }
        }
    }
    // Also check if the optional source was a File-optional variable
    if (node->getOptionalExpr()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
        auto *ident = static_cast<IdentifierExpr *>(node->getOptionalExpr());
        if (vars_.varFileOptionalTypes.count(ident->getName())) {
            vars_.varFileTypes.insert(node->getBindingName());
        }
    }
    visit(node->getThenBody());
    // Normal fallthrough (no early return/break/continue inside the
    // then-body): drop the binding's payload here explicitly. Early-return
    // paths are already handled by emitScopeCleanup's varStructTypes loop
    // (registered above, while namedValues[bindingName] is still live) —
    // this call only fires when the block reaches the end normally (the
    // terminator guard mirrors the existing `CreateBr(mergeBB2)` pattern
    // just below).
    if (bindingOwnsDrop && !builder_->GetInsertBlock()->getTerminator() &&
        !vars_.movedVars.count(node->getBindingName())) {
        std::string dropFnName = dropStructName + "_drop";
        if (auto *dropFn = module_->getFunction(dropFnName)) {
            builder_->CreateCall(dropFn, {bindAlloca});
        }
    }
    vars_.namedValues = saved;
    vars_.varFileTypes = savedFileTypes;
    vars_.varDynArrayTypes = savedDynArrayTypes;
    vars_.varStructTypes = savedStructTypesForBinding;
    // CRITICAL 2 fix: union-restore movedVars (see the snapshot comment
    // above) — re-add every name that was suppressed BEFORE the body,
    // keeping anything the body itself newly inserted.
    for (auto &nm : savedMovedVars) vars_.movedVars.insert(nm);
    if (!builder_->GetInsertBlock()->getTerminator()) builder_->CreateBr(mergeBB2);

    // Else
    builder_->SetInsertPoint(elseBB);
    if (node->hasElse()) visit(node->getElseBody());
    if (!builder_->GetInsertBlock()->getTerminator()) builder_->CreateBr(mergeBB2);

    builder_->SetInsertPoint(mergeBB2);
    return nullptr;
}

llvm::Value *IRGen::visitWhileLetStmt(WhileLetStmt *node) {
    if (diBuilder_) emitDebugLocation(node->getStartLoc());
    // while let x = optional { body }
    // Becomes: loop { check hasVal; if false -> exit; unwrap; body; continue }
    llvm::AllocaInst *optAlloca = nullptr;
    llvm::Type *innerType = nullptr;

    if (node->getOptionalExpr()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
        auto *ident = static_cast<IdentifierExpr *>(node->getOptionalExpr());
        auto it = vars_.namedValues.find(ident->getName());
        if (it != vars_.namedValues.end()) optAlloca = it->second;
        auto optIt = vars_.varOptionalTypes.find(ident->getName());
        if (optIt != vars_.varOptionalTypes.end()) innerType = optIt->second;
    }

    auto *func = builder_->GetInsertBlock()->getParent();

    // For arbitrary expressions (e.g. `while let x = it.next()`): the call must
    // be evaluated on every iteration (to advance the iterator).  We emit the call
    // inside condBB, allocating a tmp slot on first visit using createEntryBlockAlloca
    // (which inserts at the function entry block regardless of current insert point).
    // The alloca pointer is captured into optAlloca so bodyBB can GEP into it.
    auto *condBB = llvm::BasicBlock::Create(*context_, "whilelet.cond", func);
    auto *bodyBB = llvm::BasicBlock::Create(*context_, "whilelet.body", func);
    auto *exitBB = llvm::BasicBlock::Create(*context_, "whilelet.exit", func);

    builder_->CreateBr(condBB);

    // Cond: (re-)evaluate expression if it is a call, then check hasVal.
    builder_->SetInsertPoint(condBB);
    if (!optAlloca || !innerType) {
        // Emit the call here (inside condBB) — this runs on every loop iteration,
        // correctly advancing the iterator each time.
        auto *callVal = visit(node->getOptionalExpr());
        if (callVal) {
            auto *structTy = llvm::dyn_cast<llvm::StructType>(callVal->getType());
            if (structTy && structTy->getNumElements() == 2 &&
                structTy->getElementType(0)->isIntegerTy(1)) {
                innerType = structTy->getElementType(1);
                // createEntryBlockAlloca inserts at the function entry block — fine
                // even though builder_ is currently pointing at condBB.
                optAlloca = createEntryBlockAlloca(func, "whilelet.opt.tmp", structTy);
                builder_->CreateStore(callVal, optAlloca);
            }
        }
    }

    if (!optAlloca || !innerType) {
        diag_.report(node->getStartLoc(), DiagID::err_irgen_while_let_not_optional);
        return nullptr;
    }

    // Task 2 (Drop/Move Tracking): while-let ownership transfer — per-
    // iteration counterpart to if-let's single-drop rule (see
    // visitIfLetStmt for the detailed rationale; symmetric logic here).
    // The compile-time source-moved marking happens ONCE (this AST node is
    // visited once regardless of how many runtime iterations occur) — for
    // an identifier source this suppresses emitScopeCleanup's Optional-drop
    // for that variable after the loop; for a call-expr source (the common
    // case — a fresh value every iteration) there is no persistent source
    // variable to mark.
    std::string dropStructName;
    if (innerType->isStructTy()) {
        if (node->getOptionalExpr()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
            auto *srcIdent = static_cast<IdentifierExpr *>(node->getOptionalExpr());
            auto stIt = vars_.varStructTypes.find(srcIdent->getName());
            if (stIt != vars_.varStructTypes.end()) dropStructName = stIt->second;
        }
        if (dropStructName.empty()) {
            // Same O(n) type-identity fallback as visitIfLetStmt above —
            // run once per while-let AST node during codegen, not per
            // runtime iteration; see the maintainer note there.
            for (auto &[nm, ty] : structTypes_) {
                if (ty == innerType) { dropStructName = nm; break; }
            }
        }
    }
    bool bindingOwnsDrop = !dropStructName.empty() &&
        dropImplementors_.count(dropStructName) > 0;

    if (bindingOwnsDrop &&
        node->getOptionalExpr()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
        auto *srcIdent = static_cast<IdentifierExpr *>(node->getOptionalExpr());
        vars_.movedVars.insert(srcIdent->getName());
    }

    auto *optStructTy = getOptionalType(innerType);
    auto *hasValPtr = builder_->CreateStructGEP(optStructTy, optAlloca, 0);
    auto *hasVal = builder_->CreateLoad(builder_->getInt1Ty(), hasValPtr, "whilelet.hasval");
    builder_->CreateCondBr(hasVal, bodyBB, exitBB);

    // Body: unwrap + bind + execute body
    builder_->SetInsertPoint(bodyBB);
    auto *valPtr = builder_->CreateStructGEP(optStructTy, optAlloca, 1);
    auto *unwrapped = builder_->CreateLoad(innerType, valPtr, "whilelet.val");
    auto *bindAlloca = createEntryBlockAlloca(func, node->getBindingName(), innerType);
    builder_->CreateStore(unwrapped, bindAlloca);
    // CRITICAL 3 fix (final whole-branch review): see visitIfLetStmt's
    // detailed rationale — make the ownership transfer runtime-real for
    // identifier sources by clearing the source's hasVal flag here, in
    // bodyBB (only entered when hasVal was true). This is what makes a
    // bare `while let v = <identifier>` over a NON-reassigned source
    // TERMINATE: the next condBB check re-loads this same flag and sees
    // false once the (only) value has been taken. Call-expr sources are
    // unaffected (guarded by the IdentifierExpr check) — each iteration
    // already evaluates a fresh call in condBB.
    if (bindingOwnsDrop &&
        node->getOptionalExpr()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
        builder_->CreateStore(builder_->getFalse(), hasValPtr);
    }

    auto saved = vars_.namedValues;
    auto savedStructTypesForBinding = vars_.varStructTypes;
    // CRITICAL 2 fix: snapshot movedVars (see visitIfLetStmt's detailed
    // comment — identical rationale, symmetric fix for while-let).
    auto savedMovedVars = vars_.movedVars;
    vars_.namedValues[node->getBindingName()] = bindAlloca;
    if (bindingOwnsDrop) {
        vars_.varStructTypes[node->getBindingName()] = dropStructName;
        // IMPORTANT 5 fix: clear any stale moved-marker under the binding's
        // own name (see visitIfLetStmt's comment — identical rationale).
        vars_.movedVars.erase(node->getBindingName());
    }
    loopStack_.push_back({exitBB, condBB});
    visit(node->getBody());
    loopStack_.pop_back();
    // Per-iteration drop: this single instruction lives in bodyBB, which
    // runs once per runtime loop pass — normal fallthrough only (matches
    // if-let; break/continue bypass this, a pre-existing gap shared with
    // ALL Drop-conforming locals declared inside loop bodies, not
    // introduced or fixed here).
    if (bindingOwnsDrop && !builder_->GetInsertBlock()->getTerminator() &&
        !vars_.movedVars.count(node->getBindingName())) {
        std::string dropFnName = dropStructName + "_drop";
        if (auto *dropFn = module_->getFunction(dropFnName)) {
            builder_->CreateCall(dropFn, {bindAlloca});
        }
    }
    vars_.namedValues = saved;
    vars_.varStructTypes = savedStructTypesForBinding;
    // CRITICAL 2 fix: union-restore movedVars (see visitIfLetStmt's comment).
    for (auto &nm : savedMovedVars) vars_.movedVars.insert(nm);
    if (!builder_->GetInsertBlock()->getTerminator())
        builder_->CreateBr(condBB);

    builder_->SetInsertPoint(exitBB);
    return nullptr;
}

llvm::Value *IRGen::visitWhileStmt(WhileStmt *node) {
    auto *func = builder_->GetInsertBlock()->getParent();

    auto *condBB = llvm::BasicBlock::Create(*context_, "while.cond", func);
    auto *bodyBB = llvm::BasicBlock::Create(*context_, "while.body", func);
    auto *exitBB = llvm::BasicBlock::Create(*context_, "while.exit", func);

    builder_->CreateBr(condBB);

    // Condition
    builder_->SetInsertPoint(condBB);
    if (diBuilder_) emitDebugLocation(node->getStartLoc());
    auto *condVal = visit(const_cast<Expr *>(node->getCondition()));
    builder_->CreateCondBr(condVal, bodyBB, exitBB);

    // Body
    builder_->SetInsertPoint(bodyBB);
    loopStack_.push_back({exitBB, condBB});
    visit(const_cast<ASTNode *>(node->getBody()));
    loopStack_.pop_back();
    if (!builder_->GetInsertBlock()->getTerminator())
        builder_->CreateBr(condBB);

    // Exit
    builder_->SetInsertPoint(exitBB);
    return nullptr;
}

llvm::Value *IRGen::visitForStmt(ForStmt *node) {
    if (diBuilder_) emitDebugLocation(node->getStartLoc());
    auto *func = builder_->GetInsertBlock()->getParent();

    // === Generator iteration: for x in <generator value> ===
    //
    // A generator function call returns a LivaTask* (handle wrapper) whose
    // coroutine is suspended at its first yield. To iterate we read the
    // current promise value, run the body, then coro.resume to advance to
    // the next yield. coro.done tells us when the coroutine has fallen off
    // the end and there are no more values.
    //
    // The iterable's resolved Sema type tells us if this is a generator —
    // GenericTypeRepr with base "Generator" and one type arg — regardless of
    // whether it was produced by a direct call (`for x in gen()`) or read
    // from a variable (`let g = gen(); for x in g`). Visiting the iterable
    // produces the LivaTask* handle wrapper either way.
    llvm::Value *genTask = nullptr;
    llvm::Type *genYieldType = nullptr;
    if (auto *iterTy = node->getIterable()->getResolvedType();
        iterTy && iterTy->getKind() == TypeRepr::Kind::Generic) {
        auto *gt = static_cast<const GenericTypeRepr *>(iterTy);
        if (gt->getBaseName() == "Generator" && gt->getTypeArgs().size() == 1) {
            genYieldType = toLLVMType(gt->getTypeArgs()[0].get());
            genTask = visit(const_cast<Expr *>(node->getIterable()));
            if (!genTask) return nullptr;
        }
    }

    if (genTask && genYieldType) {
        // Pull the coroutine handle out of the task wrapper.
        auto *getHandleFn = getOrPanic("liva_task_get_handle");
        auto *handle = builder_->CreateCall(getHandleFn, {genTask}, "gen.hdl");

        // Loop variable lives outside the loop body.
        auto *loopVar = createEntryBlockAlloca(func, node->getVarName(), genYieldType);
        vars_.namedValues[node->getVarName()] = loopVar;

        auto *condBB  = llvm::BasicBlock::Create(*context_, "gen.cond", func);
        auto *bodyBB  = llvm::BasicBlock::Create(*context_, "gen.body", func);
        auto *latchBB = llvm::BasicBlock::Create(*context_, "gen.latch", func);
        auto *exitBB  = llvm::BasicBlock::Create(*context_, "gen.exit", func);

        builder_->CreateBr(condBB);

        // cond: if coro.done(handle) → exit, else read promise + run body.
        builder_->SetInsertPoint(condBB);
        auto *coroDoneFn = llvm::Intrinsic::getOrInsertDeclaration(
            module_.get(), llvm::Intrinsic::coro_done);
        auto *isDone = builder_->CreateCall(coroDoneFn, {handle}, "gen.done");
        builder_->CreateCondBr(isDone, exitBB, bodyBB);

        // body: read promise → loopVar = *promise; run user body.
        builder_->SetInsertPoint(bodyBB);
        auto *coroPromiseFn = llvm::Intrinsic::getOrInsertDeclaration(
            module_.get(), llvm::Intrinsic::coro_promise);
        auto &dl = module_->getDataLayout();
        uint32_t promAlign = (uint32_t)dl.getABITypeAlign(genYieldType).value();
        auto *promisePtr = builder_->CreateCall(coroPromiseFn,
            {handle, builder_->getInt32(promAlign), builder_->getFalse()},
            "gen.promise.ptr");
        auto *value = builder_->CreateLoad(genYieldType, promisePtr, "gen.value");
        builder_->CreateStore(value, loopVar);

        loopStack_.push_back({exitBB, latchBB});
        visit(const_cast<ASTNode *>(node->getBody()));
        loopStack_.pop_back();
        if (!builder_->GetInsertBlock()->getTerminator())
            builder_->CreateBr(latchBB);

        // latch: coro.resume(handle) → goto cond.
        builder_->SetInsertPoint(latchBB);
        auto *coroResumeFn = llvm::Intrinsic::getOrInsertDeclaration(
            module_.get(), llvm::Intrinsic::coro_resume);
        builder_->CreateCall(coroResumeFn, {handle});
        builder_->CreateBr(condBB);

        // exit: tear the coroutine + wrapper task down.
        builder_->SetInsertPoint(exitBB);
        auto *coroDestroyFn = getOrPanic("liva_coro_destroy");
        builder_->CreateCall(coroDestroyFn, {handle});
        auto *taskDestroyFn = getOrPanic("liva_task_destroy");
        builder_->CreateCall(taskDestroyFn, {genTask});
        return nullptr;
    }

    // Check if iterable is a RangeExpr
    if (node->getIterable()->getKind() == ASTNode::NodeKind::RangeExpr) {
        auto *range = static_cast<RangeExpr *>(
            const_cast<Expr *>(node->getIterable()));

        // Evaluate start and end
        auto *startVal = visit(range->getStart());
        auto *endVal = visit(range->getEnd());
        if (!startVal || !endVal)
            return nullptr;

        // Choose loop variable integer type: use i64 if either bound is i64
        // (e.g. 0..arr.length where arr.length returns i64), otherwise i32.
        bool use64 = (endVal->getType()->isIntegerTy(64) ||
                      startVal->getType()->isIntegerTy(64));
        llvm::Type *loopTy = use64 ? builder_->getInt64Ty()
                                   : builder_->getInt32Ty();

        // Widen start/end to loopTy if needed
        if (use64 && startVal->getType()->isIntegerTy(32))
            startVal = builder_->CreateSExt(startVal, builder_->getInt64Ty(), "for.start.ext");
        if (use64 && endVal->getType()->isIntegerTy(32))
            endVal = builder_->CreateSExt(endVal, builder_->getInt64Ty(), "for.end.ext");

        // Create loop variable alloca
        auto *loopVar = createEntryBlockAlloca(func, node->getVarName(), loopTy);
        builder_->CreateStore(startVal, loopVar);
        vars_.namedValues[node->getVarName()] = loopVar;

        // Create basic blocks
        auto *condBB = llvm::BasicBlock::Create(*context_, "for.cond", func);
        auto *bodyBB = llvm::BasicBlock::Create(*context_, "for.body", func);
        auto *latchBB = llvm::BasicBlock::Create(*context_, "for.latch", func);
        auto *exitBB = llvm::BasicBlock::Create(*context_, "for.exit", func);

        builder_->CreateBr(condBB);

        // Condition: i < end (exclusive) or i <= end (inclusive)
        builder_->SetInsertPoint(condBB);
        auto *curVal = builder_->CreateLoad(loopTy, loopVar, node->getVarName());
        auto *cond = range->isInclusive()
                         ? builder_->CreateICmpSLE(curVal, endVal, "for.cmp")
                         : builder_->CreateICmpSLT(curVal, endVal, "for.cmp");
        builder_->CreateCondBr(cond, bodyBB, exitBB);

        // Body
        builder_->SetInsertPoint(bodyBB);
        loopStack_.push_back({exitBB, latchBB});
        visit(const_cast<ASTNode *>(node->getBody()));
        loopStack_.pop_back();
        if (!builder_->GetInsertBlock()->getTerminator())
            builder_->CreateBr(latchBB);

        // Latch: increment and loop back
        builder_->SetInsertPoint(latchBB);
        auto *cur = builder_->CreateLoad(loopTy, loopVar, node->getVarName());
        auto *one = use64 ? builder_->getInt64(1) : builder_->getInt32(1);
        auto *next = builder_->CreateAdd(cur, one, "for.inc");
        builder_->CreateStore(next, loopVar);
        builder_->CreateBr(condBB);

        // Exit
        builder_->SetInsertPoint(exitBB);
        return nullptr;
    }

    // Check if iterable is an identifier referencing a collection
    if (node->getIterable()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
        auto *ident = static_cast<IdentifierExpr *>(
            const_cast<Expr *>(node->getIterable()));
        const std::string &iterName = ident->getName();

        // === DynArray iteration: for item in arr ===
        auto daIt = vars_.varDynArrayTypes.find(iterName);
        if (daIt != vars_.varDynArrayTypes.end()) {
            auto *elemType = daIt->second.elementType;
            auto *structTy = getDynArrayStructTy();
            auto *arrAlloca = vars_.namedValues[iterName];

            // Load data pointer and length
            auto *dataField = builder_->CreateStructGEP(structTy, arrAlloca, 0, "da.data.ptr");
            auto *dataPtr = builder_->CreateLoad(builder_->getPtrTy(), dataField, "da.data");
            auto *lenField = builder_->CreateStructGEP(structTy, arrAlloca, 1, "da.len.ptr");
            auto *len = builder_->CreateLoad(builder_->getInt64Ty(), lenField, "da.len");

            // Index variable
            auto *idxVar = createEntryBlockAlloca(func, "for.idx", builder_->getInt64Ty());
            builder_->CreateStore(builder_->getInt64(0), idxVar);

            // Loop variable
            auto *loopVar = createEntryBlockAlloca(func, node->getVarName(), elemType);
            vars_.namedValues[node->getVarName()] = loopVar;

            // If the element type is a struct, register loop var in varStructTypes
            // so that body method calls like `row.getText(i)` can dispatch.
            for (auto &[sName, sTy] : structTypes_) {
                if (sTy == elemType) {
                    vars_.varStructTypes[node->getVarName()] = sName;
                    break;
                }
            }

            // If iterating [dyn Protocol], register loop var for dyn dispatch
            auto dapIt = vars_.varDynArrayProtocol.find(iterName);
            if (dapIt != vars_.varDynArrayProtocol.end()) {
                vars_.varProtocolTypes[node->getVarName()] = dapIt->second;
            }

            auto *condBB = llvm::BasicBlock::Create(*context_, "for.cond", func);
            auto *bodyBB = llvm::BasicBlock::Create(*context_, "for.body", func);
            auto *latchBB = llvm::BasicBlock::Create(*context_, "for.latch", func);
            auto *exitBB = llvm::BasicBlock::Create(*context_, "for.exit", func);

            builder_->CreateBr(condBB);

            // Condition: idx < len
            builder_->SetInsertPoint(condBB);
            auto *idx = builder_->CreateLoad(builder_->getInt64Ty(), idxVar, "idx");
            auto *cond = builder_->CreateICmpSLT(idx, len, "for.cmp");
            builder_->CreateCondBr(cond, bodyBB, exitBB);

            // Body: load element, store to loop var
            builder_->SetInsertPoint(bodyBB);
            auto *elemPtr = builder_->CreateGEP(elemType, dataPtr, idx, "elem.ptr");
            auto *elem = builder_->CreateLoad(elemType, elemPtr, "elem");
            builder_->CreateStore(elem, loopVar);

            loopStack_.push_back({exitBB, latchBB});
            visit(const_cast<ASTNode *>(node->getBody()));
            loopStack_.pop_back();
            if (!builder_->GetInsertBlock()->getTerminator())
                builder_->CreateBr(latchBB);

            // Latch: idx++
            builder_->SetInsertPoint(latchBB);
            auto *curIdx = builder_->CreateLoad(builder_->getInt64Ty(), idxVar, "idx");
            auto *nextIdx = builder_->CreateAdd(curIdx, builder_->getInt64(1), "idx.inc");
            builder_->CreateStore(nextIdx, idxVar);
            builder_->CreateBr(condBB);

            builder_->SetInsertPoint(exitBB);
            return nullptr;
        }

        // === Map iteration ===
        auto mapIt = vars_.varMapTypes.find(iterName);
        if (mapIt != vars_.varMapTypes.end()) {

            auto &info = mapIt->second;
            auto *structTy = getMapStructTy();
            auto *mapAlloca = vars_.namedValues[iterName];

            int64_t stride = 9 + (int64_t)info.keySize + (int64_t)info.valSize;

            // Load entries pointer and capacity
            auto *entriesField = builder_->CreateStructGEP(structTy, mapAlloca, 0, "map.entries.ptr");
            auto *entriesPtr = builder_->CreateLoad(builder_->getPtrTy(), entriesField, "map.entries");
            auto *capField = builder_->CreateStructGEP(structTy, mapAlloca, 2, "map.cap.ptr");
            auto *cap = builder_->CreateLoad(builder_->getInt64Ty(), capField, "map.cap");

            // Index variable
            auto *idxVar = createEntryBlockAlloca(func, "for.idx", builder_->getInt64Ty());
            builder_->CreateStore(builder_->getInt64(0), idxVar);

            // Loop variable(s)
            auto *keyVar = createEntryBlockAlloca(func, node->getVarName(), info.keyType);
            vars_.namedValues[node->getVarName()] = keyVar;

            llvm::AllocaInst *valVar = nullptr;
            if (node->hasTuplePattern()) {
                valVar = createEntryBlockAlloca(func, node->getVarName2(), info.valType);
                vars_.namedValues[node->getVarName2()] = valVar;
            }

            auto *condBB = llvm::BasicBlock::Create(*context_, "for.cond", func);
            auto *bodyBB = llvm::BasicBlock::Create(*context_, "for.body", func);
            auto *processBB = llvm::BasicBlock::Create(*context_, "for.process", func);
            auto *latchBB = llvm::BasicBlock::Create(*context_, "for.latch", func);
            auto *exitBB = llvm::BasicBlock::Create(*context_, "for.exit", func);

            builder_->CreateBr(condBB);

            // Condition: idx < capacity
            builder_->SetInsertPoint(condBB);
            auto *idx = builder_->CreateLoad(builder_->getInt64Ty(), idxVar, "idx");
            auto *cond = builder_->CreateICmpSLT(idx, cap, "for.cmp");
            builder_->CreateCondBr(cond, bodyBB, exitBB);

            // Body: check if entry is occupied (state == 1)
            builder_->SetInsertPoint(bodyBB);
            auto *offset = builder_->CreateMul(idx, builder_->getInt64(stride), "entry.offset");
            auto *entryPtr = builder_->CreateGEP(builder_->getInt8Ty(), entriesPtr, offset, "entry.ptr");
            auto *state = builder_->CreateLoad(builder_->getInt8Ty(), entryPtr, "entry.state");
            auto *isOccupied = builder_->CreateICmpEQ(state, builder_->getInt8(1), "is.occupied");
            builder_->CreateCondBr(isOccupied, processBB, latchBB);

            // Process: extract key (and optionally value)
            builder_->SetInsertPoint(processBB);
            auto *keyRaw = builder_->CreateGEP(builder_->getInt8Ty(), entryPtr,
                builder_->getInt64(9), "key.raw");
            auto *key = builder_->CreateLoad(info.keyType, keyRaw, "key");
            builder_->CreateStore(key, keyVar);

            if (node->hasTuplePattern() && valVar) {
                auto *valRaw = builder_->CreateGEP(builder_->getInt8Ty(), entryPtr,
                    builder_->getInt64(9 + (int64_t)info.keySize), "val.raw");
                auto *val = builder_->CreateLoad(info.valType, valRaw, "val");
                builder_->CreateStore(val, valVar);
            }

            loopStack_.push_back({exitBB, latchBB});
            visit(const_cast<ASTNode *>(node->getBody()));
            loopStack_.pop_back();
            if (!builder_->GetInsertBlock()->getTerminator())
                builder_->CreateBr(latchBB);

            // Latch: idx++
            builder_->SetInsertPoint(latchBB);
            auto *curIdx = builder_->CreateLoad(builder_->getInt64Ty(), idxVar, "idx");
            auto *nextIdx = builder_->CreateAdd(curIdx, builder_->getInt64(1), "idx.inc");
            builder_->CreateStore(nextIdx, idxVar);
            builder_->CreateBr(condBB);

            builder_->SetInsertPoint(exitBB);
            return nullptr;
        }

        // === Set iteration: for item in set ===
        auto setIt = vars_.varSetTypes.find(iterName);
        if (setIt != vars_.varSetTypes.end()) {
            auto &info = setIt->second;
            auto *structTy = getMapStructTy();  // Set uses same struct as Map
            auto *setAlloca = vars_.namedValues[iterName];

            int64_t stride = 9 + (int64_t)info.elemSize;  // val_size=0

            // Load entries pointer and capacity
            auto *entriesField = builder_->CreateStructGEP(structTy, setAlloca, 0, "set.entries.ptr");
            auto *entriesPtr = builder_->CreateLoad(builder_->getPtrTy(), entriesField, "set.entries");
            auto *capField = builder_->CreateStructGEP(structTy, setAlloca, 2, "set.cap.ptr");
            auto *cap = builder_->CreateLoad(builder_->getInt64Ty(), capField, "set.cap");

            // Index variable
            auto *idxVar = createEntryBlockAlloca(func, "for.idx", builder_->getInt64Ty());
            builder_->CreateStore(builder_->getInt64(0), idxVar);

            // Loop variable
            auto *loopVar = createEntryBlockAlloca(func, node->getVarName(), info.elemType);
            vars_.namedValues[node->getVarName()] = loopVar;

            auto *condBB = llvm::BasicBlock::Create(*context_, "for.cond", func);
            auto *bodyBB = llvm::BasicBlock::Create(*context_, "for.body", func);
            auto *processBB = llvm::BasicBlock::Create(*context_, "for.process", func);
            auto *latchBB = llvm::BasicBlock::Create(*context_, "for.latch", func);
            auto *exitBB = llvm::BasicBlock::Create(*context_, "for.exit", func);

            builder_->CreateBr(condBB);

            // Condition: idx < capacity
            builder_->SetInsertPoint(condBB);
            auto *idx = builder_->CreateLoad(builder_->getInt64Ty(), idxVar, "idx");
            auto *cond = builder_->CreateICmpSLT(idx, cap, "for.cmp");
            builder_->CreateCondBr(cond, bodyBB, exitBB);

            // Body: check if entry is occupied (state == 1)
            builder_->SetInsertPoint(bodyBB);
            auto *offset = builder_->CreateMul(idx, builder_->getInt64(stride), "entry.offset");
            auto *entryPtr = builder_->CreateGEP(builder_->getInt8Ty(), entriesPtr, offset, "entry.ptr");
            auto *state = builder_->CreateLoad(builder_->getInt8Ty(), entryPtr, "entry.state");
            auto *isOccupied = builder_->CreateICmpEQ(state, builder_->getInt8(1), "is.occupied");
            builder_->CreateCondBr(isOccupied, processBB, latchBB);

            // Process: extract element
            builder_->SetInsertPoint(processBB);
            auto *elemRaw = builder_->CreateGEP(builder_->getInt8Ty(), entryPtr,
                builder_->getInt64(9), "elem.raw");
            auto *elem = builder_->CreateLoad(info.elemType, elemRaw, "elem");
            builder_->CreateStore(elem, loopVar);

            loopStack_.push_back({exitBB, latchBB});
            visit(const_cast<ASTNode *>(node->getBody()));
            loopStack_.pop_back();
            if (!builder_->GetInsertBlock()->getTerminator())
                builder_->CreateBr(latchBB);

            // Latch: idx++
            builder_->SetInsertPoint(latchBB);
            auto *curIdx = builder_->CreateLoad(builder_->getInt64Ty(), idxVar, "idx");
            auto *nextIdx = builder_->CreateAdd(curIdx, builder_->getInt64(1), "idx.inc");
            builder_->CreateStore(nextIdx, idxVar);
            builder_->CreateBr(condBB);

            builder_->SetInsertPoint(exitBB);
            return nullptr;
        }

        // === Custom AsyncIterator protocol iteration: for await x in asyncIter ===
        // Handles `for await x in expr` where the iterable's concrete struct type
        // conforms to the AsyncIterator protocol.  next() may be sync (returning
        // Optional<T> directly) — the "await" semantics are at the caller level.
        // This branch must come BEFORE the sync-Iterator branch so that types
        // that implement AsyncIterator are dispatched here rather than falling
        // through to the sync-Iterator fallback path.
        if (node->isAwait()) {
            auto asyncStructIt = vars_.varStructTypes.find(iterName);
            if (asyncStructIt != vars_.varStructTypes.end()) {
                const std::string &concreteType = asyncStructIt->second;

                // Resolve next() via AsyncIterator protocol registry.
                llvm::Function *nextFunc = nullptr;
                auto pcIt = protocolConformances_.find("AsyncIterator");
                auto pmIt = protocolMethodNames_.find("AsyncIterator");
                if (pcIt != protocolConformances_.end() && pmIt != protocolMethodNames_.end()
                        && !pmIt->second.empty()) {
                    auto &conformers = pcIt->second;
                    bool conforms = (std::find(conformers.begin(), conformers.end(),
                                               concreteType) != conformers.end());
                    if (conforms) {
                        const std::string &methodName = pmIt->second[0]; // "next"
                        std::string mangledName = concreteType + "_" + methodName;
                        nextFunc = module_->getFunction(mangledName);
                    }
                }
                // Fallback: direct lookup for types compiled before the
                // protocol decl was visited.
                if (!nextFunc)
                    nextFunc = module_->getFunction(concreteType + "_next");

                if (nextFunc) {
                    auto *iterAlloca = vars_.namedValues[iterName];

                    // next() returns Optional<T> = {i1, T}
                    auto *retType = nextFunc->getReturnType();
                    auto *optStructTy = llvm::cast<llvm::StructType>(retType);
                    auto *elemType = optStructTy->getElementType(1);

                    auto *loopVar = createEntryBlockAlloca(func, node->getVarName(), elemType);
                    vars_.namedValues[node->getVarName()] = loopVar;

                    auto *condBB = llvm::BasicBlock::Create(*context_, "forawait.cond", func);
                    auto *bodyBB = llvm::BasicBlock::Create(*context_, "forawait.body", func);
                    auto *exitBB = llvm::BasicBlock::Create(*context_, "forawait.exit", func);

                    builder_->CreateBr(condBB);

                    // Condition: call next() (devirtualized), check Optional hasValue.
                    builder_->SetInsertPoint(condBB);
                    auto *optVal = builder_->CreateCall(nextFunc, {iterAlloca}, "ai.next");
                    auto *hasVal = builder_->CreateExtractValue(optVal, {0}, "ai.has");
                    builder_->CreateCondBr(hasVal, bodyBB, exitBB);

                    // Body: extract value, store to loop var.
                    builder_->SetInsertPoint(bodyBB);
                    auto *elemVal = builder_->CreateExtractValue(optVal, {1}, "ai.val");
                    builder_->CreateStore(elemVal, loopVar);

                    loopStack_.push_back({exitBB, condBB});
                    visit(const_cast<ASTNode *>(node->getBody()));
                    loopStack_.pop_back();
                    if (!builder_->GetInsertBlock()->getTerminator())
                        builder_->CreateBr(condBB);

                    builder_->SetInsertPoint(exitBB);
                    return nullptr;
                }
            }
        }

        // === Custom Iterator protocol iteration ===
        // Dispatch for-in via protocol-method lookup rather than hardcoded name
        // mangling. Works for any struct that declares `impl T: Iterator`.
        auto structIt = vars_.varStructTypes.find(iterName);
        if (structIt != vars_.varStructTypes.end()) {
            const std::string &concreteType = structIt->second;

            // Verify conformance via protocol registry and resolve method name.
            // protocolMethodNames_["Iterator"][0] is the first (and only) method
            // of the Iterator protocol ("next"), obtained from the protocol decl
            // rather than hardcoded as "_next".
            llvm::Function *nextFunc = nullptr;
            auto pcIt = protocolConformances_.find("Iterator");
            auto pmIt = protocolMethodNames_.find("Iterator");
            if (pcIt != protocolConformances_.end() && pmIt != protocolMethodNames_.end()
                    && !pmIt->second.empty()) {
                // Confirm this concrete type actually conforms to Iterator.
                auto &conformers = pcIt->second;
                bool conforms = (std::find(conformers.begin(), conformers.end(),
                                           concreteType) != conformers.end());
                if (conforms) {
                    // Devirtualize: dispatch directly to the concrete method.
                    const std::string &methodName = pmIt->second[0]; // "next"
                    std::string mangledName = concreteType + "_" + methodName;
                    nextFunc = module_->getFunction(mangledName);
                }
            }
            // Fallback: legacy direct lookup (handles local-only Iterator impls
            // that were compiled before the protocol decl was visited).
            if (!nextFunc)
                nextFunc = module_->getFunction(concreteType + "_next");

            if (nextFunc) {
                auto *iterAlloca = vars_.namedValues[iterName];

                // next() returns Optional<T> = {i1, T}
                auto *retType = nextFunc->getReturnType();
                auto *optStructTy = llvm::cast<llvm::StructType>(retType);
                auto *elemType = optStructTy->getElementType(1);

                auto *loopVar = createEntryBlockAlloca(func, node->getVarName(), elemType);
                vars_.namedValues[node->getVarName()] = loopVar;

                auto *condBB = llvm::BasicBlock::Create(*context_, "iter.cond", func);
                auto *bodyBB = llvm::BasicBlock::Create(*context_, "iter.body", func);
                auto *exitBB = llvm::BasicBlock::Create(*context_, "iter.exit", func);

                builder_->CreateBr(condBB);

                // Condition: call next() via devirtualized protocol dispatch,
                // then check the Optional hasValue flag.
                builder_->SetInsertPoint(condBB);
                auto *optVal = builder_->CreateCall(nextFunc, {iterAlloca}, "iter.next");
                auto *hasVal = builder_->CreateExtractValue(optVal, {0}, "iter.has");
                builder_->CreateCondBr(hasVal, bodyBB, exitBB);

                // Body: extract value, store to loop var
                builder_->SetInsertPoint(bodyBB);
                auto *elemVal = builder_->CreateExtractValue(optVal, {1}, "iter.val");
                builder_->CreateStore(elemVal, loopVar);

                loopStack_.push_back({exitBB, condBB});
                visit(const_cast<ASTNode *>(node->getBody()));
                loopStack_.pop_back();
                if (!builder_->GetInsertBlock()->getTerminator())
                    builder_->CreateBr(condBB);

                builder_->SetInsertPoint(exitBB);
                return nullptr;
            }
        }
    }

    // === DynArray struct member iteration: for g in self.grades ===
    if (node->getIterable()->getKind() == ASTNode::NodeKind::MemberExpr) {
        auto *memberIterable = static_cast<MemberExpr *>(
            const_cast<Expr *>(node->getIterable()));
        auto daInfo = resolveMemberDynArray(memberIterable);
        if (daInfo) {
            auto *elemType = daInfo->elementType;
            auto *structTy = getDynArrayStructTy();
            auto *arrGEP = daInfo->arrGEP;

            auto *dataField = builder_->CreateStructGEP(structTy, arrGEP, 0, "mda.data.ptr");
            auto *dataPtr = builder_->CreateLoad(builder_->getPtrTy(), dataField, "mda.data");
            auto *lenField = builder_->CreateStructGEP(structTy, arrGEP, 1, "mda.len.ptr");
            auto *len = builder_->CreateLoad(builder_->getInt64Ty(), lenField, "mda.len");

            auto *idxVar = createEntryBlockAlloca(func, "for.idx", builder_->getInt64Ty());
            builder_->CreateStore(builder_->getInt64(0), idxVar);

            auto *loopVar = createEntryBlockAlloca(func, node->getVarName(), elemType);
            vars_.namedValues[node->getVarName()] = loopVar;

            auto *condBB = llvm::BasicBlock::Create(*context_, "for.cond", func);
            auto *bodyBB = llvm::BasicBlock::Create(*context_, "for.body", func);
            auto *latchBB = llvm::BasicBlock::Create(*context_, "for.latch", func);
            auto *exitBB = llvm::BasicBlock::Create(*context_, "for.exit", func);

            builder_->CreateBr(condBB);

            builder_->SetInsertPoint(condBB);
            auto *idx = builder_->CreateLoad(builder_->getInt64Ty(), idxVar, "idx");
            auto *cond = builder_->CreateICmpSLT(idx, len, "for.cmp");
            builder_->CreateCondBr(cond, bodyBB, exitBB);

            builder_->SetInsertPoint(bodyBB);
            auto *elemPtr = builder_->CreateGEP(elemType, dataPtr, idx, "mda.elem.ptr");
            auto *elem = builder_->CreateLoad(elemType, elemPtr, "mda.elem");
            builder_->CreateStore(elem, loopVar);

            loopStack_.push_back({exitBB, latchBB});
            visit(const_cast<ASTNode *>(node->getBody()));
            loopStack_.pop_back();
            if (!builder_->GetInsertBlock()->getTerminator())
                builder_->CreateBr(latchBB);

            builder_->SetInsertPoint(latchBB);
            auto *curIdx = builder_->CreateLoad(builder_->getInt64Ty(), idxVar, "idx");
            auto *nextIdx = builder_->CreateAdd(curIdx, builder_->getInt64(1), "idx.inc");
            builder_->CreateStore(nextIdx, idxVar);
            builder_->CreateBr(condBB);

            builder_->SetInsertPoint(exitBB);
            return nullptr;
        }
    }

    // Fallback: just emit body once
    visit(const_cast<ASTNode *>(node->getBody()));
    return nullptr;
}

llvm::Value *IRGen::visitBreakStmt(BreakStmt *node) {
    if (diBuilder_) emitDebugLocation(node->getStartLoc());
    if (!loopStack_.empty())
        builder_->CreateBr(loopStack_.back().breakBB);
    return nullptr;
}

llvm::Value *IRGen::visitContinueStmt(ContinueStmt *node) {
    if (diBuilder_) emitDebugLocation(node->getStartLoc());
    if (!loopStack_.empty())
        builder_->CreateBr(loopStack_.back().continueBB);
    return nullptr;
}

llvm::Value *IRGen::visitExprStmt(ExprStmt *node) {
    if (diBuilder_) emitDebugLocation(node->getStartLoc());
    return visit(node->getExpr());
}

} // namespace liva

#endif // LIVA_HAS_LLVM
