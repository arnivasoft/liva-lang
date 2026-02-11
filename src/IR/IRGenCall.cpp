#include "liva/IR/IRGen.h"

#ifdef LIVA_HAS_LLVM

#include <llvm/IR/Intrinsics.h>

namespace liva {

llvm::Value *IRGen::visitCallExpr(CallExpr *node) {
    // Check for method call or enum case constructor: obj.method(args) / Shape.Circle(3.14)
    if (node->getCallee()->getKind() == ASTNode::NodeKind::MemberExpr) {
        auto *memberExpr = static_cast<MemberExpr *>(node->getCallee());
        const auto &methodName = memberExpr->getMember();

        // Result.ok(val) / Result.err(val) constructor
        if (memberExpr->getObject()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
            auto *ident = static_cast<IdentifierExpr *>(memberExpr->getObject());
            if (ident->getName() == "Result" && currentFuncResultInfo_ &&
                (methodName == "ok" || methodName == "err")) {
                if (!node->getArgs().empty()) {
                    auto *argVal = visit(node->getArgs()[0].get());
                    if (!argVal) return nullptr;
                    if (methodName == "ok")
                        return emitResultOk(currentFuncResultInfo_->okType,
                                            currentFuncResultInfo_->errType, argVal);
                    else
                        return emitResultErr(currentFuncResultInfo_->okType,
                                             currentFuncResultInfo_->errType, argVal);
                }
                return nullptr;
            }
        }

        // r.unwrap() method for Result types
        if (memberExpr->getObject()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
            auto *ident = static_cast<IdentifierExpr *>(memberExpr->getObject());
            if (methodName == "unwrap") {
                auto rtIt = varResultTypes_.find(ident->getName());
                if (rtIt != varResultTypes_.end()) {
                    auto nvIt = namedValues_.find(ident->getName());
                    if (nvIt == namedValues_.end()) return nullptr;
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
                    auto *panicFn = module_->getFunction("liva_panic");
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

                auto *openFn = module_->getFunction("liva_file_open");
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
            if (varFileTypes_.count(ident->getName())) {
                auto nvIt = namedValues_.find(ident->getName());
                if (nvIt == namedValues_.end()) return nullptr;
                auto *ptrTy = llvm::PointerType::getUnqual(*context_);
                auto *fp = builder_->CreateLoad(ptrTy, nvIt->second, "file.ptr");

                if (methodName == "readLine") {
                    auto *fn = module_->getFunction("liva_file_read_line");
                    auto *raw = builder_->CreateCall(fn, {fp}, "file.readline.raw");
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
                    auto *fn = module_->getFunction("liva_file_read_all");
                    return builder_->CreateCall(fn, {fp}, "file.readall");
                }

                if (methodName == "write" && !node->getArgs().empty()) {
                    auto *strVal = visit(node->getArgs()[0].get());
                    if (!strVal) return nullptr;
                    auto *fn = module_->getFunction("liva_file_write");
                    builder_->CreateCall(fn, {fp, strVal});
                    return nullptr;
                }

                if (methodName == "writeLine" && !node->getArgs().empty()) {
                    auto *strVal = visit(node->getArgs()[0].get());
                    if (!strVal) return nullptr;
                    auto *fn = module_->getFunction("liva_file_write_line");
                    builder_->CreateCall(fn, {fp, strVal});
                    return nullptr;
                }

                if (methodName == "close") {
                    auto *fn = module_->getFunction("liva_file_close");
                    builder_->CreateCall(fn, {fp});
                    return nullptr;
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
                auto *fn = module_->getFunction("liva_str_contains");
                auto *result = builder_->CreateCall(fn, {obj, arg}, "str.contains");
                return builder_->CreateTrunc(result, builder_->getInt1Ty(), "str.contains.bool");
            }

            if (methodName == "startsWith" && node->getArgs().size() >= 1) {
                auto *arg = visit(node->getArgs()[0].get());
                if (!arg) return nullptr;
                auto *fn = module_->getFunction("liva_str_starts_with");
                auto *result = builder_->CreateCall(fn, {obj, arg}, "str.startswith");
                return builder_->CreateTrunc(result, builder_->getInt1Ty(), "str.startswith.bool");
            }

            if (methodName == "endsWith" && node->getArgs().size() >= 1) {
                auto *arg = visit(node->getArgs()[0].get());
                if (!arg) return nullptr;
                auto *fn = module_->getFunction("liva_str_ends_with");
                auto *result = builder_->CreateCall(fn, {obj, arg}, "str.endswith");
                return builder_->CreateTrunc(result, builder_->getInt1Ty(), "str.endswith.bool");
            }

            if (methodName == "indexOf" && node->getArgs().size() >= 1) {
                auto *arg = visit(node->getArgs()[0].get());
                if (!arg) return nullptr;
                auto *fn = module_->getFunction("liva_str_index_of");
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
                auto *fn = module_->getFunction("liva_str_substring");
                return builder_->CreateCall(fn, {obj, start, length}, "str.substring");
            }

            if (methodName == "trim") {
                auto *fn = module_->getFunction("liva_str_trim");
                return builder_->CreateCall(fn, {obj}, "str.trim");
            }

            if (methodName == "toUpper") {
                auto *fn = module_->getFunction("liva_str_to_upper");
                return builder_->CreateCall(fn, {obj}, "str.toupper");
            }

            if (methodName == "toLower") {
                auto *fn = module_->getFunction("liva_str_to_lower");
                return builder_->CreateCall(fn, {obj}, "str.tolower");
            }

            if (methodName == "replace" && node->getArgs().size() >= 2) {
                auto *oldSub = visit(node->getArgs()[0].get());
                auto *newSub = visit(node->getArgs()[1].get());
                if (!oldSub || !newSub) return nullptr;
                auto *fn = module_->getFunction("liva_str_replace");
                return builder_->CreateCall(fn, {obj, oldSub, newSub}, "str.replace");
            }

            if (methodName == "split" && node->getArgs().size() >= 1) {
                auto *delim = visit(node->getArgs()[0].get());
                if (!delim) return nullptr;
                auto *curFunc = builder_->GetInsertBlock()->getParent();
                // count output parameter
                auto *countAlloca = createEntryBlockAlloca(curFunc, "split.count", builder_->getInt64Ty());
                builder_->CreateStore(builder_->getInt64(0), countAlloca);
                auto *fn = module_->getFunction("liva_str_split");
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
            auto daIt = varDynArrayTypes_.find(ident->getName());
            if (daIt != varDynArrayTypes_.end()) {
                auto allocaIt = namedValues_.find(ident->getName());
                if (allocaIt == namedValues_.end()) return nullptr;
                auto *arrAlloca = allocaIt->second;
                auto *structTy = getDynArrayStructTy();

                if (methodName == "push" && !node->getArgs().empty()) {
                    auto *val = visit(node->getArgs()[0].get());
                    if (!val) return nullptr;
                    auto *func = builder_->GetInsertBlock()->getParent();
                    auto *elemAlloca = createEntryBlockAlloca(func, "push.tmp",
                                                              daIt->second.elementType);
                    builder_->CreateStore(val, elemAlloca);

                    auto *dataField = builder_->CreateStructGEP(structTy, arrAlloca, 0);
                    auto *lenField = builder_->CreateStructGEP(structTy, arrAlloca, 1);
                    auto *capField = builder_->CreateStructGEP(structTy, arrAlloca, 2);

                    auto *pushFn = module_->getFunction("liva_array_push");
                    builder_->CreateCall(pushFn, {dataField, lenField, capField,
                                                   elemAlloca,
                                                   builder_->getInt64(daIt->second.elemSize)});
                    return nullptr;
                }

                if (methodName == "pop") {
                    auto *lenField = builder_->CreateStructGEP(structTy, arrAlloca, 1);
                    auto *popFn = module_->getFunction("liva_array_pop");
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
                    auto *fn = module_->getFunction("liva_array_contains");
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
                    auto *fn = module_->getFunction("liva_array_index_of");
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
                    auto *fn = module_->getFunction("liva_array_reverse");
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
                    if (!closureVal) return nullptr;

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
                        auto *newFn = module_->getFunction("liva_array_new");
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

        // Map method: m.insert(k,v), m.get(k), m.contains(k), m.remove(k)
        if (memberExpr->getObject()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
            auto *ident = static_cast<IdentifierExpr *>(memberExpr->getObject());
            auto mapIt = varMapTypes_.find(ident->getName());
            if (mapIt != varMapTypes_.end()) {
                auto allocaIt = namedValues_.find(ident->getName());
                if (allocaIt == namedValues_.end()) return nullptr;
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

                    auto *insertFn = module_->getFunction("liva_map_insert");
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

                    auto *getFn = module_->getFunction("liva_map_get");
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

                    auto *containsFn = module_->getFunction("liva_map_contains");
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

                    auto *removeFn = module_->getFunction("liva_map_remove");
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
            auto setIt = varSetTypes_.find(ident->getName());
            if (setIt != varSetTypes_.end()) {
                auto allocaIt = namedValues_.find(ident->getName());
                if (allocaIt == namedValues_.end()) return nullptr;
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

                    auto *insertFn = module_->getFunction("liva_set_insert");
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

                    auto *containsFn = module_->getFunction("liva_set_contains");
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

                    auto *removeFn = module_->getFunction("liva_set_remove");
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
            auto ptIt = varProtocolTypes_.find(ident->getName());
            if (ptIt != varProtocolTypes_.end()) {
                const std::string &protocolName = ptIt->second;
                auto miIt = protocolMethodIndices_.find(protocolName);
                if (miIt != protocolMethodIndices_.end()) {
                    auto idxIt = miIt->second.find(methodName);
                    if (idxIt != miIt->second.end()) {
                        int methodIdx = idxIt->second;
                        auto *traitTy = getTraitObjectTy();
                        auto nvIt = namedValues_.find(ident->getName());
                        if (nvIt != namedValues_.end()) {
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
            auto it = namedValues_.find(objName);
            if (it != namedValues_.end())
                objAlloca = it->second;
            auto stIt = varStructTypes_.find(objName);
            if (stIt != varStructTypes_.end())
                structTypeName = stIt->second;
            // Also check enum types for enum method calls
            if (structTypeName.empty()) {
                auto enIt = varEnumTypes_.find(objName);
                if (enIt != varEnumTypes_.end())
                    structTypeName = enIt->second;
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
                                    callee = monomorphizeMethod(implDecl, m.get(),
                                                                 structTypeName, staIt->second);
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
        return nullptr;
    }

    // Get function name
    std::string funcName;
    if (node->getCallee()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
        auto *ident = static_cast<IdentifierExpr *>(node->getCallee());
        funcName = ident->getName();
    }

    // Handle len() built-in
    if (funcName == "len") {
        if (!node->getArgs().empty()) {
            auto *arg = visit(node->getArgs()[0].get());
            if (!arg) return nullptr;
            auto *lenFn = module_->getFunction("liva_str_length");
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
                return builder_->CreateCall(module_->getFunction("liva_i32_to_str"), {arg});
            } else if (arg->getType()->isDoubleTy()) {
                return builder_->CreateCall(module_->getFunction("liva_f64_to_str"), {arg});
            } else if (arg->getType()->isIntegerTy(1)) {
                auto *ext = builder_->CreateZExt(arg, llvm::Type::getInt8Ty(*context_));
                return builder_->CreateCall(module_->getFunction("liva_bool_to_str"), {ext});
            } else if (arg->getType()->isPointerTy()) {
                return arg; // already a string
            }
            return arg;
        }
        return nullptr;
    }

    // Handle parseInt/parseInt64/parseFloat built-ins → Optional<T>
    if (funcName == "parseInt" && !node->getArgs().empty()) {
        auto *strArg = visit(node->getArgs()[0].get());
        if (!strArg) return nullptr;
        auto *curFunc = builder_->GetInsertBlock()->getParent();
        auto *resultAlloca = createEntryBlockAlloca(curFunc, "parse.tmp", builder_->getInt32Ty());
        auto *fn = module_->getFunction("liva_str_parse_i32");
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
        auto *fn = module_->getFunction("liva_str_parse_i64");
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
        auto *fn = module_->getFunction("liva_str_parse_f64");
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

    // Handle readLine() built-in
    if (funcName == "readLine") {
        auto *fn = module_->getFunction("liva_read_line");
        return builder_->CreateCall(fn, {}, "readline");
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
                            module_->getFunction("liva_i32_to_str"), {argVal}, "fmt.i32");
                    } else if (argVal->getType()->isIntegerTy(64)) {
                        argVal = builder_->CreateCall(
                            module_->getFunction("liva_i64_to_str"), {argVal}, "fmt.i64");
                    } else if (argVal->getType()->isDoubleTy()) {
                        argVal = builder_->CreateCall(
                            module_->getFunction("liva_f64_to_str"), {argVal}, "fmt.f64");
                    } else if (argVal->getType()->isIntegerTy(1)) {
                        auto *ext = builder_->CreateZExt(argVal,
                            llvm::Type::getInt8Ty(*context_));
                        argVal = builder_->CreateCall(
                            module_->getFunction("liva_bool_to_str"), {ext}, "fmt.bool");
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
        auto *concatFn = module_->getFunction("liva_str_concat");
        for (size_t i = 1; i < parts.size(); ++i) {
            result = builder_->CreateCall(concatFn, {result, parts[i]}, "fmt.concat");
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

            std::string fmt;
            if (arg->getType()->isIntegerTy(32))
                fmt = "%d";
            else if (arg->getType()->isIntegerTy(64))
                fmt = "%lld";
            else if (arg->getType()->isFloatingPointTy())
                fmt = "%f";
            else if (arg->getType()->isPointerTy())
                fmt = "%s";
            else if (arg->getType()->isIntegerTy(1))
                fmt = "%d";
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
    auto funcIt = varFuncTypes_.find(funcName);
    if (funcIt != varFuncTypes_.end()) {
        auto namedIt = namedValues_.find(funcName);
        if (namedIt != namedValues_.end()) {
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
            if (!callee) return nullptr;
            if (callee->getReturnType()->isVoidTy())
                return builder_->CreateCall(callee, argValues);
            return builder_->CreateCall(callee, argValues, "calltmp");
        }
        return nullptr;
    }

    std::vector<llvm::Value *> args;
    for (auto &arg : node->getArgs()) {
        auto *val = visit(arg.get());
        if (!val)
            return nullptr;
        args.push_back(val);
    }

    // Fill in default arguments for missing params
    if (args.size() < callee->arg_size()) {
        auto fdIt = funcDecls_.find(funcName);
        if (fdIt != funcDecls_.end()) {
            const auto &params = fdIt->second->getParams();
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
            auto it = namedValues_.find(objName);
            if (it != namedValues_.end())
                objAlloca = it->second;
            auto stIt = varStructTypes_.find(objName);
            if (stIt != varStructTypes_.end())
                structTypeName = stIt->second;
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
                    builder_->CreateStore(val, gep);
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

            // Dynamic array index assign: arr[i] = val
            auto daIt = varDynArrayTypes_.find(ident->getName());
            if (daIt != varDynArrayTypes_.end()) {
                auto allocaIt = namedValues_.find(ident->getName());
                if (allocaIt != namedValues_.end()) {
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
                }
                return val;
            }

            auto arrIt = varArrayTypes_.find(ident->getName());
            auto allocaIt = namedValues_.find(ident->getName());
            if (arrIt != varArrayTypes_.end() && allocaIt != namedValues_.end()) {
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
            }
        }
        return val;
    }

    if (node->getTarget()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
        auto *ident = static_cast<IdentifierExpr *>(node->getTarget());
        auto it = namedValues_.find(ident->getName());

        // Handle optional variable assignment: x = 42, x = nil
        auto optIt = varOptionalTypes_.find(ident->getName());
        if (optIt != varOptionalTypes_.end() && it != namedValues_.end()) {
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
        auto refIt = varRefTypes_.find(ident->getName());
        if (refIt != varRefTypes_.end() && it != namedValues_.end()) {
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

        if (it != namedValues_.end()) {
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
            builder_->CreateStore(val, it->second);
        }
    }

    return val;
}

llvm::Value *IRGen::visitMemberExpr(MemberExpr *node) {
    // Optional chaining: obj?.field
    if (node->isOptionalChain())
        return emitOptionalChainMember(node);

    // Tuple element access: pair.0, pair.1
    if (node->getObject()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
        auto *ident = static_cast<IdentifierExpr *>(node->getObject());
        auto tupleIt = varTupleTypes_.find(ident->getName());
        if (tupleIt != varTupleTypes_.end()) {
            const auto &member = node->getMember();
            bool isNumeric = !member.empty();
            for (char c : member) {
                if (c < '0' || c > '9') { isNumeric = false; break; }
            }
            if (isNumeric) {
                unsigned idx = (unsigned)strtol(member.c_str(), nullptr, 10);
                auto *baseAlloca = namedValues_[ident->getName()];
                auto &ti = tupleIt->second;
                auto *tupleTy = llvm::StructType::get(*context_, ti.elementTypes);
                auto *gep = builder_->CreateStructGEP(tupleTy, baseAlloca, idx);
                return builder_->CreateLoad(ti.elementTypes[idx], gep, "tuple.elem");
            }
        }
    }

    // Check for string.length
    if (node->getObject()->getResolvedType() &&
        node->getObject()->getResolvedType()->getKind() == TypeRepr::Kind::String &&
        node->getMember() == "length") {
        auto *obj = visit(node->getObject());
        if (!obj) return nullptr;
        auto *lenFn = module_->getFunction("liva_str_length");
        return builder_->CreateCall(lenFn, {obj});
    }

    // Dynamic array properties: arr.length, arr.capacity, arr.isEmpty
    if (node->getObject()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
        auto *ident = static_cast<IdentifierExpr *>(node->getObject());
        auto daIt = varDynArrayTypes_.find(ident->getName());
        if (daIt != varDynArrayTypes_.end()) {
            auto allocaIt = namedValues_.find(ident->getName());
            if (allocaIt != namedValues_.end()) {
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

    // Map/Set properties: m.size, m.isEmpty
    if (node->getObject()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
        auto *ident = static_cast<IdentifierExpr *>(node->getObject());
        auto mapIt = varMapTypes_.find(ident->getName());
        if (mapIt != varMapTypes_.end()) {
            auto allocaIt = namedValues_.find(ident->getName());
            if (allocaIt != namedValues_.end()) {
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
        auto setIt = varSetTypes_.find(ident->getName());
        if (setIt != varSetTypes_.end()) {
            auto allocaIt = namedValues_.find(ident->getName());
            if (allocaIt != namedValues_.end()) {
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
        auto rtIt = varResultTypes_.find(ident->getName());
        if (rtIt != varResultTypes_.end()) {
            auto nvIt = namedValues_.find(ident->getName());
            if (nvIt != namedValues_.end()) {
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
        auto it = namedValues_.find(objName);
        if (it != namedValues_.end())
            objAlloca = it->second;
        auto stIt = varStructTypes_.find(objName);
        if (stIt != varStructTypes_.end())
            structTypeName = stIt->second;
    }

    if (!objAlloca || structTypeName.empty())
        return nullptr;

    auto stIt = structTypes_.find(structTypeName);
    if (stIt == structTypes_.end())
        return nullptr;

    auto *structTy = stIt->second;
    int idx = getStructFieldIndex(structTypeName, node->getMember());
    if (idx < 0)
        return nullptr;

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

        auto typeArgs = inferStructTypeArgs(gsIt->second, node->getFields(), fieldValues);
        monomorphizeStruct(gsIt->second, typeArgs);
        std::string mangledName = mangleGenericStruct(typeName, typeArgs);

        auto *structTy = structTypes_[mangledName];
        auto *func = builder_->GetInsertBlock()->getParent();
        auto *alloca = createEntryBlockAlloca(func, mangledName + ".tmp", structTy);

        for (size_t i = 0; i < node->getFields().size(); ++i) {
            int idx = getStructFieldIndex(mangledName, node->getFields()[i].name);
            if (idx < 0 || !fieldValues[i])
                continue;
            auto *gep = builder_->CreateStructGEP(structTy, alloca, idx, node->getFields()[i].name);
            builder_->CreateStore(fieldValues[i], gep);
        }

        return builder_->CreateLoad(structTy, alloca, mangledName + ".val");
    }

    auto stIt = structTypes_.find(typeName);
    if (stIt == structTypes_.end())
        return nullptr;

    auto *structTy = stIt->second;
    auto *func = builder_->GetInsertBlock()->getParent();
    auto *alloca = createEntryBlockAlloca(func, typeName + ".tmp", structTy);

    for (auto &fieldInit : node->getFields()) {
        int idx = getStructFieldIndex(typeName, fieldInit.name);
        if (idx < 0)
            continue;
        auto *val = visit(fieldInit.value.get());
        if (!val)
            continue;
        auto *gep = builder_->CreateStructGEP(structTy, alloca, idx, fieldInit.name);
        builder_->CreateStore(val, gep);
    }

    return builder_->CreateLoad(structTy, alloca, typeName + ".val");
}

llvm::Value *IRGen::emitEnumCaseConstruct(const std::string &enumName,
                                           const std::string &caseName, int tag,
                                           const std::vector<std::unique_ptr<Expr>> &args) {
    auto etIt = enumTypes_.find(enumName);
    if (etIt == enumTypes_.end())
        return nullptr;

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
    auto *func = builder_->GetInsertBlock()->getParent();

    // Find enum type name from subject (if it's an identifier)
    std::string enumTypeName;
    std::string subjectVarName;
    bool isResultMatch = false;
    if (node->getSubject()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
        auto *ident = static_cast<const IdentifierExpr *>(node->getSubject());
        subjectVarName = ident->getName();
        auto it = varEnumTypes_.find(subjectVarName);
        if (it != varEnumTypes_.end())
            enumTypeName = it->second;

        // Check for Result type match
        auto rtIt = varResultTypes_.find(subjectVarName);
        if (rtIt != varResultTypes_.end()) {
            isResultMatch = true;
            enumTypeName = "Result";
            // Set up temporary enum infrastructure for Result
            enumCases_["Result"] = {{"Ok", 0}, {"Err", 1}};
            auto *resTy = getResultType(rtIt->second.okType, rtIt->second.errType);
            enumTypes_["Result"] = resTy;
            enumCasePayloads_["Result"]["Ok"] = {rtIt->second.okType};
            enumCasePayloads_["Result"]["Err"] = {rtIt->second.errType};
            varEnumTypes_[subjectVarName] = "Result";
        }
    }

    // Determine if this is a payload enum match
    bool isPayloadEnum = !enumTypeName.empty() &&
                          enumTypes_.find(enumTypeName) != enumTypes_.end();

    llvm::Value *tagVal = nullptr;
    llvm::AllocaInst *subjectAlloca = nullptr;

    if (isPayloadEnum) {
        // Payload enum: load tag from struct field 0
        auto nIt = namedValues_.find(subjectVarName);
        if (nIt == namedValues_.end())
            return nullptr;
        subjectAlloca = nIt->second;
        auto *enumStructTy = enumTypes_[enumTypeName];
        auto *tagPtr = builder_->CreateStructGEP(enumStructTy, subjectAlloca, 0, "tag.ptr");
        tagVal = builder_->CreateLoad(builder_->getInt32Ty(), tagPtr, "tag");
    } else {
        // Simple enum or integer: evaluate subject directly
        tagVal = visit(const_cast<Expr *>(node->getSubject()));
        if (!tagVal)
            return nullptr;
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

    auto *switchInst = builder_->CreateSwitch(tagVal, defaultBB, numCases);

    for (auto &info : armInfos) {
        if (!info.pat.isWildcard && info.pat.tag >= 0) {
            switchInst->addCase(builder_->getInt32(info.pat.tag), info.bb);
        }
    }

    // Generate arm bodies with binding extraction
    for (size_t i = 0; i < node->getArms().size(); ++i) {
        auto &arm = node->getArms()[i];
        auto &info = armInfos[i];
        builder_->SetInsertPoint(info.bb);

        // Save named values for binding scope
        auto savedNamedValues = namedValues_;

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
                    "bind." + info.pat.bindings[b]);
                auto *val = builder_->CreateLoad(payloadTypes[b], fieldPtr,
                                                  info.pat.bindings[b]);
                // Create alloca for binding
                auto *bindAlloca = createEntryBlockAlloca(func, info.pat.bindings[b],
                                                           payloadTypes[b]);
                builder_->CreateStore(val, bindAlloca);
                namedValues_[info.pat.bindings[b]] = bindAlloca;
                offset += dl.getTypeAllocSize(payloadTypes[b]);
            }
        }

        visit(arm.body.get());
        if (!builder_->GetInsertBlock()->getTerminator())
            builder_->CreateBr(mergeBB);

        // Restore named values
        namedValues_ = savedNamedValues;
    }

    builder_->SetInsertPoint(mergeBB);

    // Clean up temporary Result enum entries
    if (isResultMatch) {
        enumCases_.erase("Result");
        enumTypes_.erase("Result");
        enumCasePayloads_.erase("Result");
        varEnumTypes_.erase(subjectVarName);
    }

    return nullptr;
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
            // Extract bindings between ( and )
            auto closePos = rest.find(')', parenPos);
            if (closePos != std::string::npos) {
                auto bindingStr = rest.substr(parenPos + 1, closePos - parenPos - 1);
                // Split by comma
                size_t start = 0;
                while (start < bindingStr.size()) {
                    auto commaPos = bindingStr.find(',', start);
                    std::string binding;
                    if (commaPos == std::string::npos) {
                        binding = bindingStr.substr(start);
                        start = bindingStr.size();
                    } else {
                        binding = bindingStr.substr(start, commaPos - start);
                        start = commaPos + 1;
                    }
                    // Trim whitespace
                    size_t b = binding.find_first_not_of(" \t");
                    size_t e = binding.find_last_not_of(" \t");
                    if (b != std::string::npos)
                        info.bindings.push_back(binding.substr(b, e - b + 1));
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
            }
        }
    }

    return info;
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
    auto *panicFn = module_->getFunction("liva_panic");
    auto *msg = builder_->CreateGlobalString("index out of bounds");
    builder_->CreateCall(panicFn, {msg});
    builder_->CreateUnreachable();
    builder_->SetInsertPoint(okBB);
}


} // namespace liva

#endif // LIVA_HAS_LLVM
