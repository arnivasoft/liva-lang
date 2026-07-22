#include "liva/IR/IRGen.h"

#ifdef LIVA_HAS_LLVM

#include <llvm/IR/Intrinsics.h>

namespace liva {

std::optional<llvm::Value *>
IRGen::tryEmitDataBuiltin(CallExpr *node, const std::string &funcName) {
    // === Stdlib: TOML ===

    // Helper: ensure an integer-typed value is i64 (sign-extend from smaller widths).
    // Liva integer literals default to i32, but most stdlib runtime functions expect i64.
    auto toI64 = [&](llvm::Value *v) -> llvm::Value * {
        if (!v) return v;
        auto *t = v->getType();
        if (t->isIntegerTy() && !t->isIntegerTy(64))
            return builder_->CreateSExt(v, builder_->getInt64Ty(), "i64.coerce");
        return v;
    };

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

    return std::nullopt;
}

} // namespace liva

#endif // LIVA_HAS_LLVM
