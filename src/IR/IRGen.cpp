#include "liva/IR/IRGen.h"

#ifdef LIVA_HAS_LLVM

#include <llvm/IR/Verifier.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/TargetParser/Host.h>
#include <llvm/TargetParser/Triple.h>
#include <fstream>
#include <set>

namespace liva {

IRGen::IRGen(const std::string &moduleName, DiagnosticsEngine &diag)
    : diag_(diag), context_(std::make_unique<llvm::LLVMContext>()),
      module_(std::make_unique<llvm::Module>(moduleName, *context_)),
      builder_(std::make_unique<llvm::IRBuilder<>>(*context_)) {
    module_->setTargetTriple(llvm::Triple(llvm::sys::getDefaultTargetTriple()));
}

bool IRGen::generate(TranslationUnit &tu) {
    createRuntimeDecls();

    for (auto &decl : tu.getDeclarations()) {
        // Skip generic functions (they are monomorphized at call sites)
        if (decl->getKind() == ASTNode::NodeKind::FuncDecl) {
            auto *funcDecl = static_cast<FuncDecl *>(decl.get());
            if (funcDecl->isGeneric()) {
                genericFuncDecls_[funcDecl->getName()] = funcDecl;
                continue;
            }
        }

        // Skip generic structs (they are monomorphized at usage sites)
        if (decl->getKind() == ASTNode::NodeKind::StructDecl) {
            auto *structDecl = static_cast<StructDecl *>(decl.get());
            if (structDecl->isGeneric()) {
                genericStructDecls_[structDecl->getName()] = structDecl;
                continue;
            }
        }

        // Skip generic impl blocks (methods are monomorphized at call sites)
        if (decl->getKind() == ASTNode::NodeKind::ImplDecl) {
            auto *implDecl = static_cast<ImplDecl *>(decl.get());
            if (implDecl->isGeneric()) {
                genericImplDecls_[implDecl->getTypeName()] = implDecl;
                continue;
            }
        }

        visit(decl.get());
        if (diag_.hasErrors())
            return false;
    }

    // Verify the module
    std::string errStr;
    llvm::raw_string_ostream errStream(errStr);
    if (llvm::verifyModule(*module_, &errStream)) {
        diag_.report(SourceLocation{}, DiagID::err_main_not_found);
        return false;
    }

    return true;
}

void IRGen::dump() { module_->print(llvm::errs(), nullptr); }

bool IRGen::writeToFile(const std::string &filename) {
    std::error_code ec;
    llvm::raw_fd_ostream file(filename, ec);
    if (ec)
        return false;
    module_->print(file, nullptr);
    return true;
}

void IRGen::createRuntimeDecls() {
    auto *i8PtrTy = llvm::PointerType::getUnqual(*context_);
    auto *i32Ty = builder_->getInt32Ty();
    auto *i64Ty = builder_->getInt64Ty();
    auto *i8Ty = builder_->getInt8Ty();
    auto *f64Ty = builder_->getDoubleTy();

    // printf declaration
    auto *printfType = llvm::FunctionType::get(i32Ty, {i8PtrTy}, true);
    module_->getOrInsertFunction("printf", printfType);

    // String runtime functions
    auto *concatTy = llvm::FunctionType::get(i8PtrTy, {i8PtrTy, i8PtrTy}, false);
    module_->getOrInsertFunction("liva_str_concat", concatTy);

    auto *equalTy = llvm::FunctionType::get(i32Ty, {i8PtrTy, i8PtrTy}, false);
    module_->getOrInsertFunction("liva_str_equal", equalTy);

    auto *lenTy = llvm::FunctionType::get(i64Ty, {i8PtrTy}, false);
    module_->getOrInsertFunction("liva_str_length", lenTy);

    auto *i32ToStrTy = llvm::FunctionType::get(i8PtrTy, {i32Ty}, false);
    module_->getOrInsertFunction("liva_i32_to_str", i32ToStrTy);

    auto *f64ToStrTy = llvm::FunctionType::get(i8PtrTy, {f64Ty}, false);
    module_->getOrInsertFunction("liva_f64_to_str", f64ToStrTy);

    auto *boolToStrTy = llvm::FunctionType::get(i8PtrTy, {i8Ty}, false);
    module_->getOrInsertFunction("liva_bool_to_str", boolToStrTy);

    // Dynamic array runtime
    auto *arrayNewTy = llvm::FunctionType::get(i8PtrTy, {i64Ty, i64Ty}, false);
    module_->getOrInsertFunction("liva_array_new", arrayNewTy);

    auto *arrayFreeTy = llvm::FunctionType::get(builder_->getVoidTy(), {i8PtrTy}, false);
    module_->getOrInsertFunction("liva_array_free", arrayFreeTy);

    // liva_array_push(ptr*, i64*, i64*, ptr, i64)
    auto *arrayPushTy = llvm::FunctionType::get(builder_->getVoidTy(),
        {i8PtrTy, i8PtrTy, i8PtrTy, i8PtrTy, i64Ty}, false);
    module_->getOrInsertFunction("liva_array_push", arrayPushTy);

    auto *arrayPopTy = llvm::FunctionType::get(builder_->getVoidTy(), {i8PtrTy}, false);
    module_->getOrInsertFunction("liva_array_pop", arrayPopTy);

    // liva_panic(msg) — noreturn
    auto *panicTy = llvm::FunctionType::get(builder_->getVoidTy(), {i8PtrTy}, false);
    auto panicFn = module_->getOrInsertFunction("liva_panic", panicTy);
    if (auto *f = llvm::dyn_cast<llvm::Function>(panicFn.getCallee()))
        f->addFnAttr(llvm::Attribute::NoReturn);
}

llvm::StructType *IRGen::getDynArrayStructTy() {
    if (!dynArrayStructTy_) {
        dynArrayStructTy_ = llvm::StructType::create(*context_,
            {llvm::PointerType::getUnqual(*context_),
             builder_->getInt64Ty(),
             builder_->getInt64Ty()},
            "DynArray");
    }
    return dynArrayStructTy_;
}

llvm::StructType *IRGen::getOptionalType(llvm::Type *innerType) {
    auto it = optionalTypes_.find(innerType);
    if (it != optionalTypes_.end()) return it->second;
    auto *ty = llvm::StructType::create(*context_,
        {builder_->getInt1Ty(), innerType}, "Optional");
    optionalTypes_[innerType] = ty;
    return ty;
}

llvm::StructType *IRGen::getResultType(llvm::Type *okType, llvm::Type *errType) {
    auto key = std::make_pair(okType, errType);
    auto it = resultTypes_.find(key);
    if (it != resultTypes_.end()) return it->second;
    const auto &dl = module_->getDataLayout();
    uint64_t maxSize = std::max(dl.getTypeAllocSize(okType), dl.getTypeAllocSize(errType));
    auto *payloadTy = llvm::ArrayType::get(builder_->getInt8Ty(), maxSize);
    auto *ty = llvm::StructType::create(*context_, {builder_->getInt32Ty(), payloadTy}, "Result");
    resultTypes_[key] = ty;
    return ty;
}

llvm::Value *IRGen::emitResultOk(llvm::Type *okType, llvm::Type *errType, llvm::Value *value) {
    auto *resTy = getResultType(okType, errType);
    auto *func = builder_->GetInsertBlock()->getParent();
    auto *alloca = createEntryBlockAlloca(func, "result.ok.tmp", resTy);
    auto *tagPtr = builder_->CreateStructGEP(resTy, alloca, 0, "res.tag");
    builder_->CreateStore(builder_->getInt32(0), tagPtr);
    auto *payloadPtr = builder_->CreateStructGEP(resTy, alloca, 1, "res.payload");
    builder_->CreateStore(value, payloadPtr);
    return builder_->CreateLoad(resTy, alloca, "result.ok.val");
}

llvm::Value *IRGen::emitResultErr(llvm::Type *okType, llvm::Type *errType, llvm::Value *value) {
    auto *resTy = getResultType(okType, errType);
    auto *func = builder_->GetInsertBlock()->getParent();
    auto *alloca = createEntryBlockAlloca(func, "result.err.tmp", resTy);
    auto *tagPtr = builder_->CreateStructGEP(resTy, alloca, 0, "res.tag");
    builder_->CreateStore(builder_->getInt32(1), tagPtr);
    auto *payloadPtr = builder_->CreateStructGEP(resTy, alloca, 1, "res.payload");
    builder_->CreateStore(value, payloadPtr);
    return builder_->CreateLoad(resTy, alloca, "result.err.val");
}

llvm::StructType *IRGen::getTraitObjectTy() {
    if (!traitObjectTy_) {
        auto *ptrTy = llvm::PointerType::getUnqual(*context_);
        traitObjectTy_ = llvm::StructType::create(*context_, {ptrTy, ptrTy}, "trait_object");
    }
    return traitObjectTy_;
}

llvm::StructType *IRGen::getClosureObjTy() {
    if (!closureObjTy_) {
        auto *ptrTy = llvm::PointerType::getUnqual(*context_);
        closureObjTy_ = llvm::StructType::create(*context_, {ptrTy, ptrTy}, "closure_obj");
    }
    return closureObjTy_;
}

llvm::GlobalVariable *IRGen::getOrCreateVtable(const std::string &protocolName,
                                                  const std::string &typeName) {
    std::string vtableName = "vtable_" + protocolName + "_" + typeName;
    auto it = vtableGlobals_.find(vtableName);
    if (it != vtableGlobals_.end())
        return it->second;

    auto pmIt = protocolMethodNames_.find(protocolName);
    if (pmIt == protocolMethodNames_.end())
        return nullptr;

    auto *ptrTy = llvm::PointerType::getUnqual(*context_);
    std::vector<llvm::Constant *> funcs;

    for (auto &methodName : pmIt->second) {
        std::string mangledName = typeName + "_" + methodName;
        auto *fn = module_->getFunction(mangledName);
        if (!fn) {
            funcs.push_back(llvm::ConstantPointerNull::get(ptrTy));
        } else {
            funcs.push_back(fn);
        }
    }

    auto *arrayTy = llvm::ArrayType::get(ptrTy, funcs.size());
    auto *init = llvm::ConstantArray::get(arrayTy, funcs);
    auto *gv = new llvm::GlobalVariable(*module_, arrayTy, true,
                                          llvm::GlobalValue::PrivateLinkage, init, vtableName);
    vtableGlobals_[vtableName] = gv;
    return gv;
}

llvm::Value *IRGen::visitProtocolDecl(ProtocolDecl *node) {
    std::vector<std::string> methodNames;
    std::unordered_map<std::string, int> methodIndices;
    int idx = 0;
    for (auto &method : node->getMethods()) {
        methodNames.push_back(method->getName());
        methodIndices[method->getName()] = idx++;
    }
    protocolMethodNames_[node->getName()] = std::move(methodNames);
    protocolMethodIndices_[node->getName()] = std::move(methodIndices);
    protocolDecls_[node->getName()] = node;
    return nullptr;
}

llvm::Type *IRGen::toLLVMType(const TypeRepr *type) {
    if (!type)
        return builder_->getVoidTy();

    switch (type->getKind()) {
    case TypeRepr::Kind::Void:
        return builder_->getVoidTy();
    case TypeRepr::Kind::Bool:
        return builder_->getInt1Ty();
    case TypeRepr::Kind::I8:
    case TypeRepr::Kind::U8:
        return builder_->getInt8Ty();
    case TypeRepr::Kind::I16:
    case TypeRepr::Kind::U16:
        return builder_->getInt16Ty();
    case TypeRepr::Kind::I32:
    case TypeRepr::Kind::U32:
        return builder_->getInt32Ty();
    case TypeRepr::Kind::I64:
    case TypeRepr::Kind::U64:
        return builder_->getInt64Ty();
    case TypeRepr::Kind::F32:
        return builder_->getFloatTy();
    case TypeRepr::Kind::F64:
        return builder_->getDoubleTy();
    case TypeRepr::Kind::String:
        return llvm::PointerType::getUnqual(*context_);
    case TypeRepr::Kind::Named: {
        auto *named = static_cast<const NamedTypeRepr *>(type);
        // Check type parameter substitution map
        auto substIt = currentTypeSubst_.find(named->getName());
        if (substIt != currentTypeSubst_.end()) {
            return toLLVMType(substIt->second);
        }
        auto it = structTypes_.find(named->getName());
        if (it != structTypes_.end())
            return it->second;
        return llvm::PointerType::getUnqual(*context_);
    }
    case TypeRepr::Kind::Array: {
        auto *arrayType = static_cast<const ArrayTypeRepr *>(type);
        auto *elemType = toLLVMType(arrayType->getElement());
        if (arrayType->isDynamic())
            return llvm::PointerType::getUnqual(*context_);
        return llvm::ArrayType::get(elemType, static_cast<uint64_t>(arrayType->getSize()));
    }
    case TypeRepr::Kind::Optional: {
        auto *optType = static_cast<const OptionalTypeRepr *>(type);
        return getOptionalType(toLLVMType(optType->getInner()));
    }
    case TypeRepr::Kind::Result: {
        auto *rt = static_cast<const ResultTypeRepr *>(type);
        return getResultType(toLLVMType(rt->getOkType()), toLLVMType(rt->getErrType()));
    }
    case TypeRepr::Kind::Function:
        return getClosureObjTy();
    default:
        return builder_->getInt32Ty();
    }
}

llvm::AllocaInst *IRGen::createEntryBlockAlloca(llvm::Function *func,
                                                  const std::string &name,
                                                  llvm::Type *type) {
    llvm::IRBuilder<> tmpBuilder(&func->getEntryBlock(),
                                  func->getEntryBlock().begin());
    return tmpBuilder.CreateAlloca(type, nullptr, name);
}

llvm::Value *IRGen::visitFuncDecl(FuncDecl *node) {
    // Build function type
    std::vector<llvm::Type *> paramTypes;
    for (auto &param : node->getParams()) {
        paramTypes.push_back(toLLVMType(param.type.get()));
    }

    auto *returnType = toLLVMType(node->getReturnType());

    // C ABI: main must return i32
    bool isMain = (node->getName() == "main");
    if (isMain && returnType->isVoidTy()) {
        returnType = builder_->getInt32Ty();
    }

    auto *funcType = llvm::FunctionType::get(returnType, paramTypes, false);
    auto *func = llvm::Function::Create(
        funcType, llvm::Function::ExternalLinkage, node->getName(), *module_);

    // Set parameter names
    size_t i = 0;
    for (auto &arg : func->args()) {
        arg.setName(node->getParams()[i].name);
        ++i;
    }

    if (!node->hasBody())
        return func;

    // Create entry block
    auto *entryBB = llvm::BasicBlock::Create(*context_, "entry", func);
    builder_->SetInsertPoint(entryBB);

    // Save old named values and create new scope
    auto oldNamedValues = namedValues_;
    auto oldVarStructTypes = varStructTypes_;
    auto oldVarEnumTypes = varEnumTypes_;
    auto oldVarArrayTypes = varArrayTypes_;
    auto oldVarDynArrayTypes = varDynArrayTypes_;
    auto oldVarOptionalTypes = varOptionalTypes_;
    auto oldVarFuncTypes = varFuncTypes_;
    auto oldVarProtocolTypes = varProtocolTypes_;
    auto oldVarResultTypes = varResultTypes_;
    auto *oldFuncResultInfo = currentFuncResultInfo_;
    namedValues_.clear();
    varStructTypes_.clear();
    varEnumTypes_.clear();
    varArrayTypes_.clear();
    varDynArrayTypes_.clear();
    varOptionalTypes_.clear();
    varFuncTypes_.clear();
    varProtocolTypes_.clear();
    varResultTypes_.clear();
    currentFuncResultInfo_ = nullptr;

    // Track Result return type for try expressions
    if (node->getReturnType() &&
        node->getReturnType()->getKind() == TypeRepr::Kind::Result) {
        auto *rt = static_cast<const ResultTypeRepr *>(node->getReturnType());
        currentFuncResultInfoStorage_ = {toLLVMType(rt->getOkType()), toLLVMType(rt->getErrType())};
        currentFuncResultInfo_ = &currentFuncResultInfoStorage_;
    }

    // Create allocas for parameters
    i = 0;
    for (auto &arg : func->args()) {
        auto *alloca = createEntryBlockAlloca(func, std::string(arg.getName()), arg.getType());
        builder_->CreateStore(&arg, alloca);
        namedValues_[std::string(arg.getName())] = alloca;
        ++i;
    }

    // Populate varFuncTypes_ for function-typed parameters
    for (auto &param : node->getParams()) {
        if (param.type && param.type->getKind() == TypeRepr::Kind::Function) {
            auto *ftr = static_cast<const FunctionTypeRepr *>(param.type.get());
            std::vector<llvm::Type *> fParamTypes;
            fParamTypes.push_back(llvm::PointerType::getUnqual(*context_)); // hidden env
            for (auto &p : ftr->getParams())
                fParamTypes.push_back(toLLVMType(p.get()));
            llvm::Type *fRetTy = ftr->getReturnType()
                ? toLLVMType(ftr->getReturnType()) : builder_->getVoidTy();
            varFuncTypes_[param.name] = llvm::FunctionType::get(fRetTy, fParamTypes, false);
        }
    }

    // Generate body
    visitBlockStmt(const_cast<BlockStmt *>(node->getBody()));

    // Add implicit return if needed
    if (!builder_->GetInsertBlock()->getTerminator()) {
        if (isMain) {
            // main always returns i32 0 implicitly
            builder_->CreateRet(builder_->getInt32(0));
        } else if (returnType->isVoidTy()) {
            builder_->CreateRetVoid();
        } else {
            builder_->CreateRet(llvm::Constant::getNullValue(returnType));
        }
    }

    // Restore named values
    namedValues_ = oldNamedValues;
    varStructTypes_ = oldVarStructTypes;
    varEnumTypes_ = oldVarEnumTypes;
    varArrayTypes_ = oldVarArrayTypes;
    varDynArrayTypes_ = oldVarDynArrayTypes;
    varOptionalTypes_ = oldVarOptionalTypes;
    varFuncTypes_ = oldVarFuncTypes;
    varProtocolTypes_ = oldVarProtocolTypes;
    varResultTypes_ = oldVarResultTypes;
    currentFuncResultInfo_ = oldFuncResultInfo;

    return func;
}

llvm::Value *IRGen::visitVarDecl(VarDecl *node) {
    auto *func = builder_->GetInsertBlock()->getParent();

    // Protocol trait object: let s: ref Shape = circle
    if (node->hasTypeAnnotation() && node->getType() &&
        node->getType()->getKind() == TypeRepr::Kind::Reference) {
        auto *refType = static_cast<const ReferenceTypeRepr *>(node->getType());
        if (refType->getInner() && refType->getInner()->getKind() == TypeRepr::Kind::Named) {
            auto *namedInner = static_cast<const NamedTypeRepr *>(refType->getInner());
            const std::string &protocolName = namedInner->getName();
            auto pmIt = protocolMethodNames_.find(protocolName);
            if (pmIt != protocolMethodNames_.end()) {
                // This is a protocol trait object variable
                auto *traitTy = getTraitObjectTy();
                auto *alloca = createEntryBlockAlloca(func, node->getName(), traitTy);

                // Get concrete type from init expression
                std::string concreteType;
                llvm::AllocaInst *dataAlloca = nullptr;
                if (node->hasInit() &&
                    node->getInit()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
                    auto *ident = static_cast<IdentifierExpr *>(
                        const_cast<Expr *>(node->getInit()));
                    auto stIt = varStructTypes_.find(ident->getName());
                    if (stIt != varStructTypes_.end()) {
                        concreteType = stIt->second;
                        auto nvIt = namedValues_.find(ident->getName());
                        if (nvIt != namedValues_.end())
                            dataAlloca = nvIt->second;
                    }
                }

                if (!concreteType.empty() && dataAlloca) {
                    auto *ptrTy = llvm::PointerType::getUnqual(*context_);

                    // Store data pointer (pointer to concrete struct)
                    auto *dataGEP = builder_->CreateStructGEP(traitTy, alloca, 0, "trait.data");
                    builder_->CreateStore(dataAlloca, dataGEP);

                    // Get or create vtable and store pointer
                    auto *vtable = getOrCreateVtable(protocolName, concreteType);
                    auto *vtableGEP = builder_->CreateStructGEP(traitTy, alloca, 1, "trait.vtable");
                    builder_->CreateStore(vtable, vtableGEP);

                    namedValues_[node->getName()] = alloca;
                    varProtocolTypes_[node->getName()] = protocolName;
                    return alloca;
                }
            }
        }
    }

    // Result variable: let r: Result<i32, string> = Result.ok(42)
    if (node->hasTypeAnnotation() && node->getType() &&
        node->getType()->getKind() == TypeRepr::Kind::Result) {
        auto *rtRepr = static_cast<const ResultTypeRepr *>(node->getType());
        auto *okLLVM = toLLVMType(rtRepr->getOkType());
        auto *errLLVM = toLLVMType(rtRepr->getErrType());
        auto *resTy = getResultType(okLLVM, errLLVM);
        auto *alloca = createEntryBlockAlloca(func, node->getName(), resTy);

        if (node->hasInit()) {
            // Temporarily set currentFuncResultInfo_ so emitResultOk/Err can be used from visitCallExpr
            ResultInfo ri = {okLLVM, errLLVM};
            auto *oldRI = currentFuncResultInfo_;
            currentFuncResultInfo_ = &ri;
            auto *initVal = visit(const_cast<Expr *>(node->getInit()));
            currentFuncResultInfo_ = oldRI;
            if (initVal) builder_->CreateStore(initVal, alloca);
        }

        namedValues_[node->getName()] = alloca;
        varResultTypes_[node->getName()] = {okLLVM, errLLVM};
        return alloca;
    }

    // Optional variable: let x: i32? = 42 / let x: i32? = nil
    if (node->hasTypeAnnotation() && node->getType() &&
        node->getType()->getKind() == TypeRepr::Kind::Optional) {
        auto *optTypeRepr = static_cast<const OptionalTypeRepr *>(node->getType());
        auto *innerLLVM = toLLVMType(optTypeRepr->getInner());
        auto *optStructTy = getOptionalType(innerLLVM);
        auto *alloca = createEntryBlockAlloca(func, node->getName(), optStructTy);
        auto *hasValPtr = builder_->CreateStructGEP(optStructTy, alloca, 0, "opt.hasval");
        auto *valPtr = builder_->CreateStructGEP(optStructTy, alloca, 1, "opt.val");

        if (node->hasInit() &&
            node->getInit()->getKind() == ASTNode::NodeKind::NilLiteralExpr) {
            builder_->CreateStore(builder_->getFalse(), hasValPtr);
            builder_->CreateStore(llvm::Constant::getNullValue(innerLLVM), valPtr);
        } else if (node->hasInit()) {
            auto *initVal = visit(const_cast<Expr *>(node->getInit()));
            builder_->CreateStore(builder_->getTrue(), hasValPtr);
            if (initVal) builder_->CreateStore(initVal, valPtr);
        } else {
            builder_->CreateStore(builder_->getFalse(), hasValPtr);
            builder_->CreateStore(llvm::Constant::getNullValue(innerLLVM), valPtr);
        }
        namedValues_[node->getName()] = alloca;
        varOptionalTypes_[node->getName()] = innerLLVM;
        return alloca;
    }

    // Check if init is a struct literal - reuse its alloca
    if (node->hasInit() &&
        node->getInit()->getKind() == ASTNode::NodeKind::StructLiteralExpr) {
        auto *structLit = static_cast<StructLiteralExpr *>(
            const_cast<Expr *>(node->getInit()));
        const auto &typeName = structLit->getTypeName();

        // Check for generic struct
        auto gsIt = genericStructDecls_.find(typeName);
        if (gsIt != genericStructDecls_.end()) {
            // Evaluate all field values first
            std::vector<llvm::Value *> fieldValues;
            for (auto &fieldInit : structLit->getFields()) {
                auto *val = visit(fieldInit.value.get());
                fieldValues.push_back(val);
            }

            // Infer type arguments from field values
            auto typeArgs = inferStructTypeArgs(gsIt->second, structLit->getFields(), fieldValues);

            // Monomorphize the struct
            monomorphizeStruct(gsIt->second, typeArgs);
            std::string mangledName = mangleGenericStruct(typeName, typeArgs);

            auto *structTy = structTypes_[mangledName];
            auto *alloca = createEntryBlockAlloca(func, node->getName(), structTy);

            for (size_t i = 0; i < structLit->getFields().size(); ++i) {
                int idx = getStructFieldIndex(mangledName, structLit->getFields()[i].name);
                if (idx < 0 || !fieldValues[i])
                    continue;
                auto *gep = builder_->CreateStructGEP(structTy, alloca, idx,
                                                       structLit->getFields()[i].name);
                builder_->CreateStore(fieldValues[i], gep);
            }

            namedValues_[node->getName()] = alloca;
            varStructTypes_[node->getName()] = mangledName;
            return alloca;
        }

        auto stIt = structTypes_.find(typeName);
        if (stIt != structTypes_.end()) {
            auto *structTy = stIt->second;
            auto *alloca = createEntryBlockAlloca(func, node->getName(), structTy);

            // Store each field
            for (auto &fieldInit : structLit->getFields()) {
                int idx = getStructFieldIndex(typeName, fieldInit.name);
                if (idx < 0)
                    continue;
                auto *val = visit(fieldInit.value.get());
                if (!val)
                    continue;
                auto *gep = builder_->CreateStructGEP(structTy, alloca, idx,
                                                       fieldInit.name);
                builder_->CreateStore(val, gep);
            }

            namedValues_[node->getName()] = alloca;
            varStructTypes_[node->getName()] = typeName;
            return alloca;
        }
    }

    // Check if init is a payload enum constructor: let s = Shape.Circle(3.14)
    if (node->hasInit() &&
        node->getInit()->getKind() == ASTNode::NodeKind::CallExpr) {
        auto *callExpr = static_cast<CallExpr *>(
            const_cast<Expr *>(node->getInit()));
        if (callExpr->getCallee()->getKind() == ASTNode::NodeKind::MemberExpr) {
            auto *memberExpr = static_cast<MemberExpr *>(callExpr->getCallee());
            if (memberExpr->getObject()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
                auto *ident = static_cast<IdentifierExpr *>(memberExpr->getObject());
                auto etIt = enumTypes_.find(ident->getName());
                if (etIt != enumTypes_.end()) {
                    auto *enumStructTy = etIt->second;
                    auto *alloca = createEntryBlockAlloca(func, node->getName(), enumStructTy);
                    auto *initVal = visit(const_cast<Expr *>(node->getInit()));
                    if (initVal)
                        builder_->CreateStore(initVal, alloca);
                    namedValues_[node->getName()] = alloca;
                    varEnumTypes_[node->getName()] = ident->getName();
                    return alloca;
                }
            }
        }
    }

    // Check if init is an enum case reference: let c = Color.Green (or Shape.Empty in payload enum)
    if (node->hasInit() &&
        node->getInit()->getKind() == ASTNode::NodeKind::MemberExpr) {
        auto *memberExpr = static_cast<MemberExpr *>(
            const_cast<Expr *>(node->getInit()));
        if (memberExpr->getObject()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
            auto *ident = static_cast<IdentifierExpr *>(memberExpr->getObject());

            // Check for payload enum: Shape.Empty (no-arg case in a payload enum)
            auto etIt = enumTypes_.find(ident->getName());
            if (etIt != enumTypes_.end()) {
                auto *enumStructTy = etIt->second;
                auto ecIt = enumCases_.find(ident->getName());
                if (ecIt != enumCases_.end()) {
                    auto cIt = ecIt->second.find(memberExpr->getMember());
                    if (cIt != ecIt->second.end()) {
                        auto *alloca = createEntryBlockAlloca(func, node->getName(), enumStructTy);
                        // Store tag only
                        auto *tagPtr = builder_->CreateStructGEP(enumStructTy, alloca, 0, "tag.ptr");
                        builder_->CreateStore(builder_->getInt32(cIt->second), tagPtr);
                        namedValues_[node->getName()] = alloca;
                        varEnumTypes_[node->getName()] = ident->getName();
                        return alloca;
                    }
                }
            }

            // Simple enum (no payload): let c = Color.Green
            auto eIt = enumCases_.find(ident->getName());
            if (eIt != enumCases_.end()) {
                auto *alloca = createEntryBlockAlloca(func, node->getName(),
                                                       builder_->getInt32Ty());
                auto *initVal = visit(const_cast<Expr *>(node->getInit()));
                if (initVal)
                    builder_->CreateStore(initVal, alloca);
                namedValues_[node->getName()] = alloca;
                varEnumTypes_[node->getName()] = ident->getName();
                return alloca;
            }
        }
    }

    // Dynamic array: var arr: [i32] = [1, 2, 3] or var arr: [i32] = []
    if (node->hasTypeAnnotation() && node->getType() &&
        node->getType()->getKind() == TypeRepr::Kind::Array) {
        auto *arrTypeRepr = static_cast<const ArrayTypeRepr *>(node->getType());
        if (arrTypeRepr->isDynamic()) {
            auto *elemType = toLLVMType(arrTypeRepr->getElement());
            const llvm::DataLayout &dl = module_->getDataLayout();
            uint64_t elemSize = dl.getTypeAllocSize(elemType);
            auto *structTy = getDynArrayStructTy();
            auto *alloca = createEntryBlockAlloca(func, node->getName(), structTy);

            // Collect init elements
            std::vector<llvm::Value *> initVals;
            if (node->hasInit() &&
                node->getInit()->getKind() == ASTNode::NodeKind::ArrayLiteralExpr) {
                auto *arrayLit = static_cast<ArrayLiteralExpr *>(
                    const_cast<Expr *>(node->getInit()));
                for (auto &elem : arrayLit->getElements()) {
                    auto *val = visit(elem.get());
                    if (val) initVals.push_back(val);
                }
            }

            uint64_t initLen = initVals.size();
            uint64_t initCap = initLen > 0 ? initLen : 8;

            // liva_array_new(elem_size, capacity)
            auto *newFn = module_->getFunction("liva_array_new");
            auto *dataPtr = builder_->CreateCall(newFn,
                {builder_->getInt64(elemSize), builder_->getInt64(initCap)}, "arr.data");

            // Store initial elements
            for (uint64_t i = 0; i < initLen; ++i) {
                auto *elemPtr = builder_->CreateGEP(elemType, dataPtr,
                    builder_->getInt64(i), "arr.init." + std::to_string(i));
                builder_->CreateStore(initVals[i], elemPtr);
            }

            // Fill struct fields: {data, length, capacity}
            auto *dataField = builder_->CreateStructGEP(structTy, alloca, 0, "arr.data.ptr");
            builder_->CreateStore(dataPtr, dataField);
            auto *lenField = builder_->CreateStructGEP(structTy, alloca, 1, "arr.len.ptr");
            builder_->CreateStore(builder_->getInt64(initLen), lenField);
            auto *capField = builder_->CreateStructGEP(structTy, alloca, 2, "arr.cap.ptr");
            builder_->CreateStore(builder_->getInt64(initCap), capField);

            namedValues_[node->getName()] = alloca;
            varDynArrayTypes_[node->getName()] = {elemType, elemSize};
            return alloca;
        }
    }

    // Array literal init: let arr = [10, 20, 30]
    if (node->hasInit() &&
        node->getInit()->getKind() == ASTNode::NodeKind::ArrayLiteralExpr) {
        auto *arrayLit = static_cast<ArrayLiteralExpr *>(
            const_cast<Expr *>(node->getInit()));
        auto &elements = arrayLit->getElements();
        if (!elements.empty()) {
            auto *firstVal = visit(elements[0].get());
            if (!firstVal) return nullptr;
            auto *elemType = firstVal->getType();
            uint64_t numElements = elements.size();
            auto *arrayType = llvm::ArrayType::get(elemType, numElements);
            auto *alloca = createEntryBlockAlloca(func, node->getName(), arrayType);
            auto *gep0 = builder_->CreateConstInBoundsGEP2_64(arrayType, alloca, 0, 0, "arr.elem.0");
            builder_->CreateStore(firstVal, gep0);
            for (uint64_t i = 1; i < numElements; ++i) {
                auto *val = visit(elements[i].get());
                if (!val) continue;
                auto *gep = builder_->CreateConstInBoundsGEP2_64(
                    arrayType, alloca, 0, i, "arr.elem." + std::to_string(i));
                builder_->CreateStore(val, gep);
            }
            namedValues_[node->getName()] = alloca;
            varArrayTypes_[node->getName()] = {elemType, numElements};
            return alloca;
        }
    }

    // Function-typed variable: let f: (i32) -> i32 = |x: i32| -> i32 { ... }
    if (node->hasTypeAnnotation() && node->getType() &&
        node->getType()->getKind() == TypeRepr::Kind::Function) {
        auto *funcTypeRepr = static_cast<const FunctionTypeRepr *>(node->getType());
        std::vector<llvm::Type *> paramTypes;
        paramTypes.push_back(llvm::PointerType::getUnqual(*context_)); // hidden env
        for (auto &p : funcTypeRepr->getParams())
            paramTypes.push_back(toLLVMType(p.get()));
        llvm::Type *retTy = funcTypeRepr->getReturnType()
            ? toLLVMType(funcTypeRepr->getReturnType())
            : builder_->getVoidTy();
        auto *llvmFuncTy = llvm::FunctionType::get(retTy, paramTypes, false);
        auto *closureObjTy = getClosureObjTy();
        auto *alloca = createEntryBlockAlloca(func, node->getName(), closureObjTy);
        if (node->hasInit()) {
            auto *initVal = visit(const_cast<Expr *>(node->getInit()));
            if (initVal) builder_->CreateStore(initVal, alloca);
        }
        namedValues_[node->getName()] = alloca;
        varFuncTypes_[node->getName()] = llvmFuncTy;
        return alloca;
    }

    auto *type = toLLVMType(node->getType());
    auto *alloca = createEntryBlockAlloca(func, node->getName(), type);

    if (node->hasInit()) {
        auto *initVal = visit(const_cast<Expr *>(node->getInit()));
        if (initVal)
            builder_->CreateStore(initVal, alloca);
    }

    namedValues_[node->getName()] = alloca;
    return alloca;
}

llvm::Value *IRGen::visitStructDecl(StructDecl *node) {
    std::vector<llvm::Type *> fieldTypes;
    std::vector<std::string> fieldNames;
    for (auto &field : node->getFields()) {
        fieldTypes.push_back(toLLVMType(field->getType()));
        fieldNames.push_back(field->getName());
    }

    auto *structType = llvm::StructType::create(*context_, fieldTypes, node->getName());
    structTypes_[node->getName()] = structType;
    structFieldNames_[node->getName()] = std::move(fieldNames);
    return nullptr;
}

int IRGen::getStructFieldIndex(const std::string &structName, const std::string &fieldName) {
    auto it = structFieldNames_.find(structName);
    if (it == structFieldNames_.end())
        return -1;
    for (size_t i = 0; i < it->second.size(); ++i) {
        if (it->second[i] == fieldName)
            return static_cast<int>(i);
    }
    return -1;
}

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

llvm::Value *IRGen::visitReturnStmt(ReturnStmt *node) {
    if (node->hasValue()) {
        auto *val = visit(node->getValue());
        return builder_->CreateRet(val);
    }
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
    namedValues_[node->getBindingName()] = bindAlloca;
    visit(node->getThenBody());
    namedValues_ = saved;
    if (!builder_->GetInsertBlock()->getTerminator()) builder_->CreateBr(mergeBB2);

    // Else
    builder_->SetInsertPoint(elseBB);
    if (node->hasElse()) visit(node->getElseBody());
    if (!builder_->GetInsertBlock()->getTerminator()) builder_->CreateBr(mergeBB2);

    builder_->SetInsertPoint(mergeBB2);
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

llvm::Value *IRGen::visitIntegerLiteralExpr(IntegerLiteralExpr *node) {
    return llvm::ConstantInt::get(*context_, llvm::APInt(32, node->getValue(), true));
}

llvm::Value *IRGen::visitFloatLiteralExpr(FloatLiteralExpr *node) {
    return llvm::ConstantFP::get(*context_, llvm::APFloat(node->getValue()));
}

llvm::Value *IRGen::visitBoolLiteralExpr(BoolLiteralExpr *node) {
    return llvm::ConstantInt::get(*context_, llvm::APInt(1, node->getValue() ? 1 : 0));
}

llvm::Value *IRGen::visitStringLiteralExpr(StringLiteralExpr *node) {
    auto &val = stringConstants_[node->getValue()];
    if (!val) {
        val = builder_->CreateGlobalString(node->getValue());
    }
    return val;
}

llvm::Value *IRGen::visitNilLiteralExpr(NilLiteralExpr *) {
    return nullptr; // nil is context-dependent, handled by VarDecl/AssignExpr
}

llvm::Value *IRGen::visitUnwrapExpr(UnwrapExpr *node) {
    llvm::AllocaInst *optAlloca = nullptr;
    llvm::Type *innerType = nullptr;
    if (node->getOperand()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
        auto *ident = static_cast<IdentifierExpr *>(node->getOperand());
        auto it = namedValues_.find(ident->getName());
        if (it != namedValues_.end()) optAlloca = it->second;
        auto optIt = varOptionalTypes_.find(ident->getName());
        if (optIt != varOptionalTypes_.end()) innerType = optIt->second;
    }
    if (!optAlloca || !innerType)
        return visit(node->getOperand());

    auto *optStructTy = getOptionalType(innerType);
    auto *hasValPtr = builder_->CreateStructGEP(optStructTy, optAlloca, 0);
    auto *hasVal = builder_->CreateLoad(builder_->getInt1Ty(), hasValPtr, "unwrap.hasval");

    auto *func = builder_->GetInsertBlock()->getParent();
    auto *panicBB = llvm::BasicBlock::Create(*context_, "unwrap.nil", func);
    auto *okBB = llvm::BasicBlock::Create(*context_, "unwrap.ok", func);
    builder_->CreateCondBr(hasVal, okBB, panicBB);

    builder_->SetInsertPoint(panicBB);
    auto *panicFn = module_->getFunction("liva_panic");
    auto *msg = builder_->CreateGlobalString("unwrap of nil optional value");
    builder_->CreateCall(panicFn, {msg});
    builder_->CreateUnreachable();

    builder_->SetInsertPoint(okBB);
    auto *valPtr = builder_->CreateStructGEP(optStructTy, optAlloca, 1);
    return builder_->CreateLoad(innerType, valPtr, "unwrap.val");
}

llvm::Value *IRGen::visitIdentifierExpr(IdentifierExpr *node) {
    auto it = namedValues_.find(node->getName());
    if (it != namedValues_.end()) {
        return builder_->CreateLoad(it->second->getAllocatedType(), it->second,
                                     node->getName());
    }
    return nullptr;
}

llvm::Value *IRGen::visitBinaryExpr(BinaryExpr *node) {
    if (node->getOp() == BinaryExpr::Op::NilCoalesce) {
        return emitNilCoalesce(node);
    }

    auto *lhs = visit(node->getLHS());
    auto *rhs = visit(node->getRHS());
    if (!lhs || !rhs)
        return nullptr;

    // Check for string operations via resolved type
    bool isString = node->getLHS()->getResolvedType() &&
        node->getLHS()->getResolvedType()->getKind() == TypeRepr::Kind::String;

    if (isString) {
        switch (node->getOp()) {
        case BinaryExpr::Op::Add: {
            auto *concatFn = module_->getFunction("liva_str_concat");
            return builder_->CreateCall(concatFn, {lhs, rhs});
        }
        case BinaryExpr::Op::Eq: {
            auto *equalFn = module_->getFunction("liva_str_equal");
            auto *result = builder_->CreateCall(equalFn, {lhs, rhs});
            return builder_->CreateICmpNE(result,
                llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context_), 0));
        }
        case BinaryExpr::Op::NotEq: {
            auto *equalFn = module_->getFunction("liva_str_equal");
            auto *result = builder_->CreateCall(equalFn, {lhs, rhs});
            return builder_->CreateICmpEQ(result,
                llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context_), 0));
        }
        default:
            break;
        }
    }

    // Check if float operations
    bool isFloat = lhs->getType()->isFloatingPointTy();

    switch (node->getOp()) {
    case BinaryExpr::Op::Add:
        return isFloat ? builder_->CreateFAdd(lhs, rhs, "addtmp")
                       : builder_->CreateAdd(lhs, rhs, "addtmp");
    case BinaryExpr::Op::Sub:
        return isFloat ? builder_->CreateFSub(lhs, rhs, "subtmp")
                       : builder_->CreateSub(lhs, rhs, "subtmp");
    case BinaryExpr::Op::Mul:
        return isFloat ? builder_->CreateFMul(lhs, rhs, "multmp")
                       : builder_->CreateMul(lhs, rhs, "multmp");
    case BinaryExpr::Op::Div:
        return isFloat ? builder_->CreateFDiv(lhs, rhs, "divtmp")
                       : builder_->CreateSDiv(lhs, rhs, "divtmp");
    case BinaryExpr::Op::Mod:
        return isFloat ? builder_->CreateFRem(lhs, rhs, "modtmp")
                       : builder_->CreateSRem(lhs, rhs, "modtmp");
    case BinaryExpr::Op::Eq:
        return isFloat ? builder_->CreateFCmpOEQ(lhs, rhs, "eqtmp")
                       : builder_->CreateICmpEQ(lhs, rhs, "eqtmp");
    case BinaryExpr::Op::NotEq:
        return isFloat ? builder_->CreateFCmpONE(lhs, rhs, "netmp")
                       : builder_->CreateICmpNE(lhs, rhs, "netmp");
    case BinaryExpr::Op::Less:
        return isFloat ? builder_->CreateFCmpOLT(lhs, rhs, "lttmp")
                       : builder_->CreateICmpSLT(lhs, rhs, "lttmp");
    case BinaryExpr::Op::LessEq:
        return isFloat ? builder_->CreateFCmpOLE(lhs, rhs, "letmp")
                       : builder_->CreateICmpSLE(lhs, rhs, "letmp");
    case BinaryExpr::Op::Greater:
        return isFloat ? builder_->CreateFCmpOGT(lhs, rhs, "gttmp")
                       : builder_->CreateICmpSGT(lhs, rhs, "gttmp");
    case BinaryExpr::Op::GreaterEq:
        return isFloat ? builder_->CreateFCmpOGE(lhs, rhs, "getmp")
                       : builder_->CreateICmpSGE(lhs, rhs, "getmp");
    case BinaryExpr::Op::And:
        return builder_->CreateAnd(lhs, rhs, "andtmp");
    case BinaryExpr::Op::Or:
        return builder_->CreateOr(lhs, rhs, "ortmp");
    case BinaryExpr::Op::BitAnd:
        return builder_->CreateAnd(lhs, rhs, "bandtmp");
    case BinaryExpr::Op::BitOr:
        return builder_->CreateOr(lhs, rhs, "bortmp");
    case BinaryExpr::Op::BitXor:
        return builder_->CreateXor(lhs, rhs, "bxortmp");
    case BinaryExpr::Op::Shl:
        return builder_->CreateShl(lhs, rhs, "shltmp");
    case BinaryExpr::Op::Shr:
        return builder_->CreateAShr(lhs, rhs, "shrtmp");
    case BinaryExpr::Op::NilCoalesce:
        break; // handled above via early return
    }
    return nullptr;
}

