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
    if (tempStrings_.empty()) return;
    auto *freeFn = module_->getFunction("free");
    if (!freeFn) return;
    for (auto *val : tempStrings_) {
        builder_->CreateCall(freeFn, {val});
    }
    tempStrings_.clear();
}

void IRGen::emitScopeCleanup() {
    auto *ptrTy = llvm::PointerType::getUnqual(*context_);

    // Free DynArrays
    for (auto &[name, info] : varDynArrayTypes_) {
        if (movedVars_.count(name)) continue;
        auto it = namedValues_.find(name);
        if (it == namedValues_.end()) continue;
        auto *structAlloca = it->second;
        auto *dataGEP = builder_->CreateStructGEP(getDynArrayStructTy(), structAlloca, 0,
                                                   name + ".data.drop");
        auto *dataPtr = builder_->CreateLoad(ptrTy, dataGEP, name + ".ptr.drop");
        builder_->CreateCall(getOrPanic("liva_array_free"), {dataPtr});
    }

    // Free Maps
    for (auto &[name, info] : varMapTypes_) {
        if (movedVars_.count(name)) continue;
        auto it = namedValues_.find(name);
        if (it == namedValues_.end()) continue;
        auto *structAlloca = it->second;
        auto *entriesGEP = builder_->CreateStructGEP(getMapStructTy(), structAlloca, 0,
                                                      name + ".entries.drop");
        auto *entriesPtr = builder_->CreateLoad(ptrTy, entriesGEP, name + ".ptr.drop");
        builder_->CreateCall(getOrPanic("liva_map_free"), {entriesPtr});
    }

    // Free Sets
    for (auto &[name, info] : varSetTypes_) {
        if (movedVars_.count(name)) continue;
        auto it = namedValues_.find(name);
        if (it == namedValues_.end()) continue;
        auto *structAlloca = it->second;
        auto *entriesGEP = builder_->CreateStructGEP(getMapStructTy(), structAlloca, 0,
                                                      name + ".entries.drop");
        auto *entriesPtr = builder_->CreateLoad(ptrTy, entriesGEP, name + ".ptr.drop");
        builder_->CreateCall(getOrPanic("liva_set_free"), {entriesPtr});
    }

    // Call drop() for struct variables implementing Drop protocol,
    // or auto-cleanup heap fields for structs without Drop
    for (auto &[name, structTypeName] : varStructTypes_) {
        if (name == "self") continue; // self is borrowed (ref/ref mut), not owned
        if (movedVars_.count(name)) continue;
        if (dropImplementors_.count(structTypeName)) {
            auto it = namedValues_.find(name);
            if (it == namedValues_.end()) continue;

            std::string dropFnName = structTypeName + "_drop";
            auto *dropFn = module_->getFunction(dropFnName);
            if (!dropFn) continue;

            builder_->CreateCall(dropFn, {it->second});
        } else {
            // Auto-cleanup heap fields for structs without Drop
            emitStructFieldCleanup(name, structTypeName);
        }
    }

    // Call deinit for class instances (virtual dispatch through vtable)
    for (auto &[name, clsTypeName] : varClassTypes_) {
        if (name == "self") continue; // self is not owned by current scope
        if (movedVars_.count(name)) continue;
        auto it = namedValues_.find(name);
        if (it == namedValues_.end()) continue;

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
        for (auto &name : heapStringVars_) {
            if (movedVars_.count(name)) continue;
            auto it = namedValues_.find(name);
            if (it == namedValues_.end()) continue;
            auto *strPtr = builder_->CreateLoad(ptrTy, it->second, name + ".str.drop");
            builder_->CreateCall(freeFn, {strPtr});
        }
        // Conditionally free Optional<string> inner ptrs (only if hasVal).
        for (auto &name : heapOptionalStringVars_) {
            if (movedVars_.count(name)) continue;
            auto it = namedValues_.find(name);
            if (it == namedValues_.end()) continue;
            auto optIt = varOptionalTypes_.find(name);
            if (optIt == varOptionalTypes_.end()) continue;
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
    auto namedIt = namedValues_.find(varName);
    if (namedIt == namedValues_.end()) return;

    auto *structAlloca = namedIt->second;
    auto *structTy = stIt->second;
    const auto &fieldTypeReprs = ftrIt->second;
    const auto &fieldNames = fnIt->second;

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
        // If returning a temp string, remove from tempStrings_
        auto tIt = std::find(tempStrings_.begin(), tempStrings_.end(), val);
        if (tIt != tempStrings_.end()) tempStrings_.erase(tIt);
        // If returning a heap string variable, remove from heapStringVars_
        // Same for Optional<string> variables that own their inner pointer:
        // we hand the storage to the caller, so we must NOT free here.
        if (node->getValue()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
            auto *retIdent = static_cast<IdentifierExpr *>(node->getValue());
            heapStringVars_.erase(retIdent->getName());
            heapOptionalStringVars_.erase(retIdent->getName());
        }
        // When the return expression wraps a temp string in Optional<string>
        // (e.g. `return jsonGet(...)`), the raw pointer is still in
        // tempStrings_ but is now logically owned by the Optional we return.
        // Clear the list to avoid emitting `free(raw)` *after* our ret in the
        // surrounding visitBlockStmt's per-statement cleanup pass.
        if (currentFuncOptionalInner_ && val &&
            val->getType() == getOptionalType(currentFuncOptionalInner_)) {
            tempStrings_.clear();
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
        auto it = namedValues_.find(ident->getName());
        if (it != namedValues_.end()) optAlloca = it->second;
        auto optIt = varOptionalTypes_.find(ident->getName());
        if (optIt != varOptionalTypes_.end()) innerType = optIt->second;
    }
    // Try to recover innerType from the alloca's struct type when varOptionalTypes_ miss
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
    auto saved = namedValues_;
    auto savedFileTypes = varFileTypes_;
    namedValues_[node->getBindingName()] = bindAlloca;
    // If the optional expression was File.open, the unwrapped value is a File ptr
    if (node->getOptionalExpr()->getKind() == ASTNode::NodeKind::CallExpr) {
        auto *callExpr = static_cast<CallExpr *>(node->getOptionalExpr());
        if (callExpr->getCallee()->getKind() == ASTNode::NodeKind::MemberExpr) {
            auto *me = static_cast<MemberExpr *>(callExpr->getCallee());
            if (me->getObject()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
                auto *id = static_cast<IdentifierExpr *>(me->getObject());
                if (id->getName() == "File" && me->getMember() == "open") {
                    varFileTypes_.insert(node->getBindingName());
                }
            }
        }
    }
    // Also check if the optional source was a File-optional variable
    if (node->getOptionalExpr()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
        auto *ident = static_cast<IdentifierExpr *>(node->getOptionalExpr());
        if (varFileOptionalTypes_.count(ident->getName())) {
            varFileTypes_.insert(node->getBindingName());
        }
    }
    visit(node->getThenBody());
    namedValues_ = saved;
    varFileTypes_ = savedFileTypes;
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
        auto it = namedValues_.find(ident->getName());
        if (it != namedValues_.end()) optAlloca = it->second;
        auto optIt = varOptionalTypes_.find(ident->getName());
        if (optIt != varOptionalTypes_.end()) innerType = optIt->second;
    }
    if (!optAlloca || !innerType) {
        diag_.report(node->getStartLoc(), DiagID::err_irgen_while_let_not_optional);
        return nullptr;
    }

    auto *func = builder_->GetInsertBlock()->getParent();
    auto *optStructTy = getOptionalType(innerType);

    auto *condBB = llvm::BasicBlock::Create(*context_, "whilelet.cond", func);
    auto *bodyBB = llvm::BasicBlock::Create(*context_, "whilelet.body", func);
    auto *exitBB = llvm::BasicBlock::Create(*context_, "whilelet.exit", func);

    builder_->CreateBr(condBB);

    // Cond: check hasVal
    builder_->SetInsertPoint(condBB);
    auto *hasValPtr = builder_->CreateStructGEP(optStructTy, optAlloca, 0);
    auto *hasVal = builder_->CreateLoad(builder_->getInt1Ty(), hasValPtr, "whilelet.hasval");
    builder_->CreateCondBr(hasVal, bodyBB, exitBB);

    // Body: unwrap + bind + execute body
    builder_->SetInsertPoint(bodyBB);
    auto *valPtr = builder_->CreateStructGEP(optStructTy, optAlloca, 1);
    auto *unwrapped = builder_->CreateLoad(innerType, valPtr, "whilelet.val");
    auto *bindAlloca = createEntryBlockAlloca(func, node->getBindingName(), innerType);
    builder_->CreateStore(unwrapped, bindAlloca);

    auto saved = namedValues_;
    namedValues_[node->getBindingName()] = bindAlloca;
    loopStack_.push_back({exitBB, condBB});
    visit(node->getBody());
    loopStack_.pop_back();
    namedValues_ = saved;
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

    // Check if iterable is a RangeExpr
    if (node->getIterable()->getKind() == ASTNode::NodeKind::RangeExpr) {
        auto *range = static_cast<RangeExpr *>(
            const_cast<Expr *>(node->getIterable()));

        // Evaluate start and end
        auto *startVal = visit(range->getStart());
        auto *endVal = visit(range->getEnd());
        if (!startVal || !endVal)
            return nullptr;

        // Create loop variable alloca
        auto *loopVar = createEntryBlockAlloca(func, node->getVarName(),
                                                builder_->getInt32Ty());
        builder_->CreateStore(startVal, loopVar);
        namedValues_[node->getVarName()] = loopVar;

        // Create basic blocks
        auto *condBB = llvm::BasicBlock::Create(*context_, "for.cond", func);
        auto *bodyBB = llvm::BasicBlock::Create(*context_, "for.body", func);
        auto *latchBB = llvm::BasicBlock::Create(*context_, "for.latch", func);
        auto *exitBB = llvm::BasicBlock::Create(*context_, "for.exit", func);

        builder_->CreateBr(condBB);

        // Condition: i < end (exclusive) or i <= end (inclusive)
        builder_->SetInsertPoint(condBB);
        auto *curVal = builder_->CreateLoad(builder_->getInt32Ty(), loopVar,
                                             node->getVarName());
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
        auto *cur = builder_->CreateLoad(builder_->getInt32Ty(), loopVar,
                                          node->getVarName());
        auto *next = builder_->CreateAdd(cur, builder_->getInt32(1), "for.inc");
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
        auto daIt = varDynArrayTypes_.find(iterName);
        if (daIt != varDynArrayTypes_.end()) {
            auto *elemType = daIt->second.elementType;
            auto *structTy = getDynArrayStructTy();
            auto *arrAlloca = namedValues_[iterName];

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
            namedValues_[node->getVarName()] = loopVar;

            // If iterating [dyn Protocol], register loop var for dyn dispatch
            auto dapIt = varDynArrayProtocol_.find(iterName);
            if (dapIt != varDynArrayProtocol_.end()) {
                varProtocolTypes_[node->getVarName()] = dapIt->second;
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
        auto mapIt = varMapTypes_.find(iterName);
        if (mapIt != varMapTypes_.end()) {

            auto &info = mapIt->second;
            auto *structTy = getMapStructTy();
            auto *mapAlloca = namedValues_[iterName];

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
            namedValues_[node->getVarName()] = keyVar;

            llvm::AllocaInst *valVar = nullptr;
            if (node->hasTuplePattern()) {
                valVar = createEntryBlockAlloca(func, node->getVarName2(), info.valType);
                namedValues_[node->getVarName2()] = valVar;
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
        auto setIt = varSetTypes_.find(iterName);
        if (setIt != varSetTypes_.end()) {
            auto &info = setIt->second;
            auto *structTy = getMapStructTy();  // Set uses same struct as Map
            auto *setAlloca = namedValues_[iterName];

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
            namedValues_[node->getVarName()] = loopVar;

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

        // === Custom Iterator (Iter protocol) iteration ===
        auto structIt = varStructTypes_.find(iterName);
        if (structIt != varStructTypes_.end()) {
            std::string nextFn = structIt->second + "_next";
            auto *nextFunc = module_->getFunction(nextFn);
            if (nextFunc) {
                auto *iterAlloca = namedValues_[iterName];

                // next() returns Optional<T> = {i1, T}
                auto *retType = nextFunc->getReturnType();
                auto *optStructTy = llvm::cast<llvm::StructType>(retType);
                auto *elemType = optStructTy->getElementType(1);

                auto *loopVar = createEntryBlockAlloca(func, node->getVarName(), elemType);
                namedValues_[node->getVarName()] = loopVar;

                auto *condBB = llvm::BasicBlock::Create(*context_, "iter.cond", func);
                auto *bodyBB = llvm::BasicBlock::Create(*context_, "iter.body", func);
                auto *exitBB = llvm::BasicBlock::Create(*context_, "iter.exit", func);

                builder_->CreateBr(condBB);

                // Condition: call next(), check hasValue
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
            namedValues_[node->getVarName()] = loopVar;

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
