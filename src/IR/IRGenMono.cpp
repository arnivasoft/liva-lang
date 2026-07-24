#include "liva/IR/IRGen.h"

#ifdef LIVA_HAS_LLVM

namespace liva {

static const char *primitiveTypeName(TypeRepr::Kind kind) {
    switch (kind) {
    case TypeRepr::Kind::I32: return "i32";
    case TypeRepr::Kind::I64: return "i64";
    case TypeRepr::Kind::F64: return "f64";
    case TypeRepr::Kind::Bool: return "bool";
    case TypeRepr::Kind::String: return "string";
    case TypeRepr::Kind::Void: return "void";
    case TypeRepr::Kind::I8: return "i8";
    case TypeRepr::Kind::F32: return "f32";
    default: return nullptr;
    }
}

std::string IRGen::mangleGenericFunc(const std::string &baseName,
                                    const std::vector<const TypeRepr *> &typeArgs) {
    std::string result;
    result.reserve(baseName.size() + typeArgs.size() * 5);
    result = baseName;
    for (const auto *arg : typeArgs) {
        result += '_';
        if (arg->getKind() == TypeRepr::Kind::ConstValue) {
            auto *cv = static_cast<const ConstValueTypeRepr *>(arg);
            result += 'c';
            result += cv->isLiteral() ? std::to_string(cv->getValue())
                                      : cv->getParamName();
        } else {
            const char *fast = primitiveTypeName(arg->getKind());
            result += fast ? fast : arg->toString();
        }
    }
    return result;
}

llvm::Function *IRGen::monomorphize(const FuncDecl *funcDecl,
                                     const std::vector<const TypeRepr *> &typeArgs) {
    std::string mangledName = mangleGenericFunc(funcDecl->getName(), typeArgs);

    // Cache check
    auto cacheIt = monomorphizedFuncs_.find(mangledName);
    if (cacheIt != monomorphizedFuncs_.end()) {
        ++monoStats_.funcCacheHits;
        return cacheIt->second;
    }

    // Save state: vars_ via RAII, type/const subst manually (not part of VarState)
    auto savedSubst = std::move(currentTypeSubst_);
    auto savedConstSubst = std::move(currentConstSubst_);
    auto guard = pushVarState();
    vars_ = VarState{};
    auto *savedInsertPoint = builder_->GetInsertBlock();

    // Set up type substitution map: T -> i32, U -> f64, ...
    const auto &typeParams = funcDecl->getTypeParams();
    for (size_t i = 0; i < typeParams.size() && i < typeArgs.size(); ++i) {
        currentTypeSubst_[typeParams[i]] = typeArgs[i];
    }

    // Set up const substitution from ConstValueTypeRepr in typeArgs
    const auto &constParams = funcDecl->getConstParams();
    size_t constIdx = 0;
    for (size_t i = 0; i < typeArgs.size() && constIdx < constParams.size(); ++i) {
        if (typeArgs[i]->getKind() == TypeRepr::Kind::ConstValue) {
            auto *cv = static_cast<const ConstValueTypeRepr *>(typeArgs[i]);
            currentConstSubst_[constParams[constIdx].name] =
                cv->isLiteral() ? cv->getValue() : 0;
            ++constIdx;
        }
    }

    // Per-function return/coroutine codegen state. This body is emitted
    // mid-visit of the CALLER's body, so the state visitFuncDecl established
    // for the caller must not leak in here: a stale currentFuncOptionalInner_
    // wraps plain returns with the caller's Optional type, and a stale coro
    // promise makes returns store/branch into the caller's function. Mirrors
    // visitFuncDecl's setup; monomorphized functions are always emitted as
    // plain sync functions, so async/coro state is cleared unconditionally.
    // Must run AFTER the substitution maps above so toLLVMType resolves T.
    auto *savedFuncOptInner = currentFuncOptionalInner_;
    bool savedIsAsync = currentIsAsync_;
    auto *savedAsyncRetType = asyncDeclaredRetType_;
    auto *savedCoroTask = currentCoroTask_;
    auto *savedCoroHandle = currentCoroHandle_;
    auto *savedCoroId = currentCoroId_;
    auto *savedCoroPromise = currentCoroPromise_;
    auto *savedCoroFinalBB = currentCoroFinalBB_;
    auto *savedCoroCleanupBB = currentCoroCleanupBB_;
    auto *savedCoroSuspendBB = currentCoroSuspendBB_;
    bool savedIsClassInit = currentIsClassInit_;
    currentIsAsync_ = false;
    currentIsClassInit_ = false;
    asyncDeclaredRetType_ = nullptr;
    currentCoroTask_ = nullptr;
    currentCoroHandle_ = nullptr;
    currentCoroId_ = nullptr;
    currentCoroPromise_ = nullptr;
    currentCoroFinalBB_ = nullptr;
    currentCoroCleanupBB_ = nullptr;
    currentCoroSuspendBB_ = nullptr;
    currentFuncOptionalInner_ = nullptr;
    if (funcDecl->getReturnType() &&
        funcDecl->getReturnType()->getKind() == TypeRepr::Kind::Optional) {
        auto *opt = static_cast<const OptionalTypeRepr *>(funcDecl->getReturnType());
        currentFuncOptionalInner_ = toLLVMType(opt->getInner());
    }

    // Build function type with substituted types
    // Helper: compute the LLVM type for a monomorphized parameter, using the
    // same DynArray-struct promotion that visitFuncDecl uses for non-generic funcs.
    auto monoParamType = [&](const TypeRepr *paramTypeRepr) -> llvm::Type * {
        // If the declared type is a type param, substitute first.
        const TypeRepr *effective = paramTypeRepr;
        if (effective && effective->getKind() == TypeRepr::Kind::Named) {
            auto *nr = static_cast<const NamedTypeRepr *>(effective);
            auto sit = currentTypeSubst_.find(nr->getName());
            if (sit != currentTypeSubst_.end()) effective = sit->second;
        }
        // Dynamic array [T] must be passed as the DynArray struct, not a raw ptr.
        if (effective && effective->getKind() == TypeRepr::Kind::Array) {
            auto *arr = static_cast<const ArrayTypeRepr *>(effective);
            if (arr->isDynamic()) return getDynArrayStructTy();
        }
        return toLLVMType(paramTypeRepr);
    };

    std::vector<llvm::Type *> paramTypes;
    for (auto &param : funcDecl->getParams()) {
        paramTypes.push_back(monoParamType(param.type.get()));
    }
    auto *returnType = toLLVMType(funcDecl->getReturnType());
    auto *funcType = llvm::FunctionType::get(returnType, paramTypes, false);
    auto *func = llvm::Function::Create(
        funcType, llvm::Function::LinkOnceODRLinkage, mangledName, *module_);

    // Attach debug info subprogram
    if (diBuilder_) {
        auto *funcDbgType = createFunctionDebugType(funcDecl);
        unsigned lineNo = funcDecl->getStartLoc().isValid() ? funcDecl->getStartLoc().line : 0;
        auto *sp = diBuilder_->createFunction(
            diFile_, mangledName, mangledName, diFile_, lineNo,
            funcDbgType, lineNo,
            llvm::DINode::FlagPrototyped,
            llvm::DISubprogram::SPFlagDefinition);
        func->setSubprogram(sp);
    }

    // Set parameter names
    size_t idx = 0;
    for (auto &arg : func->args()) {
        arg.setName(funcDecl->getParams()[idx].name);
        ++idx;
    }

    // Generate body
    auto *entryBB = llvm::BasicBlock::Create(*context_, "entry", func);
    builder_->SetInsertPoint(entryBB);
    // vars_ already default-constructed above; no clear needed.

    // Create parameter allocas.
    // For parameters whose type was substituted from a generic type param
    // (e.g. `iter: I` with I → Counter), also register the concrete struct
    // name in vars_.varStructTypes so method calls on those params dispatch
    // correctly inside the monomorphized body (e.g. `iter.next()` → Counter_next).
    idx = 0;
    for (auto &arg : func->args()) {
        auto *alloca = createEntryBlockAlloca(func, std::string(arg.getName()), arg.getType());
        builder_->CreateStore(&arg, alloca);
        vars_.namedValues[std::string(arg.getName())] = alloca;
        // If this param's declared type is a generic type param that maps to a
        // concrete struct, register the struct name for method dispatch.
        if (idx < funcDecl->getParams().size()) {
            const auto &paramTypeRepr = funcDecl->getParams()[idx].type;
            if (paramTypeRepr && paramTypeRepr->getKind() == TypeRepr::Kind::Named) {
                auto *namedTR = static_cast<const NamedTypeRepr *>(paramTypeRepr.get());
                auto substIt = currentTypeSubst_.find(namedTR->getName());
                if (substIt != currentTypeSubst_.end()) {
                    // The param's declared type is a type param — find the concrete name.
                    const TypeRepr *concrete = substIt->second;
                    if (concrete->getKind() == TypeRepr::Kind::Named) {
                        const std::string &concreteName =
                            static_cast<const NamedTypeRepr *>(concrete)->getName();
                        if (structTypes_.count(concreteName)) {
                            vars_.varStructTypes[std::string(arg.getName())] = concreteName;
                        }
                    }
                    // If the concrete type is a dynamic array [T], register it for
                    // DynArray iteration so visitForStmt emits the built-in path.
                    else if (concrete->getKind() == TypeRepr::Kind::Array) {
                        auto *arrTR = static_cast<const ArrayTypeRepr *>(concrete);
                        if (arrTR->isDynamic()) {
                            auto *elemType = toLLVMType(arrTR->getElement());
                            auto &DL = module_->getDataLayout();
                            uint64_t elemSize = DL.getTypeAllocSize(elemType);
                            vars_.varDynArrayTypes[std::string(arg.getName())] = {elemType, elemSize};
                            // Params borrowed from caller — skip cleanup to avoid double-free
                            vars_.movedVars.insert(std::string(arg.getName()));
                        }
                    }
                }
            }
        }
        ++idx;
    }

    // Visit body
    visitBlockStmt(const_cast<BlockStmt *>(funcDecl->getBody()));

    // Add missing terminator
    if (!builder_->GetInsertBlock()->getTerminator()) {
        emitScopeCleanup();
        if (returnType->isVoidTy())
            builder_->CreateRetVoid();
        else
            builder_->CreateRet(llvm::Constant::getNullValue(returnType));
    }

    // Cache the result
    monomorphizedFuncs_[mangledName] = func;
    ++monoStats_.funcCount;

    // Restore state: vars_ restored by guard dtor; type/const subst manually
    currentFuncOptionalInner_ = savedFuncOptInner;
    currentIsAsync_ = savedIsAsync;
    asyncDeclaredRetType_ = savedAsyncRetType;
    currentCoroTask_ = savedCoroTask;
    currentCoroHandle_ = savedCoroHandle;
    currentCoroId_ = savedCoroId;
    currentCoroPromise_ = savedCoroPromise;
    currentCoroFinalBB_ = savedCoroFinalBB;
    currentCoroCleanupBB_ = savedCoroCleanupBB;
    currentCoroSuspendBB_ = savedCoroSuspendBB;
    currentIsClassInit_ = savedIsClassInit;
    currentTypeSubst_ = std::move(savedSubst);
    currentConstSubst_ = std::move(savedConstSubst);
    if (savedInsertPoint)
        builder_->SetInsertPoint(savedInsertPoint);

    return func;
}

std::string IRGen::mangleGenericStruct(const std::string &baseName,
                                        const std::vector<const TypeRepr *> &typeArgs) {
    std::string result;
    result.reserve(baseName.size() + typeArgs.size() * 5);
    result = baseName;
    for (const auto *arg : typeArgs) {
        result += '_';
        const char *fast = primitiveTypeName(arg->getKind());
        result += fast ? fast : arg->toString();
    }
    return result;
}

// Walk a TypeRepr and produce a substituted clone, replacing any
// NamedType reference whose name matches a key in `subst` with the
// substitute. The returned type lives inside `inferredTypes_` so the
// pointers stored in structFieldTypeReprs_ stay valid for the lifetime
// of the IRGen.
const TypeRepr *IRGen::substituteTypeRepr(
    const TypeRepr *type,
    const std::unordered_map<std::string, const TypeRepr *> &subst) {
    if (!type) return nullptr;
    switch (type->getKind()) {
    case TypeRepr::Kind::Named: {
        auto *n = static_cast<const NamedTypeRepr *>(type);
        auto it = subst.find(n->getName());
        if (it != subst.end()) return it->second;
        return type; // no substitution needed, reuse original
    }
    case TypeRepr::Kind::Optional: {
        auto *o = static_cast<const OptionalTypeRepr *>(type);
        const TypeRepr *innerSub = substituteTypeRepr(o->getInner(), subst);
        if (innerSub == o->getInner()) return type; // unchanged
        inferredTypes_.push_back(std::make_unique<OptionalTypeRepr>(
            cloneTypeRepr(innerSub)));
        return inferredTypes_.back().get();
    }
    case TypeRepr::Kind::Array: {
        auto *a = static_cast<const ArrayTypeRepr *>(type);
        const TypeRepr *elemSub = substituteTypeRepr(a->getElement(), subst);
        if (elemSub == a->getElement()) return type;
        inferredTypes_.push_back(std::make_unique<ArrayTypeRepr>(
            cloneTypeRepr(elemSub), a->getSize()));
        return inferredTypes_.back().get();
    }
    default:
        return type;
    }
}

void IRGen::monomorphizeStruct(const StructDecl *structDecl,
                                const std::vector<const TypeRepr *> &typeArgs) {
    std::string mangledName = mangleGenericStruct(structDecl->getName(), typeArgs);
    if (monomorphizedStructs_.count(mangledName)) {
        ++monoStats_.structCacheHits;
        return;
    }

    auto savedSubst = currentTypeSubst_;
    auto savedConstSubst = currentConstSubst_;
    currentTypeSubst_.clear();
    currentConstSubst_.clear();
    const auto &typeParams = structDecl->getTypeParams();
    for (size_t i = 0; i < typeParams.size() && i < typeArgs.size(); ++i)
        currentTypeSubst_[typeParams[i]] = typeArgs[i];

    // Set up const substitution for struct const params
    // (ConstValueTypeRepr in typeArgs maps to constParams by index)

    std::vector<llvm::Type *> fieldTypes;
    std::vector<std::string> fieldNames;
    std::vector<const TypeRepr *> fieldTypeReprs;
    for (auto &field : structDecl->getFields()) {
        // Dynamic array field → use DynArray struct (24-byte by-value layout)
        // instead of ptr so the field can store {data, len, cap} in-line.
        // Mirrors declareExternStruct / visitStructDecl.
        auto *fieldType = field->getType();
        llvm::Type *llvmTy = nullptr;
        if (fieldType && fieldType->getKind() == TypeRepr::Kind::Array) {
            auto *arrRepr = static_cast<const ArrayTypeRepr *>(fieldType);
            if (arrRepr->isDynamic())
                llvmTy = getDynArrayStructTy();
        }
        if (!llvmTy) llvmTy = toLLVMType(fieldType);
        fieldTypes.push_back(llvmTy);
        fieldNames.push_back(field->getName());
        // Record substituted field type so member access (`x.field.length`,
        // dyn-array detection, etc.) sees `[string]` instead of `[T]`.
        fieldTypeReprs.push_back(
            substituteTypeRepr(fieldType, currentTypeSubst_));
    }

    auto *structType = llvm::StructType::create(*context_, fieldTypes, mangledName);
    structTypes_[mangledName] = structType;
    structFieldNames_[mangledName] = std::move(fieldNames);
    structFieldTypeReprs_[mangledName] = std::move(fieldTypeReprs);
    monomorphizedStructs_.insert(mangledName);
    structTypeArgs_[mangledName] = typeArgs;
    ++monoStats_.structCount;
    currentTypeSubst_ = savedSubst;
    currentConstSubst_ = savedConstSubst;
}

std::vector<const TypeRepr *> IRGen::inferStructTypeArgs(
    const StructDecl *structDecl,
    const std::vector<StructLiteralExpr::FieldInit> &fieldInits,
    const std::vector<llvm::Value *> &fieldValues) {
    const auto &typeParams = structDecl->getTypeParams();
    const auto &fields = structDecl->getFields();
    std::unordered_map<std::string, const TypeRepr *> inferred;

    // Build field-name → index map for O(1) lookup
    std::unordered_map<std::string, size_t> fieldNameToIdx;
    fieldNameToIdx.reserve(fields.size());
    for (size_t i = 0; i < fields.size(); ++i)
        fieldNameToIdx[fields[i]->getName()] = i;

    // Build type-param set for O(1) membership check
    std::unordered_set<std::string> typeParamSet(typeParams.begin(), typeParams.end());

    for (size_t fi = 0; fi < fieldInits.size(); ++fi) {
        auto nameIt = fieldNameToIdx.find(fieldInits[fi].name);
        if (nameIt == fieldNameToIdx.end())
            continue;
        const TypeRepr *ft = fields[nameIt->second]->getType();
        if (!ft)
            continue;
        // `var v: [T]` — fieldValues[fi] is a %DynArray whose LLVM type says
        // nothing about the element, so T has to come from the initializer.
        if (ft->getKind() == TypeRepr::Kind::Array) {
            auto *arrRepr = static_cast<const ArrayTypeRepr *>(ft);
            const TypeRepr *elemRepr = arrRepr->getElement();
            if (!elemRepr || elemRepr->getKind() != TypeRepr::Kind::Named)
                continue;
            const std::string &elemName =
                static_cast<const NamedTypeRepr *>(elemRepr)->getName();
            if (!typeParamSet.count(elemName) || inferred.count(elemName))
                continue;
            if (const TypeRepr *elemTy =
                    inferArrayFieldElemType(fieldInits[fi].value.get()))
                inferred[elemName] = elemTy;
            continue;
        }
        if (ft->getKind() != TypeRepr::Kind::Named)
            continue;
        auto *named = static_cast<const NamedTypeRepr *>(ft);
        const std::string &tpName = named->getName();
        if (typeParamSet.count(tpName) && inferred.find(tpName) == inferred.end()) {
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
                    inferred[tpName] = inferredType;
            }
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

const TypeRepr *IRGen::inferArrayFieldElemType(const Expr *init) {
    if (!init) return nullptr;
    if (init->getKind() == ASTNode::NodeKind::ArrayLiteralExpr) {
        // `Box { v: [10, 20, 30] }` — Sema resolved each element.
        auto *lit = static_cast<const ArrayLiteralExpr *>(init);
        const auto &elems = lit->getElements();
        // An empty literal carries no element type; the caller diagnoses it.
        return elems.empty() ? nullptr : elems[0]->getResolvedType();
    }
    // `let src: [i32] = ...; Box { v: src }` — Sema resolved the whole
    // expression, so the element type is one level down.
    if (const TypeRepr *rt = init->getResolvedType())
        if (rt->getKind() == TypeRepr::Kind::Array)
            return static_cast<const ArrayTypeRepr *>(rt)->getElement();
    return nullptr;
}

void IRGen::diagnoseGenericStructTypeArgs(
    const StructDecl *structDecl,
    const std::vector<const TypeRepr *> &typeArgs,
    const StructLiteralExpr *literal) {
    if (typeArgs.size() == structDecl->getTypeParams().size())
        return;
    // Without every type parameter bound, the fields lower against the
    // unsubstituted `T` — an opaque pointer — which silently miscompiles
    // element reads and clone strides. Report it; codegen then carries on
    // with the old lowering purely so the rest of the function still emits
    // and the user sees this error alone instead of a cascade of internal
    // ones. The reported error already fails the compilation.
    const std::string &name = literal->getTypeName();
    diag_.report(literal->getStartLoc(),
                 DiagID::err_generic_struct_type_args_uninferred, name, name);
}

llvm::Function *IRGen::monomorphizeMethod(const ImplDecl *implDecl,
                                           const FuncDecl *methodDecl,
                                           const std::string &mangledStructName,
                                           const std::vector<const TypeRepr *> &typeArgs,
                                           const std::vector<const TypeRepr *> &methodTypeArgs) {
    std::string mangledName = mangledStructName + "_" + methodDecl->getName();
    // Append method-level type args to the mangled name so different
    // U-instantiations get distinct functions.
    for (const auto *arg : methodTypeArgs) {
        mangledName += '_';
        const char *fast = primitiveTypeName(arg->getKind());
        mangledName += fast ? fast : arg->toString();
    }

    // Cache check
    auto cacheIt = monomorphizedFuncs_.find(mangledName);
    if (cacheIt != monomorphizedFuncs_.end()) {
        ++monoStats_.methodCacheHits;
        return cacheIt->second;
    }

    // Save state: vars_ via RAII, type/const subst manually (not part of VarState)
    auto savedSubst = std::move(currentTypeSubst_);
    auto savedConstSubst = std::move(currentConstSubst_);
    auto guard = pushVarState();
    vars_ = VarState{};
    auto *savedInsertPoint = builder_->GetInsertBlock();

    // Set up type substitution from impl's type params
    const auto &typeParams = implDecl->getTypeParams();
    for (size_t i = 0; i < typeParams.size() && i < typeArgs.size(); ++i) {
        currentTypeSubst_[typeParams[i]] = typeArgs[i];
    }
    // Method-level type params (U from `func map<U>`) bind on top.
    const auto &methodParams = methodDecl->getTypeParams();
    for (size_t i = 0; i < methodParams.size() && i < methodTypeArgs.size(); ++i) {
        currentTypeSubst_[methodParams[i]] = methodTypeArgs[i];
    }

    // Per-function return/coroutine codegen state — same reasoning as in
    // monomorphize() above: don't inherit the caller's Optional-return flag
    // or coro pointers, and set up our own Optional inner type so `-> T?`
    // bodies wrap their return values. Must run AFTER the substitution maps
    // above so toLLVMType resolves T.
    auto *savedFuncOptInner = currentFuncOptionalInner_;
    bool savedIsAsync = currentIsAsync_;
    auto *savedAsyncRetType = asyncDeclaredRetType_;
    auto *savedCoroTask = currentCoroTask_;
    auto *savedCoroHandle = currentCoroHandle_;
    auto *savedCoroId = currentCoroId_;
    auto *savedCoroPromise = currentCoroPromise_;
    auto *savedCoroFinalBB = currentCoroFinalBB_;
    auto *savedCoroCleanupBB = currentCoroCleanupBB_;
    auto *savedCoroSuspendBB = currentCoroSuspendBB_;
    bool savedIsClassInit = currentIsClassInit_;
    currentIsAsync_ = false;
    currentIsClassInit_ = false;
    asyncDeclaredRetType_ = nullptr;
    currentCoroTask_ = nullptr;
    currentCoroHandle_ = nullptr;
    currentCoroId_ = nullptr;
    currentCoroPromise_ = nullptr;
    currentCoroFinalBB_ = nullptr;
    currentCoroCleanupBB_ = nullptr;
    currentCoroSuspendBB_ = nullptr;
    currentFuncOptionalInner_ = nullptr;
    if (methodDecl->getReturnType() &&
        methodDecl->getReturnType()->getKind() == TypeRepr::Kind::Optional) {
        auto *opt = static_cast<const OptionalTypeRepr *>(methodDecl->getReturnType());
        currentFuncOptionalInner_ = toLLVMType(opt->getInner());
    }

    // Build function type: self is pointer, rest are substituted.
    // Dynamic arrays must be passed as the DynArray struct value, not ptr —
    // toLLVMType maps Array→ptr by default but the calling code treats [T]
    // params as struct-by-value (matching visitFuncDecl).
    std::vector<llvm::Type *> paramTypes;
    for (auto &param : methodDecl->getParams()) {
        if (param.isSelf) {
            paramTypes.push_back(llvm::PointerType::getUnqual(*context_));
        } else if (param.type && param.type->getKind() == TypeRepr::Kind::Array) {
            auto *arrTy = static_cast<const ArrayTypeRepr *>(param.type.get());
            if (arrTy->isDynamic())
                paramTypes.push_back(getDynArrayStructTy());
            else
                paramTypes.push_back(toLLVMType(param.type.get()));
        } else {
            paramTypes.push_back(toLLVMType(param.type.get()));
        }
    }
    auto *returnType = toLLVMType(methodDecl->getReturnType());
    if (methodDecl->getReturnType() &&
        methodDecl->getReturnType()->getKind() == TypeRepr::Kind::Array) {
        auto *arrRet = static_cast<const ArrayTypeRepr *>(methodDecl->getReturnType());
        if (arrRet->isDynamic())
            returnType = getDynArrayStructTy();
    }
    auto *funcType = llvm::FunctionType::get(returnType, paramTypes, false);
    auto *func = llvm::Function::Create(
        funcType, llvm::Function::LinkOnceODRLinkage, mangledName, *module_);

    // Attach debug info subprogram
    if (diBuilder_) {
        auto *funcDbgType = createFunctionDebugType(methodDecl);
        unsigned lineNo = methodDecl->getStartLoc().isValid() ? methodDecl->getStartLoc().line : 0;
        auto *sp = diBuilder_->createFunction(
            diFile_, mangledName, mangledName, diFile_, lineNo,
            funcDbgType, lineNo,
            llvm::DINode::FlagPrototyped,
            llvm::DISubprogram::SPFlagDefinition);
        func->setSubprogram(sp);
    }

    // Set parameter names
    size_t idx = 0;
    for (auto &arg : func->args()) {
        arg.setName(methodDecl->getParams()[idx].name);
        ++idx;
    }

    // Generate body
    auto *entryBB = llvm::BasicBlock::Create(*context_, "entry", func);
    builder_->SetInsertPoint(entryBB);
    // vars_ already default-constructed above; no clear needed.

    // Create parameter allocas
    idx = 0;
    for (auto &arg : func->args()) {
        auto *alloca = createEntryBlockAlloca(func, std::string(arg.getName()), arg.getType());
        builder_->CreateStore(&arg, alloca);
        vars_.namedValues[std::string(arg.getName())] = alloca;
        if (methodDecl->getParams()[idx].isSelf) {
            vars_.varStructTypes["self"] = mangledStructName;
        }
        ++idx;
    }

    // Register Function-typed method parameters in vars_.varFuncTypes so calls
    // like `pred(x)` inside the body resolve through the closure object
    // rather than as undefined function names. Mirrors visitImplDecl.
    for (auto &param : methodDecl->getParams()) {
        if (!param.isSelf && param.type &&
            param.type->getKind() == TypeRepr::Kind::Function) {
            auto *ftr = static_cast<const FunctionTypeRepr *>(param.type.get());
            std::vector<llvm::Type *> fParamTypes;
            fParamTypes.push_back(llvm::PointerType::getUnqual(*context_));
            for (auto &p : ftr->getParams())
                fParamTypes.push_back(toLLVMType(p.get()));
            llvm::Type *fRetTy = ftr->getReturnType()
                ? toLLVMType(ftr->getReturnType()) : builder_->getVoidTy();
            vars_.varFuncTypes[param.name] =
                llvm::FunctionType::get(fRetTy, fParamTypes, false);
        }
    }
    // Mark [T] params as borrowed (skip in scope cleanup) — caller owns the
    // backing buffer.
    for (auto &param : methodDecl->getParams()) {
        if (!param.isSelf && param.type &&
            param.type->getKind() == TypeRepr::Kind::Array) {
            auto *arrTy = static_cast<const ArrayTypeRepr *>(param.type.get());
            if (arrTy->isDynamic()) {
                auto *elemType = toLLVMType(arrTy->getElement());
                uint64_t elemSize = module_->getDataLayout()
                    .getTypeAllocSize(elemType);
                vars_.varDynArrayTypes[param.name] = {elemType, elemSize};
                vars_.movedVars.insert(param.name);
            }
        }
    }

    // Visit body
    visitBlockStmt(const_cast<BlockStmt *>(methodDecl->getBody()));

    // Add missing terminator
    if (!builder_->GetInsertBlock()->getTerminator()) {
        emitScopeCleanup();
        if (returnType->isVoidTy())
            builder_->CreateRetVoid();
        else
            builder_->CreateRet(llvm::Constant::getNullValue(returnType));
    }

    // Cache
    monomorphizedFuncs_[mangledName] = func;
    ++monoStats_.methodCount;

    // Restore state: vars_ restored by guard dtor; type/const subst manually
    currentFuncOptionalInner_ = savedFuncOptInner;
    currentIsAsync_ = savedIsAsync;
    asyncDeclaredRetType_ = savedAsyncRetType;
    currentCoroTask_ = savedCoroTask;
    currentCoroHandle_ = savedCoroHandle;
    currentCoroId_ = savedCoroId;
    currentCoroPromise_ = savedCoroPromise;
    currentCoroFinalBB_ = savedCoroFinalBB;
    currentCoroCleanupBB_ = savedCoroCleanupBB;
    currentCoroSuspendBB_ = savedCoroSuspendBB;
    currentIsClassInit_ = savedIsClassInit;
    currentTypeSubst_ = std::move(savedSubst);
    currentConstSubst_ = std::move(savedConstSubst);
    if (savedInsertPoint)
        builder_->SetInsertPoint(savedInsertPoint);

    return func;
}

} // namespace liva

#endif // LIVA_HAS_LLVM