llvm::Value *IRGen::emitNilCoalesce(BinaryExpr *node) {
    llvm::AllocaInst *optAlloca = nullptr;
    llvm::Type *innerType = nullptr;
    if (node->getLHS()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
        auto *ident = static_cast<IdentifierExpr *>(node->getLHS());
        auto it = namedValues_.find(ident->getName());
        if (it != namedValues_.end()) optAlloca = it->second;
        auto optIt = varOptionalTypes_.find(ident->getName());
        if (optIt != varOptionalTypes_.end()) innerType = optIt->second;
    }
    if (!optAlloca || !innerType) {
        auto *lhs = visit(node->getLHS());
        return lhs ? lhs : visit(node->getRHS());
    }

    auto *optStructTy = getOptionalType(innerType);
    auto *hasValPtr = builder_->CreateStructGEP(optStructTy, optAlloca, 0);
    auto *hasVal = builder_->CreateLoad(builder_->getInt1Ty(), hasValPtr, "coal.hasval");
    auto *func = builder_->GetInsertBlock()->getParent();
    auto *hasValBB = llvm::BasicBlock::Create(*context_, "coal.hasval", func);
    auto *nilBB = llvm::BasicBlock::Create(*context_, "coal.nil", func);
    auto *mergeBB = llvm::BasicBlock::Create(*context_, "coal.merge", func);
    builder_->CreateCondBr(hasVal, hasValBB, nilBB);

    builder_->SetInsertPoint(hasValBB);
    auto *valPtr = builder_->CreateStructGEP(optStructTy, optAlloca, 1);
    auto *optVal = builder_->CreateLoad(innerType, valPtr, "coal.val");
    auto *hasValEnd = builder_->GetInsertBlock();
    builder_->CreateBr(mergeBB);

    builder_->SetInsertPoint(nilBB);
    auto *defaultVal = visit(node->getRHS());
    auto *nilEnd = builder_->GetInsertBlock();
    builder_->CreateBr(mergeBB);

    builder_->SetInsertPoint(mergeBB);
    auto *phi = builder_->CreatePHI(innerType, 2, "coal.result");
    phi->addIncoming(optVal, hasValEnd);
    phi->addIncoming(defaultVal, nilEnd);
    return phi;
}

