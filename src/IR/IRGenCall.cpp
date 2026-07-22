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

    // === Stdlib: Synchronization ===

    // Helper: ensure an integer-typed value is i64 (sign-extend from smaller widths).
    // Liva integer literals default to i32, but most stdlib runtime functions expect i64.
    auto toI64 = [&](llvm::Value *v) -> llvm::Value * {
        if (!v) return v;
        auto *t = v->getType();
        if (t->isIntegerTy() && !t->isIntegerTy(64))
            return builder_->CreateSExt(v, builder_->getInt64Ty(), "i64.coerce");
        return v;
    };

    // mutexCreate() -> i64
    if (funcName == "mutexCreate" && node->getArgs().empty()) {
        auto *fn = getOrPanic("liva_mutex_create");
        return builder_->CreateCall(fn, {}, "mutex.create");
    }

    // mutexLock(handle) -> void
    if (funcName == "mutexLock" && !node->getArgs().empty()) {
        auto *handleArg = visit(node->getArgs()[0].get());
        if (!handleArg) return nullptr;
        auto *fn = getOrPanic("liva_mutex_lock");
        builder_->CreateCall(fn, {handleArg});
        return llvm::Constant::getNullValue(builder_->getInt64Ty());
    }

    // mutexUnlock(handle) -> void
    if (funcName == "mutexUnlock" && !node->getArgs().empty()) {
        auto *handleArg = visit(node->getArgs()[0].get());
        if (!handleArg) return nullptr;
        auto *fn = getOrPanic("liva_mutex_unlock");
        builder_->CreateCall(fn, {handleArg});
        return llvm::Constant::getNullValue(builder_->getInt64Ty());
    }

    // mutexTryLock(handle) -> bool
    if (funcName == "mutexTryLock" && !node->getArgs().empty()) {
        auto *handleArg = visit(node->getArgs()[0].get());
        if (!handleArg) return nullptr;
        auto *fn = getOrPanic("liva_mutex_try_lock");
        auto *result = builder_->CreateCall(fn, {handleArg}, "mutex.trylock");
        return builder_->CreateTrunc(result, builder_->getInt1Ty(), "mutex.trylock.bool");
    }

    // mutexFree(handle) -> void
    if (funcName == "mutexFree" && !node->getArgs().empty()) {
        auto *handleArg = visit(node->getArgs()[0].get());
        if (!handleArg) return nullptr;
        auto *fn = getOrPanic("liva_mutex_free");
        builder_->CreateCall(fn, {handleArg});
        return llvm::Constant::getNullValue(builder_->getInt64Ty());
    }

    // === Stdlib: RWLock ===

    // rwlockCreate() -> i64
    if (funcName == "rwlockCreate" && node->getArgs().empty()) {
        auto *fn = getOrPanic("liva_rwlock_create");
        return builder_->CreateCall(fn, {}, "rwlock.create");
    }

    // rwlockReadLock/ReadUnlock/WriteLock/WriteUnlock/Free(handle) -> void
    static const struct { const char *liva; const char *runtime; } kRwlockVoid[] = {
        {"rwlockReadLock", "liva_rwlock_read_lock"},
        {"rwlockReadUnlock", "liva_rwlock_read_unlock"},
        {"rwlockWriteLock", "liva_rwlock_write_lock"},
        {"rwlockWriteUnlock", "liva_rwlock_write_unlock"},
        {"rwlockFree", "liva_rwlock_free"},
    };
    for (auto &m : kRwlockVoid) {
        if (funcName == m.liva && !node->getArgs().empty()) {
            auto *handleArg = visit(node->getArgs()[0].get());
            if (!handleArg) return nullptr;
            auto *fn = getOrPanic(m.runtime);
            builder_->CreateCall(fn, {handleArg});
            return llvm::Constant::getNullValue(builder_->getInt64Ty());
        }
    }

    // rwlockTryReadLock/TryWriteLock(handle) -> bool
    static const struct { const char *liva; const char *runtime; } kRwlockBool[] = {
        {"rwlockTryReadLock", "liva_rwlock_try_read_lock"},
        {"rwlockTryWriteLock", "liva_rwlock_try_write_lock"},
    };
    for (auto &m : kRwlockBool) {
        if (funcName == m.liva && !node->getArgs().empty()) {
            auto *handleArg = visit(node->getArgs()[0].get());
            if (!handleArg) return nullptr;
            auto *fn = getOrPanic(m.runtime);
            auto *result = builder_->CreateCall(fn, {handleArg}, "rwlock.try");
            return builder_->CreateTrunc(result, builder_->getInt1Ty(), "rwlock.try.bool");
        }
    }

    // === Stdlib: ConditionVariable ===

    // condVarCreate() -> i64
    if (funcName == "condVarCreate" && node->getArgs().empty()) {
        auto *fn = getOrPanic("liva_condvar_create");
        return builder_->CreateCall(fn, {}, "condvar.create");
    }

    // condVarWait(cv, mtx) -> void
    if (funcName == "condVarWait" && node->getArgs().size() >= 2) {
        auto *cvArg = visit(node->getArgs()[0].get());
        auto *mtxArg = visit(node->getArgs()[1].get());
        if (!cvArg || !mtxArg) return nullptr;
        auto *fn = getOrPanic("liva_condvar_wait");
        builder_->CreateCall(fn, {cvArg, mtxArg});
        return llvm::Constant::getNullValue(builder_->getInt64Ty());
    }

    // condVarNotifyOne/NotifyAll/Free(handle) -> void
    static const struct { const char *liva; const char *runtime; } kCondVoid[] = {
        {"condVarNotifyOne", "liva_condvar_notify_one"},
        {"condVarNotifyAll", "liva_condvar_notify_all"},
        {"condVarFree", "liva_condvar_free"},
    };
    for (auto &m : kCondVoid) {
        if (funcName == m.liva && !node->getArgs().empty()) {
            auto *handleArg = visit(node->getArgs()[0].get());
            if (!handleArg) return nullptr;
            auto *fn = getOrPanic(m.runtime);
            builder_->CreateCall(fn, {handleArg});
            return llvm::Constant::getNullValue(builder_->getInt64Ty());
        }
    }

    // atomicCreate(initial) -> i64
    if (funcName == "atomicCreate" && !node->getArgs().empty()) {
        auto *initArg = visit(node->getArgs()[0].get());
        if (!initArg) return nullptr;
        auto *fn = getOrPanic("liva_atomic_create");
        return builder_->CreateCall(fn, {toI64(initArg)}, "atomic.create");
    }

    // atomicLoad(handle) -> i64
    if (funcName == "atomicLoad" && !node->getArgs().empty()) {
        auto *handleArg = visit(node->getArgs()[0].get());
        if (!handleArg) return nullptr;
        auto *fn = getOrPanic("liva_atomic_load");
        return builder_->CreateCall(fn, {handleArg}, "atomic.load");
    }

    // atomicStore(handle, value) -> void
    if (funcName == "atomicStore" && node->getArgs().size() >= 2) {
        auto *handleArg = visit(node->getArgs()[0].get());
        auto *valArg = visit(node->getArgs()[1].get());
        if (!handleArg || !valArg) return nullptr;
        auto *fn = getOrPanic("liva_atomic_store");
        builder_->CreateCall(fn, {toI64(handleArg), toI64(valArg)});
        return llvm::Constant::getNullValue(builder_->getInt64Ty());
    }

    // atomicAdd(handle, value) -> i64
    if (funcName == "atomicAdd" && node->getArgs().size() >= 2) {
        auto *handleArg = visit(node->getArgs()[0].get());
        auto *valArg = visit(node->getArgs()[1].get());
        if (!handleArg || !valArg) return nullptr;
        auto *fn = getOrPanic("liva_atomic_add");
        return builder_->CreateCall(fn, {toI64(handleArg), toI64(valArg)}, "atomic.add");
    }

    // atomicSub(handle, value) -> i64
    if (funcName == "atomicSub" && node->getArgs().size() >= 2) {
        auto *handleArg = visit(node->getArgs()[0].get());
        auto *valArg = visit(node->getArgs()[1].get());
        if (!handleArg || !valArg) return nullptr;
        auto *fn = getOrPanic("liva_atomic_sub");
        return builder_->CreateCall(fn, {toI64(handleArg), toI64(valArg)}, "atomic.sub");
    }

    // atomicCas(handle, expected, desired) -> bool
    if (funcName == "atomicCas" && node->getArgs().size() >= 3) {
        auto *handleArg = visit(node->getArgs()[0].get());
        auto *expArg = visit(node->getArgs()[1].get());
        auto *desArg = visit(node->getArgs()[2].get());
        if (!handleArg || !expArg || !desArg) return nullptr;
        auto *fn = getOrPanic("liva_atomic_cas");
        auto *result = builder_->CreateCall(fn, {toI64(handleArg), toI64(expArg), toI64(desArg)}, "atomic.cas");
        return builder_->CreateTrunc(result, builder_->getInt1Ty(), "atomic.cas.bool");
    }

    // atomicFree(handle) -> void
    if (funcName == "atomicFree" && !node->getArgs().empty()) {
        auto *handleArg = visit(node->getArgs()[0].get());
        if (!handleArg) return nullptr;
        auto *fn = getOrPanic("liva_atomic_free");
        builder_->CreateCall(fn, {handleArg});
        return llvm::Constant::getNullValue(builder_->getInt64Ty());
    }

    // === Stdlib: Channel ===

    // channelCreate(capacity) -> i64
    if (funcName == "channelCreate" && !node->getArgs().empty()) {
        auto *capArg = visit(node->getArgs()[0].get());
        if (!capArg) return nullptr;
        auto *fn = getOrPanic("liva_channel_create");
        return builder_->CreateCall(fn, {toI64(capArg)}, "channel.create");
    }

    // channelSend(handle, value) -> void
    if (funcName == "channelSend" && node->getArgs().size() >= 2) {
        auto *handleArg = visit(node->getArgs()[0].get());
        auto *valArg = visit(node->getArgs()[1].get());
        if (!handleArg || !valArg) return nullptr;
        auto *fn = getOrPanic("liva_channel_send");
        builder_->CreateCall(fn, {toI64(handleArg), toI64(valArg)});
        return llvm::Constant::getNullValue(builder_->getInt64Ty());
    }

    // channelReceive(handle) -> i64? (Optional<i64>)
    if (funcName == "channelReceive" && !node->getArgs().empty()) {
        auto *handleArg = visit(node->getArgs()[0].get());
        if (!handleArg) return nullptr;
        auto *fn = getOrPanic("liva_channel_receive");
        // Allocate ok flag on stack
        auto *curFunc = builder_->GetInsertBlock()->getParent();
        auto *okAlloca = createEntryBlockAlloca(curFunc, "ch.recv.ok", builder_->getInt8Ty());
        builder_->CreateStore(builder_->getInt8(0), okAlloca);
        auto *result = builder_->CreateCall(fn, {handleArg, okAlloca}, "ch.recv.val");
        auto *okVal = builder_->CreateLoad(builder_->getInt8Ty(), okAlloca, "ch.recv.ok.val");
        auto *hasVal = builder_->CreateICmpNE(okVal, builder_->getInt8(0), "ch.recv.hasval");
        // Wrap in Optional<i64> {i1, i64}
        auto *optTy = getOptionalType(builder_->getInt64Ty());
        auto *optAlloca = createEntryBlockAlloca(curFunc, "ch.recv.opt", optTy);
        auto *hasValPtr = builder_->CreateStructGEP(optTy, optAlloca, 0);
        builder_->CreateStore(hasVal, hasValPtr);
        auto *valPtr = builder_->CreateStructGEP(optTy, optAlloca, 1);
        builder_->CreateStore(result, valPtr);
        return builder_->CreateLoad(optTy, optAlloca, "ch.recv.result");
    }

    // channelClose(handle) -> void
    if (funcName == "channelClose" && !node->getArgs().empty()) {
        auto *handleArg = visit(node->getArgs()[0].get());
        if (!handleArg) return nullptr;
        auto *fn = getOrPanic("liva_channel_close");
        builder_->CreateCall(fn, {handleArg});
        return llvm::Constant::getNullValue(builder_->getInt64Ty());
    }

    // channelTrySend(handle, value) -> bool
    if (funcName == "channelTrySend" && node->getArgs().size() >= 2) {
        auto *handleArg = visit(node->getArgs()[0].get());
        auto *valArg = visit(node->getArgs()[1].get());
        if (!handleArg || !valArg) return nullptr;
        auto *fn = getOrPanic("liva_channel_try_send");
        auto *result = builder_->CreateCall(fn, {toI64(handleArg), toI64(valArg)}, "ch.try_send");
        return builder_->CreateTrunc(result, builder_->getInt1Ty(), "ch.try_send.bool");
    }

    // === Stdlib: TOML ===

    // tomlParse(text) -> i64
    if (funcName == "tomlParse" && !node->getArgs().empty()) {
        auto *textArg = visit(node->getArgs()[0].get());
        if (!textArg) return nullptr;
        auto *fn = getOrPanic("liva_toml_parse");
        return builder_->CreateCall(fn, {textArg}, "toml.parse");
    }

    // tomlGetString(handle, section, key) -> string?
    if (funcName == "tomlGetString" && node->getArgs().size() >= 3) {
        auto *handleArg = visit(node->getArgs()[0].get());
        auto *secArg = visit(node->getArgs()[1].get());
        auto *keyArg = visit(node->getArgs()[2].get());
        if (!handleArg || !secArg || !keyArg) return nullptr;
        auto *fn = getOrPanic("liva_toml_get_string");
        auto *result = builder_->CreateCall(fn, {toI64(handleArg), secArg, keyArg}, "toml.get_str.raw");
        trackStringTemp(result);
        // Wrap in Optional<string>: NULL → nil, else Some(ptr)
        auto *ptrTy = llvm::PointerType::getUnqual(*context_);
        auto *curFunc = builder_->GetInsertBlock()->getParent();
        auto *isNull = builder_->CreateICmpEQ(result,
            llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(ptrTy)),
            "toml.get_str.isnull");
        auto *hasVal = builder_->CreateNot(isNull, "toml.get_str.hasval");
        auto *optTy = getOptionalType(ptrTy);
        auto *optAlloca = createEntryBlockAlloca(curFunc, "toml.get_str.opt", optTy);
        auto *hasValPtr = builder_->CreateStructGEP(optTy, optAlloca, 0);
        builder_->CreateStore(hasVal, hasValPtr);
        auto *valPtr = builder_->CreateStructGEP(optTy, optAlloca, 1);
        builder_->CreateStore(result, valPtr);
        return builder_->CreateLoad(optTy, optAlloca, "toml.get_str.result");
    }

    // tomlGetInt(handle, section, key) -> i64? (Optional<i64>)
    if (funcName == "tomlGetInt" && node->getArgs().size() >= 3) {
        auto *handleArg = visit(node->getArgs()[0].get());
        auto *secArg = visit(node->getArgs()[1].get());
        auto *keyArg = visit(node->getArgs()[2].get());
        if (!handleArg || !secArg || !keyArg) return nullptr;
        auto *fn = getOrPanic("liva_toml_get_int");
        auto *curFunc = builder_->GetInsertBlock()->getParent();
        auto *okAlloca = createEntryBlockAlloca(curFunc, "toml.get_int.ok", builder_->getInt8Ty());
        builder_->CreateStore(builder_->getInt8(0), okAlloca);
        auto *result = builder_->CreateCall(fn, {toI64(handleArg), secArg, keyArg, okAlloca}, "toml.get_int.val");
        auto *okVal = builder_->CreateLoad(builder_->getInt8Ty(), okAlloca, "toml.get_int.ok.val");
        auto *hasVal = builder_->CreateICmpNE(okVal, builder_->getInt8(0), "toml.get_int.hasval");
        auto *optTy = getOptionalType(builder_->getInt64Ty());
        auto *optAlloca = createEntryBlockAlloca(curFunc, "toml.get_int.opt", optTy);
        auto *hasValPtr = builder_->CreateStructGEP(optTy, optAlloca, 0);
        builder_->CreateStore(hasVal, hasValPtr);
        auto *valPtr = builder_->CreateStructGEP(optTy, optAlloca, 1);
        builder_->CreateStore(result, valPtr);
        return builder_->CreateLoad(optTy, optAlloca, "toml.get_int.result");
    }

    // tomlGetBool(handle, section, key) -> bool? (Optional<bool>)
    if (funcName == "tomlGetBool" && node->getArgs().size() >= 3) {
        auto *handleArg = visit(node->getArgs()[0].get());
        auto *secArg = visit(node->getArgs()[1].get());
        auto *keyArg = visit(node->getArgs()[2].get());
        if (!handleArg || !secArg || !keyArg) return nullptr;
        auto *fn = getOrPanic("liva_toml_get_bool");
        auto *curFunc = builder_->GetInsertBlock()->getParent();
        auto *okAlloca = createEntryBlockAlloca(curFunc, "toml.get_bool.ok", builder_->getInt8Ty());
        builder_->CreateStore(builder_->getInt8(0), okAlloca);
        auto *result = builder_->CreateCall(fn, {toI64(handleArg), secArg, keyArg, okAlloca}, "toml.get_bool.val");
        auto *okVal = builder_->CreateLoad(builder_->getInt8Ty(), okAlloca, "toml.get_bool.ok.val");
        auto *hasVal = builder_->CreateICmpNE(okVal, builder_->getInt8(0), "toml.get_bool.hasval");
        auto *resultBool = builder_->CreateTrunc(result, builder_->getInt1Ty(), "toml.get_bool.bool");
        auto *optTy = getOptionalType(builder_->getInt1Ty());
        auto *optAlloca = createEntryBlockAlloca(curFunc, "toml.get_bool.opt", optTy);
        auto *hasValPtr = builder_->CreateStructGEP(optTy, optAlloca, 0);
        builder_->CreateStore(hasVal, hasValPtr);
        auto *valPtr = builder_->CreateStructGEP(optTy, optAlloca, 1);
        builder_->CreateStore(resultBool, valPtr);
        return builder_->CreateLoad(optTy, optAlloca, "toml.get_bool.result");
    }

    // tomlHasKey(handle, section, key) -> bool
    if (funcName == "tomlHasKey" && node->getArgs().size() >= 3) {
        auto *handleArg = visit(node->getArgs()[0].get());
        auto *secArg = visit(node->getArgs()[1].get());
        auto *keyArg = visit(node->getArgs()[2].get());
        if (!handleArg || !secArg || !keyArg) return nullptr;
        auto *fn = getOrPanic("liva_toml_has_key");
        auto *result = builder_->CreateCall(fn, {toI64(handleArg), secArg, keyArg}, "toml.has");
        return builder_->CreateTrunc(result, builder_->getInt1Ty(), "toml.has.bool");
    }

    // tomlFree(handle) -> void
    if (funcName == "tomlFree" && !node->getArgs().empty()) {
        auto *handleArg = visit(node->getArgs()[0].get());
        if (!handleArg) return nullptr;
        auto *fn = getOrPanic("liva_toml_free");
        builder_->CreateCall(fn, {toI64(handleArg)});
        return llvm::Constant::getNullValue(builder_->getInt64Ty());
    }

    // channelTryReceive(handle) -> i64? (Optional<i64>)
    if (funcName == "channelTryReceive" && !node->getArgs().empty()) {
        auto *handleArg = visit(node->getArgs()[0].get());
        if (!handleArg) return nullptr;
        auto *fn = getOrPanic("liva_channel_try_receive");
        auto *curFunc = builder_->GetInsertBlock()->getParent();
        auto *okAlloca = createEntryBlockAlloca(curFunc, "ch.tryrecv.ok", builder_->getInt8Ty());
        builder_->CreateStore(builder_->getInt8(0), okAlloca);
        auto *result = builder_->CreateCall(fn, {handleArg, okAlloca}, "ch.tryrecv.val");
        auto *okVal = builder_->CreateLoad(builder_->getInt8Ty(), okAlloca, "ch.tryrecv.ok.val");
        auto *hasVal = builder_->CreateICmpNE(okVal, builder_->getInt8(0), "ch.tryrecv.hasval");
        // Wrap in Optional<i64> {i1, i64}
        auto *optTy = getOptionalType(builder_->getInt64Ty());
        auto *optAlloca = createEntryBlockAlloca(curFunc, "ch.tryrecv.opt", optTy);
        auto *hasValPtr = builder_->CreateStructGEP(optTy, optAlloca, 0);
        builder_->CreateStore(hasVal, hasValPtr);
        auto *valPtr = builder_->CreateStructGEP(optTy, optAlloca, 1);
        builder_->CreateStore(result, valPtr);
        return builder_->CreateLoad(optTy, optAlloca, "ch.tryrecv.result");
    }

    // channelLen(handle) -> i64
    if (funcName == "channelLen" && !node->getArgs().empty()) {
        auto *handleArg = visit(node->getArgs()[0].get());
        if (!handleArg) return nullptr;
        auto *fn = getOrPanic("liva_channel_len");
        return builder_->CreateCall(fn, {handleArg}, "channel.len");
    }

    // channelFree(handle) -> void
    if (funcName == "channelFree" && !node->getArgs().empty()) {
        auto *handleArg = visit(node->getArgs()[0].get());
        if (!handleArg) return nullptr;
        auto *fn = getOrPanic("liva_channel_free");
        builder_->CreateCall(fn, {handleArg});
        return llvm::Constant::getNullValue(builder_->getInt64Ty());
    }

    // === Stdlib: TaskGroup ===

    // taskGroupCreate() -> i64
    if (funcName == "taskGroupCreate") {
        auto *fn = getOrPanic("liva_task_group_create");
        return builder_->CreateCall(fn, {}, "tg.create");
    }

    // === Task control: taskIsDone, taskCancel, taskIsCancelled ===

    // taskIsDone(task) -> bool
    if (funcName == "taskIsDone" && !node->getArgs().empty()) {
        auto *taskArg = visit(node->getArgs()[0].get());
        if (!taskArg) return nullptr;
        auto *taskPtr = builder_->CreateBitOrPointerCast(taskArg,
            llvm::PointerType::getUnqual(*context_), "task.ptr");
        auto *fn = getOrPanic("liva_task_is_done");
        auto *result = builder_->CreateCall(fn, {taskPtr}, "task.is_done");
        return builder_->CreateTrunc(result, builder_->getInt1Ty(), "task.is_done.bool");
    }

    // taskCancel(task) -> void
    if (funcName == "taskCancel" && !node->getArgs().empty()) {
        auto *taskArg = visit(node->getArgs()[0].get());
        if (!taskArg) return nullptr;
        auto *taskPtr = builder_->CreateBitOrPointerCast(taskArg,
            llvm::PointerType::getUnqual(*context_), "task.ptr");
        auto *fn = getOrPanic("liva_task_cancel");
        builder_->CreateCall(fn, {taskPtr});
        return llvm::Constant::getNullValue(builder_->getInt64Ty());
    }

    // taskIsCancelled(task) -> bool — check a specific task's cancel flag
    // (note: bare isCancelled() is the no-arg form that checks the current coroutine)
    if (funcName == "taskIsCancelled" && !node->getArgs().empty()) {
        auto *taskArg = visit(node->getArgs()[0].get());
        if (!taskArg) return nullptr;
        auto *taskPtr = builder_->CreateBitOrPointerCast(taskArg,
            llvm::PointerType::getUnqual(*context_), "task.ptr");
        auto *fn = getOrPanic("liva_task_is_cancelled");
        auto *result = builder_->CreateCall(fn, {taskPtr}, "task.is_cancelled");
        return builder_->CreateTrunc(result, builder_->getInt1Ty(), "task.is_cancelled.bool");
    }

    // taskGroupSpawn(group, task) -> void
    if (funcName == "taskGroupSpawn" && node->getArgs().size() >= 2) {
        auto *groupArg = visit(node->getArgs()[0].get());
        auto *taskArg = visit(node->getArgs()[1].get());
        if (!groupArg || !taskArg) return nullptr;
        auto *fn = getOrPanic("liva_task_group_spawn");
        // taskArg is a ptr (LivaTask*), cast to i8* for the function signature
        auto *taskPtr = builder_->CreateBitOrPointerCast(taskArg,
            llvm::PointerType::getUnqual(*context_), "tg.task.ptr");
        builder_->CreateCall(fn, {groupArg, taskPtr});
        return llvm::Constant::getNullValue(builder_->getInt64Ty());
    }

    // taskGroupAwaitAll(group) -> void
    if (funcName == "taskGroupAwaitAll" && !node->getArgs().empty()) {
        auto *groupArg = visit(node->getArgs()[0].get());
        if (!groupArg) return nullptr;
        auto *fn = getOrPanic("liva_task_group_await_all");
        builder_->CreateCall(fn, {groupArg});
        return llvm::Constant::getNullValue(builder_->getInt64Ty());
    }

    // taskGroupCancelAll(group) -> void
    if (funcName == "taskGroupCancelAll" && !node->getArgs().empty()) {
        auto *groupArg = visit(node->getArgs()[0].get());
        if (!groupArg) return nullptr;
        auto *fn = getOrPanic("liva_task_group_cancel_all");
        builder_->CreateCall(fn, {groupArg});
        return llvm::Constant::getNullValue(builder_->getInt64Ty());
    }

    // taskGroupCount(group) -> i64
    if (funcName == "taskGroupCount" && !node->getArgs().empty()) {
        auto *groupArg = visit(node->getArgs()[0].get());
        if (!groupArg) return nullptr;
        auto *fn = getOrPanic("liva_task_group_count");
        return builder_->CreateCall(fn, {groupArg}, "tg.count");
    }

    // taskGroupFree(group) -> void
    if (funcName == "taskGroupFree" && !node->getArgs().empty()) {
        auto *groupArg = visit(node->getArgs()[0].get());
        if (!groupArg) return nullptr;
        auto *fn = getOrPanic("liva_task_group_free");
        builder_->CreateCall(fn, {groupArg});
        return llvm::Constant::getNullValue(builder_->getInt64Ty());
    }

    // === Stdlib: Task Select & WithTimeout ===

    // taskSelect(tasks, count) -> i64 (index of first completed task)
    if (funcName == "taskSelect" && node->getArgs().size() >= 2) {
        auto *tasksArg = visit(node->getArgs()[0].get());
        auto *countArg = visit(node->getArgs()[1].get());
        if (!tasksArg || !countArg) return nullptr;
        auto *tasksPtr = builder_->CreateBitOrPointerCast(tasksArg,
            llvm::PointerType::getUnqual(*context_), "select.tasks.ptr");
        auto *fn = getOrPanic("liva_task_select");
        return builder_->CreateCall(fn, {tasksPtr, toI64(countArg)}, "task.select");
    }

    // withTimeout(task, ms) -> bool (true if completed, false if timed out)
    if (funcName == "withTimeout" && node->getArgs().size() >= 2) {
        auto *taskArg = visit(node->getArgs()[0].get());
        auto *msArg = visit(node->getArgs()[1].get());
        if (!taskArg || !msArg) return nullptr;
        auto *taskPtr = builder_->CreateBitOrPointerCast(taskArg,
            llvm::PointerType::getUnqual(*context_), "timeout.task.ptr");
        auto *fn = getOrPanic("liva_task_with_timeout");
        auto *r = builder_->CreateCall(fn, {taskPtr, toI64(msArg)}, "task.timeout");
        return builder_->CreateICmpNE(r, builder_->getInt8(0), "task.timeout.bool");
    }

    // === Stdlib: Thread Pool Scheduler ===

    // schedulerInit(numWorkers) -> void
    if (funcName == "schedulerInit" && !node->getArgs().empty()) {
        auto *arg = visit(node->getArgs()[0].get());
        if (!arg) return nullptr;
        auto *fn = getOrPanic("liva_scheduler_init");
        builder_->CreateCall(fn, {arg});
        return llvm::Constant::getNullValue(builder_->getInt64Ty());
    }

    // schedulerShutdown() -> void
    if (funcName == "schedulerShutdown") {
        auto *fn = getOrPanic("liva_scheduler_shutdown");
        builder_->CreateCall(fn, {});
        return llvm::Constant::getNullValue(builder_->getInt64Ty());
    }

    // schedulerWorkerCount() -> i32
    if (funcName == "schedulerWorkerCount") {
        auto *fn = getOrPanic("liva_scheduler_worker_count");
        return builder_->CreateCall(fn, {}, "sched.workers");
    }

    // === Stdlib: Async I/O ===

    // asyncFileRead(path) -> String?
    if (funcName == "asyncFileRead" && !node->getArgs().empty()) {
        auto *pathArg = visit(node->getArgs()[0].get());
        if (!pathArg) return nullptr;
        auto *fn = getOrPanic("liva_async_file_read");
        auto *result = builder_->CreateCall(fn, {pathArg}, "async.fread.raw");
        trackStringTemp(result);
        // Wrap in Optional<string>
        auto *curFunc = builder_->GetInsertBlock()->getParent();
        auto *isNull = builder_->CreateICmpEQ(result, llvm::ConstantPointerNull::get(
            llvm::PointerType::getUnqual(*context_)), "async.fread.isnull");
        auto *optTy = getOptionalType(llvm::PointerType::getUnqual(*context_));
        auto *optAlloca = createEntryBlockAlloca(curFunc, "async.fread.opt", optTy);
        auto *hasValPtr = builder_->CreateStructGEP(optTy, optAlloca, 0);
        auto *valPtr = builder_->CreateStructGEP(optTy, optAlloca, 1);
        auto *hasVal = builder_->CreateNot(isNull, "async.fread.hasval");
        builder_->CreateStore(hasVal, hasValPtr);
        builder_->CreateStore(result, valPtr);
        return builder_->CreateLoad(optTy, optAlloca, "async.fread.result");
    }

    // asyncFileWrite(path, content) -> bool
    if (funcName == "asyncFileWrite" && node->getArgs().size() >= 2) {
        auto *pathArg = visit(node->getArgs()[0].get());
        auto *contentArg = visit(node->getArgs()[1].get());
        if (!pathArg || !contentArg) return nullptr;
        auto *fn = getOrPanic("liva_async_file_write");
        auto *r = builder_->CreateCall(fn, {pathArg, contentArg}, "async.fwrite");
        return builder_->CreateICmpNE(r, builder_->getInt8(0), "async.fwrite.bool");
    }

    // === Stdlib: Networking ===
    // httpStatus(handle) -> i32
    if (funcName == "httpStatus" && !node->getArgs().empty()) {
        auto *handleArg = visit(node->getArgs()[0].get());
        if (!handleArg) return nullptr;
        auto *fn = getOrPanic("liva_http_req_status");
        return builder_->CreateCall(fn, {handleArg}, "http.status");
    }

    // httpBody(handle) -> string
    if (funcName == "httpBody" && !node->getArgs().empty()) {
        auto *handleArg = visit(node->getArgs()[0].get());
        if (!handleArg) return nullptr;
        auto *fn = getOrPanic("liva_http_req_body");
        auto *r = builder_->CreateCall(fn, {handleArg}, "http.body");
        trackStringTemp(r);
        return r;
    }

    // httpRequestEx(method, url, body, headersBlob, timeout_ms) -> i64 handle
    if (funcName == "httpRequestEx" && node->getArgs().size() >= 5) {
        auto *methodArg = visit(node->getArgs()[0].get());
        auto *urlArg = visit(node->getArgs()[1].get());
        auto *bodyArg = visit(node->getArgs()[2].get());
        auto *hdrArg = visit(node->getArgs()[3].get());
        auto *timeoutArg = visit(node->getArgs()[4].get());
        if (!methodArg || !urlArg || !bodyArg || !hdrArg || !timeoutArg) return nullptr;
        if (timeoutArg->getType()->isIntegerTy(32))
            timeoutArg = builder_->CreateSExt(timeoutArg, builder_->getInt64Ty());
        auto *fn = getOrPanic("liva_http_req_ex");
        return builder_->CreateCall(fn, {methodArg, urlArg, bodyArg, hdrArg, timeoutArg},
                                    "http.reqex.handle");
    }

    // httpRawHeaders(handle) -> string
    if (funcName == "httpRawHeaders" && !node->getArgs().empty()) {
        auto *handleArg = visit(node->getArgs()[0].get());
        if (!handleArg) return nullptr;
        auto *r = builder_->CreateCall(getOrPanic("liva_http_raw_headers"), {handleArg}, "http.rawhdr");
        trackStringTemp(r);
        return r;
    }

    // httpHeaderLookup(blob, name) -> string?
    if (funcName == "httpHeaderLookup" && node->getArgs().size() >= 2) {
        auto *blobArg = visit(node->getArgs()[0].get());
        auto *nameArg = visit(node->getArgs()[1].get());
        if (!blobArg || !nameArg) return nullptr;
        auto *result = builder_->CreateCall(getOrPanic("liva_http_header_lookup"),
                                            {blobArg, nameArg}, "http.hdrlookup.raw");
        trackStringTemp(result);
        auto *curFunc = builder_->GetInsertBlock()->getParent();
        auto *isNull = builder_->CreateICmpEQ(result, llvm::ConstantPointerNull::get(
            llvm::PointerType::getUnqual(*context_)), "http.hdrlookup.isnull");
        auto *hasVal = builder_->CreateNot(isNull, "http.hdrlookup.hasval");
        auto *optTy = getOptionalType(llvm::PointerType::getUnqual(*context_));
        auto *optAlloca = createEntryBlockAlloca(curFunc, "http.hdrlookup.opt", optTy);
        builder_->CreateStore(hasVal, builder_->CreateStructGEP(optTy, optAlloca, 0));
        builder_->CreateStore(result, builder_->CreateStructGEP(optTy, optAlloca, 1));
        return builder_->CreateLoad(optTy, optAlloca, "http.hdrlookup.result");
    }

    // httpClose(handle) -> void
    if (funcName == "httpClose" && !node->getArgs().empty()) {
        auto *handleArg = visit(node->getArgs()[0].get());
        if (!handleArg) return nullptr;
        auto *fn = getOrPanic("liva_http_req_close");
        builder_->CreateCall(fn, {handleArg});
        return nullptr;
    }

    // wsSend(handle, msg) -> bool (true = success)
    if (funcName == "wsSend" && node->getArgs().size() >= 2) {
        auto *handleArg = visit(node->getArgs()[0].get());
        auto *msgArg = visit(node->getArgs()[1].get());
        if (!handleArg || !msgArg) return nullptr;
        auto *fn = getOrPanic("liva_ws_send_text");
        auto *rc = builder_->CreateCall(fn, {handleArg, msgArg}, "ws.send.rc");
        return builder_->CreateICmpEQ(rc, builder_->getInt32(0), "ws.send.ok");
    }

    // wsClose(handle, status, reason) -> void
    if (funcName == "wsClose" && node->getArgs().size() >= 3) {
        auto *handleArg = visit(node->getArgs()[0].get());
        auto *statusArg = visit(node->getArgs()[1].get());
        auto *reasonArg = visit(node->getArgs()[2].get());
        if (!handleArg || !statusArg || !reasonArg) return nullptr;
        if (statusArg->getType()->isIntegerTy(64))
            statusArg = builder_->CreateTrunc(statusArg, builder_->getInt32Ty());
        auto *fn = getOrPanic("liva_ws_close");
        builder_->CreateCall(fn, {handleArg, statusArg, reasonArg});
        return nullptr;
    }

    // wsIsOpen(handle) -> bool
    if (funcName == "wsIsOpen" && !node->getArgs().empty()) {
        auto *handleArg = visit(node->getArgs()[0].get());
        if (!handleArg) return nullptr;
        auto *fn = getOrPanic("liva_ws_is_open");
        auto *r = builder_->CreateCall(fn, {handleArg}, "ws.isopen.rc");
        return builder_->CreateICmpNE(r, builder_->getInt32(0), "ws.isopen.bool");
    }

    // wsConnectEx(url, headersBlob, subprotocol, keepAliveMs) -> i64
    if (funcName == "wsConnectEx" && node->getArgs().size() >= 4) {
        auto *urlArg = visit(node->getArgs()[0].get());
        auto *hdrArg = visit(node->getArgs()[1].get());
        auto *subArg = visit(node->getArgs()[2].get());
        auto *kaArg = visit(node->getArgs()[3].get());
        if (!urlArg || !hdrArg || !subArg || !kaArg) return nullptr;
        if (kaArg->getType()->isIntegerTy(32))
            kaArg = builder_->CreateSExt(kaArg, builder_->getInt64Ty());
        auto *fn = getOrPanic("liva_ws_connect_ex");
        return builder_->CreateCall(fn, {urlArg, hdrArg, subArg, kaArg}, "ws.connectex");
    }

    // wsRecvKind(handle) -> i32
    if (funcName == "wsRecvKind" && !node->getArgs().empty()) {
        auto *handleArg = visit(node->getArgs()[0].get());
        if (!handleArg) return nullptr;
        auto *fn = getOrPanic("liva_ws_recv");
        return builder_->CreateCall(fn, {handleArg}, "ws.recvkind");
    }

    // wsMsgText(handle) -> string
    if (funcName == "wsMsgText" && !node->getArgs().empty()) {
        auto *handleArg = visit(node->getArgs()[0].get());
        if (!handleArg) return nullptr;
        auto *r = builder_->CreateCall(getOrPanic("liva_ws_msg_text"), {handleArg}, "ws.msgtext");
        trackStringTemp(r);
        return r;
    }

    // wsMsgBytes(handle) -> [u8]  (out_len out-param pattern, mirrors sqliteColumnBlob)
    if (funcName == "wsMsgBytes" && !node->getArgs().empty()) {
        auto *handleArg = visit(node->getArgs()[0].get());
        if (!handleArg) return nullptr;
        auto *funcCur = builder_->GetInsertBlock()->getParent();
        auto *outLenAlloca = createEntryBlockAlloca(funcCur, "ws.bytes.olen",
            builder_->getInt64Ty());
        builder_->CreateStore(builder_->getInt64(0), outLenAlloca);
        auto *fn = getOrPanic("liva_ws_msg_bytes");
        auto *dataPtr = builder_->CreateCall(fn, {handleArg, outLenAlloca}, "ws.bytes.data");
        auto *outLen = builder_->CreateLoad(builder_->getInt64Ty(), outLenAlloca, "ws.bytes.olen.v");
        auto *daTy = getDynArrayStructTy();
        auto *daAlloca = createEntryBlockAlloca(funcCur, "ws.bytes.da", daTy);
        builder_->CreateStore(dataPtr, builder_->CreateStructGEP(daTy, daAlloca, 0));
        builder_->CreateStore(outLen, builder_->CreateStructGEP(daTy, daAlloca, 1));
        builder_->CreateStore(outLen, builder_->CreateStructGEP(daTy, daAlloca, 2));
        return builder_->CreateLoad(daTy, daAlloca, "ws.bytes.val");
    }

    // wsSendBinary(handle, data: [u8]) -> bool  (mirrors sqliteBindBlob inbound)
    if (funcName == "wsSendBinary" && node->getArgs().size() >= 2) {
        auto *handleArg = visit(node->getArgs()[0].get());
        auto *arr = visit(node->getArgs()[1].get());
        if (!handleArg || !arr) return nullptr;
        auto *daTy = getDynArrayStructTy();
        auto *funcCur = builder_->GetInsertBlock()->getParent();
        auto *src = createEntryBlockAlloca(funcCur, "ws.bin.src", daTy);
        builder_->CreateStore(arr, src);
        auto *ptrTy = llvm::PointerType::getUnqual(*context_);
        auto *data = builder_->CreateLoad(ptrTy, builder_->CreateStructGEP(daTy, src, 0));
        auto *len = builder_->CreateLoad(builder_->getInt64Ty(),
            builder_->CreateStructGEP(daTy, src, 1));
        auto *fn = getOrPanic("liva_ws_send_binary");
        auto *rc = builder_->CreateCall(fn, {handleArg, data, len}, "ws.sendbin.rc");
        return builder_->CreateICmpEQ(rc, builder_->getInt32(0), "ws.sendbin.ok");
    }

    // pgNormalizeParams(sql) -> string
    if (funcName == "pgNormalizeParams" && !node->getArgs().empty()) {
        auto *sqlArg = visit(node->getArgs()[0].get());
        if (!sqlArg) return nullptr;
        auto *fn = getOrPanic("liva_pg_normalize_params");
        auto *r = builder_->CreateCall(fn, {sqlArg}, "pg.normparams");
        trackStringTemp(r);
        return r;
    }

    // pgConnect(conninfo) -> i64
    if (funcName == "pgConnect" && !node->getArgs().empty()) {
        auto *s = visit(node->getArgs()[0].get());
        if (!s) return nullptr;
        auto *fn = getOrPanic("liva_pg_connect");
        return builder_->CreateCall(fn, {s}, "pg.connect");
    }

    // pgClose(handle) -> void
    if (funcName == "pgClose" && !node->getArgs().empty()) {
        auto *h = visit(node->getArgs()[0].get());
        if (!h) return nullptr;
        builder_->CreateCall(getOrPanic("liva_pg_close"), {h});
        return nullptr;
    }

    // pgExec(handle, sql) -> bool
    if (funcName == "pgExec" && node->getArgs().size() >= 2) {
        auto *h = visit(node->getArgs()[0].get());
        auto *sql = visit(node->getArgs()[1].get());
        if (!h || !sql) return nullptr;
        auto *rc = builder_->CreateCall(getOrPanic("liva_pg_exec"), {h, sql}, "pg.exec.rc");
        return builder_->CreateICmpEQ(rc, builder_->getInt32(0), "pg.exec.ok");
    }

    // pgErrmsg(handle) -> string
    if (funcName == "pgErrmsg" && !node->getArgs().empty()) {
        auto *h = visit(node->getArgs()[0].get());
        if (!h) return nullptr;
        auto *r = builder_->CreateCall(getOrPanic("liva_pg_errmsg"), {h}, "pg.errmsg");
        trackStringTemp(r);
        return r;
    }

    // pgQuery(handle, sql) -> i64 (result handle)
    if (funcName == "pgQuery" && node->getArgs().size() >= 2) {
        auto *h = visit(node->getArgs()[0].get());
        auto *sql = visit(node->getArgs()[1].get());
        if (!h || !sql) return nullptr;
        return builder_->CreateCall(getOrPanic("liva_pg_query"), {h, sql}, "pg.query");
    }

    // pgClear(result) -> void
    if (funcName == "pgClear" && !node->getArgs().empty()) {
        auto *res = visit(node->getArgs()[0].get());
        if (!res) return nullptr;
        builder_->CreateCall(getOrPanic("liva_pg_clear"), {res});
        return nullptr;
    }

    // pgResultRows(result) -> i32  /  pgResultCols(result) -> i32
    if ((funcName == "pgResultRows" || funcName == "pgResultCols") &&
        !node->getArgs().empty()) {
        auto *res = visit(node->getArgs()[0].get());
        if (!res) return nullptr;
        auto *fn = getOrPanic(funcName == "pgResultRows"
                              ? "liva_pg_ntuples" : "liva_pg_nfields");
        return builder_->CreateCall(fn, {res}, "pg.rescount");
    }

    // pgResultText(result, row, col) -> string
    if (funcName == "pgResultText" && node->getArgs().size() >= 3) {
        auto *res = visit(node->getArgs()[0].get());
        auto *row = visit(node->getArgs()[1].get());
        auto *col = visit(node->getArgs()[2].get());
        if (!res || !row || !col) return nullptr;
        if (row->getType()->isIntegerTy(64))
            row = builder_->CreateTrunc(row, builder_->getInt32Ty());
        if (col->getType()->isIntegerTy(64))
            col = builder_->CreateTrunc(col, builder_->getInt32Ty());
        auto *r = builder_->CreateCall(getOrPanic("liva_pg_getvalue"), {res, row, col}, "pg.getval");
        trackStringTemp(r);
        return r;
    }

    // pgResultIsNull(result, row, col) -> bool
    if (funcName == "pgResultIsNull" && node->getArgs().size() >= 3) {
        auto *res = visit(node->getArgs()[0].get());
        auto *row = visit(node->getArgs()[1].get());
        auto *col = visit(node->getArgs()[2].get());
        if (!res || !row || !col) return nullptr;
        if (row->getType()->isIntegerTy(64))
            row = builder_->CreateTrunc(row, builder_->getInt32Ty());
        if (col->getType()->isIntegerTy(64))
            col = builder_->CreateTrunc(col, builder_->getInt32Ty());
        auto *rc = builder_->CreateCall(getOrPanic("liva_pg_getisnull"), {res, row, col}, "pg.isnull.rc");
        return builder_->CreateICmpNE(rc, builder_->getInt32(0), "pg.isnull");
    }

    // pgColumnName(result, col) -> string
    if (funcName == "pgColumnName" && node->getArgs().size() >= 2) {
        auto *res = visit(node->getArgs()[0].get());
        auto *col = visit(node->getArgs()[1].get());
        if (!res || !col) return nullptr;
        if (col->getType()->isIntegerTy(64))
            col = builder_->CreateTrunc(col, builder_->getInt32Ty());
        auto *r = builder_->CreateCall(getOrPanic("liva_pg_fname"), {res, col}, "pg.fname");
        trackStringTemp(r);
        return r;
    }

    // pgQueryParams(handle, sql, params: [String]) -> i64 (result handle)
    if (funcName == "pgQueryParams" && node->getArgs().size() >= 3) {
        auto *h = visit(node->getArgs()[0].get());
        auto *sql = visit(node->getArgs()[1].get());
        auto *arr = visit(node->getArgs()[2].get());
        if (!h || !sql || !arr) return nullptr;
        auto *daTy = getDynArrayStructTy();
        auto *funcCur = builder_->GetInsertBlock()->getParent();
        auto *src = createEntryBlockAlloca(funcCur, "pgp.src", daTy);
        builder_->CreateStore(arr, src);
        auto *ptrTy = llvm::PointerType::getUnqual(*context_);
        // .data is the contiguous buffer of char* (string pointers).
        auto *data = builder_->CreateLoad(ptrTy,
            builder_->CreateStructGEP(daTy, src, 0));
        auto *len = builder_->CreateLoad(builder_->getInt64Ty(),
            builder_->CreateStructGEP(daTy, src, 1));
        auto *fn = getOrPanic("liva_pg_query_params");
        return builder_->CreateCall(fn, {h, sql, data, len}, "pg.queryparams");
    }

    // sqliteOpen(path) -> i64
    if (funcName == "sqliteOpen" && !node->getArgs().empty()) {
        auto *pathArg = visit(node->getArgs()[0].get());
        if (!pathArg) return nullptr;
        auto *fn = getOrPanic("liva_sqlite_open");
        return builder_->CreateCall(fn, {pathArg}, "sqlite.open");
    }

    // sqliteClose(handle) -> void
    if (funcName == "sqliteClose" && !node->getArgs().empty()) {
        auto *handleArg = visit(node->getArgs()[0].get());
        if (!handleArg) return nullptr;
        auto *fn = getOrPanic("liva_sqlite_close");
        builder_->CreateCall(fn, {handleArg});
        return nullptr;
    }

    // sqliteExec(handle, sql) -> bool (true on success)
    if (funcName == "sqliteExec" && node->getArgs().size() >= 2) {
        auto *handleArg = visit(node->getArgs()[0].get());
        auto *sqlArg = visit(node->getArgs()[1].get());
        if (!handleArg || !sqlArg) return nullptr;
        auto *fn = getOrPanic("liva_sqlite_exec");
        auto *rc = builder_->CreateCall(fn, {handleArg, sqlArg}, "sqlite.exec.rc");
        return builder_->CreateICmpEQ(rc, builder_->getInt32(0), "sqlite.exec.ok");
    }

    // sqliteQueryFirst(handle, sql) -> string?
    if (funcName == "sqliteQueryFirst" && node->getArgs().size() >= 2) {
        auto *handleArg = visit(node->getArgs()[0].get());
        auto *sqlArg = visit(node->getArgs()[1].get());
        if (!handleArg || !sqlArg) return nullptr;
        auto *fn = getOrPanic("liva_sqlite_query_first");
        auto *result = builder_->CreateCall(fn, {handleArg, sqlArg}, "sqlite.qfirst.raw");
        trackStringTemp(result);
        auto *curFunc = builder_->GetInsertBlock()->getParent();
        auto *isNull = builder_->CreateICmpEQ(result, llvm::ConstantPointerNull::get(
            llvm::PointerType::getUnqual(*context_)), "sqlite.qfirst.isnull");
        auto *hasVal = builder_->CreateNot(isNull, "sqlite.qfirst.hasval");
        auto *optTy = getOptionalType(llvm::PointerType::getUnqual(*context_));
        auto *optAlloca = createEntryBlockAlloca(curFunc, "sqlite.qfirst.opt", optTy);
        builder_->CreateStore(hasVal, builder_->CreateStructGEP(optTy, optAlloca, 0));
        builder_->CreateStore(result, builder_->CreateStructGEP(optTy, optAlloca, 1));
        return builder_->CreateLoad(optTy, optAlloca, "sqlite.qfirst.result");
    }

    // sqliteQueryInt(handle, sql) -> i64? (nil when no row)
    if (funcName == "sqliteQueryInt" && node->getArgs().size() >= 2) {
        auto *handleArg = visit(node->getArgs()[0].get());
        auto *sqlArg = visit(node->getArgs()[1].get());
        if (!handleArg || !sqlArg) return nullptr;
        auto *curFunc = builder_->GetInsertBlock()->getParent();
        auto *okAlloca = createEntryBlockAlloca(curFunc, "sqlite.qint.ok",
                                                 builder_->getInt32Ty());
        builder_->CreateStore(builder_->getInt32(0), okAlloca);
        auto *fn = getOrPanic("liva_sqlite_query_int");
        auto *val = builder_->CreateCall(fn, {handleArg, sqlArg, okAlloca},
                                          "sqlite.qint.val");
        auto *okVal = builder_->CreateLoad(builder_->getInt32Ty(), okAlloca,
                                            "sqlite.qint.okv");
        auto *hasVal = builder_->CreateICmpNE(okVal, builder_->getInt32(0),
                                               "sqlite.qint.hasval");
        auto *optTy = getOptionalType(builder_->getInt64Ty());
        auto *optAlloca = createEntryBlockAlloca(curFunc, "sqlite.qint.opt", optTy);
        builder_->CreateStore(hasVal, builder_->CreateStructGEP(optTy, optAlloca, 0));
        builder_->CreateStore(val, builder_->CreateStructGEP(optTy, optAlloca, 1));
        return builder_->CreateLoad(optTy, optAlloca, "sqlite.qint.result");
    }

    // sqliteQueryColumn(handle, sql) -> string  (newline-joined first column)
    if (funcName == "sqliteQueryColumn" && node->getArgs().size() >= 2) {
        auto *handleArg = visit(node->getArgs()[0].get());
        auto *sqlArg = visit(node->getArgs()[1].get());
        if (!handleArg || !sqlArg) return nullptr;
        auto *fn = getOrPanic("liva_sqlite_query_all_first_col");
        auto *r = builder_->CreateCall(fn, {handleArg, sqlArg}, "sqlite.qall");
        trackStringTemp(r);
        return r;
    }

    // sqliteLastInsertRowid(handle) -> i64
    if (funcName == "sqliteLastInsertRowid" && !node->getArgs().empty()) {
        auto *handleArg = visit(node->getArgs()[0].get());
        if (!handleArg) return nullptr;
        auto *fn = getOrPanic("liva_sqlite_last_insert_rowid");
        return builder_->CreateCall(fn, {handleArg}, "sqlite.lastid");
    }

    // sqliteChanges(handle) -> i32
    if (funcName == "sqliteChanges" && !node->getArgs().empty()) {
        auto *handleArg = visit(node->getArgs()[0].get());
        if (!handleArg) return nullptr;
        auto *fn = getOrPanic("liva_sqlite_changes");
        return builder_->CreateCall(fn, {handleArg}, "sqlite.changes");
    }

    // sqliteErrmsg(handle) -> string
    if (funcName == "sqliteErrmsg" && !node->getArgs().empty()) {
        auto *handleArg = visit(node->getArgs()[0].get());
        if (!handleArg) return nullptr;
        auto *fn = getOrPanic("liva_sqlite_errmsg");
        auto *r = builder_->CreateCall(fn, {handleArg}, "sqlite.errmsg");
        trackStringTemp(r);
        return r;
    }

    // sqlitePrepare(db, sql) -> i64
    if (funcName == "sqlitePrepare" && node->getArgs().size() >= 2) {
        auto *dbArg = visit(node->getArgs()[0].get());
        auto *sqlArg = visit(node->getArgs()[1].get());
        if (!dbArg || !sqlArg) return nullptr;
        auto *fn = getOrPanic("liva_sqlite_prepare");
        return builder_->CreateCall(fn, {dbArg, sqlArg}, "sqlite.prep");
    }

    // sqliteBindText(stmt, idx, val) -> bool
    if (funcName == "sqliteBindText" && node->getArgs().size() >= 3) {
        auto *stmtArg = visit(node->getArgs()[0].get());
        auto *idxArg = visit(node->getArgs()[1].get());
        auto *valArg = visit(node->getArgs()[2].get());
        if (!stmtArg || !idxArg || !valArg) return nullptr;
        if (idxArg->getType()->isIntegerTy(64))
            idxArg = builder_->CreateTrunc(idxArg, builder_->getInt32Ty());
        auto *fn = getOrPanic("liva_sqlite_bind_text");
        auto *rc = builder_->CreateCall(fn, {stmtArg, idxArg, valArg}, "sqlite.bind.text.rc");
        return builder_->CreateICmpEQ(rc, builder_->getInt32(0), "sqlite.bind.text.ok");
    }

    // sqliteBindInt(stmt, idx, val) -> bool
    if (funcName == "sqliteBindInt" && node->getArgs().size() >= 3) {
        auto *stmtArg = visit(node->getArgs()[0].get());
        auto *idxArg = visit(node->getArgs()[1].get());
        auto *valArg = visit(node->getArgs()[2].get());
        if (!stmtArg || !idxArg || !valArg) return nullptr;
        if (idxArg->getType()->isIntegerTy(64))
            idxArg = builder_->CreateTrunc(idxArg, builder_->getInt32Ty());
        if (valArg->getType()->isIntegerTy(32))
            valArg = builder_->CreateSExt(valArg, builder_->getInt64Ty());
        auto *fn = getOrPanic("liva_sqlite_bind_int");
        auto *rc = builder_->CreateCall(fn, {stmtArg, idxArg, valArg}, "sqlite.bind.int.rc");
        return builder_->CreateICmpEQ(rc, builder_->getInt32(0), "sqlite.bind.int.ok");
    }

    // sqliteBindDouble(stmt, idx, val) -> bool
    if (funcName == "sqliteBindDouble" && node->getArgs().size() >= 3) {
        auto *stmtArg = visit(node->getArgs()[0].get());
        auto *idxArg = visit(node->getArgs()[1].get());
        auto *valArg = visit(node->getArgs()[2].get());
        if (!stmtArg || !idxArg || !valArg) return nullptr;
        if (idxArg->getType()->isIntegerTy(64))
            idxArg = builder_->CreateTrunc(idxArg, builder_->getInt32Ty());
        if (valArg->getType()->isFloatTy())
            valArg = builder_->CreateFPExt(valArg, builder_->getDoubleTy());
        auto *fn = getOrPanic("liva_sqlite_bind_double");
        auto *rc = builder_->CreateCall(fn, {stmtArg, idxArg, valArg}, "sqlite.bind.dbl.rc");
        return builder_->CreateICmpEQ(rc, builder_->getInt32(0), "sqlite.bind.dbl.ok");
    }

    // sqliteBindNull(stmt, idx) -> bool
    if (funcName == "sqliteBindNull" && node->getArgs().size() >= 2) {
        auto *stmtArg = visit(node->getArgs()[0].get());
        auto *idxArg = visit(node->getArgs()[1].get());
        if (!stmtArg || !idxArg) return nullptr;
        if (idxArg->getType()->isIntegerTy(64))
            idxArg = builder_->CreateTrunc(idxArg, builder_->getInt32Ty());
        auto *fn = getOrPanic("liva_sqlite_bind_null");
        auto *rc = builder_->CreateCall(fn, {stmtArg, idxArg}, "sqlite.bind.null.rc");
        return builder_->CreateICmpEQ(rc, builder_->getInt32(0), "sqlite.bind.null.ok");
    }

    // sqliteStep(stmt) -> bool (true if row available)
    if (funcName == "sqliteStep" && !node->getArgs().empty()) {
        auto *stmtArg = visit(node->getArgs()[0].get());
        if (!stmtArg) return nullptr;
        auto *fn = getOrPanic("liva_sqlite_step");
        auto *rc = builder_->CreateCall(fn, {stmtArg}, "sqlite.step.rc");
        return builder_->CreateICmpEQ(rc, builder_->getInt32(1), "sqlite.step.row");
    }

    // sqliteReset(stmt) -> bool
    if (funcName == "sqliteReset" && !node->getArgs().empty()) {
        auto *stmtArg = visit(node->getArgs()[0].get());
        if (!stmtArg) return nullptr;
        auto *fn = getOrPanic("liva_sqlite_reset");
        auto *rc = builder_->CreateCall(fn, {stmtArg}, "sqlite.reset.rc");
        return builder_->CreateICmpEQ(rc, builder_->getInt32(0), "sqlite.reset.ok");
    }

    // sqliteColumnCount(stmt) -> i32
    if (funcName == "sqliteColumnCount" && !node->getArgs().empty()) {
        auto *stmtArg = visit(node->getArgs()[0].get());
        if (!stmtArg) return nullptr;
        auto *fn = getOrPanic("liva_sqlite_column_count");
        return builder_->CreateCall(fn, {stmtArg}, "sqlite.colcount");
    }

    // sqliteColumnName(stmt, col) -> string
    if (funcName == "sqliteColumnName" && node->getArgs().size() >= 2) {
        auto *stmtArg = visit(node->getArgs()[0].get());
        auto *colArg = visit(node->getArgs()[1].get());
        if (!stmtArg || !colArg) return nullptr;
        if (colArg->getType()->isIntegerTy(64))
            colArg = builder_->CreateTrunc(colArg, builder_->getInt32Ty());
        auto *fn = getOrPanic("liva_sqlite_column_name");
        auto *r = builder_->CreateCall(fn, {stmtArg, colArg}, "sqlite.colname");
        trackStringTemp(r);
        return r;
    }

    // sqliteColumnType(stmt, col) -> i32
    if (funcName == "sqliteColumnType" && node->getArgs().size() >= 2) {
        auto *stmtArg = visit(node->getArgs()[0].get());
        auto *colArg = visit(node->getArgs()[1].get());
        if (!stmtArg || !colArg) return nullptr;
        if (colArg->getType()->isIntegerTy(64))
            colArg = builder_->CreateTrunc(colArg, builder_->getInt32Ty());
        auto *fn = getOrPanic("liva_sqlite_column_type");
        return builder_->CreateCall(fn, {stmtArg, colArg}, "sqlite.coltype");
    }

    // sqliteColumnIsNull(stmt, col) -> bool  (column_type == 5)
    if (funcName == "sqliteColumnIsNull" && node->getArgs().size() >= 2) {
        auto *stmtArg = visit(node->getArgs()[0].get());
        auto *colArg = visit(node->getArgs()[1].get());
        if (!stmtArg || !colArg) return nullptr;
        if (colArg->getType()->isIntegerTy(64))
            colArg = builder_->CreateTrunc(colArg, builder_->getInt32Ty());
        auto *fn = getOrPanic("liva_sqlite_column_type");
        auto *t = builder_->CreateCall(fn, {stmtArg, colArg}, "sqlite.coltype.n");
        return builder_->CreateICmpEQ(t, builder_->getInt32(5), "sqlite.isnull");
    }

    // sqliteBindByName(stmt, name, val) -> bool
    if (funcName == "sqliteBindByName" && node->getArgs().size() >= 3) {
        auto *stmtArg = visit(node->getArgs()[0].get());
        auto *nameArg = visit(node->getArgs()[1].get());
        auto *valArg = visit(node->getArgs()[2].get());
        if (!stmtArg || !nameArg || !valArg) return nullptr;
        auto *fn = getOrPanic("liva_sqlite_bind_by_name");
        auto *rc = builder_->CreateCall(fn, {stmtArg, nameArg, valArg}, "sqlite.bindname.rc");
        return builder_->CreateICmpEQ(rc, builder_->getInt32(0), "sqlite.bindname.ok");
    }

    // sqliteBindBlob(stmt, idx, data: [u8]) -> bool
    if (funcName == "sqliteBindBlob" && node->getArgs().size() >= 3) {
        auto *stmtArg = visit(node->getArgs()[0].get());
        auto *idxArg = visit(node->getArgs()[1].get());
        auto *arr = visit(node->getArgs()[2].get());
        if (!stmtArg || !idxArg || !arr) return nullptr;
        if (idxArg->getType()->isIntegerTy(64))
            idxArg = builder_->CreateTrunc(idxArg, builder_->getInt32Ty());
        auto *daTy = getDynArrayStructTy();
        auto *funcCur = builder_->GetInsertBlock()->getParent();
        auto *src = createEntryBlockAlloca(funcCur, "blob.src", daTy);
        builder_->CreateStore(arr, src);
        auto *ptrTy = llvm::PointerType::getUnqual(*context_);
        auto *data = builder_->CreateLoad(ptrTy,
            builder_->CreateStructGEP(daTy, src, 0));
        auto *len = builder_->CreateLoad(builder_->getInt64Ty(),
            builder_->CreateStructGEP(daTy, src, 1));
        auto *fn = getOrPanic("liva_sqlite_bind_blob");
        auto *rc = builder_->CreateCall(fn, {stmtArg, idxArg, data, len}, "sqlite.bindblob.rc");
        return builder_->CreateICmpEQ(rc, builder_->getInt32(0), "sqlite.bindblob.ok");
    }

    // sqliteColumnBlob(stmt, col) -> [u8]
    if (funcName == "sqliteColumnBlob" && node->getArgs().size() >= 2) {
        auto *stmtArg = visit(node->getArgs()[0].get());
        auto *colArg = visit(node->getArgs()[1].get());
        if (!stmtArg || !colArg) return nullptr;
        if (colArg->getType()->isIntegerTy(64))
            colArg = builder_->CreateTrunc(colArg, builder_->getInt32Ty());
        auto *funcCur = builder_->GetInsertBlock()->getParent();
        auto *outLenAlloca = createEntryBlockAlloca(funcCur, "blob.olen",
            builder_->getInt64Ty());
        builder_->CreateStore(builder_->getInt64(0), outLenAlloca);
        auto *fn = getOrPanic("liva_sqlite_column_blob");
        auto *dataPtr = builder_->CreateCall(fn, {stmtArg, colArg, outLenAlloca}, "blob.data");
        auto *outLen = builder_->CreateLoad(builder_->getInt64Ty(), outLenAlloca, "blob.olen.v");
        auto *daTy = getDynArrayStructTy();
        auto *daAlloca = createEntryBlockAlloca(funcCur, "blob.da", daTy);
        builder_->CreateStore(dataPtr, builder_->CreateStructGEP(daTy, daAlloca, 0));
        builder_->CreateStore(outLen, builder_->CreateStructGEP(daTy, daAlloca, 1));
        builder_->CreateStore(outLen, builder_->CreateStructGEP(daTy, daAlloca, 2));
        return builder_->CreateLoad(daTy, daAlloca, "blob.val");
    }

    // sqliteColumnText(stmt, col) -> string
    if (funcName == "sqliteColumnText" && node->getArgs().size() >= 2) {
        auto *stmtArg = visit(node->getArgs()[0].get());
        auto *colArg = visit(node->getArgs()[1].get());
        if (!stmtArg || !colArg) return nullptr;
        if (colArg->getType()->isIntegerTy(64))
            colArg = builder_->CreateTrunc(colArg, builder_->getInt32Ty());
        auto *fn = getOrPanic("liva_sqlite_column_text");
        auto *r = builder_->CreateCall(fn, {stmtArg, colArg}, "sqlite.coltext");
        trackStringTemp(r);
        return r;
    }

    // sqliteColumnInt(stmt, col) -> i64
    if (funcName == "sqliteColumnInt" && node->getArgs().size() >= 2) {
        auto *stmtArg = visit(node->getArgs()[0].get());
        auto *colArg = visit(node->getArgs()[1].get());
        if (!stmtArg || !colArg) return nullptr;
        if (colArg->getType()->isIntegerTy(64))
            colArg = builder_->CreateTrunc(colArg, builder_->getInt32Ty());
        auto *fn = getOrPanic("liva_sqlite_column_int");
        return builder_->CreateCall(fn, {stmtArg, colArg}, "sqlite.colint");
    }

    // sqliteColumnDouble(stmt, col) -> f64
    if (funcName == "sqliteColumnDouble" && node->getArgs().size() >= 2) {
        auto *stmtArg = visit(node->getArgs()[0].get());
        auto *colArg = visit(node->getArgs()[1].get());
        if (!stmtArg || !colArg) return nullptr;
        if (colArg->getType()->isIntegerTy(64))
            colArg = builder_->CreateTrunc(colArg, builder_->getInt32Ty());
        auto *fn = getOrPanic("liva_sqlite_column_double");
        return builder_->CreateCall(fn, {stmtArg, colArg}, "sqlite.coldbl");
    }

    // sqliteFinalize(stmt) -> void
    if (funcName == "sqliteFinalize" && !node->getArgs().empty()) {
        auto *stmtArg = visit(node->getArgs()[0].get());
        if (!stmtArg) return nullptr;
        auto *fn = getOrPanic("liva_sqlite_finalize");
        builder_->CreateCall(fn, {stmtArg});
        return nullptr;
    }

    // === JSON ===
    if (funcName == "jsonObjKeys" && !node->getArgs().empty()) {
        auto *nodeArg = visit(node->getArgs()[0].get());
        if (!nodeArg) return nullptr;
        auto *fn = getOrPanic("liva_json_obj_keys");
        auto *i64Ty = builder_->getInt64Ty();
        auto *curFunc = builder_->GetInsertBlock()->getParent();
        auto *countAlloca = createEntryBlockAlloca(curFunc, "jsonkeys.count", i64Ty);
        builder_->CreateStore(builder_->getInt64(0), countAlloca);
        auto *rawArr = builder_->CreateCall(fn, {nodeArg, countAlloca}, "jsonkeys.raw");
        auto *count = builder_->CreateLoad(i64Ty, countAlloca, "jsonkeys.count");
        auto *daTy = getDynArrayStructTy();
        auto *daAlloca = createEntryBlockAlloca(curFunc, "jsonkeys.da", daTy);
        builder_->CreateStore(rawArr, builder_->CreateStructGEP(daTy, daAlloca, 0));
        builder_->CreateStore(count, builder_->CreateStructGEP(daTy, daAlloca, 1));
        builder_->CreateStore(count, builder_->CreateStructGEP(daTy, daAlloca, 2));
        return builder_->CreateLoad(daTy, daAlloca, "jsonkeys.da.val");
    }

    // === JSON DOM (parse-tree) ===
    if (funcName == "jsonParse" && node->getArgs().size() >= 1) {
        auto *sArg = visit(node->getArgs()[0].get());
        return builder_->CreateCall(getOrPanic("liva_json_parse"), {sArg}, "json.parse");
    }
    if (funcName == "jsonFreeDoc" && node->getArgs().size() >= 1) {
        auto *h = visit(node->getArgs()[0].get());
        return builder_->CreateCall(getOrPanic("liva_json_free_doc"), {h});
    }
    if (funcName == "jsonRoot" && node->getArgs().size() >= 1) {
        auto *h = visit(node->getArgs()[0].get());
        return builder_->CreateCall(getOrPanic("liva_json_root"), {h}, "json.root");
    }
    if (funcName == "jsonNodeKind" && node->getArgs().size() >= 1) {
        auto *h = visit(node->getArgs()[0].get());
        return builder_->CreateCall(getOrPanic("liva_json_node_kind"), {h}, "json.kind");
    }
    if (funcName == "jsonNodeAsInt" && node->getArgs().size() >= 1) {
        auto *h = visit(node->getArgs()[0].get());
        return builder_->CreateCall(getOrPanic("liva_json_node_as_int"), {h}, "json.asint");
    }
    if (funcName == "jsonNodeAsFloat" && node->getArgs().size() >= 1) {
        auto *h = visit(node->getArgs()[0].get());
        return builder_->CreateCall(getOrPanic("liva_json_node_as_float"), {h}, "json.asfloat");
    }
    if (funcName == "jsonNodeAsBool" && node->getArgs().size() >= 1) {
        auto *h = visit(node->getArgs()[0].get());
        auto *r = builder_->CreateCall(getOrPanic("liva_json_node_as_bool"), {h}, "json.asbool");
        return builder_->CreateTrunc(r, builder_->getInt1Ty(), "json.asbool.bool");
    }
    if (funcName == "jsonNodeAsString" && node->getArgs().size() >= 1) {
        auto *h = visit(node->getArgs()[0].get());
        return builder_->CreateCall(getOrPanic("liva_json_node_as_string"), {h}, "json.asstr");
    }
    if (funcName == "jsonToString" && node->getArgs().size() >= 1) {
        auto *h = visit(node->getArgs()[0].get());
        return builder_->CreateCall(getOrPanic("liva_json_to_string"), {h}, "json.tostr");
    }
    if (funcName == "jsonToStringPretty" && node->getArgs().size() >= 2) {
        auto *h = visit(node->getArgs()[0].get());
        auto *ind = visit(node->getArgs()[1].get());
        if (ind->getType()->isIntegerTy(64))
            ind = builder_->CreateTrunc(ind, builder_->getInt32Ty());
        return builder_->CreateCall(getOrPanic("liva_json_to_string_pretty"), {h, ind}, "json.tostrp");
    }
    if (funcName == "jsonObjGet" && node->getArgs().size() >= 2) {
        auto *h = visit(node->getArgs()[0].get());
        auto *key = visit(node->getArgs()[1].get());
        return builder_->CreateCall(getOrPanic("liva_json_obj_get"), {h, key}, "json.objget");
    }
    if (funcName == "jsonObjHas" && node->getArgs().size() >= 2) {
        auto *h = visit(node->getArgs()[0].get());
        auto *key = visit(node->getArgs()[1].get());
        auto *r = builder_->CreateCall(getOrPanic("liva_json_obj_has"), {h, key}, "json.objhas");
        return builder_->CreateTrunc(r, builder_->getInt1Ty(), "json.objhas.bool");
    }
    if (funcName == "jsonObjCount" && node->getArgs().size() >= 1) {
        auto *h = visit(node->getArgs()[0].get());
        return builder_->CreateCall(getOrPanic("liva_json_obj_count"), {h}, "json.objcount");
    }
    if (funcName == "jsonArrCount" && node->getArgs().size() >= 1) {
        auto *h = visit(node->getArgs()[0].get());
        return builder_->CreateCall(getOrPanic("liva_json_arr_count"), {h}, "json.arrcount");
    }
    if (funcName == "jsonArrAt" && node->getArgs().size() >= 2) {
        auto *h = visit(node->getArgs()[0].get());
        auto *idx = visit(node->getArgs()[1].get());
        return builder_->CreateCall(getOrPanic("liva_json_arr_at"), {h, idx}, "json.arrat");
    }

    // === JSON DOM Building / Mutation ===
    if (funcName == "jsonNewObject" && node->getArgs().empty())
        return builder_->CreateCall(getOrPanic("liva_json_new_object"), {}, "json.newobj");
    if (funcName == "jsonNewArray" && node->getArgs().empty())
        return builder_->CreateCall(getOrPanic("liva_json_new_array"), {}, "json.newarr");

    if (funcName == "jsonObjSetString" && node->getArgs().size() >= 4) {
        auto *d = visit(node->getArgs()[0].get());
        auto *n = visit(node->getArgs()[1].get());
        auto *k = visit(node->getArgs()[2].get());
        auto *v = visit(node->getArgs()[3].get());
        return builder_->CreateCall(getOrPanic("liva_json_obj_set_string"), {d, n, k, v});
    }
    if (funcName == "jsonObjSetInt" && node->getArgs().size() >= 4) {
        auto *d = visit(node->getArgs()[0].get());
        auto *n = visit(node->getArgs()[1].get());
        auto *k = visit(node->getArgs()[2].get());
        auto *v = visit(node->getArgs()[3].get());
        if (v->getType()->isIntegerTy(32))
            v = builder_->CreateSExt(v, builder_->getInt64Ty(), "json.int.sext");
        return builder_->CreateCall(getOrPanic("liva_json_obj_set_int"), {d, n, k, v});
    }
    if (funcName == "jsonObjSetFloat" && node->getArgs().size() >= 4) {
        auto *d = visit(node->getArgs()[0].get());
        auto *n = visit(node->getArgs()[1].get());
        auto *k = visit(node->getArgs()[2].get());
        auto *v = visit(node->getArgs()[3].get());
        return builder_->CreateCall(getOrPanic("liva_json_obj_set_float"), {d, n, k, v});
    }
    if (funcName == "jsonObjSetBool" && node->getArgs().size() >= 4) {
        auto *d = visit(node->getArgs()[0].get());
        auto *n = visit(node->getArgs()[1].get());
        auto *k = visit(node->getArgs()[2].get());
        auto *v = visit(node->getArgs()[3].get());
        v = builder_->CreateZExt(v, builder_->getInt8Ty(), "json.bool.zext");
        return builder_->CreateCall(getOrPanic("liva_json_obj_set_bool"), {d, n, k, v});
    }
    if (funcName == "jsonObjSetNull" && node->getArgs().size() >= 3) {
        auto *d = visit(node->getArgs()[0].get());
        auto *n = visit(node->getArgs()[1].get());
        auto *k = visit(node->getArgs()[2].get());
        return builder_->CreateCall(getOrPanic("liva_json_obj_set_null"), {d, n, k});
    }
    if (funcName == "jsonObjSetObject" && node->getArgs().size() >= 3) {
        auto *d = visit(node->getArgs()[0].get());
        auto *n = visit(node->getArgs()[1].get());
        auto *k = visit(node->getArgs()[2].get());
        return builder_->CreateCall(getOrPanic("liva_json_obj_set_object"), {d, n, k}, "json.setobj");
    }
    if (funcName == "jsonObjSetArray" && node->getArgs().size() >= 3) {
        auto *d = visit(node->getArgs()[0].get());
        auto *n = visit(node->getArgs()[1].get());
        auto *k = visit(node->getArgs()[2].get());
        return builder_->CreateCall(getOrPanic("liva_json_obj_set_array"), {d, n, k}, "json.setarr");
    }
    if (funcName == "jsonObjRemove" && node->getArgs().size() >= 2) {
        auto *n = visit(node->getArgs()[0].get());
        auto *k = visit(node->getArgs()[1].get());
        return builder_->CreateCall(getOrPanic("liva_json_obj_remove"), {n, k});
    }
    if (funcName == "jsonArrAddString" && node->getArgs().size() >= 3) {
        auto *d = visit(node->getArgs()[0].get());
        auto *n = visit(node->getArgs()[1].get());
        auto *v = visit(node->getArgs()[2].get());
        return builder_->CreateCall(getOrPanic("liva_json_arr_add_string"), {d, n, v});
    }
    if (funcName == "jsonArrAddInt" && node->getArgs().size() >= 3) {
        auto *d = visit(node->getArgs()[0].get());
        auto *n = visit(node->getArgs()[1].get());
        auto *v = visit(node->getArgs()[2].get());
        if (v->getType()->isIntegerTy(32))
            v = builder_->CreateSExt(v, builder_->getInt64Ty(), "json.aint.sext");
        return builder_->CreateCall(getOrPanic("liva_json_arr_add_int"), {d, n, v});
    }
    if (funcName == "jsonArrAddFloat" && node->getArgs().size() >= 3) {
        auto *d = visit(node->getArgs()[0].get());
        auto *n = visit(node->getArgs()[1].get());
        auto *v = visit(node->getArgs()[2].get());
        return builder_->CreateCall(getOrPanic("liva_json_arr_add_float"), {d, n, v});
    }
    if (funcName == "jsonArrAddBool" && node->getArgs().size() >= 3) {
        auto *d = visit(node->getArgs()[0].get());
        auto *n = visit(node->getArgs()[1].get());
        auto *v = visit(node->getArgs()[2].get());
        v = builder_->CreateZExt(v, builder_->getInt8Ty(), "json.abool.zext");
        return builder_->CreateCall(getOrPanic("liva_json_arr_add_bool"), {d, n, v});
    }
    if (funcName == "jsonArrAddNull" && node->getArgs().size() >= 2) {
        auto *d = visit(node->getArgs()[0].get());
        auto *n = visit(node->getArgs()[1].get());
        return builder_->CreateCall(getOrPanic("liva_json_arr_add_null"), {d, n});
    }
    if (funcName == "jsonArrAddObject" && node->getArgs().size() >= 2) {
        auto *d = visit(node->getArgs()[0].get());
        auto *n = visit(node->getArgs()[1].get());
        return builder_->CreateCall(getOrPanic("liva_json_arr_add_object"), {d, n}, "json.addobj");
    }
    if (funcName == "jsonArrAddArray" && node->getArgs().size() >= 2) {
        auto *d = visit(node->getArgs()[0].get());
        auto *n = visit(node->getArgs()[1].get());
        return builder_->CreateCall(getOrPanic("liva_json_arr_add_array"), {d, n}, "json.addarr");
    }
    if (funcName == "jsonPathGet" && node->getArgs().size() >= 2) {
        auto *h = visit(node->getArgs()[0].get());
        auto *path = visit(node->getArgs()[1].get());
        return builder_->CreateCall(getOrPanic("liva_json_path_get"), {h, path}, "json.pathget");
    }
    if (funcName == "jsonPathSetString" && node->getArgs().size() >= 4) {
        auto *d=visit(node->getArgs()[0].get()); auto *n=visit(node->getArgs()[1].get());
        auto *pth=visit(node->getArgs()[2].get()); auto *v=visit(node->getArgs()[3].get());
        return builder_->CreateCall(getOrPanic("liva_json_path_set_string"), {d, n, pth, v});
    }
    if (funcName == "jsonPathSetInt" && node->getArgs().size() >= 4) {
        auto *d=visit(node->getArgs()[0].get()); auto *n=visit(node->getArgs()[1].get());
        auto *pth=visit(node->getArgs()[2].get()); auto *v=visit(node->getArgs()[3].get());
        return builder_->CreateCall(getOrPanic("liva_json_path_set_int"), {d, n, pth, v});
    }
    if (funcName == "jsonPathSetFloat" && node->getArgs().size() >= 4) {
        auto *d=visit(node->getArgs()[0].get()); auto *n=visit(node->getArgs()[1].get());
        auto *pth=visit(node->getArgs()[2].get()); auto *v=visit(node->getArgs()[3].get());
        return builder_->CreateCall(getOrPanic("liva_json_path_set_float"), {d, n, pth, v});
    }
    if (funcName == "jsonPathSetBool" && node->getArgs().size() >= 4) {
        auto *d=visit(node->getArgs()[0].get()); auto *n=visit(node->getArgs()[1].get());
        auto *pth=visit(node->getArgs()[2].get()); auto *v=visit(node->getArgs()[3].get());
        v = builder_->CreateZExt(v, builder_->getInt8Ty(), "json.pbool.zext");
        return builder_->CreateCall(getOrPanic("liva_json_path_set_bool"), {d, n, pth, v});
    }

    // === Stdlib: UI (wxWidgets wrapper) ===

    // Helper lambda: emit a callback call decomposing Liva closure into func+env
    auto emitCallbackCall = [&](const std::string &cFuncName, int32_t handleArgIdx,
                                 int32_t closureArgIdx) -> llvm::Value * {
        auto *handle = visit(node->getArgs()[handleArgIdx].get());
        auto *closureVal = visit(node->getArgs()[closureArgIdx].get());
        if (!handle || !closureVal) return nullptr;
        auto *closureObjTy = getClosureObjTy();
        auto *alloca = createEntryBlockAlloca(
            builder_->GetInsertBlock()->getParent(), "cb.tmp", closureObjTy);
        builder_->CreateStore(closureVal, alloca);
        auto *funcPtr = builder_->CreateLoad(
            llvm::PointerType::getUnqual(*context_),
            builder_->CreateStructGEP(closureObjTy, alloca, 0));
        auto *envPtr = builder_->CreateLoad(
            llvm::PointerType::getUnqual(*context_),
            builder_->CreateStructGEP(closureObjTy, alloca, 1));
        // Free-function / ordinary-method path: the env stays on the caller's
        // stack, so pass size 0 (no heap-own). The inline closure-literal fast
        // path in visitCallExpr passes a real size instead.
        auto *i32Ty = builder_->getInt32Ty();
        builder_->CreateCall(getOrPanic(cFuncName.c_str()),
                             {handle, funcPtr, envPtr, llvm::ConstantInt::get(i32Ty, 0)});
        return llvm::Constant::getNullValue(i32Ty);
    };

    // appInit() -> void
    if (funcName == "appInit" && node->getArgs().empty()) {
        builder_->CreateCall(getOrPanic("liva_ui_app_init"), {});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }

    // appRun() -> void
    if (funcName == "appRun" && node->getArgs().empty()) {
        builder_->CreateCall(getOrPanic("liva_ui_app_run"), {});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }

    // appQuit() -> void
    if (funcName == "appQuit" && node->getArgs().empty()) {
        builder_->CreateCall(getOrPanic("liva_ui_app_quit"), {});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }

    // createWindow(w, h, title) -> i32
    if (funcName == "createWindow" && node->getArgs().size() >= 3) {
        auto *w = visit(node->getArgs()[0].get());
        auto *h = visit(node->getArgs()[1].get());
        auto *title = visit(node->getArgs()[2].get());
        if (!w || !h || !title) return nullptr;
        return builder_->CreateCall(getOrPanic("liva_ui_create_window"), {w, h, title}, "ui.win");
    }

    // windowShow(handle, show) -> void
    if (funcName == "windowShow" && node->getArgs().size() >= 2) {
        auto *handle = visit(node->getArgs()[0].get());
        auto *show = visit(node->getArgs()[1].get());
        if (!handle || !show) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_window_show"), {handle, show});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }

    // windowSetTitle(handle, title) -> void
    if (funcName == "windowSetTitle" && node->getArgs().size() >= 2) {
        auto *handle = visit(node->getArgs()[0].get());
        auto *title = visit(node->getArgs()[1].get());
        if (!handle || !title) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_window_set_title"), {handle, title});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }

    // windowGetWidth(handle) -> i32
    if (funcName == "windowGetWidth" && node->getArgs().size() >= 1) {
        auto *handle = visit(node->getArgs()[0].get());
        if (!handle) return nullptr;
        return builder_->CreateCall(getOrPanic("liva_ui_window_get_width"), {handle}, "ui.ww");
    }

    // windowGetHeight(handle) -> i32
    if (funcName == "windowGetHeight" && node->getArgs().size() >= 1) {
        auto *handle = visit(node->getArgs()[0].get());
        if (!handle) return nullptr;
        return builder_->CreateCall(getOrPanic("liva_ui_window_get_height"), {handle}, "ui.wh");
    }

    // windowOnClose(handle, callback) -> void
    if (funcName == "windowOnClose" && node->getArgs().size() >= 2) {
        return emitCallbackCall("liva_ui_window_on_close", 0, 1);
    }

    // Widget creation: create*(parent) -> i32
    // Single-arg parent versions
    for (const auto &[livaName, cName] : std::initializer_list<std::pair<const char *, const char *>>{
             {"createPanel", "liva_ui_create_panel"},
             {"createListBox", "liva_ui_create_listbox"},
             {"createTabView", "liva_ui_create_tabview"},
             {"createScrollView", "liva_ui_create_scrollview"},
             {"createDivider", "liva_ui_create_divider"},
             {"createCanvas", "liva_ui_create_canvas"}}) {
        if (funcName == livaName && node->getArgs().size() >= 1) {
            auto *parent = visit(node->getArgs()[0].get());
            if (!parent) return nullptr;
            return builder_->CreateCall(getOrPanic(cName), {parent}, "ui.w");
        }
    }

    // Widget creation: create*(parent, text) -> i32
    for (const auto &[livaName, cName] : std::initializer_list<std::pair<const char *, const char *>>{
             {"createButton", "liva_ui_create_button"},
             {"createLabel", "liva_ui_create_label"},
             {"createTextInput", "liva_ui_create_textinput"},
             {"createCheckbox", "liva_ui_create_checkbox"},
             {"createTextArea", "liva_ui_create_textarea"},
             {"createRadioGroup", "liva_ui_create_radiogroup"},
             {"createDropdown", "liva_ui_create_dropdown"},
             {"createImageView", "liva_ui_create_imageview"}}) {
        if (funcName == livaName && node->getArgs().size() >= 2) {
            auto *parent = visit(node->getArgs()[0].get());
            auto *text = visit(node->getArgs()[1].get());
            if (!parent || !text) return nullptr;
            return builder_->CreateCall(getOrPanic(cName), {parent, text}, "ui.w");
        }
    }

    // createSlider(parent, min, max, val) -> i32
    if (funcName == "createSlider" && node->getArgs().size() >= 4) {
        auto *parent = visit(node->getArgs()[0].get());
        auto *minV = visit(node->getArgs()[1].get());
        auto *maxV = visit(node->getArgs()[2].get());
        auto *val = visit(node->getArgs()[3].get());
        if (!parent || !minV || !maxV || !val) return nullptr;
        return builder_->CreateCall(getOrPanic("liva_ui_create_slider"),
                                    {parent, minV, maxV, val}, "ui.sl");
    }

    // createProgressBar(parent, range) -> i32
    if (funcName == "createProgressBar" && node->getArgs().size() >= 2) {
        auto *parent = visit(node->getArgs()[0].get());
        auto *range = visit(node->getArgs()[1].get());
        if (!parent || !range) return nullptr;
        return builder_->CreateCall(getOrPanic("liva_ui_create_progressbar"),
                                    {parent, range}, "ui.pb");
    }

    // setText(handle, text) -> void
    if (funcName == "setText" && node->getArgs().size() >= 2) {
        auto *handle = visit(node->getArgs()[0].get());
        auto *text = visit(node->getArgs()[1].get());
        if (!handle || !text) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_set_text"), {handle, text});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }

    // getText(handle) -> string
    if (funcName == "getText" && node->getArgs().size() >= 1) {
        auto *handle = visit(node->getArgs()[0].get());
        if (!handle) return nullptr;
        return builder_->CreateCall(getOrPanic("liva_ui_get_text"), {handle}, "ui.gt");
    }

    // setValue(handle, val) -> void
    if (funcName == "setValue" && node->getArgs().size() >= 2) {
        auto *handle = visit(node->getArgs()[0].get());
        auto *val = visit(node->getArgs()[1].get());
        if (!handle || !val) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_set_value"), {handle, val});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }

    // getValue(handle) -> i32
    if (funcName == "getValue" && node->getArgs().size() >= 1) {
        auto *handle = visit(node->getArgs()[0].get());
        if (!handle) return nullptr;
        return builder_->CreateCall(getOrPanic("liva_ui_get_value"), {handle}, "ui.gv");
    }

    // setEnabled(handle, enabled) -> void
    if (funcName == "setEnabled" && node->getArgs().size() >= 2) {
        auto *handle = visit(node->getArgs()[0].get());
        auto *flag = visit(node->getArgs()[1].get());
        if (!handle || !flag) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_set_enabled"), {handle, flag});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }

    // setVisible(handle, visible) -> void
    if (funcName == "setVisible" && node->getArgs().size() >= 2) {
        auto *handle = visit(node->getArgs()[0].get());
        auto *flag = visit(node->getArgs()[1].get());
        if (!handle || !flag) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_set_visible"), {handle, flag});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }

    // setWidgetSize(handle, w, h) -> void
    if (funcName == "setWidgetSize" && node->getArgs().size() >= 3) {
        auto *handle = visit(node->getArgs()[0].get());
        auto *w = visit(node->getArgs()[1].get());
        auto *h = visit(node->getArgs()[2].get());
        if (!handle || !w || !h) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_set_size"), {handle, w, h});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }

    // setBounds(handle, x, y, w, h) -> void
    if (funcName == "setBounds" && node->getArgs().size() >= 5) {
        auto *handle = visit(node->getArgs()[0].get());
        auto *x = visit(node->getArgs()[1].get());
        auto *y = visit(node->getArgs()[2].get());
        auto *w = visit(node->getArgs()[3].get());
        auto *h = visit(node->getArgs()[4].get());
        if (!handle || !x || !y || !w || !h) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_set_bounds"), {handle, x, y, w, h});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }

    // ── Phase 2: menu / statusbar / toolbar intrinsics ──────────────────
    // createMenuBar() -> i32
    if (funcName == "createMenuBar" && node->getArgs().empty())
        return builder_->CreateCall(getOrPanic("liva_ui_create_menu_bar"), {}, "ui.mb");
    // createMenu(title) -> i32
    if (funcName == "createMenu" && node->getArgs().size() >= 1) {
        auto *title = visit(node->getArgs()[0].get());
        if (!title) return nullptr;
        return builder_->CreateCall(getOrPanic("liva_ui_create_menu"), {title}, "ui.menu");
    }
    // menuAddItem(menu, label) -> i32
    if (funcName == "menuAddItem" && node->getArgs().size() >= 2) {
        auto *m = visit(node->getArgs()[0].get());
        auto *l = visit(node->getArgs()[1].get());
        if (!m || !l) return nullptr;
        return builder_->CreateCall(getOrPanic("liva_ui_menu_add_item"), {m, l}, "ui.mi");
    }
    // menuAddCheckItem(menu, label) -> i32
    if (funcName == "menuAddCheckItem" && node->getArgs().size() >= 2) {
        auto *m = visit(node->getArgs()[0].get());
        auto *l = visit(node->getArgs()[1].get());
        if (!m || !l) return nullptr;
        return builder_->CreateCall(getOrPanic("liva_ui_menu_add_check_item"), {m, l}, "ui.mci");
    }
    // menuAddSeparator(menu) -> void
    if (funcName == "menuAddSeparator" && node->getArgs().size() >= 1) {
        auto *m = visit(node->getArgs()[0].get());
        if (!m) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_menu_add_separator"), {m});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }
    // menuAddSubmenu(menu, label, sub) -> void
    if (funcName == "menuAddSubmenu" && node->getArgs().size() >= 3) {
        auto *m = visit(node->getArgs()[0].get());
        auto *l = visit(node->getArgs()[1].get());
        auto *s = visit(node->getArgs()[2].get());
        if (!m || !l || !s) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_menu_add_submenu"), {m, l, s});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }
    // menuBarAddMenu(bar, menu) -> void
    if (funcName == "menuBarAddMenu" && node->getArgs().size() >= 2) {
        auto *b = visit(node->getArgs()[0].get());
        auto *m = visit(node->getArgs()[1].get());
        if (!b || !m) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_menu_bar_add_menu"), {b, m});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }
    // windowSetMenuBar(window, bar) -> void
    if (funcName == "windowSetMenuBar" && node->getArgs().size() >= 2) {
        auto *w = visit(node->getArgs()[0].get());
        auto *b = visit(node->getArgs()[1].get());
        if (!w || !b) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_window_set_menu_bar"), {w, b});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }
    // menuItemSetEnabled(item, enabled) -> void
    if (funcName == "menuItemSetEnabled" && node->getArgs().size() >= 2) {
        auto *i = visit(node->getArgs()[0].get());
        auto *e = visit(node->getArgs()[1].get());
        if (!i || !e) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_menu_item_set_enabled"), {i, e});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }
    // menuItemSetChecked(item, checked) -> void
    if (funcName == "menuItemSetChecked" && node->getArgs().size() >= 2) {
        auto *i = visit(node->getArgs()[0].get());
        auto *c = visit(node->getArgs()[1].get());
        if (!i || !c) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_menu_item_set_checked"), {i, c});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }
    // menuPopup(menu, target) -> void
    if (funcName == "menuPopup" && node->getArgs().size() >= 2) {
        auto *m = visit(node->getArgs()[0].get());
        auto *t = visit(node->getArgs()[1].get());
        if (!m || !t) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_menu_popup"), {m, t});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }
    // createStatusBar(window, fieldCount) -> i32
    if (funcName == "createStatusBar" && node->getArgs().size() >= 2) {
        auto *w = visit(node->getArgs()[0].get());
        auto *fc = visit(node->getArgs()[1].get());
        if (!w || !fc) return nullptr;
        return builder_->CreateCall(getOrPanic("liva_ui_create_status_bar"), {w, fc}, "ui.sb");
    }
    // statusBarSetText(sb, field, text) -> void
    if (funcName == "statusBarSetText" && node->getArgs().size() >= 3) {
        auto *s = visit(node->getArgs()[0].get());
        auto *f = visit(node->getArgs()[1].get());
        auto *t = visit(node->getArgs()[2].get());
        if (!s || !f || !t) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_status_bar_set_text"), {s, f, t});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }
    // createToolbar(window) -> i32
    if (funcName == "createToolbar" && node->getArgs().size() >= 1) {
        auto *w = visit(node->getArgs()[0].get());
        if (!w) return nullptr;
        return builder_->CreateCall(getOrPanic("liva_ui_create_toolbar"), {w}, "ui.tb");
    }
    // toolbarAddTool(tb, label) -> i32
    if (funcName == "toolbarAddTool" && node->getArgs().size() >= 2) {
        auto *tb = visit(node->getArgs()[0].get());
        auto *l = visit(node->getArgs()[1].get());
        if (!tb || !l) return nullptr;
        return builder_->CreateCall(getOrPanic("liva_ui_toolbar_add_tool"), {tb, l}, "ui.tool");
    }
    // toolbarAddSeparator(tb) -> void
    if (funcName == "toolbarAddSeparator" && node->getArgs().size() >= 1) {
        auto *tb = visit(node->getArgs()[0].get());
        if (!tb) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_toolbar_add_separator"), {tb});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }
    // toolbarRealize(tb) -> void
    if (funcName == "toolbarRealize" && node->getArgs().size() >= 1) {
        auto *tb = visit(node->getArgs()[0].get());
        if (!tb) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_toolbar_realize"), {tb});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }
    // toolItemSetEnabled(tool, enabled) -> void
    if (funcName == "toolItemSetEnabled" && node->getArgs().size() >= 2) {
        auto *t = visit(node->getArgs()[0].get());
        auto *e = visit(node->getArgs()[1].get());
        if (!t || !e) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_tool_item_set_enabled"), {t, e});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }
    // ── Phase 3: new widgets ─────────────────────────────────────────
    // createSpinCtrl(parent, min, max, val) -> i32
    if (funcName == "createSpinCtrl" && node->getArgs().size() >= 4) {
        auto *parent = visit(node->getArgs()[0].get());
        auto *minV = visit(node->getArgs()[1].get());
        auto *maxV = visit(node->getArgs()[2].get());
        auto *val = visit(node->getArgs()[3].get());
        if (!parent || !minV || !maxV || !val) return nullptr;
        return builder_->CreateCall(getOrPanic("liva_ui_create_spin_ctrl"),
                                    {parent, minV, maxV, val}, "ui.spin");
    }
    // createDatePicker(parent) -> i32
    if (funcName == "createDatePicker" && node->getArgs().size() >= 1) {
        auto *parent = visit(node->getArgs()[0].get());
        if (!parent) return nullptr;
        return builder_->CreateCall(getOrPanic("liva_ui_create_date_picker"),
                                    {parent}, "ui.date");
    }
    // dateGetValue(handle) -> string
    if (funcName == "dateGetValue" && node->getArgs().size() >= 1) {
        auto *handle = visit(node->getArgs()[0].get());
        if (!handle) return nullptr;
        return builder_->CreateCall(getOrPanic("liva_ui_date_get_value"),
                                    {handle}, "ui.dgv");
    }
    // createComboBox(parent, value) -> i32
    if (funcName == "createComboBox" && node->getArgs().size() >= 2) {
        auto *parent = visit(node->getArgs()[0].get());
        auto *value = visit(node->getArgs()[1].get());
        if (!parent || !value) return nullptr;
        return builder_->CreateCall(getOrPanic("liva_ui_create_combo_box"),
                                    {parent, value}, "ui.combo");
    }
    // comboAddItem(handle, item) -> void
    if (funcName == "comboAddItem" && node->getArgs().size() >= 2) {
        auto *handle = visit(node->getArgs()[0].get());
        auto *item = visit(node->getArgs()[1].get());
        if (!handle || !item) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_combo_add_item"), {handle, item});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }
    // createTreeView(parent) -> i32
    if (funcName == "createTreeView" && node->getArgs().size() >= 1) {
        auto *parent = visit(node->getArgs()[0].get());
        if (!parent) return nullptr;
        return builder_->CreateCall(getOrPanic("liva_ui_create_tree_view"),
                                    {parent}, "ui.tree");
    }
    // treeAddRoot(handle, label) -> i32
    if (funcName == "treeAddRoot" && node->getArgs().size() >= 2) {
        auto *handle = visit(node->getArgs()[0].get());
        auto *label = visit(node->getArgs()[1].get());
        if (!handle || !label) return nullptr;
        return builder_->CreateCall(getOrPanic("liva_ui_tree_add_root"),
                                    {handle, label}, "ui.troot");
    }
    // treeAddNode(handle, parentNode, label) -> i32
    if (funcName == "treeAddNode" && node->getArgs().size() >= 3) {
        auto *handle = visit(node->getArgs()[0].get());
        auto *parentNode = visit(node->getArgs()[1].get());
        auto *label = visit(node->getArgs()[2].get());
        if (!handle || !parentNode || !label) return nullptr;
        return builder_->CreateCall(getOrPanic("liva_ui_tree_add_node"),
                                    {handle, parentNode, label}, "ui.tnode");
    }
    // treeGetSelection(handle) -> i32
    if (funcName == "treeGetSelection" && node->getArgs().size() >= 1) {
        auto *handle = visit(node->getArgs()[0].get());
        if (!handle) return nullptr;
        return builder_->CreateCall(getOrPanic("liva_ui_tree_get_selection"),
                                    {handle}, "ui.tsel");
    }
    // createDataGrid(parent, rows, cols) -> i32
    if (funcName == "createDataGrid" && node->getArgs().size() >= 3) {
        auto *parent = visit(node->getArgs()[0].get());
        auto *rows = visit(node->getArgs()[1].get());
        auto *cols = visit(node->getArgs()[2].get());
        if (!parent || !rows || !cols) return nullptr;
        return builder_->CreateCall(getOrPanic("liva_ui_create_data_grid"),
                                    {parent, rows, cols}, "ui.grid2");
    }
    // gridSetCell(handle, row, col, text) -> void
    if (funcName == "gridSetCell" && node->getArgs().size() >= 4) {
        auto *handle = visit(node->getArgs()[0].get());
        auto *row = visit(node->getArgs()[1].get());
        auto *col = visit(node->getArgs()[2].get());
        auto *text = visit(node->getArgs()[3].get());
        if (!handle || !row || !col || !text) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_grid_set_cell"),
                             {handle, row, col, text});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }
    // gridGetCell(handle, row, col) -> string
    if (funcName == "gridGetCell" && node->getArgs().size() >= 3) {
        auto *handle = visit(node->getArgs()[0].get());
        auto *row = visit(node->getArgs()[1].get());
        auto *col = visit(node->getArgs()[2].get());
        if (!handle || !row || !col) return nullptr;
        return builder_->CreateCall(getOrPanic("liva_ui_grid_get_cell"),
                                    {handle, row, col}, "ui.gcell");
    }
    // gridSetColLabel(handle, col, text) -> void
    if (funcName == "gridSetColLabel" && node->getArgs().size() >= 3) {
        auto *handle = visit(node->getArgs()[0].get());
        auto *col = visit(node->getArgs()[1].get());
        auto *text = visit(node->getArgs()[2].get());
        if (!handle || !col || !text) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_grid_set_col_label"),
                             {handle, col, text});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }
    // createSplitter(parent) -> i32
    if (funcName == "createSplitter" && node->getArgs().size() >= 1) {
        auto *parent = visit(node->getArgs()[0].get());
        if (!parent) return nullptr;
        return builder_->CreateCall(getOrPanic("liva_ui_create_splitter"),
                                    {parent}, "ui.split");
    }
    // splitterSplitV(handle, left, right) -> void
    if (funcName == "splitterSplitV" && node->getArgs().size() >= 3) {
        auto *handle = visit(node->getArgs()[0].get());
        auto *left = visit(node->getArgs()[1].get());
        auto *right = visit(node->getArgs()[2].get());
        if (!handle || !left || !right) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_splitter_split_v"),
                             {handle, left, right});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }
    // splitterSplitH(handle, top, bottom) -> void
    if (funcName == "splitterSplitH" && node->getArgs().size() >= 3) {
        auto *handle = visit(node->getArgs()[0].get());
        auto *top = visit(node->getArgs()[1].get());
        auto *bottom = visit(node->getArgs()[2].get());
        if (!handle || !top || !bottom) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_splitter_split_h"),
                             {handle, top, bottom});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }
    // splitterSetSash(handle, px) -> void
    if (funcName == "splitterSetSash" && node->getArgs().size() >= 2) {
        auto *handle = visit(node->getArgs()[0].get());
        auto *px = visit(node->getArgs()[1].get());
        if (!handle || !px) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_splitter_set_sash"), {handle, px});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }
    // ── Phase 4: Align/Anchors layout ────────────────────────────────
    // setAlign(handle, align) -> void  (align: basit enum → i32 discriminant)
    if (funcName == "setAlign" && node->getArgs().size() >= 2) {
        auto *handle = visit(node->getArgs()[0].get());
        auto *align = visit(node->getArgs()[1].get());
        if (!handle || !align) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_set_align"), {handle, align});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }
    // setAnchors(handle, left, top, right, bottom) -> void
    if (funcName == "setAnchors" && node->getArgs().size() >= 5) {
        auto *handle = visit(node->getArgs()[0].get());
        auto *l = visit(node->getArgs()[1].get());
        auto *t = visit(node->getArgs()[2].get());
        auto *r = visit(node->getArgs()[3].get());
        auto *b = visit(node->getArgs()[4].get());
        if (!handle || !l || !t || !r || !b) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_set_anchors"), {handle, l, t, r, b});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }
    // ── Phase 5: data binding ────────────────────────────────────────
    // modelCreate() -> i32
    if (funcName == "modelCreate" && node->getArgs().empty()) {
        return builder_->CreateCall(getOrPanic("liva_ui_model_create"), {}, "ui.model");
    }
    // modelSetText(model, key, val) -> void
    if (funcName == "modelSetText" && node->getArgs().size() >= 3) {
        auto *m = visit(node->getArgs()[0].get());
        auto *k = visit(node->getArgs()[1].get());
        auto *v = visit(node->getArgs()[2].get());
        if (!m || !k || !v) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_model_set_text"), {m, k, v});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }
    // modelGetText(model, key) -> string
    if (funcName == "modelGetText" && node->getArgs().size() >= 2) {
        auto *m = visit(node->getArgs()[0].get());
        auto *k = visit(node->getArgs()[1].get());
        if (!m || !k) return nullptr;
        return builder_->CreateCall(getOrPanic("liva_ui_model_get_text"), {m, k}, "ui.mget");
    }
    // modelBindText(model, key, widget) -> void
    if (funcName == "modelBindText" && node->getArgs().size() >= 3) {
        auto *m = visit(node->getArgs()[0].get());
        auto *k = visit(node->getArgs()[1].get());
        auto *w = visit(node->getArgs()[2].get());
        if (!m || !k || !w) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_model_bind_text"), {m, k, w});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }
    // modelSetInt(model, key, val) -> void
    if (funcName == "modelSetInt" && node->getArgs().size() >= 3) {
        auto *m = visit(node->getArgs()[0].get());
        auto *k = visit(node->getArgs()[1].get());
        auto *v = visit(node->getArgs()[2].get());
        if (!m || !k || !v) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_model_set_int"), {m, k, v});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }
    // modelGetInt(model, key) -> i32
    if (funcName == "modelGetInt" && node->getArgs().size() >= 2) {
        auto *m = visit(node->getArgs()[0].get());
        auto *k = visit(node->getArgs()[1].get());
        if (!m || !k) return nullptr;
        return builder_->CreateCall(getOrPanic("liva_ui_model_get_int"), {m, k}, "ui.mgeti");
    }
    // modelBindInt(model, key, widget) -> void
    if (funcName == "modelBindInt" && node->getArgs().size() >= 3) {
        auto *m = visit(node->getArgs()[0].get());
        auto *k = visit(node->getArgs()[1].get());
        auto *w = visit(node->getArgs()[2].get());
        if (!m || !k || !w) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_model_bind_int"), {m, k, w});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }
    // ── Phase 6: collection binding ──────────────────────────────────
    // modelBindList(model, key, widget) -> void
    if (funcName == "modelBindList" && node->getArgs().size() >= 3) {
        auto *m = visit(node->getArgs()[0].get());
        auto *k = visit(node->getArgs()[1].get());
        auto *w = visit(node->getArgs()[2].get());
        if (!m || !k || !w) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_model_bind_list"), {m, k, w});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }
    // modelListAdd(model, key, item) -> void
    if (funcName == "modelListAdd" && node->getArgs().size() >= 3) {
        auto *m = visit(node->getArgs()[0].get());
        auto *k = visit(node->getArgs()[1].get());
        auto *v = visit(node->getArgs()[2].get());
        if (!m || !k || !v) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_model_list_add"), {m, k, v});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }
    // modelListClear(model, key) -> void
    if (funcName == "modelListClear" && node->getArgs().size() >= 2) {
        auto *m = visit(node->getArgs()[0].get());
        auto *k = visit(node->getArgs()[1].get());
        if (!m || !k) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_model_list_clear"), {m, k});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }
    // modelListCount(model, key) -> i32
    if (funcName == "modelListCount" && node->getArgs().size() >= 2) {
        auto *m = visit(node->getArgs()[0].get());
        auto *k = visit(node->getArgs()[1].get());
        if (!m || !k) return nullptr;
        return builder_->CreateCall(getOrPanic("liva_ui_model_list_count"), {m, k}, "ui.mlcount");
    }
    // ── Phase 6.1: list readback ─────────────────────────────────────
    // modelListGet(model, key, index) -> string
    if (funcName == "modelListGet" && node->getArgs().size() >= 3) {
        auto *m = visit(node->getArgs()[0].get());
        auto *k = visit(node->getArgs()[1].get());
        auto *i = visit(node->getArgs()[2].get());
        if (!m || !k || !i) return nullptr;
        return builder_->CreateCall(getOrPanic("liva_ui_model_list_get"),
                                    {m, k, i}, "ui.mlget");
    }
    // Closure-taking free-function forms (called from class methods; stack env, size 0)
    if (funcName == "menuItemOnClick" && node->getArgs().size() >= 2)
        return emitCallbackCall("liva_ui_menu_item_on_click", 0, 1);
    if (funcName == "toolItemOnClick" && node->getArgs().size() >= 2)
        return emitCallbackCall("liva_ui_tool_item_on_click", 0, 1);
    if (funcName == "onRightClick" && node->getArgs().size() >= 2)
        return emitCallbackCall("liva_ui_on_right_click", 0, 1);

    // setWidgetFont(handle, size, bold) -> void
    if (funcName == "setWidgetFont" && node->getArgs().size() >= 3) {
        auto *handle = visit(node->getArgs()[0].get());
        auto *size = visit(node->getArgs()[1].get());
        auto *bold = visit(node->getArgs()[2].get());
        if (!handle || !size || !bold) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_set_font"), {handle, size, bold});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }

    // setBgColor(handle, r, g, b) -> void
    if (funcName == "setBgColor" && node->getArgs().size() >= 4) {
        auto *handle = visit(node->getArgs()[0].get());
        auto *r = visit(node->getArgs()[1].get());
        auto *g = visit(node->getArgs()[2].get());
        auto *b = visit(node->getArgs()[3].get());
        if (!handle || !r || !g || !b) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_set_bg_color"), {handle, r, g, b});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }

    // setFgColor(handle, r, g, b) -> void
    if (funcName == "setFgColor" && node->getArgs().size() >= 4) {
        auto *handle = visit(node->getArgs()[0].get());
        auto *r = visit(node->getArgs()[1].get());
        auto *g = visit(node->getArgs()[2].get());
        auto *b = visit(node->getArgs()[3].get());
        if (!handle || !r || !g || !b) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_set_fg_color"), {handle, r, g, b});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }

    // setTooltip(handle, text) -> void
    if (funcName == "setTooltip" && node->getArgs().size() >= 2) {
        auto *handle = visit(node->getArgs()[0].get());
        auto *text = visit(node->getArgs()[1].get());
        if (!handle || !text) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_set_tooltip"), {handle, text});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }

    // destroyWidget(handle) -> void
    if (funcName == "destroyWidget" && node->getArgs().size() >= 1) {
        auto *handle = visit(node->getArgs()[0].get());
        if (!handle) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_destroy_widget"), {handle});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }

    // Layout: createVBoxSizer() -> i32
    if (funcName == "createVBoxSizer" && node->getArgs().empty()) {
        return builder_->CreateCall(getOrPanic("liva_ui_create_vbox_sizer"), {}, "ui.vbox");
    }

    // createHBoxSizer() -> i32
    if (funcName == "createHBoxSizer" && node->getArgs().empty()) {
        return builder_->CreateCall(getOrPanic("liva_ui_create_hbox_sizer"), {}, "ui.hbox");
    }

    // createGridSizer(rows, cols, hgap, vgap) -> i32
    if (funcName == "createGridSizer" && node->getArgs().size() >= 4) {
        auto *rows = visit(node->getArgs()[0].get());
        auto *cols = visit(node->getArgs()[1].get());
        auto *hgap = visit(node->getArgs()[2].get());
        auto *vgap = visit(node->getArgs()[3].get());
        if (!rows || !cols || !hgap || !vgap) return nullptr;
        return builder_->CreateCall(getOrPanic("liva_ui_create_grid_sizer"),
                                    {rows, cols, hgap, vgap}, "ui.grid");
    }

    // createFlexGridSizer(rows, cols, hgap, vgap) -> i32
    if (funcName == "createFlexGridSizer" && node->getArgs().size() >= 4) {
        auto *rows = visit(node->getArgs()[0].get());
        auto *cols = visit(node->getArgs()[1].get());
        auto *hgap = visit(node->getArgs()[2].get());
        auto *vgap = visit(node->getArgs()[3].get());
        if (!rows || !cols || !hgap || !vgap) return nullptr;
        return builder_->CreateCall(getOrPanic("liva_ui_create_flex_grid_sizer"),
                                    {rows, cols, hgap, vgap}, "ui.fgrid");
    }

    // sizerAdd(sizer, widget, proportion, flags, border) -> void
    if (funcName == "sizerAdd" && node->getArgs().size() >= 5) {
        auto *sizer = visit(node->getArgs()[0].get());
        auto *widget = visit(node->getArgs()[1].get());
        auto *prop = visit(node->getArgs()[2].get());
        auto *flags = visit(node->getArgs()[3].get());
        auto *border = visit(node->getArgs()[4].get());
        if (!sizer || !widget || !prop || !flags || !border) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_sizer_add"),
                             {sizer, widget, prop, flags, border});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }

    // setSizer(parent, sizer) -> void
    if (funcName == "setSizer" && node->getArgs().size() >= 2) {
        auto *parent = visit(node->getArgs()[0].get());
        auto *sizer = visit(node->getArgs()[1].get());
        if (!parent || !sizer) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_set_sizer"), {parent, sizer});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }

    // Event callbacks: onClick/onChange/onSelect/onKey(handle, closure) -> void
    if (funcName == "onClick" && node->getArgs().size() >= 2)
        return emitCallbackCall("liva_ui_on_click", 0, 1);
    if (funcName == "onChange" && node->getArgs().size() >= 2)
        return emitCallbackCall("liva_ui_on_change", 0, 1);
    if (funcName == "onSelect" && node->getArgs().size() >= 2)
        return emitCallbackCall("liva_ui_on_select", 0, 1);
    if (funcName == "onKey" && node->getArgs().size() >= 2)
        return emitCallbackCall("liva_ui_on_key", 0, 1);

    // listAddItem(handle, item) -> void
    if (funcName == "listAddItem" && node->getArgs().size() >= 2) {
        auto *handle = visit(node->getArgs()[0].get());
        auto *item = visit(node->getArgs()[1].get());
        if (!handle || !item) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_list_add_item"), {handle, item});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }

    // listClear(handle) -> void
    if (funcName == "listClear" && node->getArgs().size() >= 1) {
        auto *handle = visit(node->getArgs()[0].get());
        if (!handle) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_list_clear"), {handle});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }

    // listGetSelection(handle) -> i32
    if (funcName == "listGetSelection" && node->getArgs().size() >= 1) {
        auto *handle = visit(node->getArgs()[0].get());
        if (!handle) return nullptr;
        return builder_->CreateCall(getOrPanic("liva_ui_list_get_selection"), {handle}, "ui.lsel");
    }

    // tabAddPage(tab, page, title) -> void
    if (funcName == "tabAddPage" && node->getArgs().size() >= 3) {
        auto *tab = visit(node->getArgs()[0].get());
        auto *page = visit(node->getArgs()[1].get());
        auto *title = visit(node->getArgs()[2].get());
        if (!tab || !page || !title) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_tab_add_page"), {tab, page, title});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }

    // tabGetSelection(handle) -> i32
    if (funcName == "tabGetSelection" && node->getArgs().size() >= 1) {
        auto *handle = visit(node->getArgs()[0].get());
        if (!handle) return nullptr;
        return builder_->CreateCall(getOrPanic("liva_ui_tab_get_selection"), {handle}, "ui.tsel");
    }

    // messageBox(title, message, style) -> void
    if (funcName == "messageBox" && node->getArgs().size() >= 3) {
        auto *title = visit(node->getArgs()[0].get());
        auto *msg = visit(node->getArgs()[1].get());
        auto *style = visit(node->getArgs()[2].get());
        if (!title || !msg || !style) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_message_box"), {title, msg, style});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }

    // fileDialog(parent, title, wildcard, style) -> string
    if (funcName == "fileDialog" && node->getArgs().size() >= 4) {
        auto *parent = visit(node->getArgs()[0].get());
        auto *title = visit(node->getArgs()[1].get());
        auto *wildcard = visit(node->getArgs()[2].get());
        auto *style = visit(node->getArgs()[3].get());
        if (!parent || !title || !wildcard || !style) return nullptr;
        return builder_->CreateCall(getOrPanic("liva_ui_file_dialog"),
                                    {parent, title, wildcard, style}, "ui.fdlg");
    }

    // colorDialog(parent) -> i32
    if (funcName == "colorDialog" && node->getArgs().size() >= 1) {
        auto *parent = visit(node->getArgs()[0].get());
        if (!parent) return nullptr;
        return builder_->CreateCall(getOrPanic("liva_ui_color_dialog"), {parent}, "ui.cdlg");
    }

    // createTimer(intervalMs, callback) -> i32
    if (funcName == "createTimer" && node->getArgs().size() >= 2) {
        auto *interval = visit(node->getArgs()[0].get());
        auto *closureVal = visit(node->getArgs()[1].get());
        if (!interval || !closureVal) return nullptr;
        auto *closureObjTy = getClosureObjTy();
        auto *alloca = createEntryBlockAlloca(
            builder_->GetInsertBlock()->getParent(), "tmr.cb", closureObjTy);
        builder_->CreateStore(closureVal, alloca);
        auto *funcPtr = builder_->CreateLoad(
            llvm::PointerType::getUnqual(*context_),
            builder_->CreateStructGEP(closureObjTy, alloca, 0));
        auto *envPtr = builder_->CreateLoad(
            llvm::PointerType::getUnqual(*context_),
            builder_->CreateStructGEP(closureObjTy, alloca, 1));
        return builder_->CreateCall(getOrPanic("liva_ui_create_timer"),
                                    {interval, funcPtr, envPtr}, "ui.tmr");
    }

    // stopTimer(handle) -> void
    if (funcName == "stopTimer" && node->getArgs().size() >= 1) {
        auto *handle = visit(node->getArgs()[0].get());
        if (!handle) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_stop_timer"), {handle});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }

    // getClipboardText() -> string
    if (funcName == "getClipboardText" && node->getArgs().empty()) {
        return builder_->CreateCall(getOrPanic("liva_ui_get_clipboard_text"), {}, "ui.clip");
    }

    // setClipboardText(text) -> void
    if (funcName == "setClipboardText" && node->getArgs().size() == 1) {
        auto *text = visit(node->getArgs()[0].get());
        if (!text) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_set_clipboard_text"), {text});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }

    // Canvas: canvasOnPaint(handle, callback) -> void
    if (funcName == "canvasOnPaint" && node->getArgs().size() >= 2)
        return emitCallbackCall("liva_ui_canvas_on_paint", 0, 1);

    // canvasRefresh(handle) -> void
    if (funcName == "canvasRefresh" && node->getArgs().size() >= 1) {
        auto *handle = visit(node->getArgs()[0].get());
        if (!handle) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_canvas_refresh"), {handle});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }

    // dcClear(dc, r, g, b) -> void
    if (funcName == "dcClear" && node->getArgs().size() >= 4) {
        auto *dc = visit(node->getArgs()[0].get());
        auto *r = visit(node->getArgs()[1].get());
        auto *g = visit(node->getArgs()[2].get());
        auto *b = visit(node->getArgs()[3].get());
        if (!dc || !r || !g || !b) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_dc_clear"), {dc, r, g, b});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }

    // dcDrawRect(dc, x, y, w, h, r, g, b) -> void
    if (funcName == "dcDrawRect" && node->getArgs().size() >= 8) {
        std::vector<llvm::Value *> args;
        for (int i = 0; i < 8; ++i) {
            auto *v = visit(node->getArgs()[i].get());
            if (!v) return nullptr;
            args.push_back(v);
        }
        builder_->CreateCall(getOrPanic("liva_ui_dc_draw_rect"), args);
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }

    // dcDrawText(dc, text, x, y, r, g, b) -> void
    if (funcName == "dcDrawText" && node->getArgs().size() >= 7) {
        std::vector<llvm::Value *> args;
        for (int i = 0; i < 7; ++i) {
            auto *v = visit(node->getArgs()[i].get());
            if (!v) return nullptr;
            args.push_back(v);
        }
        builder_->CreateCall(getOrPanic("liva_ui_dc_draw_text"), args);
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }

    // dcDrawLine(dc, x1, y1, x2, y2, r, g, b) -> void
    if (funcName == "dcDrawLine" && node->getArgs().size() >= 8) {
        std::vector<llvm::Value *> args;
        for (int i = 0; i < 8; ++i) {
            auto *v = visit(node->getArgs()[i].get());
            if (!v) return nullptr;
            args.push_back(v);
        }
        builder_->CreateCall(getOrPanic("liva_ui_dc_draw_line"), args);
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }

    // dcDrawCircle(dc, cx, cy, radius, r, g, b) -> void
    if (funcName == "dcDrawCircle" && node->getArgs().size() >= 7) {
        std::vector<llvm::Value *> args;
        for (int i = 0; i < 7; ++i) {
            auto *v = visit(node->getArgs()[i].get());
            if (!v) return nullptr;
            args.push_back(v);
        }
        builder_->CreateCall(getOrPanic("liva_ui_dc_draw_circle"), args);
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }

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
        auto pat = parseMatchPattern(arm.pattern, enumTypeName);
        if (pat.isWildcard)
            defaultIdx = static_cast<int>(i);
        armInfos.push_back({armBB, std::move(pat)});
    }

    // Create default block
    llvm::BasicBlock *defaultBB = (defaultIdx >= 0) ? armInfos[defaultIdx].bb : mergeBB;

    // Count non-default, non-wildcard cases
    unsigned numCases = 0;
    for (auto &info : armInfos) {
        if (!info.pat.isWildcard && info.pat.tag >= 0)
            ++numCases;
    }

    // Collect arm results for PHI node (match-as-expression)
    std::vector<std::pair<llvm::Value *, llvm::BasicBlock *>> armResults;

    // Use if-else chain for non-integer types (float/double/pointer) or when
    // all arms are guarded bindings (no concrete switch cases)
    bool useIfElseChain = !tagVal->getType()->isIntegerTy() || numCases == 0;

    if (!useIfElseChain) {
        auto *switchInst = builder_->CreateSwitch(tagVal, defaultBB, numCases);
        for (auto &info : armInfos) {
            if (!info.pat.isWildcard && info.pat.tag >= 0) {
                switchInst->addCase(builder_->getInt32(info.pat.tag), info.bb);
            }
        }
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

                if (b < info.pat.nestedPatterns.size() && info.pat.nestedPatterns[b].tag >= 0) {
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

        // Guard clause: if guard is false, jump to next arm or default
        if (arm.guard) {
            auto *guardVal = visit(arm.guard.get());
            auto *bodyBB = llvm::BasicBlock::Create(*context_,
                "match.arm." + std::to_string(i) + ".body", func);
            // Determine fallthrough: next arm's BB, or defaultBB
            llvm::BasicBlock *nextBB = defaultBB;
            if (useIfElseChain && i + 1 < armInfos.size()) {
                nextBB = armInfos[i + 1].bb;
            }
            builder_->CreateCondBr(guardVal, bodyBB, nextBB);
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
            phi = builder_->CreatePHI(valType, armResults.size(), "match.val");
            for (auto &p : armResults) {
                phi->addIncoming(p.first, p.second);
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

/// Split a string by top-level commas (respecting nested parentheses)
static std::vector<std::string> splitTopLevelCommas(const std::string &s) {
    std::vector<std::string> result;
    int depth = 0;
    size_t start = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '(') depth++;
        else if (s[i] == ')') depth--;
        else if (s[i] == ',' && depth == 0) {
            result.push_back(s.substr(start, i - start));
            start = i + 1;
        }
    }
    if (start < s.size())
        result.push_back(s.substr(start));
    return result;
}

/// Trim whitespace from both ends
static std::string trimStr(const std::string &s) {
    size_t b = s.find_first_not_of(" \t");
    if (b == std::string::npos) return "";
    size_t e = s.find_last_not_of(" \t");
    return s.substr(b, e - b + 1);
}

IRGen::PatternInfo IRGen::parseMatchPattern(const std::string &pattern,
                                             const std::string &subjectEnumType) {
    PatternInfo info;

    // Wildcard
    if (pattern == "_") {
        info.isWildcard = true;
        return info;
    }

    // Check for "EnumName.CaseName" or "EnumName.CaseName(bindings)"
    auto dotPos = pattern.find('.');
    if (dotPos != std::string::npos) {
        info.enumName = pattern.substr(0, dotPos);
        auto rest = pattern.substr(dotPos + 1);

        // Check for parenthesized bindings: CaseName(r) or CaseName(w, h)
        auto parenPos = rest.find('(');
        if (parenPos != std::string::npos) {
            info.caseName = rest.substr(0, parenPos);
            // Find matching closing paren (depth-aware for nested patterns)
            int depth = 0;
            size_t closePos = std::string::npos;
            for (size_t ci = parenPos; ci < rest.size(); ++ci) {
                if (rest[ci] == '(') depth++;
                else if (rest[ci] == ')') {
                    depth--;
                    if (depth == 0) { closePos = ci; break; }
                }
            }
            if (closePos != std::string::npos) {
                auto innerStr = rest.substr(parenPos + 1, closePos - parenPos - 1);
                // Split by top-level commas (respecting nested parens)
                auto slots = splitTopLevelCommas(innerStr);
                for (auto &slot : slots) {
                    auto trimmed = trimStr(slot);
                    if (trimmed.find('.') != std::string::npos) {
                        // Nested enum pattern — recurse
                        info.bindings.push_back(""); // placeholder
                        info.nestedPatterns.push_back(parseMatchPattern(trimmed, ""));
                    } else {
                        info.bindings.push_back(trimmed);
                        info.nestedPatterns.push_back(PatternInfo{}); // empty (tag=-1)
                    }
                }
            }
        } else {
            info.caseName = rest;
        }

        // Look up tag
        auto ecIt = enumCases_.find(info.enumName);
        if (ecIt != enumCases_.end()) {
            auto cIt = ecIt->second.find(info.caseName);
            if (cIt != ecIt->second.end())
                info.tag = cIt->second;
        }
        return info;
    }

    // Try integer literal
    char *end = nullptr;
    long val = std::strtol(pattern.c_str(), &end, 10);
    if (end != pattern.c_str() && *end == '\0') {
        info.tag = static_cast<int>(val);
        return info;
    }

    // Try bare case name using subject's enum type
    if (!subjectEnumType.empty()) {
        auto ecIt = enumCases_.find(subjectEnumType);
        if (ecIt != enumCases_.end()) {
            auto cIt = ecIt->second.find(pattern);
            if (cIt != ecIt->second.end()) {
                info.enumName = subjectEnumType;
                info.caseName = pattern;
                info.tag = cIt->second;
                return info;
            }
        }
    }

    // Simple identifier — treat as variable binding (e.g., "s" in "s if s >= 90.0")
    if (info.tag < 0 && !info.isWildcard && info.bindings.empty() && !pattern.empty()) {
        info.bindings.push_back(pattern);
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

                        if (b < nested.nestedPatterns.size() && nested.nestedPatterns[b].tag >= 0) {
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
