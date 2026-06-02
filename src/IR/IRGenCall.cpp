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
        auto *memberExpr = static_cast<MemberExpr *>(node->getCallee());
        const auto &methodName = memberExpr->getMember();

        // ── UI event-method fast path: heap-own inline closure envs ───────
        // widget.onClick / onChange / onSelect / onKey / onRightClick and
        // MenuItem.onClick / ToolItem.onClick, when the argument is a closure
        // LITERAL. We intercept it, compute the captured-env size, and pass it
        // so the runtime heap-copies the env (freed on widget/item destroy).
        // Receiver classes all store the wx handle as `i32 handle` at field
        // index 0 (LLVM struct index 1, after the vtable). Non-literal args
        // fall through to the ordinary method (stack env).
        //
        // Special case: Menu.addItem/addCheckItem(label, <closure>) and
        // Toolbar.addTool(label, <closure>) — the closure literal is the 2nd
        // arg (bound to the new item, not to an onX method). We create the
        // item, heap-own the closure onto it, and return a MenuItem/ToolItem.
        if (node->getArgs().size() == 1 &&
            node->getArgs()[0]->getKind() == ASTNode::NodeKind::ClosureExpr) {
            std::string recvClass = resolveExprClassTypeName(memberExpr->getObject());
            auto isControlDescendant = [&](std::string tn) -> bool {
                for (int i = 0; i < 64 && !tn.empty(); ++i) {
                    if (tn == "Control") return true;
                    auto pit = classParent_.find(tn);
                    if (pit == classParent_.end()) return false;
                    tn = pit->second;
                }
                return false;
            };
            const char *cFn = nullptr;
            if (!recvClass.empty()) {
                if (recvClass == "MenuItem" && methodName == "onClick")
                    cFn = "liva_ui_menu_item_on_click";
                else if (recvClass == "ToolItem" && methodName == "onClick")
                    cFn = "liva_ui_tool_item_on_click";
                else if (isControlDescendant(recvClass)) {
                    if (methodName == "onClick")      cFn = "liva_ui_on_click";
                    else if (methodName == "onChange")     cFn = "liva_ui_on_change";
                    else if (methodName == "onSelect")     cFn = "liva_ui_on_select";
                    else if (methodName == "onKey")        cFn = "liva_ui_on_key";
                    else if (methodName == "onRightClick") cFn = "liva_ui_on_right_click";
                }
            }
            auto rcIt = recvClass.empty() ? classTypes_.end()
                                          : classTypes_.find(recvClass);
            if (cFn && rcIt != classTypes_.end()) {
                auto *ptrTy = llvm::PointerType::getUnqual(*context_);
                llvm::Value *selfPtr = visit(memberExpr->getObject());
                if (!selfPtr) return nullptr;
                // handle: first field → struct index 1 (vtable at 0).
                auto *handleGEP = builder_->CreateStructGEP(
                    rcIt->second, selfPtr, 1, "handle.gep");
                auto *handle = builder_->CreateLoad(
                    builder_->getInt32Ty(), handleGEP, "handle");
                lastClosureEnvSize_ = 0;
                auto *closureVal = visit(node->getArgs()[0].get());
                if (!closureVal) return nullptr;
                uint64_t envSize = lastClosureEnvSize_;
                auto *closureObjTy = getClosureObjTy();
                auto *cbAlloca = createEntryBlockAlloca(
                    builder_->GetInsertBlock()->getParent(), "cb.tmp", closureObjTy);
                builder_->CreateStore(closureVal, cbAlloca);
                auto *fnPtr = builder_->CreateLoad(
                    ptrTy, builder_->CreateStructGEP(closureObjTy, cbAlloca, 0));
                auto *envPtr = builder_->CreateLoad(
                    ptrTy, builder_->CreateStructGEP(closureObjTy, cbAlloca, 1));
                auto *i32Ty = builder_->getInt32Ty();
                builder_->CreateCall(
                    getOrPanic(cFn),
                    {handle, fnPtr, envPtr, llvm::ConstantInt::get(i32Ty, envSize)});
                return llvm::Constant::getNullValue(i32Ty);
            }
        }
        // addItem/addCheckItem(label, closure) on Menu, addTool on Toolbar.
        if (node->getArgs().size() == 2 &&
            node->getArgs()[1]->getKind() == ASTNode::NodeKind::ClosureExpr) {
            std::string recvClass = resolveExprClassTypeName(memberExpr->getObject());
            const char *addFn = nullptr;    // FFI to create the item
            const char *bindFn = nullptr;   // FFI to bind the click
            const char *itemClass = nullptr;
            if (recvClass == "Menu" && methodName == "addItem") {
                addFn = "liva_ui_menu_add_item";
                bindFn = "liva_ui_menu_item_on_click"; itemClass = "MenuItem";
            } else if (recvClass == "Menu" && methodName == "addCheckItem") {
                addFn = "liva_ui_menu_add_check_item";
                bindFn = "liva_ui_menu_item_on_click"; itemClass = "MenuItem";
            } else if (recvClass == "Toolbar" && methodName == "addTool") {
                addFn = "liva_ui_toolbar_add_tool";
                bindFn = "liva_ui_tool_item_on_click"; itemClass = "ToolItem";
            }
            auto rcIt = (addFn == nullptr) ? classTypes_.end()
                                           : classTypes_.find(recvClass);
            if (addFn && rcIt != classTypes_.end()) {
                auto *ptrTy = llvm::PointerType::getUnqual(*context_);
                auto *i32Ty = builder_->getInt32Ty();
                llvm::Value *selfPtr = visit(memberExpr->getObject());
                if (!selfPtr) return nullptr;
                auto *hGEP = builder_->CreateStructGEP(rcIt->second, selfPtr, 1, "rh.gep");
                auto *rh = builder_->CreateLoad(i32Ty, hGEP, "rh");
                auto *label = visit(node->getArgs()[0].get());
                if (!label) return nullptr;
                auto *itemH = builder_->CreateCall(getOrPanic(addFn), {rh, label}, "item.h");
                lastClosureEnvSize_ = 0;
                auto *closureVal = visit(node->getArgs()[1].get());
                if (!closureVal) return nullptr;
                uint64_t envSize = lastClosureEnvSize_;
                auto *closureObjTy = getClosureObjTy();
                auto *cbA = createEntryBlockAlloca(
                    builder_->GetInsertBlock()->getParent(), "cb.tmp", closureObjTy);
                builder_->CreateStore(closureVal, cbA);
                auto *fnPtr = builder_->CreateLoad(
                    ptrTy, builder_->CreateStructGEP(closureObjTy, cbA, 0));
                auto *envPtr = builder_->CreateLoad(
                    ptrTy, builder_->CreateStructGEP(closureObjTy, cbA, 1));
                builder_->CreateCall(getOrPanic(bindFn),
                    {itemH, fnPtr, envPtr, llvm::ConstantInt::get(i32Ty, envSize)});
                auto *ctor = module_->getFunction(std::string(itemClass) + "_init");
                if (ctor)
                    return builder_->CreateCall(ctor, {itemH}, "item.obj");
                return itemH;
            }
        }

        // Result.ok(val) / Result.err(val) constructor
        if (memberExpr->getObject()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
            auto *ident = static_cast<IdentifierExpr *>(memberExpr->getObject());
            if (ident->getName() == "Result" && vars_.currentFuncResultInfo &&
                (methodName == "ok" || methodName == "err")) {
                if (!node->getArgs().empty()) {
                    auto *argVal = visit(node->getArgs()[0].get());
                    if (!argVal) return nullptr;
                    if (methodName == "ok")
                        return emitResultOk(vars_.currentFuncResultInfo->okType,
                                            vars_.currentFuncResultInfo->errType, argVal);
                    else
                        return emitResultErr(vars_.currentFuncResultInfo->okType,
                                             vars_.currentFuncResultInfo->errType, argVal);
                }
                return nullptr;
            }
        }

        // Optional methods on a local Optional-typed variable:
        //   v.isSome() -> i1, v.isNone() -> i1, v.unwrap() -> T (panic if empty).
        // The Optional value is laid out as { i1 hasVal, T val }.
        if (memberExpr->getObject()->getKind() == ASTNode::NodeKind::IdentifierExpr &&
            (methodName == "isSome" || methodName == "isNone" ||
             methodName == "unwrap")) {
            auto *ident = static_cast<IdentifierExpr *>(memberExpr->getObject());
            auto optIt = vars_.varOptionalTypes.find(ident->getName());
            auto nvIt = vars_.namedValues.find(ident->getName());
            if (optIt != vars_.varOptionalTypes.end() &&
                nvIt != vars_.namedValues.end()) {
                auto *innerTy = optIt->second;
                auto *optTy = getOptionalType(innerTy);
                auto *optAlloca = nvIt->second;
                auto *hasValPtr = builder_->CreateStructGEP(optTy, optAlloca, 0,
                                                            ident->getName() + ".hasval.gep");
                auto *hasVal = builder_->CreateLoad(builder_->getInt1Ty(), hasValPtr,
                                                    ident->getName() + ".hasval");
                if (methodName == "isSome")
                    return hasVal;
                if (methodName == "isNone")
                    return builder_->CreateNot(hasVal, ident->getName() + ".isnone");
                // unwrap: branch on hasVal; panic if empty, else load the payload.
                auto *curFunc = builder_->GetInsertBlock()->getParent();
                auto *someBB = llvm::BasicBlock::Create(*context_, "opt.unwrap.some", curFunc);
                auto *panicBB = llvm::BasicBlock::Create(*context_, "opt.unwrap.panic", curFunc);
                builder_->CreateCondBr(hasVal, someBB, panicBB);

                builder_->SetInsertPoint(panicBB);
                auto *panicFn = getOrPanic("liva_panic");
                auto *msg = builder_->CreateGlobalString("unwrap of nil Optional value");
                builder_->CreateCall(panicFn, {msg});
                builder_->CreateUnreachable();

                builder_->SetInsertPoint(someBB);
                auto *valPtr = builder_->CreateStructGEP(optTy, optAlloca, 1,
                                                         ident->getName() + ".val.gep");
                return builder_->CreateLoad(innerTy, valPtr, ident->getName() + ".val");
            }
        }

        // r.unwrap() method for Result types
        if (memberExpr->getObject()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
            auto *ident = static_cast<IdentifierExpr *>(memberExpr->getObject());
            if (methodName == "unwrap") {
                auto rtIt = vars_.varResultTypes.find(ident->getName());
                if (rtIt != vars_.varResultTypes.end()) {
                    auto nvIt = vars_.namedValues.find(ident->getName());
                    if (nvIt == vars_.namedValues.end()) return nullptr;
                    auto *resAlloca = nvIt->second;
                    auto *resTy = getResultType(rtIt->second.okType, rtIt->second.errType);
                    auto *tagPtr = builder_->CreateStructGEP(resTy, resAlloca, 0, "unwrap.tag");
                    auto *tag = builder_->CreateLoad(builder_->getInt32Ty(), tagPtr, "unwrap.tag.val");
                    auto *isOk = builder_->CreateICmpEQ(tag, builder_->getInt32(0), "unwrap.isok");

                    auto *curFunc = builder_->GetInsertBlock()->getParent();
                    auto *okBB = llvm::BasicBlock::Create(*context_, "unwrap.ok", curFunc);
                    auto *panicBB = llvm::BasicBlock::Create(*context_, "unwrap.panic", curFunc);
                    builder_->CreateCondBr(isOk, okBB, panicBB);

                    builder_->SetInsertPoint(panicBB);
                    auto *panicFn = getOrPanic("liva_panic");
                    auto *msg = builder_->CreateGlobalString("unwrap of Err Result value");
                    builder_->CreateCall(panicFn, {msg});
                    builder_->CreateUnreachable();

                    builder_->SetInsertPoint(okBB);
                    auto *payloadPtr = builder_->CreateStructGEP(resTy, resAlloca, 1, "unwrap.payload");
                    return builder_->CreateLoad(rtIt->second.okType, payloadPtr, "unwrap.val");
                }
            }
        }

        // File.open(path, mode) → Optional<ptr>
        if (memberExpr->getObject()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
            auto *ident = static_cast<IdentifierExpr *>(memberExpr->getObject());
            if (ident->getName() == "File" && methodName == "open" &&
                node->getArgs().size() >= 2) {
                auto *pathVal = visit(node->getArgs()[0].get());
                auto *modeVal = visit(node->getArgs()[1].get());
                if (!pathVal || !modeVal) return nullptr;

                auto *openFn = getOrPanic("liva_file_open");
                auto *fp = builder_->CreateCall(openFn, {pathVal, modeVal}, "file.fp");
                auto *ptrTy = llvm::PointerType::getUnqual(*context_);

                // Build Optional<ptr>: { i1, ptr }
                auto *isNull = builder_->CreateICmpEQ(fp,
                    llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(ptrTy)),
                    "file.isnull");
                auto *hasVal = builder_->CreateNot(isNull, "file.hasval");
                auto *optTy = getOptionalType(ptrTy);
                auto *curFunc = builder_->GetInsertBlock()->getParent();
                auto *optAlloca = createEntryBlockAlloca(curFunc, "file.opt", optTy);
                auto *hasValPtr = builder_->CreateStructGEP(optTy, optAlloca, 0);
                builder_->CreateStore(hasVal, hasValPtr);
                auto *valPtr = builder_->CreateStructGEP(optTy, optAlloca, 1);
                builder_->CreateStore(fp, valPtr);
                return builder_->CreateLoad(optTy, optAlloca, "file.opt.val");
            }
        }

        // File instance methods: file.readLine(), file.readAll(), file.write(), file.writeLine(), file.close()
        if (memberExpr->getObject()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
            auto *ident = static_cast<IdentifierExpr *>(memberExpr->getObject());
            if (vars_.varFileTypes.count(ident->getName())) {
                auto nvIt = vars_.namedValues.find(ident->getName());
                if (nvIt == vars_.namedValues.end()) return nullptr;
                auto *ptrTy = llvm::PointerType::getUnqual(*context_);
                auto *fp = builder_->CreateLoad(ptrTy, nvIt->second, "file.ptr");

                if (methodName == "readLine") {
                    auto *fn = getOrPanic("liva_file_read_line");
                    auto *raw = builder_->CreateCall(fn, {fp}, "file.readline.raw");
                    trackStringTemp(raw);
                    // Build Optional<string>: null check → { i1, ptr }
                    auto *isNull = builder_->CreateICmpEQ(raw,
                        llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(ptrTy)),
                        "file.readline.isnull");
                    auto *hasVal = builder_->CreateNot(isNull, "file.readline.hasval");
                    auto *optTy = getOptionalType(ptrTy);
                    auto *curFunc = builder_->GetInsertBlock()->getParent();
                    auto *optAlloca = createEntryBlockAlloca(curFunc, "file.readline.opt", optTy);
                    auto *hasValPtr = builder_->CreateStructGEP(optTy, optAlloca, 0);
                    builder_->CreateStore(hasVal, hasValPtr);
                    auto *valPtr = builder_->CreateStructGEP(optTy, optAlloca, 1);
                    builder_->CreateStore(raw, valPtr);
                    return builder_->CreateLoad(optTy, optAlloca, "file.readline.opt");
                }

                if (methodName == "readAll") {
                    auto *fn = getOrPanic("liva_file_read_all");
                    auto *r = builder_->CreateCall(fn, {fp}, "file.readall");
                    trackStringTemp(r);
                    return r;
                }

                if (methodName == "write" && !node->getArgs().empty()) {
                    auto *strVal = visit(node->getArgs()[0].get());
                    if (!strVal) return nullptr;
                    auto *fn = getOrPanic("liva_file_write");
                    builder_->CreateCall(fn, {fp, strVal});
                    return nullptr;
                }

                if (methodName == "writeLine" && !node->getArgs().empty()) {
                    auto *strVal = visit(node->getArgs()[0].get());
                    if (!strVal) return nullptr;
                    auto *fn = getOrPanic("liva_file_write_line");
                    builder_->CreateCall(fn, {fp, strVal});
                    return nullptr;
                }

                if (methodName == "close") {
                    auto *fn = getOrPanic("liva_file_close");
                    builder_->CreateCall(fn, {fp});
                    return nullptr;
                }

                if (methodName == "seek" && node->getArgs().size() >= 2) {
                    auto *offsetVal = visit(node->getArgs()[0].get());
                    auto *whenceVal = visit(node->getArgs()[1].get());
                    if (!offsetVal || !whenceVal) return nullptr;
                    auto *fn = getOrPanic("liva_file_seek");
                    return builder_->CreateCall(fn, {fp, offsetVal, whenceVal}, "file.seek");
                }

                if (methodName == "tell") {
                    auto *fn = getOrPanic("liva_file_tell");
                    return builder_->CreateCall(fn, {fp}, "file.tell");
                }

                if (methodName == "size") {
                    auto *fn = getOrPanic("liva_file_size");
                    return builder_->CreateCall(fn, {fp}, "file.size");
                }
            }
        }

        // String method calls: s.contains(), s.startsWith(), etc.
        if (memberExpr->getObject()->getResolvedType() &&
            memberExpr->getObject()->getResolvedType()->getKind() == TypeRepr::Kind::String) {
            auto *obj = visit(memberExpr->getObject());
            if (!obj) return nullptr;

            if (methodName == "contains" && node->getArgs().size() >= 1) {
                auto *arg = visit(node->getArgs()[0].get());
                if (!arg) return nullptr;
                auto *fn = getOrPanic("liva_str_contains");
                auto *result = builder_->CreateCall(fn, {obj, arg}, "str.contains");
                return builder_->CreateTrunc(result, builder_->getInt1Ty(), "str.contains.bool");
            }

            if (methodName == "startsWith" && node->getArgs().size() >= 1) {
                auto *arg = visit(node->getArgs()[0].get());
                if (!arg) return nullptr;
                auto *fn = getOrPanic("liva_str_starts_with");
                auto *result = builder_->CreateCall(fn, {obj, arg}, "str.startswith");
                return builder_->CreateTrunc(result, builder_->getInt1Ty(), "str.startswith.bool");
            }

            if (methodName == "endsWith" && node->getArgs().size() >= 1) {
                auto *arg = visit(node->getArgs()[0].get());
                if (!arg) return nullptr;
                auto *fn = getOrPanic("liva_str_ends_with");
                auto *result = builder_->CreateCall(fn, {obj, arg}, "str.endswith");
                return builder_->CreateTrunc(result, builder_->getInt1Ty(), "str.endswith.bool");
            }

            if (methodName == "indexOf" && node->getArgs().size() >= 1) {
                auto *arg = visit(node->getArgs()[0].get());
                if (!arg) return nullptr;
                auto *fn = getOrPanic("liva_str_index_of");
                return builder_->CreateCall(fn, {obj, arg}, "str.indexof");
            }

            if (methodName == "substring" && node->getArgs().size() >= 2) {
                auto *start = visit(node->getArgs()[0].get());
                auto *length = visit(node->getArgs()[1].get());
                if (!start || !length) return nullptr;
                // Auto-convert i32 to i64 if needed
                if (start->getType()->isIntegerTy(32))
                    start = builder_->CreateSExt(start, builder_->getInt64Ty());
                if (length->getType()->isIntegerTy(32))
                    length = builder_->CreateSExt(length, builder_->getInt64Ty());
                auto *fn = getOrPanic("liva_str_substring");
                auto *r = builder_->CreateCall(fn, {obj, start, length}, "str.substring");
                trackStringTemp(r);
                return r;
            }

            if (methodName == "trim") {
                auto *fn = getOrPanic("liva_str_trim");
                auto *r = builder_->CreateCall(fn, {obj}, "str.trim");
                trackStringTemp(r);
                return r;
            }

            if (methodName == "toUpper") {
                auto *fn = getOrPanic("liva_str_to_upper");
                auto *r = builder_->CreateCall(fn, {obj}, "str.toupper");
                trackStringTemp(r);
                return r;
            }

            if (methodName == "toLower") {
                auto *fn = getOrPanic("liva_str_to_lower");
                auto *r = builder_->CreateCall(fn, {obj}, "str.tolower");
                trackStringTemp(r);
                return r;
            }

            if (methodName == "replace" && node->getArgs().size() >= 2) {
                auto *oldSub = visit(node->getArgs()[0].get());
                auto *newSub = visit(node->getArgs()[1].get());
                if (!oldSub || !newSub) return nullptr;
                auto *fn = getOrPanic("liva_str_replace");
                auto *r = builder_->CreateCall(fn, {obj, oldSub, newSub}, "str.replace");
                trackStringTemp(r);
                return r;
            }

            if (methodName == "split" && node->getArgs().size() >= 1) {
                auto *delim = visit(node->getArgs()[0].get());
                if (!delim) return nullptr;
                auto *curFunc = builder_->GetInsertBlock()->getParent();
                // count output parameter
                auto *countAlloca = createEntryBlockAlloca(curFunc, "split.count", builder_->getInt64Ty());
                builder_->CreateStore(builder_->getInt64(0), countAlloca);
                auto *fn = getOrPanic("liva_str_split");
                auto *resultPtr = builder_->CreateCall(fn, {obj, delim, countAlloca}, "split.data");
                auto *count = builder_->CreateLoad(builder_->getInt64Ty(), countAlloca, "split.len");
                // Build DynArray struct { ptr data, i64 length, i64 capacity }
                auto *structTy = getDynArrayStructTy();
                auto *arrAlloca = createEntryBlockAlloca(curFunc, "split.arr", structTy);
                auto *dataField = builder_->CreateStructGEP(structTy, arrAlloca, 0);
                builder_->CreateStore(resultPtr, dataField);
                auto *lenField = builder_->CreateStructGEP(structTy, arrAlloca, 1);
                builder_->CreateStore(count, lenField);
                auto *capField = builder_->CreateStructGEP(structTy, arrAlloca, 2);
                builder_->CreateStore(count, capField);  // capacity = count
                return builder_->CreateLoad(structTy, arrAlloca, "split.result");
            }
        }

        // Check for enum case constructor: Shape.Circle(3.14)
        if (memberExpr->getObject()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
            auto *ident = static_cast<IdentifierExpr *>(memberExpr->getObject());
            const auto &enumName = ident->getName();
            auto etIt = enumTypes_.find(enumName);
            auto ecIt = enumCases_.find(enumName);
            if (etIt != enumTypes_.end() && ecIt != enumCases_.end()) {
                auto cIt = ecIt->second.find(methodName);
                if (cIt != ecIt->second.end()) {
                    return emitEnumCaseConstruct(enumName, methodName, cIt->second,
                                                  node->getArgs());
                }
            }
        }

        // Dynamic array method: arr.push(val), arr.pop()
        if (memberExpr->getObject()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
            auto *ident = static_cast<IdentifierExpr *>(memberExpr->getObject());
            auto daIt = vars_.varDynArrayTypes.find(ident->getName());
            if (daIt != vars_.varDynArrayTypes.end()) {
                auto allocaIt = vars_.namedValues.find(ident->getName());
                if (allocaIt == vars_.namedValues.end()) return nullptr;
                auto *arrAlloca = allocaIt->second;
                auto *structTy = getDynArrayStructTy();

                if (methodName == "push" && !node->getArgs().empty()) {
                    auto *val = visit(node->getArgs()[0].get());
                    if (!val) return nullptr;
                    // [string].push: take an owned copy. Otherwise the
                    // pushed pointer would alias either a tracked temp
                    // (freed at end of the producing statement) or a
                    // vars_.heapStringVars slot (freed on the next reassign of
                    // that variable), which leaves the array holding a
                    // dangling pointer.
                    bool elemIsString = false;
                    if (auto *arrTr = dynamic_cast<const ArrayTypeRepr *>(
                            node->getArgs()[0]->getResolvedType())) {
                        (void)arrTr; // silence unused warning
                    }
                    if (val->getType()->isPointerTy()) {
                        // Heuristic: if the array element type is a pointer
                        // (i.e. [string]) AND the value source is dynamic
                        // (variable load or temp call), take a heap copy.
                        if (daIt->second.elementType->isPointerTy()) {
                            val = builder_->CreateCall(getOrPanic("liva_str_dup"),
                                                        {val}, "push.dup");
                            elemIsString = true;
                        } else {
                            removeFromTempStrings(val);
                        }
                    }
                    (void)elemIsString;
                    auto *func = builder_->GetInsertBlock()->getParent();
                    auto *elemAlloca = createEntryBlockAlloca(func, "push.tmp",
                                                              daIt->second.elementType);
                    builder_->CreateStore(val, elemAlloca);

                    auto *dataField = builder_->CreateStructGEP(structTy, arrAlloca, 0);
                    auto *lenField = builder_->CreateStructGEP(structTy, arrAlloca, 1);
                    auto *capField = builder_->CreateStructGEP(structTy, arrAlloca, 2);

                    auto *pushFn = getOrPanic("liva_array_push");
                    builder_->CreateCall(pushFn, {dataField, lenField, capField,
                                                   elemAlloca,
                                                   builder_->getInt64(daIt->second.elemSize)});
                    return nullptr;
                }

                if (methodName == "pop") {
                    auto *lenField = builder_->CreateStructGEP(structTy, arrAlloca, 1);
                    auto *popFn = getOrPanic("liva_array_pop");
                    builder_->CreateCall(popFn, {lenField});
                    return nullptr;
                }

                if (methodName == "contains" && !node->getArgs().empty()) {
                    auto *val = visit(node->getArgs()[0].get());
                    if (!val) return nullptr;
                    auto *curFunc = builder_->GetInsertBlock()->getParent();
                    auto *elemAlloca = createEntryBlockAlloca(curFunc, "arr.contains.tmp",
                                                              daIt->second.elementType);
                    builder_->CreateStore(val, elemAlloca);
                    auto *dataField = builder_->CreateStructGEP(structTy, arrAlloca, 0);
                    auto *data = builder_->CreateLoad(
                        llvm::PointerType::getUnqual(*context_), dataField, "arr.data");
                    auto *lenField = builder_->CreateStructGEP(structTy, arrAlloca, 1);
                    auto *len = builder_->CreateLoad(builder_->getInt64Ty(), lenField, "arr.len");
                    int8_t keyKind = daIt->second.elementType->isPointerTy() ? 1 : 0;
                    auto *fn = getOrPanic("liva_array_contains");
                    auto *result = builder_->CreateCall(fn, {
                        data, len, elemAlloca,
                        builder_->getInt64(daIt->second.elemSize),
                        builder_->getInt8(keyKind)
                    }, "arr.contains");
                    return builder_->CreateTrunc(result, builder_->getInt1Ty(), "arr.contains.bool");
                }

                if (methodName == "indexOf" && !node->getArgs().empty()) {
                    auto *val = visit(node->getArgs()[0].get());
                    if (!val) return nullptr;
                    auto *curFunc = builder_->GetInsertBlock()->getParent();
                    auto *elemAlloca = createEntryBlockAlloca(curFunc, "arr.indexof.tmp",
                                                              daIt->second.elementType);
                    builder_->CreateStore(val, elemAlloca);
                    auto *dataField = builder_->CreateStructGEP(structTy, arrAlloca, 0);
                    auto *data = builder_->CreateLoad(
                        llvm::PointerType::getUnqual(*context_), dataField, "arr.data");
                    auto *lenField = builder_->CreateStructGEP(structTy, arrAlloca, 1);
                    auto *len = builder_->CreateLoad(builder_->getInt64Ty(), lenField, "arr.len");
                    int8_t keyKind = daIt->second.elementType->isPointerTy() ? 1 : 0;
                    auto *fn = getOrPanic("liva_array_index_of");
                    return builder_->CreateCall(fn, {
                        data, len, elemAlloca,
                        builder_->getInt64(daIt->second.elemSize),
                        builder_->getInt8(keyKind)
                    }, "arr.indexof");
                }

                if (methodName == "reverse") {
                    auto *dataField = builder_->CreateStructGEP(structTy, arrAlloca, 0);
                    auto *data = builder_->CreateLoad(
                        llvm::PointerType::getUnqual(*context_), dataField, "arr.data");
                    auto *lenField = builder_->CreateStructGEP(structTy, arrAlloca, 1);
                    auto *len = builder_->CreateLoad(builder_->getInt64Ty(), lenField, "arr.len");
                    auto *fn = getOrPanic("liva_array_reverse");
                    builder_->CreateCall(fn, {data, len, builder_->getInt64(daIt->second.elemSize)});
                    return nullptr;
                }

                // forEach/map/filter: higher-order array methods with closure arg
                if ((methodName == "forEach" || methodName == "map" || methodName == "filter") &&
                    !node->getArgs().empty()) {
                    // Save DynArray info BEFORE visiting closure (which invalidates map iterators)
                    auto *elemType = daIt->second.elementType;
                    uint64_t elemSize = daIt->second.elemSize;
                    auto *savedArrAlloca = arrAlloca;

                    auto *closureVal = visit(node->getArgs()[0].get());
                    if (!closureVal) {
                        diag_.report(node->getStartLoc(), DiagID::err_irgen_closure_body_failed);
                        return nullptr;
                    }

                    auto *curFunc = builder_->GetInsertBlock()->getParent();
                    auto *closureObjTy = getClosureObjTy();
                    auto *ptrTy = llvm::PointerType::getUnqual(*context_);
                    arrAlloca = savedArrAlloca;

                    // Store closure object to extract fields
                    auto *closureAlloca = createEntryBlockAlloca(curFunc, "hof.closure", closureObjTy);
                    builder_->CreateStore(closureVal, closureAlloca);
                    auto *funcGEP = builder_->CreateStructGEP(closureObjTy, closureAlloca, 0);
                    auto *funcPtr = builder_->CreateLoad(ptrTy, funcGEP, "hof.func");
                    auto *envGEP = builder_->CreateStructGEP(closureObjTy, closureAlloca, 1);
                    auto *envPtr = builder_->CreateLoad(ptrTy, envGEP, "hof.env");

                    // Load source array data and length
                    auto *dataField = builder_->CreateStructGEP(structTy, arrAlloca, 0);
                    auto *data = builder_->CreateLoad(ptrTy, dataField, "hof.data");
                    auto *lenField = builder_->CreateStructGEP(structTy, arrAlloca, 1);
                    auto *len = builder_->CreateLoad(builder_->getInt64Ty(), lenField, "hof.len");

                    // Determine closure return type
                    auto *closureExpr = static_cast<ClosureExpr *>(node->getArgs()[0].get());
                    llvm::Type *closureRetTy = builder_->getVoidTy();
                    if (methodName == "filter") {
                        closureRetTy = builder_->getInt1Ty();
                    } else if (methodName == "map" && closureExpr->getReturnType()) {
                        closureRetTy = toLLVMType(closureExpr->getReturnType());
                    } else if (methodName == "forEach") {
                        closureRetTy = builder_->getVoidTy();
                    }

                    // Build closure function type: (ptr env, elemType) -> retTy
                    auto *closureFuncTy = llvm::FunctionType::get(
                        closureRetTy, {ptrTy, elemType}, false);

                    // Allocate result array for map/filter
                    llvm::AllocaInst *resultAlloca = nullptr;
                    llvm::Value *resultData = nullptr;
                    llvm::AllocaInst *resultLenAlloca = nullptr;
                    llvm::Type *resultElemType = nullptr;
                    uint64_t resultElemSize = 0;

                    if (methodName == "map" || methodName == "filter") {
                        resultElemType = (methodName == "map") ? closureRetTy : elemType;
                        const llvm::DataLayout &dl = module_->getDataLayout();
                        resultElemSize = dl.getTypeAllocSize(resultElemType);
                        resultAlloca = createEntryBlockAlloca(curFunc, "hof.result", structTy);
                        auto *newFn = getOrPanic("liva_array_new");
                        resultData = builder_->CreateCall(newFn,
                            {builder_->getInt64(resultElemSize), len}, "hof.newdata");
                        auto *rDataField = builder_->CreateStructGEP(structTy, resultAlloca, 0);
                        builder_->CreateStore(resultData, rDataField);
                        resultLenAlloca = createEntryBlockAlloca(curFunc, "hof.rlen", builder_->getInt64Ty());
                        builder_->CreateStore(builder_->getInt64(0), resultLenAlloca);
                        auto *rCapField = builder_->CreateStructGEP(structTy, resultAlloca, 2);
                        builder_->CreateStore(len, rCapField);
                    }

                    // Loop: i = 0; i < len; i++
                    auto *idxAlloca = createEntryBlockAlloca(curFunc, "hof.i", builder_->getInt64Ty());
                    builder_->CreateStore(builder_->getInt64(0), idxAlloca);

                    auto *condBB = llvm::BasicBlock::Create(*context_, "hof.cond", curFunc);
                    auto *bodyBB = llvm::BasicBlock::Create(*context_, "hof.body", curFunc);
                    auto *latchBB = llvm::BasicBlock::Create(*context_, "hof.latch", curFunc);
                    auto *exitBB = llvm::BasicBlock::Create(*context_, "hof.exit", curFunc);

                    loopStack_.push_back({exitBB, latchBB});
                    builder_->CreateBr(condBB);

                    // Cond
                    builder_->SetInsertPoint(condBB);
                    auto *idx = builder_->CreateLoad(builder_->getInt64Ty(), idxAlloca, "hof.idx");
                    auto *cond = builder_->CreateICmpSLT(idx, len, "hof.cmp");
                    builder_->CreateCondBr(cond, bodyBB, exitBB);

                    // Body
                    builder_->SetInsertPoint(bodyBB);
                    auto *idxBody = builder_->CreateLoad(builder_->getInt64Ty(), idxAlloca);
                    auto *elemPtr = builder_->CreateGEP(elemType, data, idxBody, "hof.elem.ptr");
                    auto *elem = builder_->CreateLoad(elemType, elemPtr, "hof.elem");

                    // Call closure: funcPtr(envPtr, elem)
                    if (methodName == "forEach") {
                        builder_->CreateCall(closureFuncTy, funcPtr, {envPtr, elem});
                        builder_->CreateBr(latchBB);
                    } else if (methodName == "map") {
                        auto *result = builder_->CreateCall(closureFuncTy, funcPtr,
                            {envPtr, elem}, "hof.map.val");
                        // Store result at resultData[i]
                        auto *rIdx = builder_->CreateLoad(builder_->getInt64Ty(), resultLenAlloca);
                        auto *rPtr = builder_->CreateGEP(resultElemType, resultData, rIdx, "hof.map.ptr");
                        builder_->CreateStore(result, rPtr);
                        // resultLen++
                        auto *rNext = builder_->CreateAdd(rIdx, builder_->getInt64(1));
                        builder_->CreateStore(rNext, resultLenAlloca);
                        builder_->CreateBr(latchBB);
                    } else if (methodName == "filter") {
                        auto *keep = builder_->CreateCall(closureFuncTy, funcPtr,
                            {envPtr, elem}, "hof.filter.keep");
                        auto *keepBB = llvm::BasicBlock::Create(*context_, "hof.filter.yes", curFunc);
                        auto *skipBB = llvm::BasicBlock::Create(*context_, "hof.filter.no", curFunc);
                        builder_->CreateCondBr(keep, keepBB, skipBB);

                        builder_->SetInsertPoint(keepBB);
                        auto *rIdx = builder_->CreateLoad(builder_->getInt64Ty(), resultLenAlloca);
                        auto *rPtr = builder_->CreateGEP(resultElemType, resultData, rIdx, "hof.filt.ptr");
                        builder_->CreateStore(elem, rPtr);
                        auto *rNext = builder_->CreateAdd(rIdx, builder_->getInt64(1));
                        builder_->CreateStore(rNext, resultLenAlloca);
                        builder_->CreateBr(skipBB);

                        builder_->SetInsertPoint(skipBB);
                        builder_->CreateBr(latchBB);
                    }

                    // Latch: i++
                    builder_->SetInsertPoint(latchBB);
                    auto *idxLatch = builder_->CreateLoad(builder_->getInt64Ty(), idxAlloca);
                    auto *next = builder_->CreateAdd(idxLatch, builder_->getInt64(1), "hof.next");
                    builder_->CreateStore(next, idxAlloca);
                    builder_->CreateBr(condBB);

                    // Exit
                    builder_->SetInsertPoint(exitBB);
                    loopStack_.pop_back();

                    if (methodName == "forEach") {
                        return nullptr;
                    }

                    // Return result DynArray for map/filter
                    auto *rLen = builder_->CreateLoad(builder_->getInt64Ty(), resultLenAlloca, "hof.rlen.final");
                    auto *rLenField = builder_->CreateStructGEP(structTy, resultAlloca, 1);
                    builder_->CreateStore(rLen, rLenField);
                    return builder_->CreateLoad(structTy, resultAlloca, "hof.result.val");
                }

                // reduce(init, |acc, x| -> T { ... })
                if (methodName == "reduce" && node->getArgs().size() >= 2) {
                    auto *elemType = daIt->second.elementType;
                    auto *savedArrAlloca = arrAlloca;

                    // Visit init value first
                    auto *initVal = visit(node->getArgs()[0].get());
                    if (!initVal) return nullptr;

                    // Visit closure
                    auto *closureVal = visit(node->getArgs()[1].get());
                    if (!closureVal) return nullptr;

                    auto *curFunc = builder_->GetInsertBlock()->getParent();
                    auto *closureObjTy = getClosureObjTy();
                    auto *ptrTy = llvm::PointerType::getUnqual(*context_);
                    arrAlloca = savedArrAlloca;

                    // Extract closure func_ptr and env_ptr
                    auto *closureAlloca = createEntryBlockAlloca(curFunc, "red.closure", closureObjTy);
                    builder_->CreateStore(closureVal, closureAlloca);
                    auto *funcGEP = builder_->CreateStructGEP(closureObjTy, closureAlloca, 0);
                    auto *funcPtr = builder_->CreateLoad(ptrTy, funcGEP, "red.func");
                    auto *envGEP = builder_->CreateStructGEP(closureObjTy, closureAlloca, 1);
                    auto *envPtr = builder_->CreateLoad(ptrTy, envGEP, "red.env");

                    // Load array data and length
                    auto *dataField = builder_->CreateStructGEP(structTy, arrAlloca, 0);
                    auto *data = builder_->CreateLoad(ptrTy, dataField, "red.data");
                    auto *lenField = builder_->CreateStructGEP(structTy, arrAlloca, 1);
                    auto *len = builder_->CreateLoad(builder_->getInt64Ty(), lenField, "red.len");

                    // Accumulator alloca
                    auto *accType = initVal->getType();
                    auto *accAlloca = createEntryBlockAlloca(curFunc, "red.acc", accType);
                    builder_->CreateStore(initVal, accAlloca);

                    // Closure type: (ptr env, accType, elemType) -> accType
                    auto *closureFuncTy = llvm::FunctionType::get(
                        accType, {ptrTy, accType, elemType}, false);

                    // Loop: i = 0; i < len; i++
                    auto *idxAlloca = createEntryBlockAlloca(curFunc, "red.i", builder_->getInt64Ty());
                    builder_->CreateStore(builder_->getInt64(0), idxAlloca);

                    auto *condBB = llvm::BasicBlock::Create(*context_, "red.cond", curFunc);
                    auto *bodyBB = llvm::BasicBlock::Create(*context_, "red.body", curFunc);
                    auto *latchBB = llvm::BasicBlock::Create(*context_, "red.latch", curFunc);
                    auto *exitBB = llvm::BasicBlock::Create(*context_, "red.exit", curFunc);

                    loopStack_.push_back({exitBB, latchBB});
                    builder_->CreateBr(condBB);

                    builder_->SetInsertPoint(condBB);
                    auto *idx = builder_->CreateLoad(builder_->getInt64Ty(), idxAlloca, "red.idx");
                    auto *cond = builder_->CreateICmpSLT(idx, len, "red.cmp");
                    builder_->CreateCondBr(cond, bodyBB, exitBB);

                    builder_->SetInsertPoint(bodyBB);
                    auto *idxBody = builder_->CreateLoad(builder_->getInt64Ty(), idxAlloca);
                    auto *elemPtr = builder_->CreateGEP(elemType, data, idxBody, "red.elem.ptr");
                    auto *elem = builder_->CreateLoad(elemType, elemPtr, "red.elem");
                    auto *acc = builder_->CreateLoad(accType, accAlloca, "red.acc.val");

                    // Call closure: funcPtr(envPtr, acc, elem)
                    auto *result = builder_->CreateCall(closureFuncTy, funcPtr,
                        {envPtr, acc, elem}, "red.result");
                    builder_->CreateStore(result, accAlloca);
                    builder_->CreateBr(latchBB);

                    builder_->SetInsertPoint(latchBB);
                    auto *idxLatch = builder_->CreateLoad(builder_->getInt64Ty(), idxAlloca);
                    auto *next = builder_->CreateAdd(idxLatch, builder_->getInt64(1), "red.next");
                    builder_->CreateStore(next, idxAlloca);
                    builder_->CreateBr(condBB);

                    builder_->SetInsertPoint(exitBB);
                    loopStack_.pop_back();

                    return builder_->CreateLoad(accType, accAlloca, "red.final");
                }
            }
        }

        // DynArray method on struct member field: self.grades.push(val) etc.
        if (memberExpr->getObject()->getKind() == ASTNode::NodeKind::MemberExpr) {
            auto *innerMember = static_cast<MemberExpr *>(memberExpr->getObject());
            auto daInfo = resolveMemberDynArray(innerMember);
            if (daInfo) {
                auto *arrGEP = daInfo->arrGEP;
                auto *structTy = getDynArrayStructTy();

                if (methodName == "push" && !node->getArgs().empty()) {
                    auto *val = visit(node->getArgs()[0].get());
                    if (!val) return nullptr;
                    auto *func = builder_->GetInsertBlock()->getParent();
                    auto *elemAlloca = createEntryBlockAlloca(func, "mpush.tmp",
                                                              daInfo->elementType);
                    builder_->CreateStore(val, elemAlloca);
                    auto *dataField = builder_->CreateStructGEP(structTy, arrGEP, 0);
                    auto *lenField = builder_->CreateStructGEP(structTy, arrGEP, 1);
                    auto *capField = builder_->CreateStructGEP(structTy, arrGEP, 2);
                    auto *pushFn = getOrPanic("liva_array_push");
                    builder_->CreateCall(pushFn, {dataField, lenField, capField,
                                                   elemAlloca,
                                                   builder_->getInt64(daInfo->elemSize)});
                    return nullptr;
                }

                if (methodName == "pop") {
                    auto *lenField = builder_->CreateStructGEP(structTy, arrGEP, 1);
                    auto *popFn = getOrPanic("liva_array_pop");
                    builder_->CreateCall(popFn, {lenField});
                    return nullptr;
                }

                if (methodName == "contains" && !node->getArgs().empty()) {
                    auto *val = visit(node->getArgs()[0].get());
                    if (!val) return nullptr;
                    auto *curFunc = builder_->GetInsertBlock()->getParent();
                    auto *elemAlloca = createEntryBlockAlloca(curFunc, "mda.contains.tmp",
                                                              daInfo->elementType);
                    builder_->CreateStore(val, elemAlloca);
                    auto *dataField = builder_->CreateStructGEP(structTy, arrGEP, 0);
                    auto *data = builder_->CreateLoad(
                        llvm::PointerType::getUnqual(*context_), dataField, "mda.data");
                    auto *lenField = builder_->CreateStructGEP(structTy, arrGEP, 1);
                    auto *len = builder_->CreateLoad(builder_->getInt64Ty(), lenField, "mda.len");
                    int8_t keyKind = daInfo->elementType->isPointerTy() ? 1 : 0;
                    auto *fn = getOrPanic("liva_array_contains");
                    auto *result = builder_->CreateCall(fn, {
                        data, len, elemAlloca,
                        builder_->getInt64(daInfo->elemSize),
                        builder_->getInt8(keyKind)
                    }, "mda.contains");
                    return builder_->CreateTrunc(result, builder_->getInt1Ty(), "mda.contains.bool");
                }

                if (methodName == "reverse") {
                    auto *dataField = builder_->CreateStructGEP(structTy, arrGEP, 0);
                    auto *data = builder_->CreateLoad(
                        llvm::PointerType::getUnqual(*context_), dataField, "mda.data");
                    auto *lenField = builder_->CreateStructGEP(structTy, arrGEP, 1);
                    auto *len = builder_->CreateLoad(builder_->getInt64Ty(), lenField, "mda.len");
                    auto *fn = getOrPanic("liva_array_reverse");
                    builder_->CreateCall(fn, {data, len, builder_->getInt64(daInfo->elemSize)});
                    return nullptr;
                }
            }
        }

        // Map method: m.insert(k,v), m.get(k), m.contains(k), m.remove(k)
        if (memberExpr->getObject()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
            auto *ident = static_cast<IdentifierExpr *>(memberExpr->getObject());
            auto mapIt = vars_.varMapTypes.find(ident->getName());
            if (mapIt != vars_.varMapTypes.end()) {
                auto allocaIt = vars_.namedValues.find(ident->getName());
                if (allocaIt == vars_.namedValues.end()) return nullptr;
                auto *mapAlloca = allocaIt->second;
                auto *structTy = getMapStructTy();
                auto &info = mapIt->second;
                auto *curFunc = builder_->GetInsertBlock()->getParent();

                if (methodName == "insert" && node->getArgs().size() >= 2) {
                    auto *keyVal = visit(node->getArgs()[0].get());
                    auto *valVal = visit(node->getArgs()[1].get());
                    if (!keyVal || !valVal) return nullptr;

                    auto *keyAlloca = createEntryBlockAlloca(curFunc, "map.key.tmp", info.keyType);
                    builder_->CreateStore(keyVal, keyAlloca);
                    auto *valAlloca = createEntryBlockAlloca(curFunc, "map.val.tmp", info.valType);
                    builder_->CreateStore(valVal, valAlloca);

                    auto *entriesField = builder_->CreateStructGEP(structTy, mapAlloca, 0);
                    auto *sizeField = builder_->CreateStructGEP(structTy, mapAlloca, 1);
                    auto *capField = builder_->CreateStructGEP(structTy, mapAlloca, 2);

                    auto *insertFn = getOrPanic("liva_map_insert");
                    builder_->CreateCall(insertFn, {
                        entriesField, sizeField, capField,
                        keyAlloca, valAlloca,
                        builder_->getInt64(info.keySize),
                        builder_->getInt64(info.valSize),
                        builder_->getInt8(info.keyKind)
                    });
                    return nullptr;
                }

                if (methodName == "get" && !node->getArgs().empty()) {
                    auto *keyVal = visit(node->getArgs()[0].get());
                    if (!keyVal) return nullptr;

                    auto *keyAlloca = createEntryBlockAlloca(curFunc, "map.get.key", info.keyType);
                    builder_->CreateStore(keyVal, keyAlloca);

                    auto *entriesField = builder_->CreateStructGEP(structTy, mapAlloca, 0);
                    auto *entries = builder_->CreateLoad(
                        llvm::PointerType::getUnqual(*context_), entriesField, "map.entries");
                    auto *capField = builder_->CreateStructGEP(structTy, mapAlloca, 2);
                    auto *cap = builder_->CreateLoad(builder_->getInt64Ty(), capField, "map.cap");

                    auto *getFn = getOrPanic("liva_map_get");
                    auto *resultPtr = builder_->CreateCall(getFn, {
                        entries, cap, keyAlloca,
                        builder_->getInt64(info.keySize),
                        builder_->getInt64(info.valSize),
                        builder_->getInt8(info.keyKind)
                    }, "map.get.ptr");

                    // Build Optional<V>: null check → {i1, V}
                    auto *isNull = builder_->CreateICmpEQ(resultPtr,
                        llvm::ConstantPointerNull::get(
                            llvm::cast<llvm::PointerType>(resultPtr->getType())),
                        "map.get.isnull");
                    auto *optTy = getOptionalType(info.valType);
                    auto *optAlloca = createEntryBlockAlloca(curFunc, "map.get.opt", optTy);

                    // nil path
                    auto *nilBB = llvm::BasicBlock::Create(*context_, "map.get.nil", curFunc);
                    auto *someBB = llvm::BasicBlock::Create(*context_, "map.get.some", curFunc);
                    auto *mergeBB = llvm::BasicBlock::Create(*context_, "map.get.merge", curFunc);
                    builder_->CreateCondBr(isNull, nilBB, someBB);

                    builder_->SetInsertPoint(nilBB);
                    auto *hasValNil = builder_->CreateStructGEP(optTy, optAlloca, 0);
                    builder_->CreateStore(builder_->getInt1(false), hasValNil);
                    builder_->CreateBr(mergeBB);

                    builder_->SetInsertPoint(someBB);
                    auto *hasValSome = builder_->CreateStructGEP(optTy, optAlloca, 0);
                    builder_->CreateStore(builder_->getInt1(true), hasValSome);
                    auto *valPtr = builder_->CreateStructGEP(optTy, optAlloca, 1);
                    auto *loadedVal = builder_->CreateLoad(info.valType, resultPtr, "map.get.val");
                    builder_->CreateStore(loadedVal, valPtr);
                    builder_->CreateBr(mergeBB);

                    builder_->SetInsertPoint(mergeBB);
                    return builder_->CreateLoad(optTy, optAlloca, "map.get.result");
                }

                if (methodName == "contains" && !node->getArgs().empty()) {
                    auto *keyVal = visit(node->getArgs()[0].get());
                    if (!keyVal) return nullptr;

                    auto *keyAlloca = createEntryBlockAlloca(curFunc, "map.contains.key", info.keyType);
                    builder_->CreateStore(keyVal, keyAlloca);

                    auto *entriesField = builder_->CreateStructGEP(structTy, mapAlloca, 0);
                    auto *entries = builder_->CreateLoad(
                        llvm::PointerType::getUnqual(*context_), entriesField);
                    auto *capField = builder_->CreateStructGEP(structTy, mapAlloca, 2);
                    auto *cap = builder_->CreateLoad(builder_->getInt64Ty(), capField);

                    auto *containsFn = getOrPanic("liva_map_contains");
                    auto *result = builder_->CreateCall(containsFn, {
                        entries, cap, keyAlloca,
                        builder_->getInt64(info.keySize),
                        builder_->getInt64(info.valSize),
                        builder_->getInt8(info.keyKind)
                    }, "map.contains");
                    return builder_->CreateTrunc(result, builder_->getInt1Ty(), "map.contains.bool");
                }

                if (methodName == "remove" && !node->getArgs().empty()) {
                    auto *keyVal = visit(node->getArgs()[0].get());
                    if (!keyVal) return nullptr;

                    auto *keyAlloca = createEntryBlockAlloca(curFunc, "map.remove.key", info.keyType);
                    builder_->CreateStore(keyVal, keyAlloca);

                    auto *entriesField = builder_->CreateStructGEP(structTy, mapAlloca, 0);
                    auto *entries = builder_->CreateLoad(
                        llvm::PointerType::getUnqual(*context_), entriesField);
                    auto *sizeField = builder_->CreateStructGEP(structTy, mapAlloca, 1);
                    auto *capField = builder_->CreateStructGEP(structTy, mapAlloca, 2);
                    auto *cap = builder_->CreateLoad(builder_->getInt64Ty(), capField);

                    auto *removeFn = getOrPanic("liva_map_remove");
                    auto *result = builder_->CreateCall(removeFn, {
                        entries, sizeField, cap, keyAlloca,
                        builder_->getInt64(info.keySize),
                        builder_->getInt64(info.valSize),
                        builder_->getInt8(info.keyKind)
                    }, "map.remove");
                    return builder_->CreateTrunc(result, builder_->getInt1Ty(), "map.remove.bool");
                }
            }
        }

        // Set method: s.insert(e), s.contains(e), s.remove(e)
        if (memberExpr->getObject()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
            auto *ident = static_cast<IdentifierExpr *>(memberExpr->getObject());
            auto setIt = vars_.varSetTypes.find(ident->getName());
            if (setIt != vars_.varSetTypes.end()) {
                auto allocaIt = vars_.namedValues.find(ident->getName());
                if (allocaIt == vars_.namedValues.end()) return nullptr;
                auto *setAlloca = allocaIt->second;
                auto *structTy = getMapStructTy();
                auto &info = setIt->second;
                auto *curFunc = builder_->GetInsertBlock()->getParent();

                if (methodName == "insert" && !node->getArgs().empty()) {
                    auto *elemVal = visit(node->getArgs()[0].get());
                    if (!elemVal) return nullptr;

                    auto *elemAlloca = createEntryBlockAlloca(curFunc, "set.elem.tmp", info.elemType);
                    builder_->CreateStore(elemVal, elemAlloca);

                    auto *entriesField = builder_->CreateStructGEP(structTy, setAlloca, 0);
                    auto *sizeField = builder_->CreateStructGEP(structTy, setAlloca, 1);
                    auto *capField = builder_->CreateStructGEP(structTy, setAlloca, 2);

                    auto *insertFn = getOrPanic("liva_set_insert");
                    builder_->CreateCall(insertFn, {
                        entriesField, sizeField, capField,
                        elemAlloca,
                        builder_->getInt64(info.elemSize),
                        builder_->getInt8(info.keyKind)
                    });
                    return nullptr;
                }

                if (methodName == "contains" && !node->getArgs().empty()) {
                    auto *elemVal = visit(node->getArgs()[0].get());
                    if (!elemVal) return nullptr;

                    auto *elemAlloca = createEntryBlockAlloca(curFunc, "set.contains.elem", info.elemType);
                    builder_->CreateStore(elemVal, elemAlloca);

                    auto *entriesField = builder_->CreateStructGEP(structTy, setAlloca, 0);
                    auto *entries = builder_->CreateLoad(
                        llvm::PointerType::getUnqual(*context_), entriesField);
                    auto *capField = builder_->CreateStructGEP(structTy, setAlloca, 2);
                    auto *cap = builder_->CreateLoad(builder_->getInt64Ty(), capField);

                    auto *containsFn = getOrPanic("liva_set_contains");
                    auto *result = builder_->CreateCall(containsFn, {
                        entries, cap, elemAlloca,
                        builder_->getInt64(info.elemSize),
                        builder_->getInt8(info.keyKind)
                    }, "set.contains");
                    return builder_->CreateTrunc(result, builder_->getInt1Ty(), "set.contains.bool");
                }

                if (methodName == "remove" && !node->getArgs().empty()) {
                    auto *elemVal = visit(node->getArgs()[0].get());
                    if (!elemVal) return nullptr;

                    auto *elemAlloca = createEntryBlockAlloca(curFunc, "set.remove.elem", info.elemType);
                    builder_->CreateStore(elemVal, elemAlloca);

                    auto *entriesField = builder_->CreateStructGEP(structTy, setAlloca, 0);
                    auto *entries = builder_->CreateLoad(
                        llvm::PointerType::getUnqual(*context_), entriesField);
                    auto *sizeField = builder_->CreateStructGEP(structTy, setAlloca, 1);
                    auto *capField = builder_->CreateStructGEP(structTy, setAlloca, 2);
                    auto *cap = builder_->CreateLoad(builder_->getInt64Ty(), capField);

                    auto *removeFn = getOrPanic("liva_set_remove");
                    auto *result = builder_->CreateCall(removeFn, {
                        entries, sizeField, cap, elemAlloca,
                        builder_->getInt64(info.elemSize),
                        builder_->getInt8(info.keyKind)
                    }, "set.remove");
                    return builder_->CreateTrunc(result, builder_->getInt1Ty(), "set.remove.bool");
                }
            }
        }

        // Dynamic dispatch for protocol trait objects: obj.method(args)
        if (memberExpr->getObject()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
            auto *ident = static_cast<IdentifierExpr *>(memberExpr->getObject());
            auto ptIt = vars_.varProtocolTypes.find(ident->getName());
            if (ptIt != vars_.varProtocolTypes.end()) {
                const std::string &protocolName = ptIt->second;

                // Devirtualization: direct call if concrete type is statically known
                auto devirtIt = vars_.varConcreteProtocolTypes.find(ident->getName());
                if (devirtIt != vars_.varConcreteProtocolTypes.end()) {
                    const std::string &concreteType = devirtIt->second;
                    std::string directFnName = concreteType + "_" + methodName;
                    auto *directFn = module_->getFunction(directFnName);
                    if (directFn) {
                        auto *traitTy = getTraitObjectTy();
                        auto nvIt = vars_.namedValues.find(ident->getName());
                        if (nvIt != vars_.namedValues.end()) {
                            auto *ptrTy = llvm::PointerType::getUnqual(*context_);
                            auto *dataGEP = builder_->CreateStructGEP(traitTy, nvIt->second, 0);
                            auto *dataPtr = builder_->CreateLoad(ptrTy, dataGEP, "devirt.data");
                            std::vector<llvm::Value *> args;
                            args.push_back(dataPtr);
                            for (auto &arg : node->getArgs()) {
                                auto *val = visit(arg.get());
                                if (!val) return nullptr;
                                args.push_back(val);
                            }
                            if (directFn->getReturnType()->isVoidTy())
                                return builder_->CreateCall(directFn, args);
                            return builder_->CreateCall(directFn, args, "devirt.call");
                        }
                    }
                }
                // Fall through to vtable dispatch

                auto miIt = protocolMethodIndices_.find(protocolName);
                if (miIt != protocolMethodIndices_.end()) {
                    auto idxIt = miIt->second.find(methodName);
                    if (idxIt != miIt->second.end()) {
                        int methodIdx = idxIt->second;
                        auto *traitTy = getTraitObjectTy();
                        auto nvIt = vars_.namedValues.find(ident->getName());
                        if (nvIt != vars_.namedValues.end()) {
                            auto *traitAlloca = nvIt->second;

                            // Load data pointer
                            auto *ptrTy = llvm::PointerType::getUnqual(*context_);
                            auto *dataGEP = builder_->CreateStructGEP(traitTy, traitAlloca, 0);
                            auto *dataPtr = builder_->CreateLoad(ptrTy, dataGEP, "dyn.data");

                            // Load vtable pointer
                            auto *vtableGEP = builder_->CreateStructGEP(traitTy, traitAlloca, 1);
                            auto *vtablePtr = builder_->CreateLoad(ptrTy, vtableGEP, "dyn.vtable");

                            // Get function pointer from vtable
                            auto *arrayTy = llvm::ArrayType::get(ptrTy, miIt->second.size());
                            auto *fnPtrGEP = builder_->CreateInBoundsGEP(
                                arrayTy, vtablePtr,
                                {builder_->getInt64(0), builder_->getInt64(methodIdx)},
                                "dyn.fnptr.gep");
                            auto *fnPtr = builder_->CreateLoad(ptrTy, fnPtrGEP, "dyn.fnptr");

                            // Build function type from protocol method signature
                            auto pcIt = protocolConformances_.find(protocolName);
                            llvm::FunctionType *fnTy = nullptr;
                            if (pcIt != protocolConformances_.end() && !pcIt->second.empty()) {
                                std::string mangledName = pcIt->second[0] + "_" + methodName;
                                auto *refFn = module_->getFunction(mangledName);
                                if (refFn) fnTy = refFn->getFunctionType();
                            }

                            if (!fnTy) {
                                std::vector<llvm::Type *> paramTys = {ptrTy};
                                for (auto &arg : node->getArgs()) {
                                    auto *val = visit(arg.get());
                                    if (val) paramTys.push_back(val->getType());
                                }
                                fnTy = llvm::FunctionType::get(builder_->getInt32Ty(), paramTys, false);
                            }

                            // Build args: data_ptr as self + user args
                            std::vector<llvm::Value *> args;
                            args.push_back(dataPtr);
                            for (auto &arg : node->getArgs()) {
                                auto *val = visit(arg.get());
                                if (!val) return nullptr;
                                args.push_back(val);
                            }

                            if (fnTy->getReturnType()->isVoidTy())
                                return builder_->CreateCall(fnTy, fnPtr, args);
                            return builder_->CreateCall(fnTy, fnPtr, args, "dyncalltmp");
                        }
                    }
                }
            }
        }

        // Find the object's struct type
        std::string objName;
        std::string structTypeName;
        llvm::AllocaInst *objAlloca = nullptr;

        if (memberExpr->getObject()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
            auto *ident = static_cast<IdentifierExpr *>(memberExpr->getObject());
            objName = ident->getName();
            auto it = vars_.namedValues.find(objName);
            if (it != vars_.namedValues.end())
                objAlloca = it->second;
            auto stIt = vars_.varStructTypes.find(objName);
            if (stIt != vars_.varStructTypes.end())
                structTypeName = stIt->second;
            // Also check enum types for enum method calls
            if (structTypeName.empty()) {
                auto enIt = vars_.varEnumTypes.find(objName);
                if (enIt != vars_.varEnumTypes.end())
                    structTypeName = enIt->second;
            }

            // Static class method call: ClassName.method(args) — no self, direct call
            if (classNames_.count(objName) && !objAlloca) {
                std::string mangledName = objName + "_" + methodName;
                auto *callee = module_->getFunction(mangledName);
                if (callee && callee->arg_size() == node->getArgs().size()) {
                    std::vector<llvm::Value *> args;
                    for (auto &arg : node->getArgs()) {
                        auto *val = visit(arg.get());
                        if (!val) return nullptr;
                        args.push_back(val);
                    }
                    if (callee->getReturnType()->isVoidTy()) {
                        builder_->CreateCall(callee, args);
                        return nullptr;
                    }
                    return builder_->CreateCall(callee, args, "static.call");
                }
            }

            // Class instance method call (virtual dispatch)
            auto clsIt = vars_.varClassTypes.find(objName);
            if (clsIt != vars_.varClassTypes.end()) {
                const std::string &clsTypeName = clsIt->second;
                auto miIt = classMethodIndices_.find(clsTypeName);
                if (miIt != classMethodIndices_.end()) {
                    auto methodIt = miIt->second.find(methodName);
                    if (methodIt != miIt->second.end()) {
                        int methodIdx = methodIt->second;
                        auto *ptrTy = llvm::PointerType::getUnqual(*context_);
                        auto ctIt = classTypes_.find(clsTypeName);
                        auto *classTy = ctIt->second;

                        // Load self
                        llvm::Value *selfVal = objAlloca;
                        if (objAlloca->getAllocatedType()->isPointerTy()) {
                            selfVal = builder_->CreateLoad(ptrTy, objAlloca, "self.ptr");
                        }

                        // Load vtable ptr: gep(self, 0, 0)
                        auto *vtableGEP = builder_->CreateStructGEP(
                            classTy, selfVal, 0, "vtable.ptr.gep");
                        auto *vtablePtrTy = llvm::ArrayType::get(ptrTy,
                            classVtableMethods_[clsTypeName].size());
                        auto *vtablePtr = builder_->CreateLoad(ptrTy, vtableGEP, "vtable.ptr");

                        // Load method ptr from vtable: gep(vtable, methodIdx)
                        auto *methodGEP = builder_->CreateGEP(
                            vtablePtrTy, vtablePtr,
                            {builder_->getInt32(0), builder_->getInt32(methodIdx)},
                            "method.ptr.gep");
                        auto *methodPtr = builder_->CreateLoad(ptrTy, methodGEP, "method.ptr");

                        // Build args: self + user args
                        std::vector<llvm::Value *> args;
                        args.push_back(selfVal);
                        for (auto &arg : node->getArgs()) {
                            auto *val = visit(arg.get());
                            if (!val) return nullptr;
                            args.push_back(val);
                        }

                        // Build function type for indirect call
                        std::vector<llvm::Type *> paramTypes;
                        paramTypes.push_back(ptrTy); // self
                        for (auto *a : args) {
                            if (a != selfVal) paramTypes.push_back(a->getType());
                        }

                        // Find the mangled function to infer return type
                        std::string mangledName = clsTypeName + "_" + methodName;
                        auto *directFn = module_->getFunction(mangledName);
                        llvm::Type *retTy = llvm::Type::getVoidTy(*context_);
                        if (directFn) {
                            retTy = directFn->getReturnType();
                        }

                        auto *funcTy = llvm::FunctionType::get(retTy, paramTypes, false);
                        if (retTy->isVoidTy()) {
                            builder_->CreateCall(funcTy, methodPtr, args);
                            return nullptr;
                        }
                        return builder_->CreateCall(funcTy, methodPtr, args, "vcall");
                    }
                }
                // Non-virtual direct method call
                std::string mangledName = clsTypeName + "_" + methodName;
                auto *callee = module_->getFunction(mangledName);
                if (callee) {
                    llvm::Value *selfVal = objAlloca;
                    if (objAlloca->getAllocatedType()->isPointerTy()) {
                        selfVal = builder_->CreateLoad(
                            llvm::PointerType::getUnqual(*context_), objAlloca, "self.ptr");
                    }
                    std::vector<llvm::Value *> args;
                    args.push_back(selfVal);
                    for (auto &arg : node->getArgs()) {
                        auto *val = visit(arg.get());
                        if (!val) return nullptr;
                        args.push_back(val);
                    }
                    if (callee->getReturnType()->isVoidTy()) {
                        builder_->CreateCall(callee, args);
                        return nullptr;
                    }
                    return builder_->CreateCall(callee, args, "mcall");
                }
            }

            // self.init(args) → convenience init delegation: ClassName_init_fields(self, args)
            if (objName == "self" && methodName == "init" && !currentClassContext_.empty()) {
                std::string initFields = currentClassContext_ + "_init_fields";
                auto *fn = module_->getFunction(initFields);
                if (fn) {
                    auto selfIt = vars_.namedValues.find("self");
                    if (selfIt != vars_.namedValues.end()) {
                        auto *ptrTy = llvm::PointerType::getUnqual(*context_);
                        auto *selfVal = builder_->CreateLoad(ptrTy, selfIt->second, "self.ptr");
                        std::vector<llvm::Value *> args;
                        args.push_back(selfVal);
                        for (auto &arg : node->getArgs()) {
                            auto *val = visit(arg.get());
                            if (!val) return nullptr;
                            args.push_back(val);
                        }
                        builder_->CreateCall(fn, args);
                        return nullptr;
                    }
                }
            }

            // super.method(args) → direct call to parent method
            if (objName == "super" && !currentClassContext_.empty()) {
                auto pit = classParent_.find(currentClassContext_);
                if (pit != classParent_.end()) {
                    // super.init(args) → call ParentName_init_fields(self, args)
                    if (methodName == "init") {
                        std::string parentInitFields = pit->second + "_init_fields";
                        auto *parentFn = module_->getFunction(parentInitFields);
                        if (parentFn) {
                            auto selfIt = vars_.namedValues.find("self");
                            if (selfIt != vars_.namedValues.end()) {
                                auto *ptrTy = llvm::PointerType::getUnqual(*context_);
                                auto *selfVal = builder_->CreateLoad(
                                    ptrTy, selfIt->second, "self.ptr");
                                std::vector<llvm::Value *> args;
                                args.push_back(selfVal);
                                for (auto &arg : node->getArgs()) {
                                    auto *val = visit(arg.get());
                                    if (!val) return nullptr;
                                    args.push_back(val);
                                }
                                builder_->CreateCall(parentFn, args);
                                return nullptr;
                            }
                        }
                    } else {
                        // super.method(args) → direct call
                        std::string parentMethod = pit->second + "_" + methodName;
                        auto *parentFn = module_->getFunction(parentMethod);
                        if (parentFn) {
                            auto selfIt = vars_.namedValues.find("self");
                            if (selfIt != vars_.namedValues.end()) {
                                auto *ptrTy = llvm::PointerType::getUnqual(*context_);
                                auto *selfVal = builder_->CreateLoad(
                                    ptrTy, selfIt->second, "self.ptr");
                                std::vector<llvm::Value *> args;
                                args.push_back(selfVal);
                                for (auto &arg : node->getArgs()) {
                                    auto *val = visit(arg.get());
                                    if (!val) return nullptr;
                                    args.push_back(val);
                                }
                                if (parentFn->getReturnType()->isVoidTy()) {
                                    builder_->CreateCall(parentFn, args);
                                    return nullptr;
                                }
                                return builder_->CreateCall(parentFn, args, "super_call");
                            }
                        }
                    }
                }
                return nullptr;
            }
        }

        // Chained class method call: obj.field.method() where the receiver
        // resolves to a class type. The identifier-based class path above only
        // fires for a plain identifier receiver; here the receiver is an
        // arbitrary expression (e.g. a member access) whose resolved type is a
        // class. Class instances are pointers, so visiting the receiver yields
        // the self pointer directly.
        // We try the Sema resolved type first; if that is absent (e.g. inside a
        // class method body where TypeChecker uses currentClassName_ rather than
        // currentImplTypeName_), fall back to resolveExprClassTypeName().
        {
            std::string chainedClsName;
            if (auto *recvType = memberExpr->getObject()->getResolvedType()) {
                if (recvType->getKind() == TypeRepr::Kind::Named) {
                    auto *named = static_cast<const NamedTypeRepr *>(recvType);
                    if (classTypes_.count(named->getName()))
                        chainedClsName = named->getName();
                }
            }
            if (chainedClsName.empty())
                chainedClsName = resolveExprClassTypeName(memberExpr->getObject());

            if (!chainedClsName.empty()) {
                // Find the method owner by walking the inheritance chain
                // and looking for a mangled `Type_method` definition.
                std::string foundClass;
                {
                    std::string tn = chainedClsName;
                    for (int i = 0; i < 64 && !tn.empty(); ++i) {
                        if (module_->getFunction(tn + "_" + methodName)) {
                            foundClass = tn;
                            break;
                        }
                        auto pit = classParent_.find(tn);
                        if (pit == classParent_.end()) break;
                        tn = pit->second;
                    }
                }
                if (!foundClass.empty()) {
                    std::string mangledName = foundClass + "_" + methodName;
                    if (auto *callee = module_->getFunction(mangledName)) {
                        llvm::Value *selfPtr = visit(memberExpr->getObject());
                        if (!selfPtr) return nullptr;
                        std::vector<llvm::Value *> callArgs;
                        callArgs.push_back(selfPtr);
                        for (auto &arg : node->getArgs()) {
                            auto *v = visit(arg.get());
                            if (!v) return nullptr;
                            callArgs.push_back(v);
                        }
                        if (callee->getReturnType()->isVoidTy()) {
                            builder_->CreateCall(callee, callArgs);
                            return nullptr;
                        }
                        return builder_->CreateCall(callee, callArgs, "callm");
                    }
                }
            }
        }

        // Nested struct method call: obj.field.method() (MemberExpr chain)
        if (!objAlloca && structTypeName.empty() &&
            memberExpr->getObject()->getKind() == ASTNode::NodeKind::MemberExpr) {
            // Walk the MemberExpr chain to find root variable and field path
            std::vector<std::string> fieldChain;
            ASTNode *current = memberExpr->getObject();
            while (current->getKind() == ASTNode::NodeKind::MemberExpr) {
                auto *memExpr = static_cast<MemberExpr *>(current);
                fieldChain.push_back(memExpr->getMember());
                current = memExpr->getObject();
            }

            if (current->getKind() == ASTNode::NodeKind::IdentifierExpr) {
                auto *rootIdent = static_cast<IdentifierExpr *>(current);
                std::string rootName = rootIdent->getName();

                auto rootValIt = vars_.namedValues.find(rootName);
                auto rootStIt = vars_.varStructTypes.find(rootName);

                if (rootValIt != vars_.namedValues.end() && rootStIt != vars_.varStructTypes.end()) {
                    llvm::AllocaInst *rootAlloca = rootValIt->second;
                    std::string curStructType = rootStIt->second;

                    // fieldChain is in reverse order, reverse it
                    std::reverse(fieldChain.begin(), fieldChain.end());

                    llvm::Value *currentPtr = rootAlloca;
                    if (rootAlloca->getAllocatedType()->isPointerTy()) {
                        currentPtr = builder_->CreateLoad(
                            rootAlloca->getAllocatedType(), rootAlloca, rootName + ".ptr");
                    }

                    bool resolved = true;
                    for (const auto &fld : fieldChain) {
                        auto stIt = structTypes_.find(curStructType);
                        if (stIt == structTypes_.end()) { resolved = false; break; }

                        int fieldIdx = getStructFieldIndex(curStructType, fld);
                        if (fieldIdx < 0) { resolved = false; break; }

                        // GEP to the field
                        currentPtr = builder_->CreateStructGEP(
                            stIt->second, currentPtr, fieldIdx, fld + ".gep");

                        // Determine the struct type of this field
                        auto ftrIt = structFieldTypeReprs_.find(curStructType);
                        if (ftrIt != structFieldTypeReprs_.end() &&
                            static_cast<size_t>(fieldIdx) < ftrIt->second.size()) {
                            auto *fieldTR = ftrIt->second[fieldIdx];
                            if (fieldTR && fieldTR->getKind() == TypeRepr::Kind::Named) {
                                curStructType = static_cast<const NamedTypeRepr *>(fieldTR)->getName();
                            } else {
                                resolved = false;
                                break;
                            }
                        } else {
                            resolved = false;
                            break;
                        }
                    }

                    if (resolved && !curStructType.empty()) {
                        structTypeName = curStructType;
                        objName = fieldChain.back();
                        // Create temp alloca storing pointer to nested field
                        auto *func = builder_->GetInsertBlock()->getParent();
                        auto *ptrTy = llvm::PointerType::getUnqual(*context_);
                        objAlloca = createEntryBlockAlloca(func, "nested.self", ptrTy);
                        builder_->CreateStore(currentPtr, objAlloca);
                    }
                }
            }
        }

        // Static method call: Type.method() (e.g., Student.new("Alice"))
        if (!objAlloca && structTypeName.empty()) {
            auto typeIt = structTypes_.find(objName);
            if (typeIt != structTypes_.end()) {
                structTypeName = objName;
                std::string mangledName = structTypeName + "_" + methodName;
                auto *callee = module_->getFunction(mangledName);
                if (callee) {
                    std::vector<llvm::Value *> args;
                    // No self argument for static methods
                    for (auto &arg : node->getArgs()) {
                        auto *val = visit(arg.get());
                        if (!val)
                            return nullptr;
                        args.push_back(val);
                    }
                    if (callee->getReturnType()->isVoidTy())
                        return builder_->CreateCall(callee, args);
                    return builder_->CreateCall(callee, args, "scall");
                }
            }
            // Generic struct static method: Stream.from(arr), Map.new(), ...
            // We don't know the type args until we look at the call args, so
            // infer them from arg AST types matched against the method's param
            // type signature, then monomorphize struct + method on demand.
            auto gsIt = genericStructDecls_.find(objName);
            if (gsIt != genericStructDecls_.end()) {
                auto giIt = genericImplDecls_.find(objName);
                if (giIt != genericImplDecls_.end()) {
                    const ImplDecl *implDecl = giIt->second;
                    const FuncDecl *methodDecl = nullptr;
                    for (auto &m : implDecl->getMethods()) {
                        if (m->getName() == methodName && !m->isMethod()) {
                            methodDecl = m.get();
                            break;
                        }
                    }
                    if (methodDecl) {
                        // Visit args first so resolved-type info is available.
                        std::vector<llvm::Value *> args;
                        args.reserve(node->getArgs().size());
                        for (auto &arg : node->getArgs()) {
                            auto *val = visit(arg.get());
                            if (!val) return nullptr;
                            args.push_back(val);
                        }

                        // Type-arg inference: walk param type AST and arg
                        // resolved type AST in parallel, recording which type
                        // param maps to which concrete type.
                        const auto &typeParams = gsIt->second->getTypeParams();
                        std::unordered_set<std::string> tpSet(typeParams.begin(),
                                                              typeParams.end());
                        std::unordered_map<std::string, const TypeRepr *> inferred;
                        std::function<void(const TypeRepr *, const TypeRepr *)> unify =
                            [&](const TypeRepr *p, const TypeRepr *a) {
                            if (!p || !a) return;
                            if (p->getKind() == TypeRepr::Kind::Named) {
                                auto *n = static_cast<const NamedTypeRepr *>(p);
                                if (tpSet.count(n->getName()) &&
                                    !inferred.count(n->getName())) {
                                    inferred[n->getName()] = a;
                                }
                                return;
                            }
                            if (p->getKind() == TypeRepr::Kind::Array &&
                                a->getKind() == TypeRepr::Kind::Array) {
                                unify(static_cast<const ArrayTypeRepr *>(p)->getElement(),
                                      static_cast<const ArrayTypeRepr *>(a)->getElement());
                                return;
                            }
                            if (p->getKind() == TypeRepr::Kind::Optional &&
                                a->getKind() == TypeRepr::Kind::Optional) {
                                unify(static_cast<const OptionalTypeRepr *>(p)->getInner(),
                                      static_cast<const OptionalTypeRepr *>(a)->getInner());
                                return;
                            }
                        };
                        for (size_t i = 0;
                             i < methodDecl->getParams().size() &&
                             i < node->getArgs().size(); ++i) {
                            unify(methodDecl->getParams()[i].type.get(),
                                  node->getArgs()[i]->getResolvedType());
                        }
                        std::vector<const TypeRepr *> typeArgs;
                        typeArgs.reserve(typeParams.size());
                        for (const auto &tp : typeParams) {
                            auto inIt = inferred.find(tp);
                            if (inIt != inferred.end())
                                typeArgs.push_back(inIt->second);
                        }
                        if (typeArgs.size() == typeParams.size()) {
                            monomorphizeStruct(gsIt->second, typeArgs);
                            std::string mangledStructName =
                                mangleGenericStruct(objName, typeArgs);
                            auto *callee = monomorphizeMethod(implDecl, methodDecl,
                                                               mangledStructName, typeArgs);
                            if (callee) {
                                if (callee->getReturnType()->isVoidTy())
                                    return builder_->CreateCall(callee, args);
                                return builder_->CreateCall(callee, args, "scall");
                            }
                        }
                    }
                }
            }
            // Also check for enum type static calls
            auto enumIt = enumTypes_.find(objName);
            if (enumIt != enumTypes_.end()) {
                structTypeName = objName;
            }
        }

        // Closure field call: obj.closureField(args) — e.g. self.onClick(id)
        if (objAlloca && !structTypeName.empty()) {
            auto sfIt = structFieldFuncTypes_.find(structTypeName);
            if (sfIt != structFieldFuncTypes_.end()) {
                auto ffIt = sfIt->second.find(methodName);
                if (ffIt != sfIt->second.end()) {
                    auto *llvmFuncTy = ffIt->second;
                    auto *structTy = structTypes_[structTypeName];
                    int fieldIdx = getStructFieldIndex(structTypeName, methodName);
                    if (fieldIdx >= 0) {
                        auto *closureObjTy = getClosureObjTy();
                        auto *ptrTy = llvm::PointerType::getUnqual(*context_);

                        llvm::Value *basePtr = objAlloca;
                        if (auto *ai = llvm::dyn_cast<llvm::AllocaInst>(objAlloca)) {
                            if (ai->getAllocatedType()->isPointerTy())
                                basePtr = builder_->CreateLoad(
                                    ai->getAllocatedType(), objAlloca, objName);
                        }

                        // GEP to closure field, then extract func/env ptrs
                        auto *fieldGEP = builder_->CreateStructGEP(
                            structTy, basePtr, fieldIdx, methodName + ".closure");
                        auto *funcGEP = builder_->CreateStructGEP(closureObjTy, fieldGEP, 0);
                        auto *funcPtr = builder_->CreateLoad(ptrTy, funcGEP, "cb.func");
                        auto *envGEP = builder_->CreateStructGEP(closureObjTy, fieldGEP, 1);
                        auto *envPtr = builder_->CreateLoad(ptrTy, envGEP, "cb.env");

                        std::vector<llvm::Value *> args;
                        args.push_back(envPtr);
                        for (auto &arg : node->getArgs()) {
                            auto *val = visit(arg.get());
                            if (!val) return nullptr;
                            args.push_back(val);
                        }

                        if (llvmFuncTy->getReturnType()->isVoidTy()) {
                            builder_->CreateCall(llvmFuncTy, funcPtr, args);
                            return llvm::Constant::getNullValue(builder_->getInt32Ty());
                        }
                        return builder_->CreateCall(llvmFuncTy, funcPtr, args, "cb.call");
                    }
                }
            }
        }

        if (objAlloca && !structTypeName.empty()) {
            std::string mangledName = structTypeName + "_" + methodName;
            auto *callee = module_->getFunction(mangledName);

            // Try monomorphizing from generic impl if not found
            if (!callee) {
                auto staIt = structTypeArgs_.find(structTypeName);
                if (staIt != structTypeArgs_.end()) {
                    for (auto &[baseName, implDecl] : genericImplDecls_) {
                        if (structTypeName.size() > baseName.size() &&
                            structTypeName.substr(0, baseName.size()) == baseName &&
                            structTypeName[baseName.size()] == '_') {
                            for (auto &m : implDecl->getMethods()) {
                                if (m->getName() == methodName) {
                                    // Explicit method type args at the call
                                    // site (`s.map::<i64>(...)` or
                                    // `s.map<i64>(...)`) take priority over
                                    // inference. Pulled from the MemberExpr
                                    // that names the method.
                                    std::vector<const TypeRepr *> methodTypeArgs;
                                    const auto &mtps = m->getTypeParams();
                                    if (auto *callExpr = node) {
                                        if (callExpr->getCallee()->getKind() ==
                                            ASTNode::NodeKind::MemberExpr) {
                                            auto *me = static_cast<const MemberExpr *>(
                                                callExpr->getCallee());
                                            if (!me->getTypeArgs().empty()) {
                                                for (auto &ta : me->getTypeArgs())
                                                    methodTypeArgs.push_back(ta.get());
                                            }
                                        }
                                    }
                                    if (methodTypeArgs.empty() && !mtps.empty()) {
                                        std::unordered_set<std::string> tpSet(
                                            mtps.begin(), mtps.end());
                                        std::unordered_map<std::string,
                                            const TypeRepr *> inferred;
                                        std::function<void(const TypeRepr *,
                                                            const TypeRepr *)> unify =
                                            [&](const TypeRepr *p, const TypeRepr *a) {
                                            if (!p || !a) return;
                                            if (p->getKind() == TypeRepr::Kind::Named) {
                                                auto *n = static_cast<
                                                    const NamedTypeRepr *>(p);
                                                if (tpSet.count(n->getName()) &&
                                                    !inferred.count(n->getName())) {
                                                    inferred[n->getName()] = a;
                                                }
                                                return;
                                            }
                                            if (p->getKind() == TypeRepr::Kind::Array &&
                                                a->getKind() == TypeRepr::Kind::Array) {
                                                unify(static_cast<const ArrayTypeRepr *>(p)->getElement(),
                                                      static_cast<const ArrayTypeRepr *>(a)->getElement());
                                                return;
                                            }
                                            if (p->getKind() == TypeRepr::Kind::Optional &&
                                                a->getKind() == TypeRepr::Kind::Optional) {
                                                unify(static_cast<const OptionalTypeRepr *>(p)->getInner(),
                                                      static_cast<const OptionalTypeRepr *>(a)->getInner());
                                                return;
                                            }
                                            if (p->getKind() == TypeRepr::Kind::Function &&
                                                a->getKind() == TypeRepr::Kind::Function) {
                                                auto *pf = static_cast<const FunctionTypeRepr *>(p);
                                                auto *af = static_cast<const FunctionTypeRepr *>(a);
                                                unify(pf->getReturnType(), af->getReturnType());
                                                for (size_t k = 0;
                                                     k < pf->getParams().size() &&
                                                     k < af->getParams().size(); ++k) {
                                                    unify(pf->getParams()[k].get(),
                                                          af->getParams()[k].get());
                                                }
                                                return;
                                            }
                                        };
                                        // Skip the implicit self parameter
                                        // (no AST type for it).
                                        size_t paramIdx = 0;
                                        for (auto &p : m->getParams()) {
                                            if (p.isSelf) continue;
                                            if (paramIdx >= node->getArgs().size()) break;
                                            unify(p.type.get(),
                                                  node->getArgs()[paramIdx]->getResolvedType());
                                            paramIdx++;
                                        }
                                        for (const auto &tp : mtps) {
                                            auto inIt = inferred.find(tp);
                                            if (inIt != inferred.end())
                                                methodTypeArgs.push_back(inIt->second);
                                        }
                                    }
                                    callee = monomorphizeMethod(implDecl, m.get(),
                                                                 structTypeName,
                                                                 staIt->second,
                                                                 methodTypeArgs);
                                    break;
                                }
                            }
                            break;
                        }
                    }
                }
            }

            if (callee) {
                std::vector<llvm::Value *> args;
                // Pass object pointer as first arg (self)
                llvm::Value *selfPtr = objAlloca;
                if (objAlloca->getAllocatedType()->isPointerTy()) {
                    selfPtr = builder_->CreateLoad(objAlloca->getAllocatedType(),
                                                    objAlloca, objName);
                }
                args.push_back(selfPtr);

                for (auto &arg : node->getArgs()) {
                    auto *val = visit(arg.get());
                    if (!val)
                        return nullptr;
                    args.push_back(val);
                }

                if (callee->getReturnType()->isVoidTy())
                    return builder_->CreateCall(callee, args);
                return builder_->CreateCall(callee, args, "mcalltmp");
            }
        }

        // P1-8 alt-spec 2: built-in Hashable for primitive receivers.
        // Fallback after struct/class method dispatch — if a user defines their
        // own `hash()` method on a struct, that takes precedence; only the
        // built-in primitive cases (i8/i16/i32/i64, u8/u16/u32/u64, string,
        // bool, Char) reach here. Sema has already validated the receiver type
        // and set this call's resolved type to i64; we mirror its primitive
        // detection here to pick the right runtime entry point.
        if (methodName == "hash" && node->getArgs().empty()) {
            const TypeRepr *recvType = memberExpr->getObject()->getResolvedType();
            if (recvType) {
                auto k = recvType->getKind();
                bool isChar = false;
                if (k == TypeRepr::Kind::Named) {
                    auto *nt = static_cast<const NamedTypeRepr *>(recvType);
                    if (nt->getName() == "Char") isChar = true;
                }
                if (k == TypeRepr::Kind::String) {
                    auto *recv = visit(memberExpr->getObject());
                    if (!recv) return nullptr;
                    auto *fn = getOrPanic("liva_hash_string");
                    return builder_->CreateCall(fn, {recv}, "hash.str");
                }
                if (k == TypeRepr::Kind::Bool) {
                    auto *recv = visit(memberExpr->getObject());
                    if (!recv) return nullptr;
                    auto *fn = getOrPanic("liva_hash_bool");
                    auto *zextI8 = builder_->CreateZExt(recv,
                        builder_->getInt8Ty(), "bool.zext");
                    return builder_->CreateCall(fn, {zextI8}, "hash.bool");
                }
                if (isChar) {
                    auto *recv = visit(memberExpr->getObject());
                    if (!recv) return nullptr;
                    // Char codepoints flow as i32; ensure type matches signature.
                    if (recv->getType()->isIntegerTy() &&
                        recv->getType()->getIntegerBitWidth() != 32) {
                        unsigned bits = recv->getType()->getIntegerBitWidth();
                        if (bits < 32)
                            recv = builder_->CreateZExt(recv, builder_->getInt32Ty(), "char.zext");
                        else
                            recv = builder_->CreateTrunc(recv, builder_->getInt32Ty(), "char.trunc");
                    }
                    auto *fn = getOrPanic("liva_hash_char");
                    return builder_->CreateCall(fn, {recv}, "hash.char");
                }
                if (k == TypeRepr::Kind::I8 || k == TypeRepr::Kind::I16 ||
                    k == TypeRepr::Kind::I32 || k == TypeRepr::Kind::I64 ||
                    k == TypeRepr::Kind::U8 || k == TypeRepr::Kind::U16 ||
                    k == TypeRepr::Kind::U32 || k == TypeRepr::Kind::U64) {
                    auto *recv = visit(memberExpr->getObject());
                    if (!recv) return nullptr;
                    if (!recv->getType()->isIntegerTy()) return nullptr;
                    unsigned bits = recv->getType()->getIntegerBitWidth();
                    if (bits == 32 && (k == TypeRepr::Kind::I32 || k == TypeRepr::Kind::U32)) {
                        auto *fn = getOrPanic("liva_hash_i32");
                        return builder_->CreateCall(fn, {recv}, "hash.i32");
                    }
                    // Widen/narrow to i64. Use sign-extend for signed kinds and
                    // zero-extend for unsigned to preserve the unsigned value
                    // when the kind is u8/u16/u32.
                    llvm::Value *widened = recv;
                    bool isSigned = (k == TypeRepr::Kind::I8 || k == TypeRepr::Kind::I16 ||
                                     k == TypeRepr::Kind::I32 || k == TypeRepr::Kind::I64);
                    if (bits < 64) {
                        widened = isSigned
                            ? builder_->CreateSExt(recv, builder_->getInt64Ty(), "int.sext")
                            : builder_->CreateZExt(recv, builder_->getInt64Ty(), "int.zext");
                    } else if (bits > 64) {
                        widened = builder_->CreateTrunc(recv,
                            builder_->getInt64Ty(), "int.trunc");
                    }
                    auto *fn = getOrPanic("liva_hash_i64");
                    return builder_->CreateCall(fn, {widened}, "hash.i64");
                }
            }
        }

        return nullptr;
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

    // Handle len() built-in
    if (funcName == "len") {
        if (!node->getArgs().empty()) {
            auto *arg = visit(node->getArgs()[0].get());
            if (!arg) return nullptr;
            auto *lenFn = getOrPanic("liva_str_length");
            return builder_->CreateCall(lenFn, {arg});
        }
        return nullptr;
    }

    // Handle toString() built-in
    if (funcName == "toString") {
        if (!node->getArgs().empty()) {
            auto *arg = visit(node->getArgs()[0].get());
            if (!arg) return nullptr;
            if (arg->getType()->isIntegerTy(32)) {
                auto *r = builder_->CreateCall(getOrPanic("liva_i32_to_str"), {arg});
                trackStringTemp(r);
                return r;
            } else if (arg->getType()->isIntegerTy(64)) {
                auto *r = builder_->CreateCall(getOrPanic("liva_i64_to_str"), {arg});
                trackStringTemp(r);
                return r;
            } else if (arg->getType()->isDoubleTy()) {
                auto *r = builder_->CreateCall(getOrPanic("liva_f64_to_str"), {arg});
                trackStringTemp(r);
                return r;
            } else if (arg->getType()->isIntegerTy(1)) {
                auto *ext = builder_->CreateZExt(arg, llvm::Type::getInt8Ty(*context_));
                auto *r = builder_->CreateCall(getOrPanic("liva_bool_to_str"), {ext});
                trackStringTemp(r);
                return r;
            } else if (arg->getType()->isPointerTy()) {
                return arg; // already a string
            }
            return arg;
        }
        return nullptr;
    }

    // Handle charToString(i32) -> string
    if (funcName == "charToString" && !node->getArgs().empty()) {
        auto *arg = visit(node->getArgs()[0].get());
        if (!arg) return nullptr;
        auto *r = builder_->CreateCall(getOrPanic("liva_char_to_str"), {arg});
        trackStringTemp(r);
        return r;
    }

    // Handle parseInt/parseInt64/parseFloat built-ins → Optional<T>
    if (funcName == "parseInt" && !node->getArgs().empty()) {
        auto *strArg = visit(node->getArgs()[0].get());
        if (!strArg) return nullptr;
        auto *curFunc = builder_->GetInsertBlock()->getParent();
        auto *resultAlloca = createEntryBlockAlloca(curFunc, "parse.tmp", builder_->getInt32Ty());
        auto *fn = getOrPanic("liva_str_parse_i32");
        auto *ok = builder_->CreateCall(fn, {strArg, resultAlloca}, "parse.ok");
        auto *hasVal = builder_->CreateTrunc(ok, builder_->getInt1Ty(), "parse.hasval");
        auto *val = builder_->CreateLoad(builder_->getInt32Ty(), resultAlloca, "parse.val");
        auto *optTy = getOptionalType(builder_->getInt32Ty());
        auto *optAlloca = createEntryBlockAlloca(curFunc, "parse.opt", optTy);
        auto *hasValPtr = builder_->CreateStructGEP(optTy, optAlloca, 0);
        builder_->CreateStore(hasVal, hasValPtr);
        auto *valPtr = builder_->CreateStructGEP(optTy, optAlloca, 1);
        builder_->CreateStore(val, valPtr);
        return builder_->CreateLoad(optTy, optAlloca, "parse.result");
    }

    if (funcName == "parseInt64" && !node->getArgs().empty()) {
        auto *strArg = visit(node->getArgs()[0].get());
        if (!strArg) return nullptr;
        auto *curFunc = builder_->GetInsertBlock()->getParent();
        auto *resultAlloca = createEntryBlockAlloca(curFunc, "parse.tmp", builder_->getInt64Ty());
        auto *fn = getOrPanic("liva_str_parse_i64");
        auto *ok = builder_->CreateCall(fn, {strArg, resultAlloca}, "parse.ok");
        auto *hasVal = builder_->CreateTrunc(ok, builder_->getInt1Ty(), "parse.hasval");
        auto *val = builder_->CreateLoad(builder_->getInt64Ty(), resultAlloca, "parse.val");
        auto *optTy = getOptionalType(builder_->getInt64Ty());
        auto *optAlloca = createEntryBlockAlloca(curFunc, "parse.opt", optTy);
        auto *hasValPtr = builder_->CreateStructGEP(optTy, optAlloca, 0);
        builder_->CreateStore(hasVal, hasValPtr);
        auto *valPtr = builder_->CreateStructGEP(optTy, optAlloca, 1);
        builder_->CreateStore(val, valPtr);
        return builder_->CreateLoad(optTy, optAlloca, "parse.result");
    }

    if (funcName == "parseFloat" && !node->getArgs().empty()) {
        auto *strArg = visit(node->getArgs()[0].get());
        if (!strArg) return nullptr;
        auto *curFunc = builder_->GetInsertBlock()->getParent();
        auto *resultAlloca = createEntryBlockAlloca(curFunc, "parse.tmp", builder_->getDoubleTy());
        auto *fn = getOrPanic("liva_str_parse_f64");
        auto *ok = builder_->CreateCall(fn, {strArg, resultAlloca}, "parse.ok");
        auto *hasVal = builder_->CreateTrunc(ok, builder_->getInt1Ty(), "parse.hasval");
        auto *val = builder_->CreateLoad(builder_->getDoubleTy(), resultAlloca, "parse.val");
        auto *optTy = getOptionalType(builder_->getDoubleTy());
        auto *optAlloca = createEntryBlockAlloca(curFunc, "parse.opt", optTy);
        auto *hasValPtr = builder_->CreateStructGEP(optTy, optAlloca, 0);
        builder_->CreateStore(hasVal, hasValPtr);
        auto *valPtr = builder_->CreateStructGEP(optTy, optAlloca, 1);
        builder_->CreateStore(val, valPtr);
        return builder_->CreateLoad(optTy, optAlloca, "parse.result");
    }

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
    if (funcName == "httpGet" && !node->getArgs().empty()) {
        auto *urlArg = visit(node->getArgs()[0].get());
        if (!urlArg) return nullptr;
        auto *fn = getOrPanic("liva_http_get");
        auto *result = builder_->CreateCall(fn, {urlArg}, "http.get.raw");
        trackStringTemp(result);
        // Wrap in Optional<string>
        auto *curFunc = builder_->GetInsertBlock()->getParent();
        auto *isNull = builder_->CreateICmpEQ(result, llvm::ConstantPointerNull::get(
            llvm::PointerType::getUnqual(*context_)), "http.get.isnull");
        auto *hasVal = builder_->CreateNot(isNull, "http.get.hasval");
        auto *optTy = getOptionalType(llvm::PointerType::getUnqual(*context_));
        auto *optAlloca = createEntryBlockAlloca(curFunc, "http.get.opt", optTy);
        auto *hasValPtr = builder_->CreateStructGEP(optTy, optAlloca, 0);
        builder_->CreateStore(hasVal, hasValPtr);
        auto *valPtr = builder_->CreateStructGEP(optTy, optAlloca, 1);
        builder_->CreateStore(result, valPtr);
        return builder_->CreateLoad(optTy, optAlloca, "http.get.result");
    }

    if (funcName == "httpPost" && node->getArgs().size() >= 2) {
        auto *urlArg = visit(node->getArgs()[0].get());
        auto *bodyArg = visit(node->getArgs()[1].get());
        if (!urlArg || !bodyArg) return nullptr;
        auto *fn = getOrPanic("liva_http_post");
        auto *result = builder_->CreateCall(fn, {urlArg, bodyArg}, "http.post.raw");
        trackStringTemp(result);
        // Wrap in Optional<string>
        auto *curFunc = builder_->GetInsertBlock()->getParent();
        auto *isNull = builder_->CreateICmpEQ(result, llvm::ConstantPointerNull::get(
            llvm::PointerType::getUnqual(*context_)), "http.post.isnull");
        auto *hasVal = builder_->CreateNot(isNull, "http.post.hasval");
        auto *optTy = getOptionalType(llvm::PointerType::getUnqual(*context_));
        auto *optAlloca = createEntryBlockAlloca(curFunc, "http.post.opt", optTy);
        auto *hasValPtr = builder_->CreateStructGEP(optTy, optAlloca, 0);
        builder_->CreateStore(hasVal, hasValPtr);
        auto *valPtr = builder_->CreateStructGEP(optTy, optAlloca, 1);
        builder_->CreateStore(result, valPtr);
        return builder_->CreateLoad(optTy, optAlloca, "http.post.result");
    }

    // httpPut(url, body) -> string?
    if (funcName == "httpPut" && node->getArgs().size() >= 2) {
        auto *urlArg = visit(node->getArgs()[0].get());
        auto *bodyArg = visit(node->getArgs()[1].get());
        if (!urlArg || !bodyArg) return nullptr;
        auto *fn = getOrPanic("liva_http_put");
        auto *result = builder_->CreateCall(fn, {urlArg, bodyArg}, "http.put.raw");
        trackStringTemp(result);
        auto *curFunc = builder_->GetInsertBlock()->getParent();
        auto *isNull = builder_->CreateICmpEQ(result, llvm::ConstantPointerNull::get(
            llvm::PointerType::getUnqual(*context_)), "http.put.isnull");
        auto *hasVal = builder_->CreateNot(isNull, "http.put.hasval");
        auto *optTy = getOptionalType(llvm::PointerType::getUnqual(*context_));
        auto *optAlloca = createEntryBlockAlloca(curFunc, "http.put.opt", optTy);
        auto *hasValPtr = builder_->CreateStructGEP(optTy, optAlloca, 0);
        builder_->CreateStore(hasVal, hasValPtr);
        auto *valPtr = builder_->CreateStructGEP(optTy, optAlloca, 1);
        builder_->CreateStore(result, valPtr);
        return builder_->CreateLoad(optTy, optAlloca, "http.put.result");
    }

    // httpPatch(url, body) -> string?
    if (funcName == "httpPatch" && node->getArgs().size() >= 2) {
        auto *urlArg = visit(node->getArgs()[0].get());
        auto *bodyArg = visit(node->getArgs()[1].get());
        if (!urlArg || !bodyArg) return nullptr;
        auto *fn = getOrPanic("liva_http_patch");
        auto *result = builder_->CreateCall(fn, {urlArg, bodyArg}, "http.patch.raw");
        trackStringTemp(result);
        auto *curFunc = builder_->GetInsertBlock()->getParent();
        auto *isNull = builder_->CreateICmpEQ(result, llvm::ConstantPointerNull::get(
            llvm::PointerType::getUnqual(*context_)), "http.patch.isnull");
        auto *hasVal = builder_->CreateNot(isNull, "http.patch.hasval");
        auto *optTy = getOptionalType(llvm::PointerType::getUnqual(*context_));
        auto *optAlloca = createEntryBlockAlloca(curFunc, "http.patch.opt", optTy);
        auto *hasValPtr = builder_->CreateStructGEP(optTy, optAlloca, 0);
        builder_->CreateStore(hasVal, hasValPtr);
        auto *valPtr = builder_->CreateStructGEP(optTy, optAlloca, 1);
        builder_->CreateStore(result, valPtr);
        return builder_->CreateLoad(optTy, optAlloca, "http.patch.result");
    }

    // httpDelete(url) -> string?
    if (funcName == "httpDelete" && !node->getArgs().empty()) {
        auto *urlArg = visit(node->getArgs()[0].get());
        if (!urlArg) return nullptr;
        auto *fn = getOrPanic("liva_http_delete");
        auto *result = builder_->CreateCall(fn, {urlArg}, "http.delete.raw");
        trackStringTemp(result);
        auto *curFunc = builder_->GetInsertBlock()->getParent();
        auto *isNull = builder_->CreateICmpEQ(result, llvm::ConstantPointerNull::get(
            llvm::PointerType::getUnqual(*context_)), "http.delete.isnull");
        auto *hasVal = builder_->CreateNot(isNull, "http.delete.hasval");
        auto *optTy = getOptionalType(llvm::PointerType::getUnqual(*context_));
        auto *optAlloca = createEntryBlockAlloca(curFunc, "http.delete.opt", optTy);
        auto *hasValPtr = builder_->CreateStructGEP(optTy, optAlloca, 0);
        builder_->CreateStore(hasVal, hasValPtr);
        auto *valPtr = builder_->CreateStructGEP(optTy, optAlloca, 1);
        builder_->CreateStore(result, valPtr);
        return builder_->CreateLoad(optTy, optAlloca, "http.delete.result");
    }

    // httpRequest(method, url, body, timeout_ms) -> i64 handle
    if (funcName == "httpRequest" && node->getArgs().size() >= 4) {
        auto *methodArg = visit(node->getArgs()[0].get());
        auto *urlArg = visit(node->getArgs()[1].get());
        auto *bodyArg = visit(node->getArgs()[2].get());
        auto *timeoutArg = visit(node->getArgs()[3].get());
        if (!methodArg || !urlArg || !bodyArg || !timeoutArg) return nullptr;
        if (timeoutArg->getType()->isIntegerTy(32))
            timeoutArg = builder_->CreateSExt(timeoutArg, builder_->getInt64Ty());
        auto *fn = getOrPanic("liva_http_req");
        return builder_->CreateCall(fn, {methodArg, urlArg, bodyArg, timeoutArg},
                                    "http.req.handle");
    }

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

    // httpHeader(handle, name) -> string?
    if (funcName == "httpHeader" && node->getArgs().size() >= 2) {
        auto *handleArg = visit(node->getArgs()[0].get());
        auto *nameArg = visit(node->getArgs()[1].get());
        if (!handleArg || !nameArg) return nullptr;
        auto *fn = getOrPanic("liva_http_req_header");
        auto *result = builder_->CreateCall(fn, {handleArg, nameArg}, "http.header.raw");
        trackStringTemp(result);
        auto *curFunc = builder_->GetInsertBlock()->getParent();
        auto *isNull = builder_->CreateICmpEQ(result, llvm::ConstantPointerNull::get(
            llvm::PointerType::getUnqual(*context_)), "http.header.isnull");
        auto *hasVal = builder_->CreateNot(isNull, "http.header.hasval");
        auto *optTy = getOptionalType(llvm::PointerType::getUnqual(*context_));
        auto *optAlloca = createEntryBlockAlloca(curFunc, "http.header.opt", optTy);
        builder_->CreateStore(hasVal, builder_->CreateStructGEP(optTy, optAlloca, 0));
        builder_->CreateStore(result, builder_->CreateStructGEP(optTy, optAlloca, 1));
        return builder_->CreateLoad(optTy, optAlloca, "http.header.result");
    }

    // httpClose(handle) -> void
    if (funcName == "httpClose" && !node->getArgs().empty()) {
        auto *handleArg = visit(node->getArgs()[0].get());
        if (!handleArg) return nullptr;
        auto *fn = getOrPanic("liva_http_req_close");
        builder_->CreateCall(fn, {handleArg});
        return nullptr;
    }

    // wsConnect(url) -> i64 (0 = failure)
    if (funcName == "wsConnect" && !node->getArgs().empty()) {
        auto *urlArg = visit(node->getArgs()[0].get());
        if (!urlArg) return nullptr;
        auto *fn = getOrPanic("liva_ws_connect");
        return builder_->CreateCall(fn, {urlArg}, "ws.connect");
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

    // wsRecv(handle) -> string?
    if (funcName == "wsRecv" && !node->getArgs().empty()) {
        auto *handleArg = visit(node->getArgs()[0].get());
        if (!handleArg) return nullptr;
        auto *fn = getOrPanic("liva_ws_recv_text");
        auto *result = builder_->CreateCall(fn, {handleArg}, "ws.recv.raw");
        trackStringTemp(result);
        auto *curFunc = builder_->GetInsertBlock()->getParent();
        auto *isNull = builder_->CreateICmpEQ(result, llvm::ConstantPointerNull::get(
            llvm::PointerType::getUnqual(*context_)), "ws.recv.isnull");
        auto *hasVal = builder_->CreateNot(isNull, "ws.recv.hasval");
        auto *optTy = getOptionalType(llvm::PointerType::getUnqual(*context_));
        auto *optAlloca = createEntryBlockAlloca(curFunc, "ws.recv.opt", optTy);
        builder_->CreateStore(hasVal, builder_->CreateStructGEP(optTy, optAlloca, 0));
        builder_->CreateStore(result, builder_->CreateStructGEP(optTy, optAlloca, 1));
        return builder_->CreateLoad(optTy, optAlloca, "ws.recv.result");
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

    // === Directory operations ===
    if (funcName == "dirList" && !node->getArgs().empty()) {
        auto *pathArg = visit(node->getArgs()[0].get());
        if (!pathArg) return nullptr;
        auto *fn = getOrPanic("liva_dir_list");
        auto *ptrTy = llvm::PointerType::getUnqual(*context_);
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

    // === JSON ===
    if (funcName == "jsonGet" && node->getArgs().size() >= 2) {
        auto *jsonArg = visit(node->getArgs()[0].get());
        auto *keyArg = visit(node->getArgs()[1].get());
        if (!jsonArg || !keyArg) return nullptr;
        auto *fn = getOrPanic("liva_json_get");
        auto *result = builder_->CreateCall(fn, {jsonArg, keyArg}, "json.get.raw");
        trackStringTemp(result);
        // Wrap in Optional<string>
        auto *ptrTy = llvm::PointerType::getUnqual(*context_);
        auto *curFunc = builder_->GetInsertBlock()->getParent();
        auto *isNull = builder_->CreateICmpEQ(result, llvm::ConstantPointerNull::get(
            llvm::cast<llvm::PointerType>(ptrTy)), "json.get.isnull");
        auto *hasVal = builder_->CreateNot(isNull, "json.get.hasval");
        auto *optTy = getOptionalType(ptrTy);
        auto *optAlloca = createEntryBlockAlloca(curFunc, "json.get.opt", optTy);
        builder_->CreateStructGEP(optTy, optAlloca, 0);
        auto *hasValPtr = builder_->CreateStructGEP(optTy, optAlloca, 0);
        builder_->CreateStore(hasVal, hasValPtr);
        auto *valPtr = builder_->CreateStructGEP(optTy, optAlloca, 1);
        builder_->CreateStore(result, valPtr);
        return builder_->CreateLoad(optTy, optAlloca, "json.get.result");
    }

    if (funcName == "jsonGetInt" && node->getArgs().size() >= 2) {
        auto *jsonArg = visit(node->getArgs()[0].get());
        auto *keyArg = visit(node->getArgs()[1].get());
        if (!jsonArg || !keyArg) return nullptr;
        auto *fn = getOrPanic("liva_json_get_int");
        return builder_->CreateCall(fn, {jsonArg, keyArg}, "json.getint");
    }

    if (funcName == "jsonGetFloat" && node->getArgs().size() >= 2) {
        auto *jsonArg = visit(node->getArgs()[0].get());
        auto *keyArg = visit(node->getArgs()[1].get());
        if (!jsonArg || !keyArg) return nullptr;
        auto *fn = getOrPanic("liva_json_get_float");
        return builder_->CreateCall(fn, {jsonArg, keyArg}, "json.getfloat");
    }

    if (funcName == "jsonGetBool" && node->getArgs().size() >= 2) {
        auto *jsonArg = visit(node->getArgs()[0].get());
        auto *keyArg = visit(node->getArgs()[1].get());
        if (!jsonArg || !keyArg) return nullptr;
        auto *fn = getOrPanic("liva_json_get_bool");
        auto *r = builder_->CreateCall(fn, {jsonArg, keyArg}, "json.getbool");
        return builder_->CreateTrunc(r, builder_->getInt1Ty(), "json.getbool.bool");
    }

    if (funcName == "jsonIsValid" && !node->getArgs().empty()) {
        auto *jsonArg = visit(node->getArgs()[0].get());
        if (!jsonArg) return nullptr;
        auto *fn = getOrPanic("liva_json_is_valid");
        auto *r = builder_->CreateCall(fn, {jsonArg}, "json.isvalid");
        return builder_->CreateTrunc(r, builder_->getInt1Ty(), "json.isvalid.bool");
    }

    if (funcName == "jsonKeys" && !node->getArgs().empty()) {
        auto *jsonArg = visit(node->getArgs()[0].get());
        if (!jsonArg) return nullptr;
        auto *fn = getOrPanic("liva_json_keys");
        auto *i64Ty = builder_->getInt64Ty();
        auto *curFunc = builder_->GetInsertBlock()->getParent();
        auto *countAlloca = createEntryBlockAlloca(curFunc, "jsonkeys.count", i64Ty);
        builder_->CreateStore(builder_->getInt64(0), countAlloca);
        auto *rawArr = builder_->CreateCall(fn, {jsonArg, countAlloca}, "jsonkeys.raw");
        auto *count = builder_->CreateLoad(i64Ty, countAlloca, "jsonkeys.count");
        auto *daTy = getDynArrayStructTy();
        auto *daAlloca = createEntryBlockAlloca(curFunc, "jsonkeys.da", daTy);
        builder_->CreateStore(rawArr, builder_->CreateStructGEP(daTy, daAlloca, 0));
        builder_->CreateStore(count, builder_->CreateStructGEP(daTy, daAlloca, 1));
        builder_->CreateStore(count, builder_->CreateStructGEP(daTy, daAlloca, 2));
        return builder_->CreateLoad(daTy, daAlloca, "jsonkeys.da.val");
    }

    if (funcName == "jsonCreate" && node->getArgs().empty()) {
        auto *fn = getOrPanic("liva_json_create");
        auto *r = builder_->CreateCall(fn, {}, "json.create");
        trackStringTemp(r);
        return r;
    }

    if (funcName == "jsonSet" && node->getArgs().size() >= 3) {
        auto *jsonArg = visit(node->getArgs()[0].get());
        auto *keyArg = visit(node->getArgs()[1].get());
        auto *valArg = visit(node->getArgs()[2].get());
        if (!jsonArg || !keyArg || !valArg) return nullptr;
        auto *fn = getOrPanic("liva_json_set");
        auto *r = builder_->CreateCall(fn, {jsonArg, keyArg, valArg}, "json.set");
        trackStringTemp(r);
        return r;
    }

    if (funcName == "jsonSetInt" && node->getArgs().size() >= 3) {
        auto *jsonArg = visit(node->getArgs()[0].get());
        auto *keyArg = visit(node->getArgs()[1].get());
        auto *valArg = visit(node->getArgs()[2].get());
        if (!jsonArg || !keyArg || !valArg) return nullptr;
        auto *fn = getOrPanic("liva_json_set_int");
        auto *r = builder_->CreateCall(fn, {jsonArg, keyArg, valArg}, "json.setint");
        trackStringTemp(r);
        return r;
    }

    if (funcName == "jsonSetFloat" && node->getArgs().size() >= 3) {
        auto *jsonArg = visit(node->getArgs()[0].get());
        auto *keyArg = visit(node->getArgs()[1].get());
        auto *valArg = visit(node->getArgs()[2].get());
        if (!jsonArg || !keyArg || !valArg) return nullptr;
        auto *fn = getOrPanic("liva_json_set_float");
        auto *r = builder_->CreateCall(fn, {jsonArg, keyArg, valArg}, "json.setfloat");
        trackStringTemp(r);
        return r;
    }

    if (funcName == "jsonSetBool" && node->getArgs().size() >= 3) {
        auto *jsonArg = visit(node->getArgs()[0].get());
        auto *keyArg = visit(node->getArgs()[1].get());
        auto *valArg = visit(node->getArgs()[2].get());
        if (!jsonArg || !keyArg || !valArg) return nullptr;
        auto *i8Val = builder_->CreateZExt(valArg, builder_->getInt8Ty(), "json.setbool.i8");
        auto *fn = getOrPanic("liva_json_set_bool");
        auto *r = builder_->CreateCall(fn, {jsonArg, keyArg, i8Val}, "json.setbool");
        trackStringTemp(r);
        return r;
    }

    if (funcName == "jsonRemove" && node->getArgs().size() >= 2) {
        auto *jsonArg = visit(node->getArgs()[0].get());
        auto *keyArg = visit(node->getArgs()[1].get());
        if (!jsonArg || !keyArg) return nullptr;
        auto *fn = getOrPanic("liva_json_remove");
        auto *r = builder_->CreateCall(fn, {jsonArg, keyArg}, "json.remove");
        trackStringTemp(r);
        return r;
    }

    if (funcName == "jsonGetArray" && node->getArgs().size() >= 2) {
        auto *jsonArg = visit(node->getArgs()[0].get());
        auto *keyArg = visit(node->getArgs()[1].get());
        if (!jsonArg || !keyArg) return nullptr;
        auto *fn = getOrPanic("liva_json_get_array");
        auto *result = builder_->CreateCall(fn, {jsonArg, keyArg}, "json.getarray.raw");
        trackStringTemp(result);
        auto *ptrTy = llvm::PointerType::getUnqual(*context_);
        auto *curFunc = builder_->GetInsertBlock()->getParent();
        auto *isNull = builder_->CreateICmpEQ(result, llvm::ConstantPointerNull::get(
            llvm::cast<llvm::PointerType>(ptrTy)), "json.getarray.isnull");
        auto *hasVal = builder_->CreateNot(isNull, "json.getarray.hasval");
        auto *optTy = getOptionalType(ptrTy);
        auto *optAlloca = createEntryBlockAlloca(curFunc, "json.getarray.opt", optTy);
        builder_->CreateStore(hasVal, builder_->CreateStructGEP(optTy, optAlloca, 0));
        builder_->CreateStore(result, builder_->CreateStructGEP(optTy, optAlloca, 1));
        return builder_->CreateLoad(optTy, optAlloca, "json.getarray.result");
    }

    if (funcName == "jsonGetObject" && node->getArgs().size() >= 2) {
        auto *jsonArg = visit(node->getArgs()[0].get());
        auto *keyArg = visit(node->getArgs()[1].get());
        if (!jsonArg || !keyArg) return nullptr;
        auto *fn = getOrPanic("liva_json_get_object");
        auto *result = builder_->CreateCall(fn, {jsonArg, keyArg}, "json.getobj.raw");
        trackStringTemp(result);
        auto *ptrTy = llvm::PointerType::getUnqual(*context_);
        auto *curFunc = builder_->GetInsertBlock()->getParent();
        auto *isNull = builder_->CreateICmpEQ(result, llvm::ConstantPointerNull::get(
            llvm::cast<llvm::PointerType>(ptrTy)), "json.getobj.isnull");
        auto *hasVal = builder_->CreateNot(isNull, "json.getobj.hasval");
        auto *optTy = getOptionalType(ptrTy);
        auto *optAlloca = createEntryBlockAlloca(curFunc, "json.getobj.opt", optTy);
        builder_->CreateStore(hasVal, builder_->CreateStructGEP(optTy, optAlloca, 0));
        builder_->CreateStore(result, builder_->CreateStructGEP(optTy, optAlloca, 1));
        return builder_->CreateLoad(optTy, optAlloca, "json.getobj.result");
    }

    if (funcName == "jsonCount" && !node->getArgs().empty()) {
        auto *jsonArg = visit(node->getArgs()[0].get());
        if (!jsonArg) return nullptr;
        auto *fn = getOrPanic("liva_json_count");
        return builder_->CreateCall(fn, {jsonArg}, "json.count");
    }

    if (funcName == "jsonStringifyPretty" && node->getArgs().size() >= 2) {
        auto *jsonArg = visit(node->getArgs()[0].get());
        auto *indentArg = visit(node->getArgs()[1].get());
        if (!jsonArg || !indentArg) return nullptr;
        if (indentArg->getType()->isIntegerTy(64))
            indentArg = builder_->CreateTrunc(indentArg, builder_->getInt32Ty());
        auto *fn = getOrPanic("liva_json_stringify_pretty");
        auto *result = builder_->CreateCall(fn, {jsonArg, indentArg}, "json.pretty");
        trackStringTemp(result);
        return result;
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

    // Handle readLine() built-in
    if (funcName == "readLine") {
        auto *fn = getOrPanic("liva_read_line");
        auto *r = builder_->CreateCall(fn, {}, "readline");
        trackStringTemp(r);
        return r;
    }

    // Handle format() built-in
    if (funcName == "format" && !node->getArgs().empty()) {
        // First arg is format string literal
        auto *fmtArg = visit(node->getArgs()[0].get());
        if (!fmtArg) return nullptr;

        // If only format string, return it directly
        if (node->getArgs().size() == 1) return fmtArg;

        // Parse format string from AST for {} placeholders
        auto *fmtLit = node->getArgs()[0].get();
        std::string fmtStr;
        if (fmtLit->getKind() == ASTNode::NodeKind::StringLiteralExpr) {
            fmtStr = static_cast<StringLiteralExpr *>(fmtLit)->getValue();
        }

        // Split format string by {} and interleave with toString'd args
        std::vector<llvm::Value *> parts;
        size_t argIdx = 1;
        size_t pos = 0;
        while (pos < fmtStr.size()) {
            auto bracePos = fmtStr.find("{}", pos);
            if (bracePos == std::string::npos) {
                // Remaining literal text
                auto *lit = builder_->CreateGlobalString(
                    llvm::StringRef(fmtStr.c_str() + pos, fmtStr.size() - pos));
                parts.push_back(lit);
                break;
            }
            // Text before {}
            if (bracePos > pos) {
                auto *lit = builder_->CreateGlobalString(
                    llvm::StringRef(fmtStr.c_str() + pos, bracePos - pos));
                parts.push_back(lit);
            }
            // Convert arg to string
            if (argIdx < node->getArgs().size()) {
                auto *argVal = visit(node->getArgs()[argIdx].get());
                if (argVal) {
                    // emitToString inline
                    if (argVal->getType()->isIntegerTy(32)) {
                        argVal = builder_->CreateCall(
                            getOrPanic("liva_i32_to_str"), {argVal}, "fmt.i32");
                        trackStringTemp(argVal);
                    } else if (argVal->getType()->isIntegerTy(64)) {
                        argVal = builder_->CreateCall(
                            getOrPanic("liva_i64_to_str"), {argVal}, "fmt.i64");
                        trackStringTemp(argVal);
                    } else if (argVal->getType()->isDoubleTy()) {
                        argVal = builder_->CreateCall(
                            getOrPanic("liva_f64_to_str"), {argVal}, "fmt.f64");
                        trackStringTemp(argVal);
                    } else if (argVal->getType()->isIntegerTy(1)) {
                        auto *ext = builder_->CreateZExt(argVal,
                            llvm::Type::getInt8Ty(*context_));
                        argVal = builder_->CreateCall(
                            getOrPanic("liva_bool_to_str"), {ext}, "fmt.bool");
                        trackStringTemp(argVal);
                    }
                    // ptr (string) → use directly
                    parts.push_back(argVal);
                }
                ++argIdx;
            }
            pos = bracePos + 2;
        }

        // Concatenate all parts with liva_str_concat
        if (parts.empty()) return fmtArg;
        llvm::Value *result = parts[0];
        auto *concatFn = getOrPanic("liva_str_concat");
        for (size_t i = 1; i < parts.size(); ++i) {
            result = builder_->CreateCall(concatFn, {result, parts[i]}, "fmt.concat");
            trackStringTemp(result);
        }
        return result;
    }

    // Handle math built-ins: abs, min, max, sqrt, pow, floor, ceil
    if (funcName == "abs" && !node->getArgs().empty()) {
        auto *x = visit(node->getArgs()[0].get());
        if (!x) return nullptr;
        if (x->getType()->isDoubleTy()) {
            auto *fn = llvm::Intrinsic::getOrInsertDeclaration(
                module_.get(), llvm::Intrinsic::fabs, {llvm::Type::getDoubleTy(*context_)});
            return builder_->CreateCall(fn, {x}, "fabstmp");
        }
        // Integer abs: select (x < 0), -x, x
        auto *zero = llvm::ConstantInt::get(x->getType(), 0);
        auto *neg = builder_->CreateNeg(x, "negtmp");
        auto *cmp = builder_->CreateICmpSLT(x, zero, "abstmp");
        return builder_->CreateSelect(cmp, neg, x, "abs");
    }

    if (funcName == "min" && node->getArgs().size() >= 2) {
        auto *a = visit(node->getArgs()[0].get());
        auto *b = visit(node->getArgs()[1].get());
        if (!a || !b) return nullptr;
        if (a->getType()->isDoubleTy()) {
            auto *fn = llvm::Intrinsic::getOrInsertDeclaration(
                module_.get(), llvm::Intrinsic::minnum, {llvm::Type::getDoubleTy(*context_)});
            return builder_->CreateCall(fn, {a, b}, "mintmp");
        }
        auto *cmp = builder_->CreateICmpSLT(a, b, "mincmp");
        return builder_->CreateSelect(cmp, a, b, "min");
    }

    if (funcName == "max" && node->getArgs().size() >= 2) {
        auto *a = visit(node->getArgs()[0].get());
        auto *b = visit(node->getArgs()[1].get());
        if (!a || !b) return nullptr;
        if (a->getType()->isDoubleTy()) {
            auto *fn = llvm::Intrinsic::getOrInsertDeclaration(
                module_.get(), llvm::Intrinsic::maxnum, {llvm::Type::getDoubleTy(*context_)});
            return builder_->CreateCall(fn, {a, b}, "maxtmp");
        }
        auto *cmp = builder_->CreateICmpSGT(a, b, "maxcmp");
        return builder_->CreateSelect(cmp, a, b, "max");
    }

    if (funcName == "sqrt" && !node->getArgs().empty()) {
        auto *x = visit(node->getArgs()[0].get());
        if (!x) return nullptr;
        if (x->getType()->isIntegerTy())
            x = builder_->CreateSIToFP(x, llvm::Type::getDoubleTy(*context_), "tofp");
        auto *fn = llvm::Intrinsic::getOrInsertDeclaration(
            module_.get(), llvm::Intrinsic::sqrt, {llvm::Type::getDoubleTy(*context_)});
        return builder_->CreateCall(fn, {x}, "sqrttmp");
    }

    if (funcName == "pow" && node->getArgs().size() >= 2) {
        auto *x = visit(node->getArgs()[0].get());
        auto *y = visit(node->getArgs()[1].get());
        if (!x || !y) return nullptr;
        if (x->getType()->isIntegerTy())
            x = builder_->CreateSIToFP(x, llvm::Type::getDoubleTy(*context_), "tofp");
        if (y->getType()->isIntegerTy())
            y = builder_->CreateSIToFP(y, llvm::Type::getDoubleTy(*context_), "tofp");
        auto *fn = llvm::Intrinsic::getOrInsertDeclaration(
            module_.get(), llvm::Intrinsic::pow, {llvm::Type::getDoubleTy(*context_)});
        return builder_->CreateCall(fn, {x, y}, "powtmp");
    }

    if (funcName == "floor" && !node->getArgs().empty()) {
        auto *x = visit(node->getArgs()[0].get());
        if (!x) return nullptr;
        if (x->getType()->isIntegerTy())
            x = builder_->CreateSIToFP(x, llvm::Type::getDoubleTy(*context_), "tofp");
        auto *fn = llvm::Intrinsic::getOrInsertDeclaration(
            module_.get(), llvm::Intrinsic::floor, {llvm::Type::getDoubleTy(*context_)});
        return builder_->CreateCall(fn, {x}, "floortmp");
    }

    if (funcName == "ceil" && !node->getArgs().empty()) {
        auto *x = visit(node->getArgs()[0].get());
        if (!x) return nullptr;
        if (x->getType()->isIntegerTy())
            x = builder_->CreateSIToFP(x, llvm::Type::getDoubleTy(*context_), "tofp");
        auto *fn = llvm::Intrinsic::getOrInsertDeclaration(
            module_.get(), llvm::Intrinsic::ceil, {llvm::Type::getDoubleTy(*context_)});
        return builder_->CreateCall(fn, {x}, "ceiltmp");
    }

    if (funcName == "log" && !node->getArgs().empty()) {
        auto *x = visit(node->getArgs()[0].get());
        if (!x) return nullptr;
        if (x->getType()->isIntegerTy())
            x = builder_->CreateSIToFP(x, llvm::Type::getDoubleTy(*context_), "tofp");
        auto *fn = llvm::Intrinsic::getOrInsertDeclaration(
            module_.get(), llvm::Intrinsic::log, {llvm::Type::getDoubleTy(*context_)});
        return builder_->CreateCall(fn, {x}, "logtmp");
    }

    if (funcName == "log10" && !node->getArgs().empty()) {
        auto *x = visit(node->getArgs()[0].get());
        if (!x) return nullptr;
        if (x->getType()->isIntegerTy())
            x = builder_->CreateSIToFP(x, llvm::Type::getDoubleTy(*context_), "tofp");
        auto *fn = llvm::Intrinsic::getOrInsertDeclaration(
            module_.get(), llvm::Intrinsic::log10, {llvm::Type::getDoubleTy(*context_)});
        return builder_->CreateCall(fn, {x}, "log10tmp");
    }

    if (funcName == "sin" && !node->getArgs().empty()) {
        auto *x = visit(node->getArgs()[0].get());
        if (!x) return nullptr;
        if (x->getType()->isIntegerTy())
            x = builder_->CreateSIToFP(x, llvm::Type::getDoubleTy(*context_), "tofp");
        auto *fn = llvm::Intrinsic::getOrInsertDeclaration(
            module_.get(), llvm::Intrinsic::sin, {llvm::Type::getDoubleTy(*context_)});
        return builder_->CreateCall(fn, {x}, "sintmp");
    }

    if (funcName == "cos" && !node->getArgs().empty()) {
        auto *x = visit(node->getArgs()[0].get());
        if (!x) return nullptr;
        if (x->getType()->isIntegerTy())
            x = builder_->CreateSIToFP(x, llvm::Type::getDoubleTy(*context_), "tofp");
        auto *fn = llvm::Intrinsic::getOrInsertDeclaration(
            module_.get(), llvm::Intrinsic::cos, {llvm::Type::getDoubleTy(*context_)});
        return builder_->CreateCall(fn, {x}, "costmp");
    }

    if (funcName == "tan" && !node->getArgs().empty()) {
        auto *x = visit(node->getArgs()[0].get());
        if (!x) return nullptr;
        if (x->getType()->isIntegerTy())
            x = builder_->CreateSIToFP(x, llvm::Type::getDoubleTy(*context_), "tofp");
        auto *fn = llvm::Intrinsic::getOrInsertDeclaration(
            module_.get(), llvm::Intrinsic::tan, {llvm::Type::getDoubleTy(*context_)});
        return builder_->CreateCall(fn, {x}, "tantmp");
    }

    if (funcName == "round" && !node->getArgs().empty()) {
        auto *x = visit(node->getArgs()[0].get());
        if (!x) return nullptr;
        if (x->getType()->isIntegerTy())
            x = builder_->CreateSIToFP(x, llvm::Type::getDoubleTy(*context_), "tofp");
        auto *f64Ty = llvm::Type::getDoubleTy(*context_);
        if (node->getArgs().size() >= 2) {
            // round(x, digits): round(x * 10^d) / 10^d
            auto *d = visit(node->getArgs()[1].get());
            if (!d) return nullptr;
            if (d->getType()->isIntegerTy())
                d = builder_->CreateSIToFP(d, f64Ty, "tofp");
            auto *ten = llvm::ConstantFP::get(f64Ty, 10.0);
            auto *powFn = llvm::Intrinsic::getOrInsertDeclaration(
                module_.get(), llvm::Intrinsic::pow, {f64Ty});
            auto *factor = builder_->CreateCall(powFn, {ten, d}, "factor");
            auto *scaled = builder_->CreateFMul(x, factor, "scaled");
            auto *roundFn = llvm::Intrinsic::getOrInsertDeclaration(
                module_.get(), llvm::Intrinsic::round, {f64Ty});
            auto *rounded = builder_->CreateCall(roundFn, {scaled}, "rounded");
            return builder_->CreateFDiv(rounded, factor, "roundtmp");
        }
        // round(x): round to nearest integer
        auto *fn = llvm::Intrinsic::getOrInsertDeclaration(
            module_.get(), llvm::Intrinsic::round, {f64Ty});
        return builder_->CreateCall(fn, {x}, "roundtmp");
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

    // Handle print/println built-ins
    if (funcName == "print" || funcName == "println") {
        auto *printfFunc = module_->getFunction("printf");
        if (!printfFunc)
            return nullptr;

        if (node->getArgs().empty()) {
            if (funcName == "println") {
                auto *newline = builder_->CreateGlobalString("\n");
                return builder_->CreateCall(printfFunc, {newline});
            }
            return nullptr;
        }

        // Multi-arg: println(a, b, c) → print each with space separator, then newline
        llvm::Value *lastCall = nullptr;
        for (size_t i = 0; i < node->getArgs().size(); ++i) {
            auto *arg = visit(node->getArgs()[i].get());
            if (!arg) continue;

            // bool → "true"/"false" via runtime helper, printed as %s. Without
            // this, printf with %d on an i1 vararg reads garbage from the
            // calling-convention slot (varargs require at least i32).
            if (arg->getType()->isIntegerTy(1)) {
                auto *ext = builder_->CreateZExt(arg, llvm::Type::getInt8Ty(*context_));
                arg = builder_->CreateCall(getOrPanic("liva_bool_to_str"), {ext},
                                            "bool.str");
                trackStringTemp(arg);
            }

            std::string fmt;
            if (arg->getType()->isIntegerTy(32))
                fmt = "%d";
            else if (arg->getType()->isIntegerTy(64))
                fmt = "%lld";
            else if (arg->getType()->isFloatingPointTy())
                fmt = "%f";
            else if (arg->getType()->isPointerTy())
                fmt = "%s";
            else
                fmt = "%d";

            // Add space between args, newline at end for println
            if (i + 1 < node->getArgs().size())
                fmt += " ";
            else if (funcName == "println")
                fmt += "\n";

            auto *fmtStr = builder_->CreateGlobalString(fmt);
            lastCall = builder_->CreateCall(printfFunc, {fmtStr, arg});
        }
        return lastCall;
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