llvm::Value *IRGen::visitUnaryExpr(UnaryExpr *node) {
    auto *operand = visit(node->getOperand());
    if (!operand)
        return nullptr;

    switch (node->getOp()) {
    case UnaryExpr::Op::Negate:
        if (operand->getType()->isFloatingPointTy())
            return builder_->CreateFNeg(operand, "negtmp");
        return builder_->CreateNeg(operand, "negtmp");
    case UnaryExpr::Op::Not:
        return builder_->CreateNot(operand, "nottmp");
    case UnaryExpr::Op::BitNot:
        return builder_->CreateNot(operand, "bnottmp");
    }
    return nullptr;
}

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

        auto *arg = visit(node->getArgs()[0].get());
        if (!arg)
            return nullptr;

        // Determine format string based on argument type
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

        if (funcName == "println")
            fmt += "\n";

        auto *fmtStr = builder_->CreateGlobalString(fmt);
        return builder_->CreateCall(printfFunc, {fmtStr, arg});
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

llvm::Value *IRGen::visitGroupExpr(GroupExpr *node) {
    return visit(node->getExpr());
}

llvm::Value *IRGen::visitCastExpr(CastExpr *node) {
    auto *val = visit(const_cast<Expr *>(node->getExpr()));
    if (!val)
        return nullptr;

    auto *targetType = toLLVMType(node->getTargetType());

    // Integer to integer
    if (val->getType()->isIntegerTy() && targetType->isIntegerTy()) {
        if (val->getType()->getIntegerBitWidth() < targetType->getIntegerBitWidth())
            return builder_->CreateSExt(val, targetType, "sext");
        return builder_->CreateTrunc(val, targetType, "trunc");
    }

    // Integer to float
    if (val->getType()->isIntegerTy() && targetType->isFloatingPointTy())
        return builder_->CreateSIToFP(val, targetType, "sitofp");

    // Float to integer
    if (val->getType()->isFloatingPointTy() && targetType->isIntegerTy())
        return builder_->CreateFPToSI(val, targetType, "fptosi");

    // Float to float
    if (val->getType()->isFloatingPointTy() && targetType->isFloatingPointTy()) {
        if (val->getType() == llvm::Type::getFloatTy(*context_))
            return builder_->CreateFPExt(val, targetType, "fpext");
        return builder_->CreateFPTrunc(val, targetType, "fptrunc");
    }

    return val;
}

