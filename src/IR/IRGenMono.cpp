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

    // Save state (move for O(1) swap)
    auto savedSubst = std::move(currentTypeSubst_);
    auto savedConstSubst = std::move(currentConstSubst_);
    auto savedNamedValues = std::move(namedValues_);
    auto savedVarStructTypes = std::move(varStructTypes_);
    auto savedVarEnumTypes = std::move(varEnumTypes_);
    auto savedVarArrayTypes = std::move(varArrayTypes_);
    auto savedVarDynArrayTypes = std::move(varDynArrayTypes_);
    auto savedVarDynArrayProtocol = std::move(varDynArrayProtocol_);
    auto savedVarMapTypes = std::move(varMapTypes_);
    auto savedVarSetTypes = std::move(varSetTypes_);
    auto savedVarOptionalTypes = std::move(varOptionalTypes_);
    auto savedVarFuncTypes = std::move(varFuncTypes_);
    auto savedVarProtocolTypes = std::move(varProtocolTypes_);
    auto savedVarConcreteProtocolTypes = std::move(varConcreteProtocolTypes_);
    auto savedVarResultTypes = std::move(varResultTypes_);
    auto *savedFuncRI = currentFuncResultInfo_;
    auto *savedInsertPoint = builder_->GetInsertBlock();
    auto savedMovedVars = std::move(movedVars_);
    auto savedHeapStringVars = std::move(heapStringVars_);
    auto savedTempStrings = std::move(tempStrings_);

    // Set up type substitution map: T -> i32, U -> f64, ...
    // After move, containers are empty — no need for .clear()
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

    // Build function type with substituted types
    std::vector<llvm::Type *> paramTypes;
    for (auto &param : funcDecl->getParams()) {
        paramTypes.push_back(toLLVMType(param.type.get()));
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
    // After move, all containers are already empty
    varFileTypes_.clear();
    varFileOptionalTypes_.clear();
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
        emitScopeCleanup();
        if (returnType->isVoidTy())
            builder_->CreateRetVoid();
        else
            builder_->CreateRet(llvm::Constant::getNullValue(returnType));
    }

    // Cache the result
    monomorphizedFuncs_[mangledName] = func;
    ++monoStats_.funcCount;

    // Restore state (move back)
    currentTypeSubst_ = std::move(savedSubst);
    currentConstSubst_ = std::move(savedConstSubst);
    namedValues_ = std::move(savedNamedValues);
    varStructTypes_ = std::move(savedVarStructTypes);
    varEnumTypes_ = std::move(savedVarEnumTypes);
    varArrayTypes_ = std::move(savedVarArrayTypes);
    varDynArrayTypes_ = std::move(savedVarDynArrayTypes);
    varDynArrayProtocol_ = std::move(savedVarDynArrayProtocol);
    varMapTypes_ = std::move(savedVarMapTypes);
    varSetTypes_ = std::move(savedVarSetTypes);
    varOptionalTypes_ = std::move(savedVarOptionalTypes);
    varFuncTypes_ = std::move(savedVarFuncTypes);
    varProtocolTypes_ = std::move(savedVarProtocolTypes);
    varConcreteProtocolTypes_ = std::move(savedVarConcreteProtocolTypes);
    varResultTypes_ = std::move(savedVarResultTypes);
    currentFuncResultInfo_ = savedFuncRI;
    movedVars_ = std::move(savedMovedVars);
    heapStringVars_ = std::move(savedHeapStringVars);
    tempStrings_ = std::move(savedTempStrings);
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
        if (!ft || ft->getKind() != TypeRepr::Kind::Named)
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

