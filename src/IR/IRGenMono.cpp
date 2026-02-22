#include "liva/IR/IRGen.h"

#ifdef LIVA_HAS_LLVM

namespace liva {

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
    auto savedVarDynArrayProtocol = varDynArrayProtocol_;
    auto savedVarMapTypes = varMapTypes_;
    auto savedVarSetTypes = varSetTypes_;
    auto savedVarOptionalTypes = varOptionalTypes_;
    auto savedVarFuncTypes = varFuncTypes_;
    auto savedVarProtocolTypes = varProtocolTypes_;
    auto savedVarConcreteProtocolTypes = varConcreteProtocolTypes_;
    auto savedVarResultTypes = varResultTypes_;
    auto *savedFuncRI = currentFuncResultInfo_;
    auto *savedInsertPoint = builder_->GetInsertBlock();
    auto savedMovedVars = movedVars_;
    auto savedHeapStringVars = heapStringVars_;
    auto savedTempStrings = tempStrings_;

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
    namedValues_.clear();
    varStructTypes_.clear();
    varEnumTypes_.clear();
    varArrayTypes_.clear();
    varDynArrayTypes_.clear();
    varDynArrayProtocol_.clear();
    varMapTypes_.clear();
    varSetTypes_.clear();
    varOptionalTypes_.clear();
    varFuncTypes_.clear();
    varProtocolTypes_.clear();
    varConcreteProtocolTypes_.clear();
    varResultTypes_.clear();
    varFileTypes_.clear();
    varFileOptionalTypes_.clear();
    currentFuncResultInfo_ = nullptr;
    movedVars_.clear();
    heapStringVars_.clear();
    tempStrings_.clear();

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

    // Restore state
    currentTypeSubst_ = savedSubst;
    namedValues_ = savedNamedValues;
    varStructTypes_ = savedVarStructTypes;
    varEnumTypes_ = savedVarEnumTypes;
    varArrayTypes_ = savedVarArrayTypes;
    varDynArrayTypes_ = savedVarDynArrayTypes;
    varDynArrayProtocol_ = savedVarDynArrayProtocol;
    varMapTypes_ = savedVarMapTypes;
    varSetTypes_ = savedVarSetTypes;
    varOptionalTypes_ = savedVarOptionalTypes;
    varFuncTypes_ = savedVarFuncTypes;
    varProtocolTypes_ = savedVarProtocolTypes;
    varConcreteProtocolTypes_ = savedVarConcreteProtocolTypes;
    varResultTypes_ = savedVarResultTypes;
    currentFuncResultInfo_ = savedFuncRI;
    movedVars_ = savedMovedVars;
    heapStringVars_ = savedHeapStringVars;
    tempStrings_ = savedTempStrings;
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
    auto savedVarDynArrayProtocol2 = varDynArrayProtocol_;
    auto savedVarMapTypes = varMapTypes_;
    auto savedVarSetTypes = varSetTypes_;
    auto savedVarOptionalTypes = varOptionalTypes_;
    auto savedVarFuncTypes = varFuncTypes_;
    auto savedVarProtocolTypes2 = varProtocolTypes_;
    auto savedVarConcreteProtocolTypes2 = varConcreteProtocolTypes_;
    auto savedVarResultTypes2 = varResultTypes_;
    auto *savedFuncRI2 = currentFuncResultInfo_;
    auto *savedInsertPoint = builder_->GetInsertBlock();
    auto savedMovedVars2 = movedVars_;
    auto savedHeapStringVars2 = heapStringVars_;
    auto savedTempStrings2 = tempStrings_;

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
    namedValues_.clear();
    varStructTypes_.clear();
    varEnumTypes_.clear();
    varArrayTypes_.clear();
    varDynArrayTypes_.clear();
    varDynArrayProtocol_.clear();
    varMapTypes_.clear();
    varSetTypes_.clear();
    varOptionalTypes_.clear();
    varFuncTypes_.clear();
    varProtocolTypes_.clear();
    varConcreteProtocolTypes_.clear();
    varResultTypes_.clear();
    varFileTypes_.clear();
    varFileOptionalTypes_.clear();
    currentFuncResultInfo_ = nullptr;
    movedVars_.clear();
    heapStringVars_.clear();
    tempStrings_.clear();

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
        emitScopeCleanup();
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
    varDynArrayProtocol_ = savedVarDynArrayProtocol2;
    varMapTypes_ = savedVarMapTypes;
    varSetTypes_ = savedVarSetTypes;
    varOptionalTypes_ = savedVarOptionalTypes;
    varFuncTypes_ = savedVarFuncTypes;
    varProtocolTypes_ = savedVarProtocolTypes2;
    varConcreteProtocolTypes_ = savedVarConcreteProtocolTypes2;
    varResultTypes_ = savedVarResultTypes2;
    currentFuncResultInfo_ = savedFuncRI2;
    movedVars_ = savedMovedVars2;
    heapStringVars_ = savedHeapStringVars2;
    tempStrings_ = savedTempStrings2;
    if (savedInsertPoint)
        builder_->SetInsertPoint(savedInsertPoint);

    return func;
}

} // namespace liva

#endif // LIVA_HAS_LLVM