llvm::Value *IRGen::visitMemberExpr(MemberExpr *node) {
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

// --- Free variable collection for closure capture ---

static void collectFreeVarsImpl(const ASTNode *node,
                                const std::set<std::string> &params,
                                std::set<std::string> &locals,
                                std::set<std::string> &freeVars) {
    if (!node) return;
    using NK = ASTNode::NodeKind;
    switch (node->getKind()) {
    // --- Expressions ---
    case NK::IdentifierExpr: {
        auto *id = static_cast<const IdentifierExpr *>(node);
        if (params.find(id->getName()) == params.end() &&
            locals.find(id->getName()) == locals.end())
            freeVars.insert(id->getName());
        break;
    }
    case NK::BinaryExpr: {
        auto *e = static_cast<const BinaryExpr *>(node);
        collectFreeVarsImpl(e->getLHS(), params, locals, freeVars);
        collectFreeVarsImpl(e->getRHS(), params, locals, freeVars);
        break;
    }
    case NK::UnaryExpr: {
        auto *e = static_cast<const UnaryExpr *>(node);
        collectFreeVarsImpl(e->getOperand(), params, locals, freeVars);
        break;
    }
    case NK::CallExpr: {
        auto *e = static_cast<const CallExpr *>(node);
        collectFreeVarsImpl(e->getCallee(), params, locals, freeVars);
        for (auto &arg : e->getArgs())
            collectFreeVarsImpl(arg.get(), params, locals, freeVars);
        break;
    }
    case NK::GroupExpr: {
        auto *e = static_cast<const GroupExpr *>(node);
        collectFreeVarsImpl(e->getExpr(), params, locals, freeVars);
        break;
    }
    case NK::CastExpr: {
        auto *e = static_cast<const CastExpr *>(node);
        collectFreeVarsImpl(e->getExpr(), params, locals, freeVars);
        break;
    }
    case NK::MemberExpr: {
        auto *e = static_cast<const MemberExpr *>(node);
        collectFreeVarsImpl(e->getObject(), params, locals, freeVars);
        break;
    }
    case NK::IndexExpr: {
        auto *e = static_cast<const IndexExpr *>(node);
        collectFreeVarsImpl(e->getBase(), params, locals, freeVars);
        collectFreeVarsImpl(e->getIndex(), params, locals, freeVars);
        break;
    }
    case NK::AssignExpr: {
        auto *e = static_cast<const AssignExpr *>(node);
        collectFreeVarsImpl(e->getTarget(), params, locals, freeVars);
        collectFreeVarsImpl(e->getValue(), params, locals, freeVars);
        break;
    }
    case NK::UnwrapExpr: {
        auto *e = static_cast<const UnwrapExpr *>(node);
        collectFreeVarsImpl(e->getOperand(), params, locals, freeVars);
        break;
    }
    case NK::RangeExpr: {
        auto *e = static_cast<const RangeExpr *>(node);
        collectFreeVarsImpl(e->getStart(), params, locals, freeVars);
        collectFreeVarsImpl(e->getEnd(), params, locals, freeVars);
        break;
    }
    case NK::MatchExpr: {
        auto *e = static_cast<const MatchExpr *>(node);
        collectFreeVarsImpl(e->getSubject(), params, locals, freeVars);
        for (auto &arm : e->getArms())
            collectFreeVarsImpl(arm.body.get(), params, locals, freeVars);
        break;
    }
    case NK::ArrayLiteralExpr: {
        auto *e = static_cast<const ArrayLiteralExpr *>(node);
        for (auto &el : e->getElements())
            collectFreeVarsImpl(el.get(), params, locals, freeVars);
        break;
    }
    case NK::StructLiteralExpr: {
        auto *e = static_cast<const StructLiteralExpr *>(node);
        for (auto &f : e->getFields())
            collectFreeVarsImpl(f.value.get(), params, locals, freeVars);
        break;
    }
    case NK::TryExpr: {
        auto *e = static_cast<const TryExpr *>(node);
        collectFreeVarsImpl(e->getOperand(), params, locals, freeVars);
        break;
    }
    case NK::ClosureExpr: {
        auto *e = static_cast<const ClosureExpr *>(node);
        std::set<std::string> innerParams = params;
        for (auto &p : e->getParams())
            innerParams.insert(p.name);
        std::set<std::string> innerLocals = locals;
        collectFreeVarsImpl(e->getBody(), innerParams, innerLocals, freeVars);
        break;
    }
    // --- Statements ---
    case NK::BlockStmt: {
        auto *s = static_cast<const BlockStmt *>(node);
        for (auto &stmt : s->getStatements())
            collectFreeVarsImpl(stmt.get(), params, locals, freeVars);
        break;
    }
    case NK::ReturnStmt: {
        auto *s = static_cast<const ReturnStmt *>(node);
        if (s->hasValue())
            collectFreeVarsImpl(s->getValue(), params, locals, freeVars);
        break;
    }
    case NK::ExprStmt: {
        auto *s = static_cast<const ExprStmt *>(node);
        collectFreeVarsImpl(s->getExpr(), params, locals, freeVars);
        break;
    }
    case NK::IfStmt: {
        auto *s = static_cast<const IfStmt *>(node);
        collectFreeVarsImpl(s->getCondition(), params, locals, freeVars);
        collectFreeVarsImpl(s->getThenBody(), params, locals, freeVars);
        if (s->hasElse())
            collectFreeVarsImpl(s->getElseBody(), params, locals, freeVars);
        break;
    }
    case NK::IfLetStmt: {
        auto *s = static_cast<const IfLetStmt *>(node);
        collectFreeVarsImpl(s->getOptionalExpr(), params, locals, freeVars);
        std::set<std::string> thenLocals = locals;
        thenLocals.insert(s->getBindingName());
        collectFreeVarsImpl(s->getThenBody(), params, thenLocals, freeVars);
        if (s->hasElse())
            collectFreeVarsImpl(s->getElseBody(), params, locals, freeVars);
        break;
    }
    case NK::WhileStmt: {
        auto *s = static_cast<const WhileStmt *>(node);
        collectFreeVarsImpl(s->getCondition(), params, locals, freeVars);
        collectFreeVarsImpl(s->getBody(), params, locals, freeVars);
        break;
    }
    case NK::ForStmt: {
        auto *s = static_cast<const ForStmt *>(node);
        collectFreeVarsImpl(s->getIterable(), params, locals, freeVars);
        std::set<std::string> bodyLocals = locals;
        bodyLocals.insert(s->getVarName());
        collectFreeVarsImpl(s->getBody(), params, bodyLocals, freeVars);
        break;
    }
    // --- Declarations ---
    case NK::VarDecl: {
        auto *d = static_cast<const VarDecl *>(node);
        if (d->hasInit())
            collectFreeVarsImpl(d->getInit(), params, locals, freeVars);
        locals.insert(d->getName());
        break;
    }
    // Skip: Literals, Break, Continue, FuncDecl, StructDecl, etc.
    default:
        break;
    }
}

static std::vector<std::string> collectFreeVars(
    const ClosureExpr *closure,
    const std::unordered_map<std::string, llvm::AllocaInst *> &outerScope) {
    std::set<std::string> params;
    for (auto &p : closure->getParams())
        params.insert(p.name);
    std::set<std::string> locals;
    std::set<std::string> freeVars;
    collectFreeVarsImpl(closure->getBody(), params, locals, freeVars);
    // Filter: only keep names that exist in outerScope
    std::vector<std::string> result;
    for (auto &name : freeVars) {
        if (outerScope.find(name) != outerScope.end())
            result.push_back(name);
    }
    return result;
}

llvm::Value *IRGen::visitClosureExpr(ClosureExpr *node) {
    // Save outer scope state
    auto savedBlock = builder_->GetInsertBlock();
    auto savedValues = namedValues_;
    auto savedStructTypes2 = varStructTypes_;
    auto savedEnumTypes2 = varEnumTypes_;
    auto savedArrayTypes2 = varArrayTypes_;
    auto savedDynArrayTypes2 = varDynArrayTypes_;
    auto savedOptionalTypes2 = varOptionalTypes_;
    auto savedFuncTypes2 = varFuncTypes_;
    auto savedProtocolTypes2 = varProtocolTypes_;
    auto savedResultTypes2 = varResultTypes_;
    auto *savedFuncRI2 = currentFuncResultInfo_;

    // --- Capture analysis ---
    auto captured = collectFreeVars(node, namedValues_);

    auto *ptrTy = llvm::PointerType::getUnqual(*context_);

    // --- Build environment struct (in outer function context) ---
    llvm::Value *envPtr = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(ptrTy));
    llvm::StructType *envStructTy = nullptr;

    if (!captured.empty()) {
        std::vector<llvm::Type *> envFields;
        for (auto &name : captured) {
            auto *alloca = namedValues_[name];
            envFields.push_back(alloca->getAllocatedType());
        }
        envStructTy = llvm::StructType::create(*context_, envFields,
            "__env_" + std::to_string(closureCounter_));

        auto *outerFunc = builder_->GetInsertBlock()->getParent();
        auto *envAlloca = createEntryBlockAlloca(outerFunc, "env", envStructTy);

        // Copy captured values into env struct
        for (unsigned i = 0; i < captured.size(); ++i) {
            auto *srcAlloca = namedValues_[captured[i]];
            auto *val = builder_->CreateLoad(srcAlloca->getAllocatedType(), srcAlloca,
                                             captured[i] + ".cap");
            auto *gep = builder_->CreateStructGEP(envStructTy, envAlloca, i,
                                                  "env." + captured[i]);
            builder_->CreateStore(val, gep);
        }
        envPtr = envAlloca;
    }

    // --- Create closure function (hidden env param + user params) ---
    std::vector<llvm::Type *> paramTypes;
    paramTypes.push_back(ptrTy); // hidden env parameter
    for (auto &p : node->getParams())
        paramTypes.push_back(toLLVMType(p.type.get()));

    llvm::Type *retTy = node->getReturnType()
        ? toLLVMType(node->getReturnType())
        : builder_->getVoidTy();
    auto *funcTy = llvm::FunctionType::get(retTy, paramTypes, false);

    std::string name = "__closure_" + std::to_string(closureCounter_++);
    auto *func = llvm::Function::Create(
        funcTy, llvm::Function::InternalLinkage, name, module_.get());

    // --- Generate closure body ---
    auto *entry = llvm::BasicBlock::Create(*context_, "entry", func);
    builder_->SetInsertPoint(entry);
    namedValues_.clear();
    varStructTypes_.clear();
    varEnumTypes_.clear();
    varArrayTypes_.clear();
    varDynArrayTypes_.clear();
    varOptionalTypes_.clear();
    varFuncTypes_.clear();
    varProtocolTypes_.clear();
    varResultTypes_.clear();
    currentFuncResultInfo_ = nullptr;

    // Extract captured values from env
    auto argIt = func->arg_begin();
    llvm::Value *envArg = &*argIt; // first arg is env ptr
    envArg->setName("env");
    ++argIt;

    if (!captured.empty() && envStructTy) {
        for (unsigned i = 0; i < captured.size(); ++i) {
            auto *fieldTy = envStructTy->getElementType(i);
            auto *gep = builder_->CreateStructGEP(envStructTy, envArg, i,
                                                  "env." + captured[i]);
            auto *val = builder_->CreateLoad(fieldTy, gep, captured[i] + ".env");
            auto *alloca = createEntryBlockAlloca(func, captured[i], fieldTy);
            builder_->CreateStore(val, alloca);
            namedValues_[captured[i]] = alloca;

            // Restore type tracking for captured variables
            auto stIt = savedStructTypes2.find(captured[i]);
            if (stIt != savedStructTypes2.end())
                varStructTypes_[captured[i]] = stIt->second;
            auto enIt = savedEnumTypes2.find(captured[i]);
            if (enIt != savedEnumTypes2.end())
                varEnumTypes_[captured[i]] = enIt->second;
            auto optIt = savedOptionalTypes2.find(captured[i]);
            if (optIt != savedOptionalTypes2.end())
                varOptionalTypes_[captured[i]] = optIt->second;
            auto fnIt = savedFuncTypes2.find(captured[i]);
            if (fnIt != savedFuncTypes2.end())
                varFuncTypes_[captured[i]] = fnIt->second;
        }
    }

    // Bind user parameters
    unsigned pi = 0;
    for (; argIt != func->arg_end(); ++argIt, ++pi) {
        auto &param = node->getParams()[pi];
        argIt->setName(param.name);
        auto *alloca = createEntryBlockAlloca(func, param.name, argIt->getType());
        builder_->CreateStore(&*argIt, alloca);
        namedValues_[param.name] = alloca;
    }

    // Generate body
    visit(node->getBody());

    if (!builder_->GetInsertBlock()->getTerminator()) {
        if (retTy->isVoidTy()) builder_->CreateRetVoid();
    }

    // --- Restore outer scope and build closure object ---
    builder_->SetInsertPoint(savedBlock);
    namedValues_ = savedValues;
    varStructTypes_ = savedStructTypes2;
    varEnumTypes_ = savedEnumTypes2;
    varArrayTypes_ = savedArrayTypes2;
    varDynArrayTypes_ = savedDynArrayTypes2;
    varOptionalTypes_ = savedOptionalTypes2;
    varFuncTypes_ = savedFuncTypes2;
    varProtocolTypes_ = savedProtocolTypes2;
    varResultTypes_ = savedResultTypes2;
    currentFuncResultInfo_ = savedFuncRI2;

    // Build closure object: { func_ptr, env_ptr }
    auto *closureObjTy = getClosureObjTy();
    auto *outerFunc = builder_->GetInsertBlock()->getParent();
    auto *closureAlloca = createEntryBlockAlloca(outerFunc, "closure.obj", closureObjTy);
    auto *funcGEP = builder_->CreateStructGEP(closureObjTy, closureAlloca, 0, "closure.func");
    builder_->CreateStore(func, funcGEP);
    auto *envGEP = builder_->CreateStructGEP(closureObjTy, closureAlloca, 1, "closure.env");
    builder_->CreateStore(envPtr, envGEP);

    return builder_->CreateLoad(closureObjTy, closureAlloca, "closure.val");
}

