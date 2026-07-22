#include "liva/IR/IRGen.h"

#ifdef LIVA_HAS_LLVM

#include <llvm/IR/Intrinsics.h>

namespace liva {

std::optional<llvm::Value *>
IRGen::tryEmitConcurrencyBuiltin(CallExpr *node, const std::string &funcName) {
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

    return std::nullopt;
}

} // namespace liva

#endif // LIVA_HAS_LLVM
