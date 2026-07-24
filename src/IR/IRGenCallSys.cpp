#include "liva/IR/IRGen.h"

#ifdef LIVA_HAS_LLVM

#include <llvm/IR/Intrinsics.h>

namespace liva {

std::optional<llvm::Value *>
IRGen::tryEmitSysBuiltin(CallExpr *node, const std::string &funcName) {
    // === Stdlib: Random ===
    if (funcName == "randInt" && node->getArgs().size() >= 2) {
        auto *minArg = visit(node->getArgs()[0].get());
        auto *maxArg = visit(node->getArgs()[1].get());
        if (!minArg || !maxArg) return nullptr;
        if (minArg->getType()->isIntegerTy(64))
            minArg = builder_->CreateTrunc(minArg, builder_->getInt32Ty());
        if (maxArg->getType()->isIntegerTy(64))
            maxArg = builder_->CreateTrunc(maxArg, builder_->getInt32Ty());
        auto *fn = getOrPanic("liva_rand_int");
        return builder_->CreateCall(fn, {minArg, maxArg}, "randint");
    }

    if (funcName == "randFloat") {
        auto *fn = getOrPanic("liva_rand_float");
        return builder_->CreateCall(fn, {}, "randfloat");
    }

    if (funcName == "randSeed" && !node->getArgs().empty()) {
        auto *seedArg = visit(node->getArgs()[0].get());
        if (!seedArg) return nullptr;
        if (seedArg->getType()->isIntegerTy(32))
            seedArg = builder_->CreateSExt(seedArg, builder_->getInt64Ty());
        auto *fn = getOrPanic("liva_rand_seed");
        builder_->CreateCall(fn, {seedArg});
        return nullptr;
    }

    if (funcName == "randI64") {
        auto *fn = getOrPanic("liva_rand_i64");
        return builder_->CreateCall(fn, {}, "randi64");
    }

    if (funcName == "randUuid") {
        auto *fn = getOrPanic("liva_rand_uuid");
        auto *result = builder_->CreateCall(fn, {}, "randuuid");
        trackStringTemp(result);
        return result;
    }

    if (funcName == "randUuidV7") {
        auto *fn = getOrPanic("liva_rand_uuid_v7");
        auto *result = builder_->CreateCall(fn, {}, "randuuidv7");
        trackStringTemp(result);
        return result;
    }

    // === Stdlib: Process/Env ===
    if (funcName == "env" && !node->getArgs().empty()) {
        auto *nameArg = visit(node->getArgs()[0].get());
        if (!nameArg) return nullptr;
        auto *fn = getOrPanic("liva_env_get");
        auto *result = builder_->CreateCall(fn, {nameArg}, "env.raw");
        trackStringTemp(result);
        // Wrap in Optional<string>: null → nil, non-null → some
        auto *curFunc = builder_->GetInsertBlock()->getParent();
        auto *isNull = builder_->CreateICmpEQ(result, llvm::ConstantPointerNull::get(
            llvm::PointerType::getUnqual(*context_)), "env.isnull");
        auto *hasVal = builder_->CreateNot(isNull, "env.hasval");
        auto *optTy = getOptionalType(llvm::PointerType::getUnqual(*context_));
        auto *optAlloca = createEntryBlockAlloca(curFunc, "env.opt", optTy);
        auto *hasValPtr = builder_->CreateStructGEP(optTy, optAlloca, 0);
        builder_->CreateStore(hasVal, hasValPtr);
        auto *valPtr = builder_->CreateStructGEP(optTy, optAlloca, 1);
        builder_->CreateStore(result, valPtr);
        return builder_->CreateLoad(optTy, optAlloca, "env.result");
    }

    if (funcName == "exit" && !node->getArgs().empty()) {
        auto *codeArg = visit(node->getArgs()[0].get());
        if (!codeArg) return nullptr;
        if (codeArg->getType()->isIntegerTy(64))
            codeArg = builder_->CreateTrunc(codeArg, builder_->getInt32Ty());
        auto *fn = getOrPanic("liva_exit");
        builder_->CreateCall(fn, {codeArg});
        builder_->CreateUnreachable();
        return nullptr;
    }

    if (funcName == "args") {
        auto *curFunc = builder_->GetInsertBlock()->getParent();
        auto *countAlloca = createEntryBlockAlloca(curFunc, "args.count", builder_->getInt64Ty());
        auto *fn = getOrPanic("liva_args");
        auto *resultPtr = builder_->CreateCall(fn, {countAlloca}, "args.data");
        auto *count = builder_->CreateLoad(builder_->getInt64Ty(), countAlloca, "args.len");
        auto *structTy = getDynArrayStructTy();
        auto *arrAlloca = createEntryBlockAlloca(curFunc, "args.arr", structTy);
        auto *dataPtr = builder_->CreateStructGEP(structTy, arrAlloca, 0);
        builder_->CreateStore(resultPtr, dataPtr);
        auto *lenPtr = builder_->CreateStructGEP(structTy, arrAlloca, 1);
        builder_->CreateStore(count, lenPtr);
        auto *capPtr = builder_->CreateStructGEP(structTy, arrAlloca, 2);
        builder_->CreateStore(count, capPtr);
        return builder_->CreateLoad(structTy, arrAlloca, "args.result");
    }

    // === Stdlib: Date/Time ===
    if (funcName == "clock") {
        auto *fn = getOrPanic("liva_clock");
        return builder_->CreateCall(fn, {}, "clock");
    }

    if (funcName == "clockMs") {
        auto *fn = getOrPanic("liva_clock_ms");
        return builder_->CreateCall(fn, {}, "clockms");
    }

    // === Stdlib: Benchmarking ===
    if (funcName == "benchStart") {
        auto *fn = getOrPanic("liva_bench_start");
        return builder_->CreateCall(fn, {}, "bench.start");
    }

    if (funcName == "benchIter") {
        auto *arg = visit(node->getArgs()[0].get());
        if (!arg) return nullptr;
        if (arg->getType()->isIntegerTy(32))
            arg = builder_->CreateSExt(arg, builder_->getInt64Ty());
        auto *fn = getOrPanic("liva_bench_iter");
        return builder_->CreateCall(fn, {arg}, "bench.iter");
    }

    if (funcName == "benchDone") {
        auto *arg = visit(node->getArgs()[0].get());
        if (!arg) return nullptr;
        if (arg->getType()->isIntegerTy(32))
            arg = builder_->CreateSExt(arg, builder_->getInt64Ty());
        auto *fn = getOrPanic("liva_bench_done");
        return builder_->CreateCall(fn, {arg}, "bench.done");
    }

    if (funcName == "benchReport") {
        auto *nameArg = visit(node->getArgs()[0].get());
        auto *handleArg = visit(node->getArgs()[1].get());
        if (!nameArg || !handleArg) return nullptr;
        if (handleArg->getType()->isIntegerTy(32))
            handleArg = builder_->CreateSExt(handleArg, builder_->getInt64Ty());
        auto *fn = getOrPanic("liva_bench_report");
        return builder_->CreateCall(fn, {nameArg, handleArg});
    }

    if (funcName == "benchReset") {
        auto *arg = visit(node->getArgs()[0].get());
        if (!arg) return nullptr;
        if (arg->getType()->isIntegerTy(32))
            arg = builder_->CreateSExt(arg, builder_->getInt64Ty());
        auto *fn = getOrPanic("liva_bench_reset");
        return builder_->CreateCall(fn, {arg});
    }

    if (funcName == "sleep" && !node->getArgs().empty()) {
        auto *msArg = visit(node->getArgs()[0].get());
        if (!msArg) return nullptr;
        if (msArg->getType()->isIntegerTy(32))
            msArg = builder_->CreateSExt(msArg, builder_->getInt64Ty());

        if (currentIsAsync_ && currentCoroTask_) {
            // Async context: register timer + suspend coroutine
            auto *ptrTy = llvm::PointerType::getUnqual(*context_);
            auto *curTask = builder_->CreateLoad(ptrTy, currentCoroTask_, "sleep.task");
            builder_->CreateCall(getOrPanic("liva_async_sleep"), {curTask, msArg});

            auto *func = builder_->GetInsertBlock()->getParent();
            auto *resumeBB = llvm::BasicBlock::Create(*context_, "sleep.resume", func);
            auto *coroSuspendFn = llvm::Intrinsic::getOrInsertDeclaration(
                module_.get(), llvm::Intrinsic::coro_suspend);
            auto *noneToken = llvm::ConstantTokenNone::get(*context_);
            auto *suspVal = builder_->CreateCall(coroSuspendFn,
                {noneToken, builder_->getFalse()}, "sleep.sus");
            auto *sw = builder_->CreateSwitch(suspVal, currentCoroSuspendBB_, 2);
            sw->addCase(builder_->getInt8(0), resumeBB);
            sw->addCase(builder_->getInt8(1), currentCoroCleanupBB_);
            builder_->SetInsertPoint(resumeBB);
            return nullptr;
        }

        // Sync context: blocking sleep
        auto *fn = getOrPanic("liva_sleep");
        builder_->CreateCall(fn, {msArg});
        return nullptr;
    }

    if (funcName == "isCancelled" && currentIsAsync_ && currentCoroTask_) {
        auto *ptrTy = llvm::PointerType::getUnqual(*context_);
        auto *curTask = builder_->CreateLoad(ptrTy, currentCoroTask_, "cancel.task");
        auto *isCancelledFn = getOrPanic("liva_task_is_cancelled");
        auto *cancelledI8 = builder_->CreateCall(isCancelledFn, {curTask}, "cancelled.i8");
        return builder_->CreateICmpNE(cancelledI8, builder_->getInt8(0), "is.cancelled");
    }

    // === Directory operations ===
    if (funcName == "dirList" && !node->getArgs().empty()) {
        auto *pathArg = visit(node->getArgs()[0].get());
        if (!pathArg) return nullptr;
        auto *fn = getOrPanic("liva_dir_list");
        auto *i64Ty = builder_->getInt64Ty();
        auto *curFunc = builder_->GetInsertBlock()->getParent();

        // Allocate count variable
        auto *countAlloca = createEntryBlockAlloca(curFunc, "dirlist.count", i64Ty);
        builder_->CreateStore(builder_->getInt64(0), countAlloca);

        // Call liva_dir_list(path, &count) -> char**
        auto *rawArr = builder_->CreateCall(fn, {pathArg, countAlloca}, "dirlist.raw");
        auto *count = builder_->CreateLoad(i64Ty, countAlloca, "dirlist.count");

        // Build DynArray<string> from raw array
        auto *daTy = getDynArrayStructTy();
        auto *daAlloca = createEntryBlockAlloca(curFunc, "dirlist.da", daTy);

        // data = rawArr, len = count, cap = count
        auto *dataPtr = builder_->CreateStructGEP(daTy, daAlloca, 0);
        builder_->CreateStore(rawArr, dataPtr);
        auto *lenPtr = builder_->CreateStructGEP(daTy, daAlloca, 1);
        builder_->CreateStore(count, lenPtr);
        auto *capPtr = builder_->CreateStructGEP(daTy, daAlloca, 2);
        builder_->CreateStore(count, capPtr);

        return builder_->CreateLoad(daTy, daAlloca, "dirlist.da.val");
    }

    if (funcName == "dirCreate" && !node->getArgs().empty()) {
        auto *pathArg = visit(node->getArgs()[0].get());
        if (!pathArg) return nullptr;
        auto *fn = getOrPanic("liva_dir_create");
        auto *r = builder_->CreateCall(fn, {pathArg}, "dir.create");
        return builder_->CreateTrunc(r, builder_->getInt1Ty(), "dir.create.bool");
    }

    if (funcName == "dirRemove" && !node->getArgs().empty()) {
        auto *pathArg = visit(node->getArgs()[0].get());
        if (!pathArg) return nullptr;
        auto *fn = getOrPanic("liva_dir_remove");
        auto *r = builder_->CreateCall(fn, {pathArg}, "dir.remove");
        return builder_->CreateTrunc(r, builder_->getInt1Ty(), "dir.remove.bool");
    }

    if (funcName == "dirExists" && !node->getArgs().empty()) {
        auto *pathArg = visit(node->getArgs()[0].get());
        if (!pathArg) return nullptr;
        auto *fn = getOrPanic("liva_dir_exists");
        auto *r = builder_->CreateCall(fn, {pathArg}, "dir.exists");
        return builder_->CreateTrunc(r, builder_->getInt1Ty(), "dir.exists.bool");
    }

    // === Path operations ===
    if (funcName == "pathJoin" && node->getArgs().size() >= 2) {
        auto *aArg = visit(node->getArgs()[0].get());
        auto *bArg = visit(node->getArgs()[1].get());
        if (!aArg || !bArg) return nullptr;
        auto *fn = getOrPanic("liva_path_join");
        auto *r = builder_->CreateCall(fn, {aArg, bArg}, "path.join");
        trackStringTemp(r);
        return r;
    }

    if (funcName == "pathDirname" && !node->getArgs().empty()) {
        auto *pathArg = visit(node->getArgs()[0].get());
        if (!pathArg) return nullptr;
        auto *fn = getOrPanic("liva_path_dirname");
        auto *r = builder_->CreateCall(fn, {pathArg}, "path.dirname");
        trackStringTemp(r);
        return r;
    }

    if (funcName == "pathBasename" && !node->getArgs().empty()) {
        auto *pathArg = visit(node->getArgs()[0].get());
        if (!pathArg) return nullptr;
        auto *fn = getOrPanic("liva_path_basename");
        auto *r = builder_->CreateCall(fn, {pathArg}, "path.basename");
        trackStringTemp(r);
        return r;
    }

    if (funcName == "pathExtension" && !node->getArgs().empty()) {
        auto *pathArg = visit(node->getArgs()[0].get());
        if (!pathArg) return nullptr;
        auto *fn = getOrPanic("liva_path_extension");
        auto *r = builder_->CreateCall(fn, {pathArg}, "path.extension");
        trackStringTemp(r);
        return r;
    }

    if (funcName == "pathExists" && !node->getArgs().empty()) {
        auto *pathArg = visit(node->getArgs()[0].get());
        if (!pathArg) return nullptr;
        auto *fn = getOrPanic("liva_path_exists");
        auto *r = builder_->CreateCall(fn, {pathArg}, "path.exists");
        return builder_->CreateTrunc(r, builder_->getInt1Ty(), "path.exists.bool");
    }

    if (funcName == "isFile" && !node->getArgs().empty()) {
        auto *pathArg = visit(node->getArgs()[0].get());
        if (!pathArg) return nullptr;
        auto *fn = getOrPanic("liva_file_is_file");
        auto *r = builder_->CreateCall(fn, {pathArg}, "is.file");
        return builder_->CreateTrunc(r, builder_->getInt1Ty(), "is.file.bool");
    }

    if (funcName == "isDir" && !node->getArgs().empty()) {
        auto *pathArg = visit(node->getArgs()[0].get());
        if (!pathArg) return nullptr;
        auto *fn = getOrPanic("liva_path_is_dir");
        auto *r = builder_->CreateCall(fn, {pathArg}, "is.dir");
        return builder_->CreateTrunc(r, builder_->getInt1Ty(), "is.dir.bool");
    }

    if (funcName == "fileSize" && !node->getArgs().empty()) {
        auto *pathArg = visit(node->getArgs()[0].get());
        if (!pathArg) return nullptr;
        auto *fn = getOrPanic("liva_path_size");
        return builder_->CreateCall(fn, {pathArg}, "file.size");
    }

    if (funcName == "fileModifiedTime" && !node->getArgs().empty()) {
        auto *pathArg = visit(node->getArgs()[0].get());
        if (!pathArg) return nullptr;
        auto *fn = getOrPanic("liva_path_modified_time");
        return builder_->CreateCall(fn, {pathArg}, "file.mtime");
    }

    if (funcName == "fileRead" && !node->getArgs().empty()) {
        auto *pathArg = visit(node->getArgs()[0].get());
        if (!pathArg) return nullptr;
        auto *fn = getOrPanic("liva_file_read");
        auto *result = builder_->CreateCall(fn, {pathArg}, "file.read.raw");
        trackStringTemp(result);
        auto *ptrTy = llvm::PointerType::getUnqual(*context_);
        auto *curFunc = builder_->GetInsertBlock()->getParent();
        auto *isNull = builder_->CreateICmpEQ(result, llvm::ConstantPointerNull::get(
            llvm::cast<llvm::PointerType>(ptrTy)), "file.read.isnull");
        auto *hasVal = builder_->CreateNot(isNull, "file.read.hasval");
        auto *optTy = getOptionalType(ptrTy);
        auto *optAlloca = createEntryBlockAlloca(curFunc, "file.read.opt", optTy);
        builder_->CreateStore(hasVal, builder_->CreateStructGEP(optTy, optAlloca, 0));
        builder_->CreateStore(result, builder_->CreateStructGEP(optTy, optAlloca, 1));
        return builder_->CreateLoad(optTy, optAlloca, "file.read.result");
    }

    if (funcName == "fileWrite" && node->getArgs().size() >= 2) {
        auto *pathArg = visit(node->getArgs()[0].get());
        auto *contentArg = visit(node->getArgs()[1].get());
        if (!pathArg || !contentArg) return nullptr;
        auto *fn = getOrPanic("liva_file_write_path");
        auto *r = builder_->CreateCall(fn, {pathArg, contentArg}, "file.write");
        return builder_->CreateTrunc(r, builder_->getInt1Ty(), "file.write.bool");
    }

    if (funcName == "fileAppend" && node->getArgs().size() >= 2) {
        auto *pathArg = visit(node->getArgs()[0].get());
        auto *contentArg = visit(node->getArgs()[1].get());
        if (!pathArg || !contentArg) return nullptr;
        auto *fn = getOrPanic("liva_file_append");
        auto *r = builder_->CreateCall(fn, {pathArg, contentArg}, "file.append");
        return builder_->CreateTrunc(r, builder_->getInt1Ty(), "file.append.bool");
    }

    if (funcName == "fileRemove" && !node->getArgs().empty()) {
        auto *pathArg = visit(node->getArgs()[0].get());
        if (!pathArg) return nullptr;
        auto *fn = getOrPanic("liva_file_remove");
        auto *r = builder_->CreateCall(fn, {pathArg}, "file.remove");
        return builder_->CreateTrunc(r, builder_->getInt1Ty(), "file.remove.bool");
    }

    if (funcName == "fileCopy" && node->getArgs().size() >= 2) {
        auto *srcArg = visit(node->getArgs()[0].get());
        auto *dstArg = visit(node->getArgs()[1].get());
        if (!srcArg || !dstArg) return nullptr;
        auto *fn = getOrPanic("liva_file_copy");
        auto *r = builder_->CreateCall(fn, {srcArg, dstArg}, "file.copy");
        return builder_->CreateTrunc(r, builder_->getInt1Ty(), "file.copy.bool");
    }

    if (funcName == "pathAbsolute" && !node->getArgs().empty()) {
        auto *pathArg = visit(node->getArgs()[0].get());
        if (!pathArg) return nullptr;
        auto *fn = getOrPanic("liva_path_absolute");
        auto *r = builder_->CreateCall(fn, {pathArg}, "path.absolute");
        trackStringTemp(r);
        return r;
    }

    // === Subprocess ===
    if (funcName == "exec" && !node->getArgs().empty()) {
        auto *cmdArg = visit(node->getArgs()[0].get());
        if (!cmdArg) return nullptr;
        auto *fn = getOrPanic("liva_exec");
        return builder_->CreateCall(fn, {cmdArg}, "exec.code");
    }

    if (funcName == "execOutput" && !node->getArgs().empty()) {
        auto *cmdArg = visit(node->getArgs()[0].get());
        if (!cmdArg) return nullptr;
        auto *fn = getOrPanic("liva_exec_output");
        auto *result = builder_->CreateCall(fn, {cmdArg}, "exec.output.raw");
        trackStringTemp(result);
        // Wrap in Optional<string>
        auto *ptrTy = llvm::PointerType::getUnqual(*context_);
        auto *curFunc = builder_->GetInsertBlock()->getParent();
        auto *isNull = builder_->CreateICmpEQ(result, llvm::ConstantPointerNull::get(
            llvm::cast<llvm::PointerType>(ptrTy)), "exec.output.isnull");
        auto *hasVal = builder_->CreateNot(isNull, "exec.output.hasval");
        auto *optTy = getOptionalType(ptrTy);
        auto *optAlloca = createEntryBlockAlloca(curFunc, "exec.output.opt", optTy);
        auto *hasValPtr = builder_->CreateStructGEP(optTy, optAlloca, 0);
        builder_->CreateStore(hasVal, hasValPtr);
        auto *valPtr = builder_->CreateStructGEP(optTy, optAlloca, 1);
        builder_->CreateStore(result, valPtr);
        return builder_->CreateLoad(optTy, optAlloca, "exec.output.result");
    }

    if (funcName == "processStart" && !node->getArgs().empty()) {
        auto *cmdArg = visit(node->getArgs()[0].get());
        if (!cmdArg) return nullptr;
        auto *fn = getOrPanic("liva_process_start");
        return builder_->CreateCall(fn, {cmdArg}, "proc.start");
    }

    if (funcName == "processWait" && !node->getArgs().empty()) {
        auto *handleArg = visit(node->getArgs()[0].get());
        if (!handleArg) return nullptr;
        auto *fn = getOrPanic("liva_process_wait");
        return builder_->CreateCall(fn, {handleArg}, "proc.wait");
    }

    if (funcName == "processKill" && !node->getArgs().empty()) {
        auto *handleArg = visit(node->getArgs()[0].get());
        if (!handleArg) return nullptr;
        auto *fn = getOrPanic("liva_process_kill");
        auto *r = builder_->CreateCall(fn, {handleArg}, "proc.kill");
        return builder_->CreateTrunc(r, builder_->getInt1Ty(), "proc.kill.bool");
    }

    if (funcName == "processRead" && !node->getArgs().empty()) {
        auto *handleArg = visit(node->getArgs()[0].get());
        if (!handleArg) return nullptr;
        auto *fn = getOrPanic("liva_process_read");
        auto *result = builder_->CreateCall(fn, {handleArg}, "proc.read.raw");
        trackStringTemp(result);
        // Wrap in Optional<string>
        auto *ptrTy = llvm::PointerType::getUnqual(*context_);
        auto *curFunc = builder_->GetInsertBlock()->getParent();
        auto *isNull = builder_->CreateICmpEQ(result, llvm::ConstantPointerNull::get(
            llvm::cast<llvm::PointerType>(ptrTy)), "proc.read.isnull");
        auto *hasVal = builder_->CreateNot(isNull, "proc.read.hasval");
        auto *optTy = getOptionalType(ptrTy);
        auto *optAlloca = createEntryBlockAlloca(curFunc, "proc.read.opt", optTy);
        auto *hasValPtr = builder_->CreateStructGEP(optTy, optAlloca, 0);
        builder_->CreateStore(hasVal, hasValPtr);
        auto *valPtr = builder_->CreateStructGEP(optTy, optAlloca, 1);
        builder_->CreateStore(result, valPtr);
        return builder_->CreateLoad(optTy, optAlloca, "proc.read.result");
    }

    if (funcName == "processClose" && !node->getArgs().empty()) {
        auto *handleArg = visit(node->getArgs()[0].get());
        if (!handleArg) return nullptr;
        auto *fn = getOrPanic("liva_process_close");
        builder_->CreateCall(fn, {handleArg});
        return nullptr;
    }

    // === Logging ===
    if (funcName == "logDebug" && !node->getArgs().empty()) {
        auto *msgArg = visit(node->getArgs()[0].get());
        if (!msgArg) return nullptr;
        builder_->CreateCall(getOrPanic("liva_log_debug"), {msgArg});
        return nullptr;
    }
    if (funcName == "logInfo" && !node->getArgs().empty()) {
        auto *msgArg = visit(node->getArgs()[0].get());
        if (!msgArg) return nullptr;
        builder_->CreateCall(getOrPanic("liva_log_info"), {msgArg});
        return nullptr;
    }
    if (funcName == "logWarn" && !node->getArgs().empty()) {
        auto *msgArg = visit(node->getArgs()[0].get());
        if (!msgArg) return nullptr;
        builder_->CreateCall(getOrPanic("liva_log_warn"), {msgArg});
        return nullptr;
    }
    if (funcName == "logError" && !node->getArgs().empty()) {
        auto *msgArg = visit(node->getArgs()[0].get());
        if (!msgArg) return nullptr;
        builder_->CreateCall(getOrPanic("liva_log_error"), {msgArg});
        return nullptr;
    }
    if (funcName == "logSetLevel" && !node->getArgs().empty()) {
        auto *levelArg = visit(node->getArgs()[0].get());
        if (!levelArg) return nullptr;
        builder_->CreateCall(getOrPanic("liva_log_set_level"), {levelArg});
        return nullptr;
    }

    // === Testing ===
    // testRunClosure(name, closure) -> bool
    // Decomposes closure into funcPtr+envPtr, invokes runtime with setjmp
    if (funcName == "testRunClosure" && node->getArgs().size() >= 2) {
        auto *nameArg = visit(node->getArgs()[0].get());
        auto *closureVal = visit(node->getArgs()[1].get());
        if (!nameArg || !closureVal) return nullptr;
        auto *curFunc = builder_->GetInsertBlock()->getParent();
        auto *closureObjTy = getClosureObjTy();
        auto *ptrTy = llvm::PointerType::getUnqual(*context_);
        auto *alloca = createEntryBlockAlloca(curFunc, "tc.tmp", closureObjTy);
        builder_->CreateStore(closureVal, alloca);
        auto *funcPtr = builder_->CreateLoad(ptrTy,
            builder_->CreateStructGEP(closureObjTy, alloca, 0), "tc.func");
        auto *envPtr = builder_->CreateLoad(ptrTy,
            builder_->CreateStructGEP(closureObjTy, alloca, 1), "tc.env");
        auto *fn = getOrPanic("liva_test_run_closure");
        auto *r = builder_->CreateCall(fn, {nameArg, funcPtr, envPtr}, "tc.result");
        return builder_->CreateTrunc(r, builder_->getInt1Ty(), "tc.bool");
    }

    if (funcName == "assert" && !node->getArgs().empty()) {
        auto *condArg = visit(node->getArgs()[0].get());
        if (!condArg) return nullptr;
        auto *i8Val = builder_->CreateZExt(condArg, builder_->getInt8Ty(), "assert.i8");
        builder_->CreateCall(getOrPanic("liva_assert"), {i8Val});
        return nullptr;
    }
    if (funcName == "assertMsg" && node->getArgs().size() >= 2) {
        auto *condArg = visit(node->getArgs()[0].get());
        auto *msgArg = visit(node->getArgs()[1].get());
        if (!condArg || !msgArg) return nullptr;
        auto *i8Val = builder_->CreateZExt(condArg, builder_->getInt8Ty(), "assertmsg.i8");
        builder_->CreateCall(getOrPanic("liva_assert_msg"), {i8Val, msgArg});
        return nullptr;
    }
    if (funcName == "assertEq" && node->getArgs().size() >= 2) {
        auto *aArg = visit(node->getArgs()[0].get());
        auto *bArg = visit(node->getArgs()[1].get());
        if (!aArg || !bArg) return nullptr;
        builder_->CreateCall(getOrPanic("liva_assert_eq"), {aArg, bArg});
        return nullptr;
    }
    if (funcName == "assertEqStr" && node->getArgs().size() >= 2) {
        auto *aArg = visit(node->getArgs()[0].get());
        auto *bArg = visit(node->getArgs()[1].get());
        if (!aArg || !bArg) return nullptr;
        builder_->CreateCall(getOrPanic("liva_assert_eq_str"), {aArg, bArg});
        return nullptr;
    }
    if (funcName == "assertEqFloat" && node->getArgs().size() >= 2) {
        auto *aArg = visit(node->getArgs()[0].get());
        auto *bArg = visit(node->getArgs()[1].get());
        if (!aArg || !bArg) return nullptr;
        builder_->CreateCall(getOrPanic("liva_assert_eq_float"), {aArg, bArg});
        return nullptr;
    }

    // === DateTime ===
    if (funcName == "dateNow" && node->getArgs().empty()) {
        auto *fn = getOrPanic("liva_date_now");
        auto *r = builder_->CreateCall(fn, {}, "date.now");
        trackStringTemp(r);
        return r;
    }
    if (funcName == "timeNow" && node->getArgs().empty()) {
        auto *fn = getOrPanic("liva_time_now");
        auto *r = builder_->CreateCall(fn, {}, "time.now");
        trackStringTemp(r);
        return r;
    }
    if (funcName == "datetimeNow" && node->getArgs().empty()) {
        auto *fn = getOrPanic("liva_datetime_now");
        auto *r = builder_->CreateCall(fn, {}, "datetime.now");
        trackStringTemp(r);
        return r;
    }
    if (funcName == "dateFormat" && node->getArgs().size() >= 2) {
        auto *tsArg = visit(node->getArgs()[0].get());
        auto *fmtArg = visit(node->getArgs()[1].get());
        if (!tsArg || !fmtArg) return nullptr;
        auto *fn = getOrPanic("liva_date_format");
        auto *r = builder_->CreateCall(fn, {tsArg, fmtArg}, "date.format");
        trackStringTemp(r);
        return r;
    }
    if (funcName == "dateYear" && !node->getArgs().empty()) {
        auto *tsArg = visit(node->getArgs()[0].get());
        if (!tsArg) return nullptr;
        return builder_->CreateCall(getOrPanic("liva_date_year"), {tsArg}, "date.year");
    }
    if (funcName == "dateMonth" && !node->getArgs().empty()) {
        auto *tsArg = visit(node->getArgs()[0].get());
        if (!tsArg) return nullptr;
        return builder_->CreateCall(getOrPanic("liva_date_month"), {tsArg}, "date.month");
    }
    if (funcName == "dateDay" && !node->getArgs().empty()) {
        auto *tsArg = visit(node->getArgs()[0].get());
        if (!tsArg) return nullptr;
        return builder_->CreateCall(getOrPanic("liva_date_day"), {tsArg}, "date.day");
    }
    if (funcName == "dateWeekday" && !node->getArgs().empty()) {
        auto *tsArg = visit(node->getArgs()[0].get());
        if (!tsArg) return nullptr;
        return builder_->CreateCall(getOrPanic("liva_date_weekday"), {tsArg}, "date.weekday");
    }

    if (funcName == "dateTimestamp" && node->getArgs().empty()) {
        auto *fn = getOrPanic("liva_date_timestamp");
        return builder_->CreateCall(fn, {}, "date.timestamp");
    }

    if (funcName == "dateParse" && node->getArgs().size() >= 2) {
        auto *strArg = visit(node->getArgs()[0].get());
        auto *fmtArg = visit(node->getArgs()[1].get());
        if (!strArg || !fmtArg) return nullptr;
        auto *fn = getOrPanic("liva_date_parse");
        return builder_->CreateCall(fn, {strArg, fmtArg}, "date.parse");
    }

    if (funcName == "isoFormatUtc" && !node->getArgs().empty()) {
        auto *tsArg = visit(node->getArgs()[0].get());
        if (!tsArg) return nullptr;
        // Auto-promote i64 → f64 (Liva's dateTimestamp returns f64 already, but
        // callers passing literal seconds may use i64).
        if (tsArg->getType()->isIntegerTy()) {
            tsArg = builder_->CreateSIToFP(tsArg, builder_->getDoubleTy(), "ts.f64");
        }
        auto *fn = getOrPanic("liva_iso_format_utc");
        auto *r = builder_->CreateCall(fn, {tsArg}, "iso.fmt");
        trackStringTemp(r);
        return r;
    }

    if (funcName == "isoParse" && !node->getArgs().empty()) {
        auto *strArg = visit(node->getArgs()[0].get());
        if (!strArg) return nullptr;
        auto *curFunc = builder_->GetInsertBlock()->getParent();
        auto *okAlloca = createEntryBlockAlloca(curFunc, "iso.ok",
            builder_->getInt8Ty());
        builder_->CreateStore(builder_->getInt8(0), okAlloca);
        auto *fn = getOrPanic("liva_iso_parse");
        auto *result = builder_->CreateCall(fn, {strArg, okAlloca}, "iso.parse.raw");
        auto *okVal = builder_->CreateLoad(builder_->getInt8Ty(), okAlloca, "iso.ok.val");
        auto *hasVal = builder_->CreateICmpNE(okVal, builder_->getInt8(0), "iso.parse.hasval");
        // Wrap into Optional<f64>.
        auto *f64Ty = builder_->getDoubleTy();
        auto *optTy = getOptionalType(f64Ty);
        auto *optAlloca = createEntryBlockAlloca(curFunc, "iso.parse.opt", optTy);
        builder_->CreateStore(hasVal, builder_->CreateStructGEP(optTy, optAlloca, 0));
        builder_->CreateStore(result, builder_->CreateStructGEP(optTy, optAlloca, 1));
        return builder_->CreateLoad(optTy, optAlloca, "iso.parse.result");
    }

    if (funcName == "dateAdd" && node->getArgs().size() >= 2) {
        auto *tsArg = visit(node->getArgs()[0].get());
        auto *secsArg = visit(node->getArgs()[1].get());
        if (!tsArg || !secsArg) return nullptr;
        auto *fn = getOrPanic("liva_date_add");
        return builder_->CreateCall(fn, {tsArg, secsArg}, "date.add");
    }

    if (funcName == "dateDiff" && node->getArgs().size() >= 2) {
        auto *ts1Arg = visit(node->getArgs()[0].get());
        auto *ts2Arg = visit(node->getArgs()[1].get());
        if (!ts1Arg || !ts2Arg) return nullptr;
        auto *fn = getOrPanic("liva_date_diff");
        return builder_->CreateCall(fn, {ts1Arg, ts2Arg}, "date.diff");
    }

    if (funcName == "dateHour" && !node->getArgs().empty()) {
        auto *tsArg = visit(node->getArgs()[0].get());
        if (!tsArg) return nullptr;
        return builder_->CreateCall(getOrPanic("liva_date_hour"), {tsArg}, "date.hour");
    }

    if (funcName == "dateMinute" && !node->getArgs().empty()) {
        auto *tsArg = visit(node->getArgs()[0].get());
        if (!tsArg) return nullptr;
        return builder_->CreateCall(getOrPanic("liva_date_minute"), {tsArg}, "date.minute");
    }

    if (funcName == "dateSecond" && !node->getArgs().empty()) {
        auto *tsArg = visit(node->getArgs()[0].get());
        if (!tsArg) return nullptr;
        return builder_->CreateCall(getOrPanic("liva_date_second"), {tsArg}, "date.second");
    }

    // === Encoding/Compression ===
    if (funcName == "base64Encode" && !node->getArgs().empty()) {
        auto *dataArg = visit(node->getArgs()[0].get());
        if (!dataArg) return nullptr;
        auto *r = builder_->CreateCall(getOrPanic("liva_base64_encode"), {dataArg}, "b64.enc");
        trackStringTemp(r);
        return r;
    }
    if (funcName == "base64Decode" && !node->getArgs().empty()) {
        auto *dataArg = visit(node->getArgs()[0].get());
        if (!dataArg) return nullptr;
        auto *fn = getOrPanic("liva_base64_decode");
        auto *result = builder_->CreateCall(fn, {dataArg}, "b64.dec.raw");
        trackStringTemp(result);
        auto *ptrTy = llvm::PointerType::getUnqual(*context_);
        auto *curFunc = builder_->GetInsertBlock()->getParent();
        auto *isNull = builder_->CreateICmpEQ(result, llvm::ConstantPointerNull::get(
            llvm::cast<llvm::PointerType>(ptrTy)), "b64.dec.isnull");
        auto *hasVal = builder_->CreateNot(isNull, "b64.dec.hasval");
        auto *optTy = getOptionalType(ptrTy);
        auto *optAlloca = createEntryBlockAlloca(curFunc, "b64.dec.opt", optTy);
        builder_->CreateStore(hasVal, builder_->CreateStructGEP(optTy, optAlloca, 0));
        builder_->CreateStore(result, builder_->CreateStructGEP(optTy, optAlloca, 1));
        return builder_->CreateLoad(optTy, optAlloca, "b64.dec.result");
    }
    if (funcName == "hexEncode" && !node->getArgs().empty()) {
        auto *dataArg = visit(node->getArgs()[0].get());
        if (!dataArg) return nullptr;
        auto *r = builder_->CreateCall(getOrPanic("liva_hex_encode"), {dataArg}, "hex.enc");
        trackStringTemp(r);
        return r;
    }
    if (funcName == "hexDecode" && !node->getArgs().empty()) {
        auto *dataArg = visit(node->getArgs()[0].get());
        if (!dataArg) return nullptr;
        auto *fn = getOrPanic("liva_hex_decode");
        auto *result = builder_->CreateCall(fn, {dataArg}, "hex.dec.raw");
        trackStringTemp(result);
        auto *ptrTy = llvm::PointerType::getUnqual(*context_);
        auto *curFunc = builder_->GetInsertBlock()->getParent();
        auto *isNull = builder_->CreateICmpEQ(result, llvm::ConstantPointerNull::get(
            llvm::cast<llvm::PointerType>(ptrTy)), "hex.dec.isnull");
        auto *hasVal = builder_->CreateNot(isNull, "hex.dec.hasval");
        auto *optTy = getOptionalType(ptrTy);
        auto *optAlloca = createEntryBlockAlloca(curFunc, "hex.dec.opt", optTy);
        builder_->CreateStore(hasVal, builder_->CreateStructGEP(optTy, optAlloca, 0));
        builder_->CreateStore(result, builder_->CreateStructGEP(optTy, optAlloca, 1));
        return builder_->CreateLoad(optTy, optAlloca, "hex.dec.result");
    }
    if (funcName == "crc32" && !node->getArgs().empty()) {
        auto *dataArg = visit(node->getArgs()[0].get());
        if (!dataArg) return nullptr;
        return builder_->CreateCall(getOrPanic("liva_crc32"), {dataArg}, "crc32");
    }

    if (funcName == "urlEncode" && !node->getArgs().empty()) {
        auto *dataArg = visit(node->getArgs()[0].get());
        if (!dataArg) return nullptr;
        auto *r = builder_->CreateCall(getOrPanic("liva_url_encode"), {dataArg}, "url.enc");
        trackStringTemp(r);
        return r;
    }
    if (funcName == "urlDecode" && !node->getArgs().empty()) {
        auto *dataArg = visit(node->getArgs()[0].get());
        if (!dataArg) return nullptr;
        auto *fn = getOrPanic("liva_url_decode");
        auto *result = builder_->CreateCall(fn, {dataArg}, "url.dec.raw");
        trackStringTemp(result);
        auto *ptrTy = llvm::PointerType::getUnqual(*context_);
        auto *curFunc = builder_->GetInsertBlock()->getParent();
        auto *isNull = builder_->CreateICmpEQ(result, llvm::ConstantPointerNull::get(
            llvm::cast<llvm::PointerType>(ptrTy)), "url.dec.isnull");
        auto *hasVal = builder_->CreateNot(isNull, "url.dec.hasval");
        auto *optTy = getOptionalType(ptrTy);
        auto *optAlloca = createEntryBlockAlloca(curFunc, "url.dec.opt", optTy);
        builder_->CreateStore(hasVal, builder_->CreateStructGEP(optTy, optAlloca, 0));
        builder_->CreateStore(result, builder_->CreateStructGEP(optTy, optAlloca, 1));
        return builder_->CreateLoad(optTy, optAlloca, "url.dec.result");
    }

    if (funcName == "base64UrlEncode" && !node->getArgs().empty()) {
        auto *dataArg = visit(node->getArgs()[0].get());
        if (!dataArg) return nullptr;
        auto *r = builder_->CreateCall(getOrPanic("liva_base64_url_encode"), {dataArg}, "b64u.enc");
        trackStringTemp(r);
        return r;
    }
    if (funcName == "jwtHS256Sig" && node->getArgs().size() >= 2) {
        auto *secretArg = visit(node->getArgs()[0].get());
        auto *dataArg = visit(node->getArgs()[1].get());
        if (!secretArg || !dataArg) return nullptr;
        auto *r = builder_->CreateCall(getOrPanic("liva_jwt_hs256_sig"),
                                        {secretArg, dataArg}, "jwt.hs256");
        trackStringTemp(r);
        return r;
    }
    if (funcName == "jwtHS512Sig" && node->getArgs().size() >= 2) {
        auto *secretArg = visit(node->getArgs()[0].get());
        auto *dataArg = visit(node->getArgs()[1].get());
        if (!secretArg || !dataArg) return nullptr;
        auto *r = builder_->CreateCall(getOrPanic("liva_jwt_hs512_sig"),
                                        {secretArg, dataArg}, "jwt.hs512");
        trackStringTemp(r);
        return r;
    }
    if (funcName == "constTimeEq" && node->getArgs().size() >= 2) {
        auto *aArg = visit(node->getArgs()[0].get());
        auto *bArg = visit(node->getArgs()[1].get());
        if (!aArg || !bArg) return nullptr;
        auto *r = builder_->CreateCall(getOrPanic("liva_const_time_eq"),
                                        {aArg, bArg}, "const.eq");
        return builder_->CreateICmpNE(r, builder_->getInt8(0), "const.eq.bool");
    }
    if (funcName == "base64UrlDecode" && !node->getArgs().empty()) {
        auto *dataArg = visit(node->getArgs()[0].get());
        if (!dataArg) return nullptr;
        auto *fn = getOrPanic("liva_base64_url_decode");
        auto *result = builder_->CreateCall(fn, {dataArg}, "b64u.dec.raw");
        trackStringTemp(result);
        auto *ptrTy = llvm::PointerType::getUnqual(*context_);
        auto *curFunc = builder_->GetInsertBlock()->getParent();
        auto *isNull = builder_->CreateICmpEQ(result, llvm::ConstantPointerNull::get(
            llvm::cast<llvm::PointerType>(ptrTy)), "b64u.dec.isnull");
        auto *hasVal = builder_->CreateNot(isNull, "b64u.dec.hasval");
        auto *optTy = getOptionalType(ptrTy);
        auto *optAlloca = createEntryBlockAlloca(curFunc, "b64u.dec.opt", optTy);
        builder_->CreateStore(hasVal, builder_->CreateStructGEP(optTy, optAlloca, 0));
        builder_->CreateStore(result, builder_->CreateStructGEP(optTy, optAlloca, 1));
        return builder_->CreateLoad(optTy, optAlloca, "b64u.dec.result");
    }

    // Crypto builtins: sha256, md5, hmacSha256
    if (funcName == "sha256" && !node->getArgs().empty()) {
        auto *dataArg = visit(node->getArgs()[0].get());
        if (!dataArg) return nullptr;
        auto *r = builder_->CreateCall(getOrPanic("liva_sha256"), {dataArg}, "sha256");
        trackStringTemp(r);
        return r;
    }
    if (funcName == "md5" && !node->getArgs().empty()) {
        auto *dataArg = visit(node->getArgs()[0].get());
        if (!dataArg) return nullptr;
        auto *r = builder_->CreateCall(getOrPanic("liva_md5"), {dataArg}, "md5");
        trackStringTemp(r);
        return r;
    }
    if (funcName == "hmacSha256" && node->getArgs().size() >= 2) {
        auto *keyArg = visit(node->getArgs()[0].get());
        auto *dataArg = visit(node->getArgs()[1].get());
        if (!keyArg || !dataArg) return nullptr;
        auto *r = builder_->CreateCall(getOrPanic("liva_hmac_sha256"), {keyArg, dataArg}, "hmac");
        trackStringTemp(r);
        return r;
    }
    if (funcName == "sha1" && !node->getArgs().empty()) {
        auto *dataArg = visit(node->getArgs()[0].get());
        if (!dataArg) return nullptr;
        auto *r = builder_->CreateCall(getOrPanic("liva_sha1"), {dataArg}, "sha1");
        trackStringTemp(r);
        return r;
    }
    if (funcName == "sha512" && !node->getArgs().empty()) {
        auto *dataArg = visit(node->getArgs()[0].get());
        if (!dataArg) return nullptr;
        auto *r = builder_->CreateCall(getOrPanic("liva_sha512"), {dataArg}, "sha512");
        trackStringTemp(r);
        return r;
    }
    if (funcName == "hmacSha1" && node->getArgs().size() >= 2) {
        auto *keyArg = visit(node->getArgs()[0].get());
        auto *dataArg = visit(node->getArgs()[1].get());
        if (!keyArg || !dataArg) return nullptr;
        auto *r = builder_->CreateCall(getOrPanic("liva_hmac_sha1"), {keyArg, dataArg}, "hmac");
        trackStringTemp(r);
        return r;
    }
    if (funcName == "hmacSha512" && node->getArgs().size() >= 2) {
        auto *keyArg = visit(node->getArgs()[0].get());
        auto *dataArg = visit(node->getArgs()[1].get());
        if (!keyArg || !dataArg) return nullptr;
        auto *r = builder_->CreateCall(getOrPanic("liva_hmac_sha512"), {keyArg, dataArg}, "hmac");
        trackStringTemp(r);
        return r;
    }

    return std::nullopt;
}

} // namespace liva

#endif // LIVA_HAS_LLVM