llvm::Value *IRGen::visitImplDecl(ImplDecl *node) {
    const auto &typeName = node->getTypeName();

    // Record protocol conformance for vtable creation
    if (node->hasProtocol()) {
        protocolConformances_[node->getProtocolName()].push_back(typeName);
    }

    for (auto &method : node->getMethods()) {
        // Mangled name: TypeName_methodName
        std::string mangledName = typeName + "_" + method->getName();

        // Build param types: self is a pointer to struct
        std::vector<llvm::Type *> paramTypes;
        for (auto &param : method->getParams()) {
            if (param.isSelf) {
                auto stIt = structTypes_.find(typeName);
                if (stIt != structTypes_.end())
                    paramTypes.push_back(llvm::PointerType::getUnqual(*context_));
                else
                    paramTypes.push_back(llvm::PointerType::getUnqual(*context_));
            } else {
                paramTypes.push_back(toLLVMType(param.type.get()));
            }
        }

        auto *returnType = toLLVMType(method->getReturnType());
        auto *funcType = llvm::FunctionType::get(returnType, paramTypes, false);
        auto *func = llvm::Function::Create(
            funcType, llvm::Function::ExternalLinkage, mangledName, *module_);

        // Set parameter names
        size_t i = 0;
        for (auto &arg : func->args()) {
            arg.setName(method->getParams()[i].name);
            ++i;
        }

        if (!method->hasBody())
            continue;

        auto *entryBB = llvm::BasicBlock::Create(*context_, "entry", func);
        builder_->SetInsertPoint(entryBB);

        // Save and clear scope
        auto oldNamedValues = namedValues_;
        auto oldVarStructTypes = varStructTypes_;
        auto oldVarEnumTypes = varEnumTypes_;
        auto oldVarArrayTypes = varArrayTypes_;
        auto oldVarDynArrayTypes = varDynArrayTypes_;
        auto oldVarOptionalTypes = varOptionalTypes_;
        auto oldVarFuncTypes = varFuncTypes_;
        auto oldVarProtocolTypes = varProtocolTypes_;
        auto oldVarResultTypes = varResultTypes_;
        auto *oldFuncRI = currentFuncResultInfo_;
        namedValues_.clear();
        varStructTypes_.clear();
        varEnumTypes_.clear();
        varArrayTypes_.clear();
        varDynArrayTypes_.clear();
        varOptionalTypes_.clear();
        varFuncTypes_.clear();
        varProtocolTypes_.clear();
        varResultTypes_.clear();
        currentFuncResultInfo_ = nullptr;

        // Create allocas for parameters
        i = 0;
        for (auto &arg : func->args()) {
            auto *alloca = createEntryBlockAlloca(func, std::string(arg.getName()),
                                                   arg.getType());
            builder_->CreateStore(&arg, alloca);
            namedValues_[std::string(arg.getName())] = alloca;
            if (method->getParams()[i].isSelf) {
                varStructTypes_["self"] = typeName;
            }
            ++i;
        }

        visitBlockStmt(const_cast<BlockStmt *>(method->getBody()));

        // Add implicit return if needed
        if (!builder_->GetInsertBlock()->getTerminator()) {
            if (returnType->isVoidTy()) {
                builder_->CreateRetVoid();
            } else {
                builder_->CreateRet(llvm::Constant::getNullValue(returnType));
            }
        }

        // Restore scope
        namedValues_ = oldNamedValues;
        varStructTypes_ = oldVarStructTypes;
        varEnumTypes_ = oldVarEnumTypes;
        varArrayTypes_ = oldVarArrayTypes;
        varDynArrayTypes_ = oldVarDynArrayTypes;
        varOptionalTypes_ = oldVarOptionalTypes;
        varFuncTypes_ = oldVarFuncTypes;
        varProtocolTypes_ = oldVarProtocolTypes;
        varResultTypes_ = oldVarResultTypes;
        currentFuncResultInfo_ = oldFuncRI;
    }

    // Generate default method implementations from protocol
    if (node->hasProtocol()) {
        auto pdIt = protocolDecls_.find(node->getProtocolName());
        if (pdIt != protocolDecls_.end()) {
            // Collect implemented method names
            std::set<std::string> implMethods;
            for (auto &m : node->getMethods())
                implMethods.insert(m->getName());

            for (auto &protoMethod : pdIt->second->getMethods()) {
                if (implMethods.count(protoMethod->getName()) || !protoMethod->hasBody())
                    continue;

                std::string mangledName = typeName + "_" + protoMethod->getName();

                std::vector<llvm::Type *> paramTypes;
                for (auto &param : protoMethod->getParams()) {
                    if (param.isSelf) {
                        paramTypes.push_back(llvm::PointerType::getUnqual(*context_));
                    } else {
                        paramTypes.push_back(toLLVMType(param.type.get()));
                    }
                }

                auto *returnType = toLLVMType(protoMethod->getReturnType());
                auto *funcType = llvm::FunctionType::get(returnType, paramTypes, false);
                auto *func = llvm::Function::Create(
                    funcType, llvm::Function::ExternalLinkage, mangledName, *module_);

                size_t i = 0;
                for (auto &arg : func->args()) {
                    arg.setName(protoMethod->getParams()[i].name);
                    ++i;
                }

                auto *entryBB = llvm::BasicBlock::Create(*context_, "entry", func);
                builder_->SetInsertPoint(entryBB);

                auto oldNamedValues = namedValues_;
                auto oldVarStructTypes = varStructTypes_;
                auto oldVarEnumTypes = varEnumTypes_;
                auto oldVarArrayTypes = varArrayTypes_;
                auto oldVarDynArrayTypes = varDynArrayTypes_;
                auto oldVarOptionalTypes = varOptionalTypes_;
                auto oldVarFuncTypes = varFuncTypes_;
                auto oldVarProtocolTypes = varProtocolTypes_;
                auto oldVarResultTypes = varResultTypes_;
                auto *oldFuncRI = currentFuncResultInfo_;
                namedValues_.clear();
                varStructTypes_.clear();
                varEnumTypes_.clear();
                varArrayTypes_.clear();
                varDynArrayTypes_.clear();
                varOptionalTypes_.clear();
                varFuncTypes_.clear();
                varProtocolTypes_.clear();
                varResultTypes_.clear();
                currentFuncResultInfo_ = nullptr;

                i = 0;
                for (auto &arg : func->args()) {
                    auto *alloca = createEntryBlockAlloca(func, std::string(arg.getName()),
                                                           arg.getType());
                    builder_->CreateStore(&arg, alloca);
                    namedValues_[std::string(arg.getName())] = alloca;
                    if (protoMethod->getParams()[i].isSelf) {
                        varStructTypes_["self"] = typeName;
                    }
                    ++i;
                }

                visitBlockStmt(const_cast<BlockStmt *>(protoMethod->getBody()));

                if (!builder_->GetInsertBlock()->getTerminator()) {
                    if (returnType->isVoidTy()) {
                        builder_->CreateRetVoid();
                    } else {
                        builder_->CreateRet(llvm::Constant::getNullValue(returnType));
                    }
                }

                namedValues_ = oldNamedValues;
                varStructTypes_ = oldVarStructTypes;
                varEnumTypes_ = oldVarEnumTypes;
                varArrayTypes_ = oldVarArrayTypes;
                varDynArrayTypes_ = oldVarDynArrayTypes;
                varOptionalTypes_ = oldVarOptionalTypes;
                varFuncTypes_ = oldVarFuncTypes;
                varProtocolTypes_ = oldVarProtocolTypes;
                varResultTypes_ = oldVarResultTypes;
                currentFuncResultInfo_ = oldFuncRI;
            }
        }
    }

    return nullptr;
}