llvm::Function *IRGen::monomorphizeMethod(const ImplDecl *implDecl,
                                           const FuncDecl *methodDecl,
                                           const std::string &mangledStructName,
                                           const std::vector<const TypeRepr *> &typeArgs) {
    std::string mangledName = mangledStructName + "_" + methodDecl->getName();

    // Cache check
    auto cacheIt = monomorphizedFuncs_.find(mangledName);
    if (cacheIt != monomorphizedFuncs_.end()) {
        ++monoStats_.methodCacheHits;
        return cacheIt->second;
    }

    // Save state (move for O(1) swap)
    auto savedSubst = std::move(currentTypeSubst_);
    auto savedConstSubst = std::move(currentConstSubst_);
    auto savedNamedValues = std::move(namedValues_);
    auto savedVarStructTypes = std::move(varStructTypes_);
    auto savedVarEnumTypes = std::move(varEnumTypes_);
    auto savedVarArrayTypes = std::move(varArrayTypes_);
    auto savedVarDynArrayTypes = std::move(varDynArrayTypes_);
    auto savedVarDynArrayProtocol2 = std::move(varDynArrayProtocol_);
    auto savedVarMapTypes = std::move(varMapTypes_);
    auto savedVarSetTypes = std::move(varSetTypes_);
    auto savedVarOptionalTypes = std::move(varOptionalTypes_);
    auto savedVarFuncTypes = std::move(varFuncTypes_);
    auto savedVarProtocolTypes2 = std::move(varProtocolTypes_);
    auto savedVarConcreteProtocolTypes2 = std::move(varConcreteProtocolTypes_);
    auto savedVarResultTypes2 = std::move(varResultTypes_);
    auto *savedFuncRI2 = currentFuncResultInfo_;
    auto *savedInsertPoint = builder_->GetInsertBlock();
    auto savedMovedVars2 = std::move(movedVars_);
    auto savedHeapStringVars2 = std::move(heapStringVars_);
    auto savedTempStrings2 = std::move(tempStrings_);

    // Set up type substitution from impl's type params
    // After move, containers are empty — no need for .clear()
    const auto &typeParams = implDecl->getTypeParams();
    for (size_t i = 0; i < typeParams.size() && i < typeArgs.size(); ++i) {
        currentTypeSubst_[typeParams[i]] = typeArgs[i];
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
    // After move, all containers are already empty
    varFileTypes_.clear();
    varFileOptionalTypes_.clear();
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

    // Register Function-typed method parameters in varFuncTypes_ so calls
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
            varFuncTypes_[param.name] =
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
                varDynArrayTypes_[param.name] = {elemType, elemSize};
                movedVars_.insert(param.name);
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

    // Restore state (move back)
    currentTypeSubst_ = std::move(savedSubst);
    currentConstSubst_ = std::move(savedConstSubst);
    namedValues_ = std::move(savedNamedValues);
    varStructTypes_ = std::move(savedVarStructTypes);
    varEnumTypes_ = std::move(savedVarEnumTypes);
    varArrayTypes_ = std::move(savedVarArrayTypes);
    varDynArrayTypes_ = std::move(savedVarDynArrayTypes);
    varDynArrayProtocol_ = std::move(savedVarDynArrayProtocol2);
    varMapTypes_ = std::move(savedVarMapTypes);
    varSetTypes_ = std::move(savedVarSetTypes);
    varOptionalTypes_ = std::move(savedVarOptionalTypes);
    varFuncTypes_ = std::move(savedVarFuncTypes);
    varProtocolTypes_ = std::move(savedVarProtocolTypes2);
    varConcreteProtocolTypes_ = std::move(savedVarConcreteProtocolTypes2);
    varResultTypes_ = std::move(savedVarResultTypes2);
    currentFuncResultInfo_ = savedFuncRI2;
    movedVars_ = std::move(savedMovedVars2);
    heapStringVars_ = std::move(savedHeapStringVars2);
    tempStrings_ = std::move(savedTempStrings2);
    if (savedInsertPoint)
        builder_->SetInsertPoint(savedInsertPoint);

    return func;
}

} // namespace liva

#endif // LIVA_HAS_LLVM
