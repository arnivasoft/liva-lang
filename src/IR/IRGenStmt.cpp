#include "liva/IR/IRGen.h"

#ifdef LIVA_HAS_LLVM

namespace liva {

llvm::Value *IRGen::visitBlockStmt(BlockStmt *node) {
    llvm::Value *last = nullptr;
    for (auto &stmt : node->getStatements()) {
        last = visit(stmt.get());
        // Stop if we hit a terminator
        if (builder_->GetInsertBlock()->getTerminator())
            break;
    }
    return last;
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
        builder_->CreateCall(module_->getFunction("liva_array_free"), {dataPtr});
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
        builder_->CreateCall(module_->getFunction("liva_map_free"), {entriesPtr});
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
        builder_->CreateCall(module_->getFunction("liva_set_free"), {entriesPtr});
    }
}

llvm::Value *IRGen::visitReturnStmt(ReturnStmt *node) {
    if (node->hasValue()) {
        auto *val = visit(node->getValue());
        emitScopeCleanup();
        return builder_->CreateRet(val);
    }
    emitScopeCleanup();
    return builder_->CreateRetVoid();
}

llvm::Value *IRGen::visitIfStmt(IfStmt *node) {
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
    llvm::AllocaInst *optAlloca = nullptr;
    llvm::Type *innerType = nullptr;
    if (node->getOptionalExpr()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
        auto *ident = static_cast<IdentifierExpr *>(node->getOptionalExpr());
        auto it = namedValues_.find(ident->getName());
        if (it != namedValues_.end()) optAlloca = it->second;
        auto optIt = varOptionalTypes_.find(ident->getName());
        if (optIt != varOptionalTypes_.end()) innerType = optIt->second;
    }
    if (!optAlloca || !innerType) return nullptr;

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
    if (!optAlloca || !innerType) return nullptr;

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

        // Condition: i < end
        builder_->SetInsertPoint(condBB);
        auto *curVal = builder_->CreateLoad(builder_->getInt32Ty(), loopVar,
                                             node->getVarName());
        auto *cond = builder_->CreateICmpSLT(curVal, endVal, "for.cmp");
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
    }

    // Fallback: just emit body once
    visit(const_cast<ASTNode *>(node->getBody()));
    return nullptr;
}

llvm::Value *IRGen::visitBreakStmt(BreakStmt *) {
    if (!loopStack_.empty())
        builder_->CreateBr(loopStack_.back().breakBB);
    return nullptr;
}

llvm::Value *IRGen::visitContinueStmt(ContinueStmt *) {
    if (!loopStack_.empty())
        builder_->CreateBr(loopStack_.back().continueBB);
    return nullptr;
}

llvm::Value *IRGen::visitExprStmt(ExprStmt *node) {
    return visit(node->getExpr());
}

} // namespace liva

#endif // LIVA_HAS_LLVM
