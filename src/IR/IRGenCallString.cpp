#include "liva/IR/IRGen.h"

#ifdef LIVA_HAS_LLVM

namespace liva {

std::optional<llvm::Value *>
IRGen::tryEmitStringBuiltin(CallExpr *node, const std::string &funcName) {
    // === Stdlib: Regex ===
    if (funcName == "regexMatch" && node->getArgs().size() >= 2) {
        auto *strArg = visit(node->getArgs()[0].get());
        auto *patArg = visit(node->getArgs()[1].get());
        if (!strArg || !patArg) return nullptr;
        auto *fn = getOrPanic("liva_regex_match");
        auto *result = builder_->CreateCall(fn, {strArg, patArg}, "regex.match");
        return builder_->CreateTrunc(result, builder_->getInt1Ty(), "regex.bool");
    }

    if (funcName == "regexFind" && node->getArgs().size() >= 2) {
        auto *strArg = visit(node->getArgs()[0].get());
        auto *patArg = visit(node->getArgs()[1].get());
        if (!strArg || !patArg) return nullptr;
        auto *fn = getOrPanic("liva_regex_find");
        auto *result = builder_->CreateCall(fn, {strArg, patArg}, "regex.find.raw");
        trackStringTemp(result);
        // Wrap in Optional<string>
        auto *curFunc = builder_->GetInsertBlock()->getParent();
        auto *isNull = builder_->CreateICmpEQ(result, llvm::ConstantPointerNull::get(
            llvm::PointerType::getUnqual(*context_)), "regex.find.isnull");
        auto *hasVal = builder_->CreateNot(isNull, "regex.find.hasval");
        auto *optTy = getOptionalType(llvm::PointerType::getUnqual(*context_));
        auto *optAlloca = createEntryBlockAlloca(curFunc, "regex.find.opt", optTy);
        auto *hasValPtr = builder_->CreateStructGEP(optTy, optAlloca, 0);
        builder_->CreateStore(hasVal, hasValPtr);
        auto *valPtr = builder_->CreateStructGEP(optTy, optAlloca, 1);
        builder_->CreateStore(result, valPtr);
        return builder_->CreateLoad(optTy, optAlloca, "regex.find.result");
    }

    if (funcName == "regexFindAll" && node->getArgs().size() >= 2) {
        auto *strArg = visit(node->getArgs()[0].get());
        auto *patArg = visit(node->getArgs()[1].get());
        if (!strArg || !patArg) return nullptr;
        auto *curFunc = builder_->GetInsertBlock()->getParent();
        auto *countAlloca = createEntryBlockAlloca(curFunc, "regex.findall.count", builder_->getInt64Ty());
        auto *fn = getOrPanic("liva_regex_find_all");
        auto *resultPtr = builder_->CreateCall(fn, {strArg, patArg, countAlloca}, "regex.findall.data");
        auto *count = builder_->CreateLoad(builder_->getInt64Ty(), countAlloca, "regex.findall.len");
        auto *structTy = getDynArrayStructTy();
        auto *arrAlloca = createEntryBlockAlloca(curFunc, "regex.findall.arr", structTy);
        auto *dataPtr = builder_->CreateStructGEP(structTy, arrAlloca, 0);
        builder_->CreateStore(resultPtr, dataPtr);
        auto *lenPtr = builder_->CreateStructGEP(structTy, arrAlloca, 1);
        builder_->CreateStore(count, lenPtr);
        auto *capPtr = builder_->CreateStructGEP(structTy, arrAlloca, 2);
        builder_->CreateStore(count, capPtr);
        return builder_->CreateLoad(structTy, arrAlloca, "regex.findall.result");
    }

    if (funcName == "regexSplit" && node->getArgs().size() >= 2) {
        auto *strArg = visit(node->getArgs()[0].get());
        auto *patArg = visit(node->getArgs()[1].get());
        if (!strArg || !patArg) return nullptr;
        auto *curFunc = builder_->GetInsertBlock()->getParent();
        auto *countAlloca = createEntryBlockAlloca(curFunc, "regex.split.count", builder_->getInt64Ty());
        auto *fn = getOrPanic("liva_regex_split");
        auto *resultPtr = builder_->CreateCall(fn, {strArg, patArg, countAlloca}, "regex.split.data");
        auto *count = builder_->CreateLoad(builder_->getInt64Ty(), countAlloca, "regex.split.len");
        auto *structTy = getDynArrayStructTy();
        auto *arrAlloca = createEntryBlockAlloca(curFunc, "regex.split.arr", structTy);
        auto *dataPtr = builder_->CreateStructGEP(structTy, arrAlloca, 0);
        builder_->CreateStore(resultPtr, dataPtr);
        auto *lenPtr = builder_->CreateStructGEP(structTy, arrAlloca, 1);
        builder_->CreateStore(count, lenPtr);
        auto *capPtr = builder_->CreateStructGEP(structTy, arrAlloca, 2);
        builder_->CreateStore(count, capPtr);
        return builder_->CreateLoad(structTy, arrAlloca, "regex.split.result");
    }

    if (funcName == "regexReplace" && node->getArgs().size() >= 3) {
        auto *strArg = visit(node->getArgs()[0].get());
        auto *patArg = visit(node->getArgs()[1].get());
        auto *replArg = visit(node->getArgs()[2].get());
        if (!strArg || !patArg || !replArg) return nullptr;
        auto *fn = getOrPanic("liva_regex_replace");
        auto *r = builder_->CreateCall(fn, {strArg, patArg, replArg}, "regex.replace");
        trackStringTemp(r);
        return r;
    }

    // regexFindGroups(str, pattern) -> [string]
    if (funcName == "regexFindGroups" && node->getArgs().size() >= 2) {
        auto *strArg = visit(node->getArgs()[0].get());
        auto *patArg = visit(node->getArgs()[1].get());
        if (!strArg || !patArg) return nullptr;
        auto *curFunc = builder_->GetInsertBlock()->getParent();
        auto *countAlloca = createEntryBlockAlloca(curFunc, "regex.groups.count", builder_->getInt64Ty());
        auto *fn = getOrPanic("liva_regex_find_groups");
        auto *resultPtr = builder_->CreateCall(fn, {strArg, patArg, countAlloca}, "regex.groups.data");
        auto *count = builder_->CreateLoad(builder_->getInt64Ty(), countAlloca, "regex.groups.len");
        auto *structTy = getDynArrayStructTy();
        auto *arrAlloca = createEntryBlockAlloca(curFunc, "regex.groups.arr", structTy);
        auto *dataPtr = builder_->CreateStructGEP(structTy, arrAlloca, 0);
        builder_->CreateStore(resultPtr, dataPtr);
        auto *lenPtr = builder_->CreateStructGEP(structTy, arrAlloca, 1);
        builder_->CreateStore(count, lenPtr);
        auto *capPtr = builder_->CreateStructGEP(structTy, arrAlloca, 2);
        builder_->CreateStore(count, capPtr);
        return builder_->CreateLoad(structTy, arrAlloca, "regex.groups.result");
    }

    // regexCompile(pattern) -> i64
    if (funcName == "regexCompile" && !node->getArgs().empty()) {
        auto *patArg = visit(node->getArgs()[0].get());
        if (!patArg) return nullptr;
        auto *fn = getOrPanic("liva_regex_compile");
        return builder_->CreateCall(fn, {patArg}, "regex.compile");
    }

    // regexTest(handle, str) -> bool
    if (funcName == "regexTest" && node->getArgs().size() >= 2) {
        auto *handleArg = visit(node->getArgs()[0].get());
        auto *strArg = visit(node->getArgs()[1].get());
        if (!handleArg || !strArg) return nullptr;
        auto *fn = getOrPanic("liva_regex_test");
        auto *result = builder_->CreateCall(fn, {handleArg, strArg}, "regex.test");
        return builder_->CreateTrunc(result, builder_->getInt1Ty(), "regex.test.bool");
    }

    // regexExec(handle, str) -> string?
    if (funcName == "regexExec" && node->getArgs().size() >= 2) {
        auto *handleArg = visit(node->getArgs()[0].get());
        auto *strArg = visit(node->getArgs()[1].get());
        if (!handleArg || !strArg) return nullptr;
        auto *fn = getOrPanic("liva_regex_exec");
        auto *result = builder_->CreateCall(fn, {handleArg, strArg}, "regex.exec.raw");
        trackStringTemp(result);
        auto *curFunc = builder_->GetInsertBlock()->getParent();
        auto *isNull = builder_->CreateICmpEQ(result, llvm::ConstantPointerNull::get(
            llvm::PointerType::getUnqual(*context_)), "regex.exec.isnull");
        auto *hasVal = builder_->CreateNot(isNull, "regex.exec.hasval");
        auto *optTy = getOptionalType(llvm::PointerType::getUnqual(*context_));
        auto *optAlloca = createEntryBlockAlloca(curFunc, "regex.exec.opt", optTy);
        auto *hasValPtr = builder_->CreateStructGEP(optTy, optAlloca, 0);
        builder_->CreateStore(hasVal, hasValPtr);
        auto *valPtr = builder_->CreateStructGEP(optTy, optAlloca, 1);
        builder_->CreateStore(result, valPtr);
        return builder_->CreateLoad(optTy, optAlloca, "regex.exec.result");
    }

    // regexExecGroups(handle, str) -> [string]
    if (funcName == "regexExecGroups" && node->getArgs().size() >= 2) {
        auto *handleArg = visit(node->getArgs()[0].get());
        auto *strArg = visit(node->getArgs()[1].get());
        if (!handleArg || !strArg) return nullptr;
        auto *curFunc = builder_->GetInsertBlock()->getParent();
        auto *countAlloca = createEntryBlockAlloca(curFunc, "regex.execgrp.count", builder_->getInt64Ty());
        auto *fn = getOrPanic("liva_regex_exec_groups");
        auto *resultPtr = builder_->CreateCall(fn, {handleArg, strArg, countAlloca}, "regex.execgrp.data");
        auto *count = builder_->CreateLoad(builder_->getInt64Ty(), countAlloca, "regex.execgrp.len");
        auto *structTy = getDynArrayStructTy();
        auto *arrAlloca = createEntryBlockAlloca(curFunc, "regex.execgrp.arr", structTy);
        auto *dataPtr = builder_->CreateStructGEP(structTy, arrAlloca, 0);
        builder_->CreateStore(resultPtr, dataPtr);
        auto *lenPtr = builder_->CreateStructGEP(structTy, arrAlloca, 1);
        builder_->CreateStore(count, lenPtr);
        auto *capPtr = builder_->CreateStructGEP(structTy, arrAlloca, 2);
        builder_->CreateStore(count, capPtr);
        return builder_->CreateLoad(structTy, arrAlloca, "regex.execgrp.result");
    }

    // regexReplaceCompiled(handle, str, replacement) -> string
    if (funcName == "regexReplaceCompiled" && node->getArgs().size() >= 3) {
        auto *handleArg = visit(node->getArgs()[0].get());
        auto *strArg = visit(node->getArgs()[1].get());
        auto *replArg = visit(node->getArgs()[2].get());
        if (!handleArg || !strArg || !replArg) return nullptr;
        auto *fn = getOrPanic("liva_regex_replace_compiled");
        auto *r = builder_->CreateCall(fn, {handleArg, strArg, replArg}, "regex.replcomp");
        trackStringTemp(r);
        return r;
    }

    // regexFree(handle) -> void
    if (funcName == "regexFree" && !node->getArgs().empty()) {
        auto *handleArg = visit(node->getArgs()[0].get());
        if (!handleArg) return nullptr;
        auto *fn = getOrPanic("liva_regex_free");
        builder_->CreateCall(fn, {handleArg});
        return llvm::Constant::getNullValue(builder_->getInt64Ty());
    }

    // === Stdlib: String utility functions ===
    if (funcName == "strRepeat" && node->getArgs().size() >= 2) {
        auto *s = visit(node->getArgs()[0].get());
        auto *n = visit(node->getArgs()[1].get());
        if (!s || !n) return nullptr;
        if (!n->getType()->isIntegerTy(64))
            n = builder_->CreateSExt(n, builder_->getInt64Ty());
        auto *result = builder_->CreateCall(getOrPanic("liva_str_repeat"), {s, n}, "strrepeat");
        trackStringTemp(result);
        return result;
    }
    if ((funcName == "strPadLeft" || funcName == "strPadRight") && node->getArgs().size() >= 3) {
        auto *s = visit(node->getArgs()[0].get());
        auto *w = visit(node->getArgs()[1].get());
        auto *f = visit(node->getArgs()[2].get());
        if (!s || !w || !f) return nullptr;
        if (!w->getType()->isIntegerTy(64))
            w = builder_->CreateSExt(w, builder_->getInt64Ty());
        auto *fn = getOrPanic(funcName == "strPadLeft" ? "liva_str_pad_left" : "liva_str_pad_right");
        auto *result = builder_->CreateCall(fn, {s, w, f}, "strpad");
        trackStringTemp(result);
        return result;
    }
    if (funcName == "strJoin" && node->getArgs().size() >= 2) {
        // strJoin(arr, sep) — arr is DynArray of strings
        auto *arrVal = visit(node->getArgs()[0].get());
        auto *sep = visit(node->getArgs()[1].get());
        if (!arrVal || !sep) return nullptr;
        auto *arrStructTy = getDynArrayStructTy();
        auto *dataGEP = builder_->CreateStructGEP(arrStructTy, arrVal, 0);
        auto *data = builder_->CreateLoad(llvm::PointerType::getUnqual(*context_), dataGEP, "data");
        auto *lenGEP = builder_->CreateStructGEP(arrStructTy, arrVal, 1);
        auto *len = builder_->CreateLoad(builder_->getInt64Ty(), lenGEP, "len");
        auto *result = builder_->CreateCall(getOrPanic("liva_str_join"), {data, len, sep}, "strjoin");
        trackStringTemp(result);
        return result;
    }
    if (funcName == "strTrimLeft" && node->getArgs().size() >= 1) {
        auto *s = visit(node->getArgs()[0].get());
        if (!s) return nullptr;
        auto *result = builder_->CreateCall(getOrPanic("liva_str_trim_left"), {s}, "strtriml");
        trackStringTemp(result);
        return result;
    }
    if (funcName == "strTrimRight" && node->getArgs().size() >= 1) {
        auto *s = visit(node->getArgs()[0].get());
        if (!s) return nullptr;
        auto *result = builder_->CreateCall(getOrPanic("liva_str_trim_right"), {s}, "strtrimr");
        trackStringTemp(result);
        return result;
    }
    if (funcName == "strReverse" && node->getArgs().size() >= 1) {
        auto *s = visit(node->getArgs()[0].get());
        if (!s) return nullptr;
        auto *result = builder_->CreateCall(getOrPanic("liva_str_reverse"), {s}, "strrev");
        trackStringTemp(result);
        return result;
    }
    if (funcName == "strTrim" && node->getArgs().size() >= 1) {
        auto *s = visit(node->getArgs()[0].get());
        if (!s) return nullptr;
        auto *result = builder_->CreateCall(getOrPanic("liva_str_trim"), {s}, "strtrim");
        trackStringTemp(result);
        return result;
    }
    if (funcName == "strToUpper" && node->getArgs().size() >= 1) {
        auto *s = visit(node->getArgs()[0].get());
        if (!s) return nullptr;
        auto *result = builder_->CreateCall(getOrPanic("liva_str_to_upper"), {s}, "strupper");
        trackStringTemp(result);
        return result;
    }
    if (funcName == "strToLower" && node->getArgs().size() >= 1) {
        auto *s = visit(node->getArgs()[0].get());
        if (!s) return nullptr;
        auto *result = builder_->CreateCall(getOrPanic("liva_str_to_lower"), {s}, "strlower");
        trackStringTemp(result);
        return result;
    }
    if (funcName == "strContains" && node->getArgs().size() >= 2) {
        auto *s = visit(node->getArgs()[0].get());
        auto *sub = visit(node->getArgs()[1].get());
        if (!s || !sub) return nullptr;
        auto *result = builder_->CreateCall(getOrPanic("liva_str_contains"), {s, sub}, "strcontains");
        return builder_->CreateICmpNE(result, llvm::ConstantInt::get(builder_->getInt8Ty(), 0), "strcontainsbool");
    }
    if (funcName == "strStartsWith" && node->getArgs().size() >= 2) {
        auto *s = visit(node->getArgs()[0].get());
        auto *pre = visit(node->getArgs()[1].get());
        if (!s || !pre) return nullptr;
        auto *result = builder_->CreateCall(getOrPanic("liva_str_starts_with"), {s, pre}, "strstartswith");
        return builder_->CreateICmpNE(result, llvm::ConstantInt::get(builder_->getInt8Ty(), 0), "strswbool");
    }
    if (funcName == "strEndsWith" && node->getArgs().size() >= 2) {
        auto *s = visit(node->getArgs()[0].get());
        auto *suf = visit(node->getArgs()[1].get());
        if (!s || !suf) return nullptr;
        auto *result = builder_->CreateCall(getOrPanic("liva_str_ends_with"), {s, suf}, "strendswith");
        return builder_->CreateICmpNE(result, llvm::ConstantInt::get(builder_->getInt8Ty(), 0), "strewbool");
    }
    if (funcName == "strReplace" && node->getArgs().size() >= 3) {
        auto *s = visit(node->getArgs()[0].get());
        auto *old = visit(node->getArgs()[1].get());
        auto *rep = visit(node->getArgs()[2].get());
        if (!s || !old || !rep) return nullptr;
        auto *result = builder_->CreateCall(getOrPanic("liva_str_replace"), {s, old, rep}, "strreplace");
        trackStringTemp(result);
        return result;
    }
    if (funcName == "strSplit" && node->getArgs().size() >= 2) {
        auto *s = visit(node->getArgs()[0].get());
        auto *delim = visit(node->getArgs()[1].get());
        if (!s || !delim) return nullptr;
        // Allocate count on stack
        auto *countAlloca = builder_->CreateAlloca(builder_->getInt64Ty(), nullptr, "splitcount");
        auto *arrPtr = builder_->CreateCall(getOrPanic("liva_str_split"), {s, delim, countAlloca}, "strsplit");
        // Build DynArray struct
        auto *arrStructTy = getDynArrayStructTy();
        auto *arrStruct = builder_->CreateAlloca(arrStructTy, nullptr, "splitarr");
        auto *dataGEP = builder_->CreateStructGEP(arrStructTy, arrStruct, 0);
        builder_->CreateStore(arrPtr, dataGEP);
        auto *lenGEP = builder_->CreateStructGEP(arrStructTy, arrStruct, 1);
        auto *cnt = builder_->CreateLoad(builder_->getInt64Ty(), countAlloca, "cnt");
        builder_->CreateStore(cnt, lenGEP);
        auto *capGEP = builder_->CreateStructGEP(arrStructTy, arrStruct, 2);
        builder_->CreateStore(cnt, capGEP);
        // Return the DynArray *value* — callers store it into a struct-typed
        // alloca, not a pointer slot, so handing back the alloca pointer would
        // truncate the store to 8 of 24 bytes (data only), zeroing length.
        return builder_->CreateLoad(arrStructTy, arrStruct, "strsplit.val");
    }
    if ((funcName == "strChars" || funcName == "strLines") && node->getArgs().size() >= 1) {
        auto *s = visit(node->getArgs()[0].get());
        if (!s) return nullptr;
        auto *countAlloca = builder_->CreateAlloca(builder_->getInt64Ty(), nullptr, "count");
        auto *fn = getOrPanic(funcName == "strChars" ? "liva_str_chars" : "liva_str_lines");
        auto *arrPtr = builder_->CreateCall(fn, {s, countAlloca}, funcName.c_str());
        auto *arrStructTy = getDynArrayStructTy();
        auto *arrStruct = builder_->CreateAlloca(arrStructTy, nullptr, "arr");
        auto *dataGEP = builder_->CreateStructGEP(arrStructTy, arrStruct, 0);
        builder_->CreateStore(arrPtr, dataGEP);
        auto *lenGEP = builder_->CreateStructGEP(arrStructTy, arrStruct, 1);
        auto *cnt = builder_->CreateLoad(builder_->getInt64Ty(), countAlloca, "cnt");
        builder_->CreateStore(cnt, lenGEP);
        auto *capGEP = builder_->CreateStructGEP(arrStructTy, arrStruct, 2);
        builder_->CreateStore(cnt, capGEP);
        return builder_->CreateLoad(arrStructTy, arrStruct, "strarr.val");
    }

    // === Bytes <-> string / hex / base64url converters with explicit length ===

    // strToBytes(s) -> [u8]
    if (funcName == "strToBytes" && !node->getArgs().empty()) {
        auto *s = visit(node->getArgs()[0].get());
        if (!s) return nullptr;
        auto *funcCur = builder_->GetInsertBlock()->getParent();
        auto *countAlloca = createEntryBlockAlloca(funcCur, "bytes.len",
            builder_->getInt64Ty());
        auto *dataPtr = builder_->CreateCall(getOrPanic("liva_str_to_bytes"),
            {s, countAlloca}, "bytes.data");
        auto *daTy = getDynArrayStructTy();
        auto *daAlloca = createEntryBlockAlloca(funcCur, "bytes.da", daTy);
        builder_->CreateStore(dataPtr,
            builder_->CreateStructGEP(daTy, daAlloca, 0));
        auto *cnt = builder_->CreateLoad(builder_->getInt64Ty(), countAlloca, "bytes.cnt");
        builder_->CreateStore(cnt, builder_->CreateStructGEP(daTy, daAlloca, 1));
        builder_->CreateStore(cnt, builder_->CreateStructGEP(daTy, daAlloca, 2));
        return builder_->CreateLoad(daTy, daAlloca, "bytes.val");
    }

    // bytesToStr(b: [u8]) -> string. Pass DynArray by-value, runtime reads
    // .data and .length explicitly.
    if (funcName == "bytesToStr" && !node->getArgs().empty()) {
        auto *arr = visit(node->getArgs()[0].get());
        if (!arr) return nullptr;
        auto *daTy = getDynArrayStructTy();
        auto *funcCur = builder_->GetInsertBlock()->getParent();
        auto *src = createEntryBlockAlloca(funcCur, "b2s.src", daTy);
        builder_->CreateStore(arr, src);
        auto *ptrTy = llvm::PointerType::getUnqual(*context_);
        auto *data = builder_->CreateLoad(ptrTy,
            builder_->CreateStructGEP(daTy, src, 0), "b2s.data");
        auto *len = builder_->CreateLoad(builder_->getInt64Ty(),
            builder_->CreateStructGEP(daTy, src, 1), "b2s.len");
        auto *r = builder_->CreateCall(getOrPanic("liva_bytes_to_str"),
            {data, len}, "b2s.str");
        trackStringTemp(r);
        return r;
    }

    // hexEncodeBytes(b: [u8]) -> string
    if (funcName == "hexEncodeBytes" && !node->getArgs().empty()) {
        auto *arr = visit(node->getArgs()[0].get());
        if (!arr) return nullptr;
        auto *daTy = getDynArrayStructTy();
        auto *funcCur = builder_->GetInsertBlock()->getParent();
        auto *src = createEntryBlockAlloca(funcCur, "hex.src", daTy);
        builder_->CreateStore(arr, src);
        auto *ptrTy = llvm::PointerType::getUnqual(*context_);
        auto *data = builder_->CreateLoad(ptrTy,
            builder_->CreateStructGEP(daTy, src, 0));
        auto *len = builder_->CreateLoad(builder_->getInt64Ty(),
            builder_->CreateStructGEP(daTy, src, 1));
        auto *r = builder_->CreateCall(getOrPanic("liva_hex_encode_bytes"),
            {data, len}, "hex.enc");
        trackStringTemp(r);
        return r;
    }

    // base64UrlEncodeBytes(b: [u8]) -> string
    if (funcName == "base64UrlEncodeBytes" && !node->getArgs().empty()) {
        auto *arr = visit(node->getArgs()[0].get());
        if (!arr) return nullptr;
        auto *daTy = getDynArrayStructTy();
        auto *funcCur = builder_->GetInsertBlock()->getParent();
        auto *src = createEntryBlockAlloca(funcCur, "b64u.src", daTy);
        builder_->CreateStore(arr, src);
        auto *ptrTy = llvm::PointerType::getUnqual(*context_);
        auto *data = builder_->CreateLoad(ptrTy,
            builder_->CreateStructGEP(daTy, src, 0));
        auto *len = builder_->CreateLoad(builder_->getInt64Ty(),
            builder_->CreateStructGEP(daTy, src, 1));
        auto *r = builder_->CreateCall(getOrPanic("liva_base64_url_encode_bytes"),
            {data, len}, "b64u.enc");
        trackStringTemp(r);
        return r;
    }

    // gzipEncode(b: [u8]) -> [u8]
    if (funcName == "gzipEncode" && !node->getArgs().empty()) {
        auto *arr = visit(node->getArgs()[0].get());
        if (!arr) return nullptr;
        auto *daTy = getDynArrayStructTy();
        auto *funcCur = builder_->GetInsertBlock()->getParent();
        auto *src = createEntryBlockAlloca(funcCur, "gz.enc.src", daTy);
        builder_->CreateStore(arr, src);
        auto *ptrTy = llvm::PointerType::getUnqual(*context_);
        auto *data = builder_->CreateLoad(ptrTy,
            builder_->CreateStructGEP(daTy, src, 0));
        auto *len = builder_->CreateLoad(builder_->getInt64Ty(),
            builder_->CreateStructGEP(daTy, src, 1));
        auto *outLenAlloca = createEntryBlockAlloca(funcCur, "gz.enc.olen",
            builder_->getInt64Ty());
        auto *encoded = builder_->CreateCall(getOrPanic("liva_gzip_encode_bytes"),
            {data, len, outLenAlloca}, "gz.enc.data");
        auto *outLen = builder_->CreateLoad(builder_->getInt64Ty(),
            outLenAlloca, "gz.enc.olen.v");
        // Build [u8] DynArray.
        auto *daAlloca = createEntryBlockAlloca(funcCur, "gz.enc.da", daTy);
        builder_->CreateStore(encoded,
            builder_->CreateStructGEP(daTy, daAlloca, 0));
        builder_->CreateStore(outLen,
            builder_->CreateStructGEP(daTy, daAlloca, 1));
        builder_->CreateStore(outLen,
            builder_->CreateStructGEP(daTy, daAlloca, 2));
        return builder_->CreateLoad(daTy, daAlloca, "gz.enc.val");
    }

    // gzipDecode(b: [u8]) -> [u8]?
    if (funcName == "gzipDecode" && !node->getArgs().empty()) {
        auto *arr = visit(node->getArgs()[0].get());
        if (!arr) return nullptr;
        auto *daTy = getDynArrayStructTy();
        auto *funcCur = builder_->GetInsertBlock()->getParent();
        auto *src = createEntryBlockAlloca(funcCur, "gz.dec.src", daTy);
        builder_->CreateStore(arr, src);
        auto *ptrTy = llvm::PointerType::getUnqual(*context_);
        auto *data = builder_->CreateLoad(ptrTy,
            builder_->CreateStructGEP(daTy, src, 0));
        auto *len = builder_->CreateLoad(builder_->getInt64Ty(),
            builder_->CreateStructGEP(daTy, src, 1));
        auto *outLenAlloca = createEntryBlockAlloca(funcCur, "gz.dec.olen",
            builder_->getInt64Ty());
        auto *okAlloca = createEntryBlockAlloca(funcCur, "gz.dec.ok",
            builder_->getInt8Ty());
        builder_->CreateStore(builder_->getInt8(0), okAlloca);
        auto *decoded = builder_->CreateCall(getOrPanic("liva_gzip_decode_bytes"),
            {data, len, outLenAlloca, okAlloca}, "gz.dec.data");
        auto *outLen = builder_->CreateLoad(builder_->getInt64Ty(),
            outLenAlloca, "gz.dec.olen.v");
        auto *okVal = builder_->CreateLoad(builder_->getInt8Ty(), okAlloca, "gz.dec.ok.v");
        auto *hasVal = builder_->CreateICmpNE(okVal, builder_->getInt8(0));
        // Build inner DynArray + wrap in Optional.
        auto *daAlloca = createEntryBlockAlloca(funcCur, "gz.dec.da", daTy);
        builder_->CreateStore(decoded,
            builder_->CreateStructGEP(daTy, daAlloca, 0));
        builder_->CreateStore(outLen,
            builder_->CreateStructGEP(daTy, daAlloca, 1));
        builder_->CreateStore(outLen,
            builder_->CreateStructGEP(daTy, daAlloca, 2));
        auto *daVal = builder_->CreateLoad(daTy, daAlloca, "gz.dec.da.val");
        auto *optTy = getOptionalType(daTy);
        auto *optAlloca = createEntryBlockAlloca(funcCur, "gz.dec.opt", optTy);
        builder_->CreateStore(hasVal,
            builder_->CreateStructGEP(optTy, optAlloca, 0));
        builder_->CreateStore(daVal,
            builder_->CreateStructGEP(optTy, optAlloca, 1));
        return builder_->CreateLoad(optTy, optAlloca, "gz.dec.opt.val");
    }

    // hexDecodeBytes(s) -> [u8]?     base64UrlDecodeBytes(s) -> [u8]?
    if ((funcName == "hexDecodeBytes" || funcName == "base64UrlDecodeBytes") &&
        !node->getArgs().empty()) {
        auto *s = visit(node->getArgs()[0].get());
        if (!s) return nullptr;
        auto *funcCur = builder_->GetInsertBlock()->getParent();
        auto *countAlloca = createEntryBlockAlloca(funcCur, "dec.len",
            builder_->getInt64Ty());
        auto *okAlloca = createEntryBlockAlloca(funcCur, "dec.ok",
            builder_->getInt8Ty());
        builder_->CreateStore(builder_->getInt8(0), okAlloca);
        auto *fn = getOrPanic(funcName == "hexDecodeBytes"
                              ? "liva_hex_decode_bytes"
                              : "liva_base64_url_decode_bytes");
        auto *dataPtr = builder_->CreateCall(fn, {s, countAlloca, okAlloca},
            "dec.data");
        auto *okVal = builder_->CreateLoad(builder_->getInt8Ty(), okAlloca, "dec.ok.v");
        auto *hasVal = builder_->CreateICmpNE(okVal, builder_->getInt8(0), "dec.has");

        // Build the inner DynArray value.
        auto *daTy = getDynArrayStructTy();
        auto *daAlloca = createEntryBlockAlloca(funcCur, "dec.da", daTy);
        builder_->CreateStore(dataPtr,
            builder_->CreateStructGEP(daTy, daAlloca, 0));
        auto *cnt = builder_->CreateLoad(builder_->getInt64Ty(), countAlloca, "dec.cnt");
        builder_->CreateStore(cnt, builder_->CreateStructGEP(daTy, daAlloca, 1));
        builder_->CreateStore(cnt, builder_->CreateStructGEP(daTy, daAlloca, 2));
        auto *daVal = builder_->CreateLoad(daTy, daAlloca, "dec.da.val");

        // Wrap in Optional<DynArray>.
        auto *optTy = getOptionalType(daTy);
        auto *optAlloca = createEntryBlockAlloca(funcCur, "dec.opt", optTy);
        builder_->CreateStore(hasVal,
            builder_->CreateStructGEP(optTy, optAlloca, 0));
        builder_->CreateStore(daVal,
            builder_->CreateStructGEP(optTy, optAlloca, 1));
        return builder_->CreateLoad(optTy, optAlloca, "dec.opt.val");
    }

    // === Stdlib: UTF-8 helpers ===

    if (funcName == "strCharCount" && !node->getArgs().empty()) {
        auto *s = visit(node->getArgs()[0].get());
        if (!s) return nullptr;
        auto *fn = getOrPanic("liva_str_char_count");
        return builder_->CreateCall(fn, {s}, "str.char.count");
    }

    if (funcName == "strCodepointAt" && node->getArgs().size() >= 2) {
        auto *s = visit(node->getArgs()[0].get());
        auto *idx = visit(node->getArgs()[1].get());
        if (!s || !idx) return nullptr;
        if (idx->getType()->isIntegerTy(32))
            idx = builder_->CreateSExt(idx, builder_->getInt64Ty());
        auto *fn = getOrPanic("liva_str_codepoint_at");
        return builder_->CreateCall(fn, {s, idx}, "str.cp.at");
    }

    if (funcName == "strIsAscii" && !node->getArgs().empty()) {
        auto *s = visit(node->getArgs()[0].get());
        if (!s) return nullptr;
        auto *fn = getOrPanic("liva_str_is_ascii");
        auto *r = builder_->CreateCall(fn, {s}, "str.is.ascii");
        return builder_->CreateTrunc(r, builder_->getInt1Ty(), "str.is.ascii.bool");
    }

    // charIsX(cp) -> bool  — codepoint predicates
    if ((funcName == "charIsAlpha" || funcName == "charIsDigit" ||
         funcName == "charIsAlnum" || funcName == "charIsSpace" ||
         funcName == "charIsUpper" || funcName == "charIsLower") &&
        !node->getArgs().empty()) {
        auto *cp = visit(node->getArgs()[0].get());
        if (!cp) return nullptr;
        if (cp->getType()->isIntegerTy(64))
            cp = builder_->CreateTrunc(cp, builder_->getInt32Ty());
        std::string rt;
        if (funcName == "charIsAlpha")      rt = "liva_char_is_alpha";
        else if (funcName == "charIsDigit") rt = "liva_char_is_digit";
        else if (funcName == "charIsAlnum") rt = "liva_char_is_alnum";
        else if (funcName == "charIsSpace") rt = "liva_char_is_space";
        else if (funcName == "charIsUpper") rt = "liva_char_is_upper";
        else                                 rt = "liva_char_is_lower";
        auto *fn = getOrPanic(rt.c_str());
        auto *r = builder_->CreateCall(fn, {cp}, "char.pred");
        return builder_->CreateTrunc(r, builder_->getInt1Ty(), "char.pred.bool");
    }

    // charToUpper/Lower(cp) -> i32
    if ((funcName == "charToUpper" || funcName == "charToLower") &&
        !node->getArgs().empty()) {
        auto *cp = visit(node->getArgs()[0].get());
        if (!cp) return nullptr;
        if (cp->getType()->isIntegerTy(64))
            cp = builder_->CreateTrunc(cp, builder_->getInt32Ty());
        auto *fn = getOrPanic(funcName == "charToUpper"
            ? "liva_char_to_upper" : "liva_char_to_lower");
        return builder_->CreateCall(fn, {cp}, "char.case");
    }

    return std::nullopt;
}

} // namespace liva

#endif // LIVA_HAS_LLVM