llvm::Value *IRGen::visitEnumDecl(EnumDecl *node) {
    const auto &enumName = node->getName();
    auto &caseMap = enumCases_[enumName];
    int tag = 0;
    bool hasPayload = false;

    for (auto &c : node->getCases()) {
        caseMap[c->getName()] = tag++;
        if (c->hasAssociatedValues())
            hasPayload = true;

        // Record payload types for each case
        std::vector<llvm::Type *> payloadTypes;
        for (auto &assocType : c->getAssociatedTypes()) {
            payloadTypes.push_back(toLLVMType(assocType.get()));
        }
        enumCasePayloads_[enumName][c->getName()] = std::move(payloadTypes);
    }

    if (hasPayload) {
        // Compute max payload size across all cases
        const llvm::DataLayout &dl = module_->getDataLayout();
        uint64_t maxPayloadSize = 0;
        for (auto &c : node->getCases()) {
            uint64_t caseSize = 0;
            for (auto &assocType : c->getAssociatedTypes()) {
                auto *llvmTy = toLLVMType(assocType.get());
                caseSize += dl.getTypeAllocSize(llvmTy);
            }
            if (caseSize > maxPayloadSize)
                maxPayloadSize = caseSize;
        }

        // Create tagged union: { i32 tag, [maxPayloadSize x i8] payload }
        auto *payloadArrayTy = llvm::ArrayType::get(builder_->getInt8Ty(), maxPayloadSize);
        auto *enumStructTy = llvm::StructType::create(
            *context_, {builder_->getInt32Ty(), payloadArrayTy}, enumName);
        enumTypes_[enumName] = enumStructTy;
    }

    return nullptr;
}

