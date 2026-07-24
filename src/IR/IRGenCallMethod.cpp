#include "liva/IR/IRGen.h"

#ifdef LIVA_HAS_LLVM

namespace liva {

std::optional<llvm::Value *> IRGen::tryEmitMethodCall(CallExpr *node) {
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
                    return emitDynArrayPopValue(arrAlloca, daIt->second.elementType);
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
                    // Arrays own COPIES of their string elements — same rule
                    // as the local-variable push path above. Storing the raw
                    // pointer dangled as soon as the pushed local/temp string
                    // was freed at scope exit (use-after-free; part of the
                    // intermittent Cli ctest flake family).
                    if (val->getType()->isPointerTy()) {
                        if (daInfo->elementType->isPointerTy()) {
                            val = builder_->CreateCall(getOrPanic("liva_str_dup"),
                                                        {val}, "mpush.dup");
                        } else {
                            removeFromTempStrings(val);
                        }
                    }
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
                    return emitDynArrayPopValue(arrGEP, daInfo->elementType);
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

                // Widen a visited key/value to the slot's expected integer type. Int
                // literals are strictly i32 in Liva; Map<_, i64> slots would otherwise
                // receive a 4-byte store into an 8-byte alloca (upper half undefined).
                auto widenToSlot = [&](llvm::Value *v, llvm::Type *slotTy) -> llvm::Value * {
                    if (v && slotTy && v->getType() != slotTy &&
                        v->getType()->isIntegerTy() && slotTy->isIntegerTy() &&
                        v->getType()->getIntegerBitWidth() < slotTy->getIntegerBitWidth())
                        return builder_->CreateSExt(v, slotTy, "map.widen");
                    return v;
                };

                if (methodName == "insert" && node->getArgs().size() >= 2) {
                    auto *keyVal = visit(node->getArgs()[0].get());
                    auto *valVal = visit(node->getArgs()[1].get());
                    if (!keyVal || !valVal) return nullptr;
                    keyVal = widenToSlot(keyVal, info.keyType);
                    valVal = widenToSlot(valVal, info.valType);

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
                    keyVal = widenToSlot(keyVal, info.keyType);

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
                    keyVal = widenToSlot(keyVal, info.keyType);

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
                    keyVal = widenToSlot(keyVal, info.keyType);

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

                if (methodName == "size" && node->getArgs().empty()) {
                    auto *sizeField = builder_->CreateStructGEP(structTy, mapAlloca, 1);
                    return builder_->CreateLoad(builder_->getInt64Ty(), sizeField, "map.size");
                }
                if (methodName == "isEmpty" && node->getArgs().empty()) {
                    auto *sizeField = builder_->CreateStructGEP(structTy, mapAlloca, 1);
                    auto *sz = builder_->CreateLoad(builder_->getInt64Ty(), sizeField, "map.size");
                    return builder_->CreateICmpEQ(sz, builder_->getInt64(0), "map.isempty");
                }
                if (methodName == "clear" && node->getArgs().empty()) {
                    int64_t stride = 9 + (int64_t)info.keySize + (int64_t)info.valSize;
                    auto *entriesField = builder_->CreateStructGEP(structTy, mapAlloca, 0);
                    auto *entries = builder_->CreateLoad(
                        llvm::PointerType::getUnqual(*context_), entriesField, "map.entries");
                    auto *capField = builder_->CreateStructGEP(structTy, mapAlloca, 2);
                    auto *cap = builder_->CreateLoad(builder_->getInt64Ty(), capField, "map.cap");
                    auto *sizeField = builder_->CreateStructGEP(structTy, mapAlloca, 1);
                    builder_->CreateCall(getOrPanic("liva_map_clear"),
                        {entries, cap, builder_->getInt64(stride), sizeField});
                    return nullptr;
                }

                // keys() -> [K] / values() -> [V]: build a fresh DynArray by
                // walking occupied entries (state == 1), pushing each key/value
                // via liva_array_push. Starts as {null,0,0} — liva_array_push
                // grows from null (realloc(NULL) == malloc), same as the
                // filter/strSplit patterns. Returned as a struct VALUE so the
                // annotated-var path in visitVarDecl registers it as DynArray.
                if ((methodName == "keys" || methodName == "values") &&
                    node->getArgs().empty()) {
                    bool wantKeys = (methodName == "keys");
                    auto *elemType = wantKeys ? info.keyType : info.valType;
                    uint64_t elemSize = wantKeys ? info.keySize : info.valSize;
                    int64_t elemOffset = wantKeys ? 9 : 9 + (int64_t)info.keySize;
                    int64_t stride = 9 + (int64_t)info.keySize + (int64_t)info.valSize;

                    auto *arrTy = getDynArrayStructTy();
                    auto *resultAlloca = createEntryBlockAlloca(curFunc, "map.kv.arr", arrTy);
                    auto *rDataField = builder_->CreateStructGEP(arrTy, resultAlloca, 0);
                    builder_->CreateStore(
                        llvm::ConstantPointerNull::get(builder_->getPtrTy()), rDataField);
                    auto *rLenField = builder_->CreateStructGEP(arrTy, resultAlloca, 1);
                    builder_->CreateStore(builder_->getInt64(0), rLenField);
                    auto *rCapField = builder_->CreateStructGEP(arrTy, resultAlloca, 2);
                    builder_->CreateStore(builder_->getInt64(0), rCapField);

                    auto *entriesField = builder_->CreateStructGEP(structTy, mapAlloca, 0);
                    auto *entriesPtr = builder_->CreateLoad(
                        builder_->getPtrTy(), entriesField, "map.entries");
                    auto *capField = builder_->CreateStructGEP(structTy, mapAlloca, 2);
                    auto *cap = builder_->CreateLoad(builder_->getInt64Ty(), capField, "map.cap");

                    auto *elemTmp = createEntryBlockAlloca(curFunc, "map.kv.elem", elemType);
                    auto *idxVar = createEntryBlockAlloca(curFunc, "map.kv.idx",
                        builder_->getInt64Ty());
                    builder_->CreateStore(builder_->getInt64(0), idxVar);

                    auto *condBB = llvm::BasicBlock::Create(*context_, "map.kv.cond", curFunc);
                    auto *bodyBB = llvm::BasicBlock::Create(*context_, "map.kv.body", curFunc);
                    auto *processBB = llvm::BasicBlock::Create(*context_, "map.kv.take", curFunc);
                    auto *latchBB = llvm::BasicBlock::Create(*context_, "map.kv.latch", curFunc);
                    auto *exitBB = llvm::BasicBlock::Create(*context_, "map.kv.exit", curFunc);
                    builder_->CreateBr(condBB);

                    // Cond: idx < capacity
                    builder_->SetInsertPoint(condBB);
                    auto *idx = builder_->CreateLoad(builder_->getInt64Ty(), idxVar, "map.kv.idx");
                    auto *cond = builder_->CreateICmpSLT(idx, cap, "map.kv.cmp");
                    builder_->CreateCondBr(cond, bodyBB, exitBB);

                    // Body: only occupied entries (state == 1; skips empty 0
                    // and tombstone 2)
                    builder_->SetInsertPoint(bodyBB);
                    auto *offset = builder_->CreateMul(idx, builder_->getInt64(stride),
                        "map.kv.offset");
                    auto *entryPtr = builder_->CreateGEP(builder_->getInt8Ty(), entriesPtr,
                        offset, "map.kv.entry");
                    auto *state = builder_->CreateLoad(builder_->getInt8Ty(), entryPtr,
                        "map.kv.state");
                    auto *isOccupied = builder_->CreateICmpEQ(state, builder_->getInt8(1),
                        "map.kv.occupied");
                    builder_->CreateCondBr(isOccupied, processBB, latchBB);

                    // Take: load element, push into result array
                    builder_->SetInsertPoint(processBB);
                    auto *elemRaw = builder_->CreateGEP(builder_->getInt8Ty(), entryPtr,
                        builder_->getInt64(elemOffset), "map.kv.elem.raw");
                    auto *elem = builder_->CreateLoad(elemType, elemRaw, "map.kv.elem.val");
                    builder_->CreateStore(elem, elemTmp);
                    builder_->CreateCall(getOrPanic("liva_array_push"),
                        {rDataField, rLenField, rCapField, elemTmp,
                         builder_->getInt64(elemSize)});
                    builder_->CreateBr(latchBB);

                    // Latch: idx++
                    builder_->SetInsertPoint(latchBB);
                    auto *curIdx = builder_->CreateLoad(builder_->getInt64Ty(), idxVar);
                    auto *nextIdx = builder_->CreateAdd(curIdx, builder_->getInt64(1),
                        "map.kv.inc");
                    builder_->CreateStore(nextIdx, idxVar);
                    builder_->CreateBr(condBB);

                    // Exit: return the DynArray by VALUE (24-byte struct load),
                    // matching filter/strSplit.
                    builder_->SetInsertPoint(exitBB);
                    return builder_->CreateLoad(arrTy, resultAlloca, "map.kv.result");
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

                // Widen a visited element to the slot's expected integer type. Int
                // literals are strictly i32 in Liva; Set<i64> slots would otherwise
                // receive a 4-byte store into an 8-byte alloca (upper half undefined).
                auto widenToSlot = [&](llvm::Value *v, llvm::Type *slotTy) -> llvm::Value * {
                    if (v && slotTy && v->getType() != slotTy &&
                        v->getType()->isIntegerTy() && slotTy->isIntegerTy() &&
                        v->getType()->getIntegerBitWidth() < slotTy->getIntegerBitWidth())
                        return builder_->CreateSExt(v, slotTy, "map.widen");
                    return v;
                };

                if (methodName == "insert" && !node->getArgs().empty()) {
                    auto *elemVal = visit(node->getArgs()[0].get());
                    if (!elemVal) return nullptr;
                    elemVal = widenToSlot(elemVal, info.elemType);

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
                    elemVal = widenToSlot(elemVal, info.elemType);

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
                    elemVal = widenToSlot(elemVal, info.elemType);

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

                if (methodName == "size" && node->getArgs().empty()) {
                    auto *sizeField = builder_->CreateStructGEP(structTy, setAlloca, 1);
                    return builder_->CreateLoad(builder_->getInt64Ty(), sizeField, "set.size");
                }
                if (methodName == "isEmpty" && node->getArgs().empty()) {
                    auto *sizeField = builder_->CreateStructGEP(structTy, setAlloca, 1);
                    auto *sz = builder_->CreateLoad(builder_->getInt64Ty(), sizeField, "set.size");
                    return builder_->CreateICmpEQ(sz, builder_->getInt64(0), "set.isempty");
                }
                if (methodName == "clear" && node->getArgs().empty()) {
                    int64_t stride = 9 + (int64_t)info.elemSize;
                    auto *entriesField = builder_->CreateStructGEP(structTy, setAlloca, 0);
                    auto *entries = builder_->CreateLoad(
                        llvm::PointerType::getUnqual(*context_), entriesField, "set.entries");
                    auto *capField = builder_->CreateStructGEP(structTy, setAlloca, 2);
                    auto *cap = builder_->CreateLoad(builder_->getInt64Ty(), capField, "set.cap");
                    auto *sizeField = builder_->CreateStructGEP(structTy, setAlloca, 1);
                    builder_->CreateCall(getOrPanic("liva_map_clear"),
                        {entries, cap, builder_->getInt64(stride), sizeField});
                    return nullptr;
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

        // Array element method call: arr[i].method() — IndexExpr base
        // e.g. rows[0].byName("name") where rows is [Row]
        if (!objAlloca && structTypeName.empty() &&
            memberExpr->getObject()->getKind() == ASTNode::NodeKind::IndexExpr) {
            auto *idxExpr = static_cast<IndexExpr *>(memberExpr->getObject());
            if (idxExpr->getBase()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
                auto *baseIdent = static_cast<const IdentifierExpr *>(idxExpr->getBase());
                auto daIt = vars_.varDynArrayTypes.find(baseIdent->getName());
                if (daIt != vars_.varDynArrayTypes.end()) {
                    // Find which struct type matches the element LLVM type
                    llvm::Type *elemTy = daIt->second.elementType;
                    for (auto &[sName, sLLVMTy] : structTypes_) {
                        if (sLLVMTy == elemTy) {
                            structTypeName = sName;
                            break;
                        }
                    }
                    if (!structTypeName.empty()) {
                        // Evaluate the index expression to get the struct value
                        auto *elemVal = visit(idxExpr);
                        if (elemVal) {
                            auto *func = builder_->GetInsertBlock()->getParent();
                            objAlloca = createEntryBlockAlloca(func, "arr.elem.tmp", elemTy);
                            builder_->CreateStore(elemVal, objAlloca);
                        }
                    }
                }
            }
        }

        // Chained subscript call: obj[k].method() / arr[i].method() where the
        // index expression resolves to a Named struct (e.g. JsonObject["k"] ->
        // JsonValue, then .asString()). The subscript value is materialised into
        // a temp alloca and the method dispatched on it. The TypeChecker sets the
        // IndexExpr's resolved type to the subscript method's return type.
        if (!objAlloca && structTypeName.empty() &&
            memberExpr->getObject()->getKind() == ASTNode::NodeKind::IndexExpr) {
            const TypeRepr *recvTy = memberExpr->getObject()->getResolvedType();
            if (recvTy && recvTy->getKind() == TypeRepr::Kind::Named) {
                auto *named = static_cast<const NamedTypeRepr *>(recvTy);
                const std::string &sName = named->getName();
                auto stIt = structTypes_.find(sName);
                if (stIt != structTypes_.end()) {
                    auto *structVal = visit(memberExpr->getObject());
                    if (structVal) {
                        auto *func = builder_->GetInsertBlock()->getParent();
                        objAlloca = createEntryBlockAlloca(func, "subchain.tmp", stIt->second);
                        builder_->CreateStore(structVal, objAlloca);
                        structTypeName = sName;
                    }
                }
            }
        }

        // Chained struct call: expr.method() where expr is a CallExpr returning
        // a Named struct. e.g. stmt.columnDate(i).year() — columnDate returns
        // Date, so we materialise the Date value in a temp alloca and dispatch
        // year() on it.  This mirrors the arr[i].method() pattern above.
        //
        // Also handles builder-style same-type chains: Url.parse(...).withQuery(...)
        // where Sema doesn't propagate the Named return type across modules (to avoid
        // ownership false-positives), but the LLVM return type IS the struct type.
        if (!objAlloca && structTypeName.empty() &&
            memberExpr->getObject()->getKind() == ASTNode::NodeKind::CallExpr) {
            const TypeRepr *recvTy = memberExpr->getObject()->getResolvedType();
            if (recvTy && recvTy->getKind() == TypeRepr::Kind::Named) {
                auto *named = static_cast<const NamedTypeRepr *>(recvTy);
                const std::string &sName = named->getName();
                auto stIt = structTypes_.find(sName);
                if (stIt != structTypes_.end()) {
                    auto *structVal = visit(memberExpr->getObject());
                    if (structVal) {
                        auto *func = builder_->GetInsertBlock()->getParent();
                        objAlloca = createEntryBlockAlloca(func, "chain.tmp", stIt->second);
                        builder_->CreateStore(structVal, objAlloca);
                        structTypeName = sName;
                    }
                }
            } else if (!recvTy) {
                // No Sema resolved type (e.g. same-type builder methods filtered
                // from cross-module registration). Try LLVM type inference: visit
                // the inner call, check if its return type matches a known struct.
                auto *structVal = visit(memberExpr->getObject());
                if (structVal && structVal->getType()->isStructTy()) {
                    auto *llvmStructTy = llvm::cast<llvm::StructType>(structVal->getType());
                    for (auto &[sName, sLLVMTy] : structTypes_) {
                        if (sLLVMTy == llvmStructTy) {
                            auto *func = builder_->GetInsertBlock()->getParent();
                            objAlloca = createEntryBlockAlloca(func, "chain.tmp", sLLVMTy);
                            builder_->CreateStore(structVal, objAlloca);
                            structTypeName = sName;
                            break;
                        }
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

                auto *calleeFTy = callee->getFunctionType();
                size_t calleeParamIdx = 1; // 0 is self
                for (auto &arg : node->getArgs()) {
                    auto *val = visit(arg.get());
                    if (!val)
                        return nullptr;
                    // Coerce i32 arg to i64 if callee expects i64. Zero-extend
                    // unsigned source kinds (u8/u16/u32) to preserve the value;
                    // sign-extend signed kinds (default for i32 literals).
                    if (calleeParamIdx < calleeFTy->getNumParams()) {
                        auto *expectedTy = calleeFTy->getParamType(calleeParamIdx);
                        if (val->getType()->isIntegerTy(32) && expectedTy->isIntegerTy(64)) {
                            const TypeRepr *argTy = arg->getResolvedType();
                            bool isUnsigned = argTy &&
                                (argTy->getKind() == TypeRepr::Kind::U8 ||
                                 argTy->getKind() == TypeRepr::Kind::U16 ||
                                 argTy->getKind() == TypeRepr::Kind::U32 ||
                                 argTy->getKind() == TypeRepr::Kind::U64);
                            val = isUnsigned
                                ? builder_->CreateZExt(val, builder_->getInt64Ty(), "mcall.zext")
                                : builder_->CreateSExt(val, builder_->getInt64Ty(), "mcall.sext");
                        }
                    }
                    args.push_back(val);
                    ++calleeParamIdx;
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
    return std::nullopt;   // unreachable (body always returns); kept as the helper-contract terminator
}

llvm::Value *IRGen::emitDynArrayPopValue(llvm::Value *arrPtr,
                                         llvm::Type *elemType) {
    auto *structTy = getDynArrayStructTy();
    auto *lenField = builder_->CreateStructGEP(structTy, arrPtr, 1, "pop.len.ptr");
    auto *len = builder_->CreateLoad(builder_->getInt64Ty(), lenField, "pop.len");
    auto *isEmpty = builder_->CreateICmpEQ(len, builder_->getInt64(0), "pop.empty");

    // The element load must be guarded: on an empty array `data` may still be
    // null (never allocated), so an unconditional data[len-1] load would fault.
    auto *func = builder_->GetInsertBlock()->getParent();
    auto *takeBB = llvm::BasicBlock::Create(*context_, "pop.take", func);
    auto *emptyBB = llvm::BasicBlock::Create(*context_, "pop.zero", func);
    auto *doneBB = llvm::BasicBlock::Create(*context_, "pop.done", func);
    builder_->CreateCondBr(isEmpty, emptyBB, takeBB);

    builder_->SetInsertPoint(takeBB);
    auto *dataField = builder_->CreateStructGEP(structTy, arrPtr, 0, "pop.data.ptr");
    auto *data = builder_->CreateLoad(
        llvm::PointerType::getUnqual(*context_), dataField, "pop.data");
    auto *idx = builder_->CreateSub(len, builder_->getInt64(1), "pop.idx");
    auto *elemPtr = builder_->CreateGEP(elemType, data, idx, "pop.elem.ptr");
    auto *elem = builder_->CreateLoad(elemType, elemPtr, "pop.elem");
    builder_->CreateCall(getOrPanic("liva_array_pop"), {lenField});
    builder_->CreateBr(doneBB);

    builder_->SetInsertPoint(emptyBB);
    builder_->CreateBr(doneBB);

    builder_->SetInsertPoint(doneBB);
    auto *phi = builder_->CreatePHI(elemType, 2, "pop.result");
    phi->addIncoming(elem, takeBB);
    phi->addIncoming(llvm::Constant::getNullValue(elemType), emptyBB);
    return phi;
}

} // namespace liva

#endif // LIVA_HAS_LLVM