llvm::Value *IRGen::visitEnumCaseDecl(EnumCaseDecl *) {
    // Handled by visitEnumDecl
    return nullptr;
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

llvm::Value *IRGen::visitRangeExpr(RangeExpr *) {
    // Range expressions are handled inline by visitForStmt
    return nullptr;
}

llvm::Value *IRGen::visitArrayLiteralExpr(ArrayLiteralExpr *node) {
    auto &elements = node->getElements();
    if (elements.empty()) return nullptr;
    auto *firstVal = visit(elements[0].get());
    if (!firstVal) return nullptr;
    auto *elemType = firstVal->getType();
    uint64_t numElements = elements.size();
    auto *arrayType = llvm::ArrayType::get(elemType, numElements);
    auto *func = builder_->GetInsertBlock()->getParent();
    auto *alloca = createEntryBlockAlloca(func, "arr.tmp", arrayType);
    auto *gep0 = builder_->CreateConstInBoundsGEP2_64(arrayType, alloca, 0, 0, "arr.elem.0");
    builder_->CreateStore(firstVal, gep0);
    for (uint64_t i = 1; i < numElements; ++i) {
        auto *val = visit(elements[i].get());
        if (!val) continue;
        auto *gep = builder_->CreateConstInBoundsGEP2_64(
            arrayType, alloca, 0, i, "arr.elem." + std::to_string(i));
        builder_->CreateStore(val, gep);
    }
    return builder_->CreateLoad(arrayType, alloca, "arr.val");
}

llvm::Value *IRGen::visitIndexExpr(IndexExpr *node) {
    auto *indexVal = visit(const_cast<Expr *>(node->getIndex()));
    if (!indexVal) return nullptr;
    if (node->getBase()->getKind() != ASTNode::NodeKind::IdentifierExpr)
        return nullptr;
    auto *ident = static_cast<const IdentifierExpr *>(node->getBase());

    // Dynamic array index: arr[i]
    auto daIt = varDynArrayTypes_.find(ident->getName());
    if (daIt != varDynArrayTypes_.end()) {
        auto allocaIt = namedValues_.find(ident->getName());
        if (allocaIt == namedValues_.end()) return nullptr;
        auto *arrAlloca = allocaIt->second;
        auto *structTy = getDynArrayStructTy();
        auto *dataField = builder_->CreateStructGEP(structTy, arrAlloca, 0);
        auto *dataPtr = builder_->CreateLoad(llvm::PointerType::getUnqual(*context_), dataField);
        if (indexVal->getType()->isIntegerTy(32))
            indexVal = builder_->CreateSExt(indexVal, builder_->getInt64Ty(), "idx.ext");
        auto *lenField = builder_->CreateStructGEP(structTy, arrAlloca, 1);
        auto *lenVal = builder_->CreateLoad(builder_->getInt64Ty(), lenField, "darr.len");
        emitBoundsCheck(indexVal, lenVal);
        auto *elemPtr = builder_->CreateGEP(daIt->second.elementType, dataPtr, indexVal, "darr.elem");
        return builder_->CreateLoad(daIt->second.elementType, elemPtr, "darr.val");
    }

    auto arrIt = varArrayTypes_.find(ident->getName());
    if (arrIt == varArrayTypes_.end()) return nullptr;
    auto allocaIt = namedValues_.find(ident->getName());
    if (allocaIt == namedValues_.end()) return nullptr;
    auto *alloca = allocaIt->second;
    auto *elemType = arrIt->second.elementType;
    auto *arrayType = alloca->getAllocatedType();
    if (indexVal->getType()->isIntegerTy(32))
        indexVal = builder_->CreateSExt(indexVal, builder_->getInt64Ty(), "idx.ext");
    emitBoundsCheck(indexVal, builder_->getInt64(arrIt->second.size));
    auto *gep = builder_->CreateInBoundsGEP(
        arrayType, alloca, {builder_->getInt64(0), indexVal},
        ident->getName() + ".elem");
    return builder_->CreateLoad(elemType, gep, ident->getName() + ".val");
}

std::string IRGen::mangleGenericFunc(const std::string &baseName,
                                      const std::vector<const TypeRepr *> &typeArgs) {
    std::string result = baseName;
    for (const auto *arg : typeArgs) {
        result += "_";
        result += arg->toString();
    }
    return result;
}

llvm::Function *IRGen::monomorphize(const FuncDecl *funcDecl,
                                     const std::vector<const TypeRepr *> &typeArgs) {
    std::string mangledName = mangleGenericFunc(funcDecl->getName(), typeArgs);

    // Cache check
    auto cacheIt = monomorphizedFuncs_.find(mangledName);
    if (cacheIt != monomorphizedFuncs_.end())
        return cacheIt->second;

    // Save state
    auto savedSubst = currentTypeSubst_;
    auto savedNamedValues = namedValues_;
    auto savedVarStructTypes = varStructTypes_;
    auto savedVarEnumTypes = varEnumTypes_;
    auto savedVarArrayTypes = varArrayTypes_;
    auto savedVarDynArrayTypes = varDynArrayTypes_;
    auto savedVarOptionalTypes = varOptionalTypes_;
    auto savedVarFuncTypes = varFuncTypes_;
    auto savedVarProtocolTypes = varProtocolTypes_;
    auto savedVarResultTypes = varResultTypes_;
    auto *savedFuncRI = currentFuncResultInfo_;
    auto *savedInsertPoint = builder_->GetInsertBlock();

    // Set up type substitution map: T -> i32, U -> f64, ...
    currentTypeSubst_.clear();
    const auto &typeParams = funcDecl->getTypeParams();
    for (size_t i = 0; i < typeParams.size() && i < typeArgs.size(); ++i) {
        currentTypeSubst_[typeParams[i]] = typeArgs[i];
    }

    // Build function type with substituted types
    std::vector<llvm::Type *> paramTypes;
    for (auto &param : funcDecl->getParams()) {
        paramTypes.push_back(toLLVMType(param.type.get()));
    }
    auto *returnType = toLLVMType(funcDecl->getReturnType());
    auto *funcType = llvm::FunctionType::get(returnType, paramTypes, false);
    auto *func = llvm::Function::Create(
        funcType, llvm::Function::ExternalLinkage, mangledName, *module_);

    // Set parameter names
    size_t idx = 0;
    for (auto &arg : func->args()) {
        arg.setName(funcDecl->getParams()[idx].name);
        ++idx;
    }

    // Generate body
    auto *entryBB = llvm::BasicBlock::Create(*context_, "entry", func);
    builder_->SetInsertPoint(entryBB);
    namedValues_.clear();
    varStructTypes_.clear();
    varEnumTypes_.clear();
    varArrayTypes_.clear();
    varDynArrayTypes_.clear();
    varOptionalTypes_.clear();
    varFuncTypes_.clear();
    varProtocolTypes_.clear();
    varResultTypes_.clear();
    currentFuncResultInfo_ = nullptr;

    // Create parameter allocas
    idx = 0;
    for (auto &arg : func->args()) {
        auto *alloca = createEntryBlockAlloca(func, std::string(arg.getName()), arg.getType());
        builder_->CreateStore(&arg, alloca);
        namedValues_[std::string(arg.getName())] = alloca;
        ++idx;
    }

    // Visit body
    visitBlockStmt(const_cast<BlockStmt *>(funcDecl->getBody()));

    // Add missing terminator
    if (!builder_->GetInsertBlock()->getTerminator()) {
        if (returnType->isVoidTy())
            builder_->CreateRetVoid();
        else
            builder_->CreateRet(llvm::Constant::getNullValue(returnType));
    }

    // Cache the result
    monomorphizedFuncs_[mangledName] = func;

    // Restore state
    currentTypeSubst_ = savedSubst;
    namedValues_ = savedNamedValues;
    varStructTypes_ = savedVarStructTypes;
    varEnumTypes_ = savedVarEnumTypes;
    varArrayTypes_ = savedVarArrayTypes;
    varDynArrayTypes_ = savedVarDynArrayTypes;
    varOptionalTypes_ = savedVarOptionalTypes;
    varFuncTypes_ = savedVarFuncTypes;
    varProtocolTypes_ = savedVarProtocolTypes;
    varResultTypes_ = savedVarResultTypes;
    currentFuncResultInfo_ = savedFuncRI;
    if (savedInsertPoint)
        builder_->SetInsertPoint(savedInsertPoint);

    return func;
}

std::string IRGen::mangleGenericStruct(const std::string &baseName,
                                        const std::vector<const TypeRepr *> &typeArgs) {
    std::string result = baseName;
    for (const auto *arg : typeArgs) {
        result += "_";
        result += arg->toString();
    }
    return result;
}

void IRGen::monomorphizeStruct(const StructDecl *structDecl,
                                const std::vector<const TypeRepr *> &typeArgs) {
    std::string mangledName = mangleGenericStruct(structDecl->getName(), typeArgs);
    if (monomorphizedStructs_.count(mangledName))
        return;

    auto savedSubst = currentTypeSubst_;
    currentTypeSubst_.clear();
    const auto &typeParams = structDecl->getTypeParams();
    for (size_t i = 0; i < typeParams.size() && i < typeArgs.size(); ++i)
        currentTypeSubst_[typeParams[i]] = typeArgs[i];

    std::vector<llvm::Type *> fieldTypes;
    std::vector<std::string> fieldNames;
    for (auto &field : structDecl->getFields()) {
        fieldTypes.push_back(toLLVMType(field->getType()));
        fieldNames.push_back(field->getName());
    }

    auto *structType = llvm::StructType::create(*context_, fieldTypes, mangledName);
    structTypes_[mangledName] = structType;
    structFieldNames_[mangledName] = std::move(fieldNames);
    monomorphizedStructs_[mangledName] = true;
    structTypeArgs_[mangledName] = typeArgs;
    currentTypeSubst_ = savedSubst;
}

std::vector<const TypeRepr *> IRGen::inferStructTypeArgs(
    const StructDecl *structDecl,
    const std::vector<StructLiteralExpr::FieldInit> &fieldInits,
    const std::vector<llvm::Value *> &fieldValues) {
    const auto &typeParams = structDecl->getTypeParams();
    std::unordered_map<std::string, const TypeRepr *> inferred;

    for (size_t fi = 0; fi < fieldInits.size(); ++fi) {
        for (auto &fieldDecl : structDecl->getFields()) {
            if (fieldDecl->getName() != fieldInits[fi].name)
                continue;
            const TypeRepr *ft = fieldDecl->getType();
            if (!ft || ft->getKind() != TypeRepr::Kind::Named)
                continue;
            auto *named = static_cast<const NamedTypeRepr *>(ft);
            for (const auto &tp : typeParams) {
                if (named->getName() == tp && inferred.find(tp) == inferred.end()) {
                    if (fi < fieldValues.size() && fieldValues[fi]) {
                        llvm::Type *valTy = fieldValues[fi]->getType();
                        const TypeRepr *inferredType = nullptr;
                        if (valTy->isIntegerTy(32)) {
                            inferredTypes_.push_back(makeI32Type());
                            inferredType = inferredTypes_.back().get();
                        } else if (valTy->isIntegerTy(64)) {
                            inferredTypes_.push_back(makeI64Type());
                            inferredType = inferredTypes_.back().get();
                        } else if (valTy->isDoubleTy()) {
                            inferredTypes_.push_back(makeF64Type());
                            inferredType = inferredTypes_.back().get();
                        } else if (valTy->isIntegerTy(1)) {
                            inferredTypes_.push_back(makeBoolType());
                            inferredType = inferredTypes_.back().get();
                        } else if (valTy->isPointerTy()) {
                            inferredTypes_.push_back(makeStringType());
                            inferredType = inferredTypes_.back().get();
                        }
                        if (inferredType)
                            inferred[tp] = inferredType;
                    }
                    break;
                }
            }
            break;
        }
    }

    std::vector<const TypeRepr *> result;
    for (const auto &tp : typeParams) {
        auto it = inferred.find(tp);
        if (it != inferred.end())
            result.push_back(it->second);
    }
    return result;
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

llvm::Function *IRGen::monomorphizeMethod(const ImplDecl *implDecl,
                                           const FuncDecl *methodDecl,
                                           const std::string &mangledStructName,
                                           const std::vector<const TypeRepr *> &typeArgs) {
    std::string mangledName = mangledStructName + "_" + methodDecl->getName();

    // Cache check
    auto cacheIt = monomorphizedFuncs_.find(mangledName);
    if (cacheIt != monomorphizedFuncs_.end())
        return cacheIt->second;

    // Save state
    auto savedSubst = currentTypeSubst_;
    auto savedNamedValues = namedValues_;
    auto savedVarStructTypes = varStructTypes_;
    auto savedVarEnumTypes = varEnumTypes_;
    auto savedVarArrayTypes = varArrayTypes_;
    auto savedVarDynArrayTypes = varDynArrayTypes_;
    auto savedVarOptionalTypes = varOptionalTypes_;
    auto savedVarFuncTypes = varFuncTypes_;
    auto savedVarProtocolTypes2 = varProtocolTypes_;
    auto savedVarResultTypes2 = varResultTypes_;
    auto *savedFuncRI2 = currentFuncResultInfo_;
    auto *savedInsertPoint = builder_->GetInsertBlock();

    // Set up type substitution from impl's type params
    currentTypeSubst_.clear();
    const auto &typeParams = implDecl->getTypeParams();
    for (size_t i = 0; i < typeParams.size() && i < typeArgs.size(); ++i) {
        currentTypeSubst_[typeParams[i]] = typeArgs[i];
    }

    // Build function type: self is pointer, rest are substituted
    std::vector<llvm::Type *> paramTypes;
    for (auto &param : methodDecl->getParams()) {
        if (param.isSelf) {
            paramTypes.push_back(llvm::PointerType::getUnqual(*context_));
        } else {
            paramTypes.push_back(toLLVMType(param.type.get()));
        }
    }
    auto *returnType = toLLVMType(methodDecl->getReturnType());
    auto *funcType = llvm::FunctionType::get(returnType, paramTypes, false);
    auto *func = llvm::Function::Create(
        funcType, llvm::Function::ExternalLinkage, mangledName, *module_);

    // Set parameter names
    size_t idx = 0;
    for (auto &arg : func->args()) {
        arg.setName(methodDecl->getParams()[idx].name);
        ++idx;
    }

    // Generate body
    auto *entryBB = llvm::BasicBlock::Create(*context_, "entry", func);
    builder_->SetInsertPoint(entryBB);
    namedValues_.clear();
    varStructTypes_.clear();
    varEnumTypes_.clear();
    varArrayTypes_.clear();
    varDynArrayTypes_.clear();
    varOptionalTypes_.clear();
    varFuncTypes_.clear();
    varProtocolTypes_.clear();
    varResultTypes_.clear();
    currentFuncResultInfo_ = nullptr;

    // Create parameter allocas
    idx = 0;
    for (auto &arg : func->args()) {
        auto *alloca = createEntryBlockAlloca(func, std::string(arg.getName()), arg.getType());
        builder_->CreateStore(&arg, alloca);
        namedValues_[std::string(arg.getName())] = alloca;
        if (methodDecl->getParams()[idx].isSelf) {
            varStructTypes_["self"] = mangledStructName;
        }
        ++idx;
    }

    // Visit body
    visitBlockStmt(const_cast<BlockStmt *>(methodDecl->getBody()));

    // Add missing terminator
    if (!builder_->GetInsertBlock()->getTerminator()) {
        if (returnType->isVoidTy())
            builder_->CreateRetVoid();
        else
            builder_->CreateRet(llvm::Constant::getNullValue(returnType));
    }

    // Cache
    monomorphizedFuncs_[mangledName] = func;

    // Restore state
    currentTypeSubst_ = savedSubst;
    namedValues_ = savedNamedValues;
    varStructTypes_ = savedVarStructTypes;
    varEnumTypes_ = savedVarEnumTypes;
    varArrayTypes_ = savedVarArrayTypes;
    varDynArrayTypes_ = savedVarDynArrayTypes;
    varOptionalTypes_ = savedVarOptionalTypes;
    varFuncTypes_ = savedVarFuncTypes;
    varProtocolTypes_ = savedVarProtocolTypes2;
    varResultTypes_ = savedVarResultTypes2;
    currentFuncResultInfo_ = savedFuncRI2;
    if (savedInsertPoint)
        builder_->SetInsertPoint(savedInsertPoint);

    return func;
}

llvm::Value *IRGen::visitTryExpr(TryExpr *node) {
    if (!currentFuncResultInfo_) {
        // If not in a Result-returning function, just evaluate the operand
        return visit(node->getOperand());
    }

    // Evaluate operand (should return a Result value)
    auto *resultVal = visit(node->getOperand());
    if (!resultVal) return nullptr;

    // Store to a temp alloca to extract tag
    auto *resTy = getResultType(currentFuncResultInfo_->okType, currentFuncResultInfo_->errType);
    auto *func = builder_->GetInsertBlock()->getParent();
    auto *tmpAlloca = createEntryBlockAlloca(func, "try.tmp", resTy);
    builder_->CreateStore(resultVal, tmpAlloca);

    auto *tagPtr = builder_->CreateStructGEP(resTy, tmpAlloca, 0, "try.tag");
    auto *tag = builder_->CreateLoad(builder_->getInt32Ty(), tagPtr, "try.tag.val");
    auto *isOk = builder_->CreateICmpEQ(tag, builder_->getInt32(0), "try.isok");

    auto *okBB = llvm::BasicBlock::Create(*context_, "try.ok", func);
    auto *errBB = llvm::BasicBlock::Create(*context_, "try.err", func);
    builder_->CreateCondBr(isOk, okBB, errBB);

    // Error path: propagate the Err by returning it
    builder_->SetInsertPoint(errBB);
    auto *errPayloadPtr = builder_->CreateStructGEP(resTy, tmpAlloca, 1, "try.err.payload");
    auto *errVal = builder_->CreateLoad(currentFuncResultInfo_->errType, errPayloadPtr, "try.err.val");
    auto *errResult = emitResultErr(currentFuncResultInfo_->okType,
                                     currentFuncResultInfo_->errType, errVal);
    builder_->CreateRet(errResult);

    // Ok path: extract the Ok value and continue
    builder_->SetInsertPoint(okBB);
    auto *okPayloadPtr = builder_->CreateStructGEP(resTy, tmpAlloca, 1, "try.ok.payload");
    return builder_->CreateLoad(currentFuncResultInfo_->okType, okPayloadPtr, "try.ok.val");
}

} // namespace liva

#endif // LIVA_HAS_LLVM
