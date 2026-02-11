#include "liva/IR/IRGen.h"

#ifdef LIVA_HAS_LLVM

#include <llvm/IR/Intrinsics.h>
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

llvm::Value *IRGen::visitImportDecl(ImportDecl *node) {
    if (!moduleLoader_) return nullptr;

    std::string moduleName = node->getPathString();

    // Don't process the same module twice
    if (processedModules_.count(moduleName)) return nullptr;
    processedModules_.insert(moduleName);

    auto *mod = moduleLoader_->getLoadedModule(moduleName);
    if (!mod || !mod->tu) return nullptr;

    // Process all declarations from the imported module
    for (auto &decl : mod->tu->getDeclarations()) {
        if (decl->getKind() == ASTNode::NodeKind::FuncDecl) {
            auto *funcDecl = static_cast<FuncDecl *>(decl.get());
            if (funcDecl->isGeneric()) {
                genericFuncDecls_[funcDecl->getName()] = funcDecl;
                continue;
            }
        }

        if (decl->getKind() == ASTNode::NodeKind::StructDecl) {
            auto *structDecl = static_cast<StructDecl *>(decl.get());
            if (structDecl->isGeneric()) {
                genericStructDecls_[structDecl->getName()] = structDecl;
                continue;
            }
        }

        if (decl->getKind() == ASTNode::NodeKind::ImplDecl) {
            auto *implDecl = static_cast<ImplDecl *>(decl.get());
            if (implDecl->isGeneric()) {
                genericImplDecls_[implDecl->getTypeName()] = implDecl;
                continue;
            }
        }

        visit(decl.get());
    }

    return nullptr;
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

    auto *i64ToStrTy = llvm::FunctionType::get(i8PtrTy, {i64Ty}, false);
    module_->getOrInsertFunction("liva_i64_to_str", i64ToStrTy);

    // String method runtime functions
    auto *strBoolTy = llvm::FunctionType::get(i8Ty, {i8PtrTy, i8PtrTy}, false);
    module_->getOrInsertFunction("liva_str_contains", strBoolTy);
    module_->getOrInsertFunction("liva_str_starts_with", strBoolTy);
    module_->getOrInsertFunction("liva_str_ends_with", strBoolTy);

    auto *strIndexOfTy = llvm::FunctionType::get(i64Ty, {i8PtrTy, i8PtrTy}, false);
    module_->getOrInsertFunction("liva_str_index_of", strIndexOfTy);

    auto *strSubstringTy = llvm::FunctionType::get(i8PtrTy, {i8PtrTy, i64Ty, i64Ty}, false);
    module_->getOrInsertFunction("liva_str_substring", strSubstringTy);

    auto *strNoArgTy = llvm::FunctionType::get(i8PtrTy, {i8PtrTy}, false);
    module_->getOrInsertFunction("liva_str_trim", strNoArgTy);
    module_->getOrInsertFunction("liva_str_to_upper", strNoArgTy);
    module_->getOrInsertFunction("liva_str_to_lower", strNoArgTy);

    auto *strReplaceTy = llvm::FunctionType::get(i8PtrTy, {i8PtrTy, i8PtrTy, i8PtrTy}, false);
    module_->getOrInsertFunction("liva_str_replace", strReplaceTy);

    auto *strSplitTy = llvm::FunctionType::get(i8PtrTy, {i8PtrTy, i8PtrTy, llvm::PointerType::getUnqual(*context_)}, false);
    module_->getOrInsertFunction("liva_str_split", strSplitTy);

    // Type conversion: string → number (returns i8 success flag, writes to output ptr)
    auto *parseI32Ty = llvm::FunctionType::get(i8Ty, {i8PtrTy, llvm::PointerType::getUnqual(*context_)}, false);
    module_->getOrInsertFunction("liva_str_parse_i32", parseI32Ty);

    auto *parseI64Ty = llvm::FunctionType::get(i8Ty, {i8PtrTy, llvm::PointerType::getUnqual(*context_)}, false);
    module_->getOrInsertFunction("liva_str_parse_i64", parseI64Ty);

    auto *parseF64Ty = llvm::FunctionType::get(i8Ty, {i8PtrTy, llvm::PointerType::getUnqual(*context_)}, false);
    module_->getOrInsertFunction("liva_str_parse_f64", parseF64Ty);

    // File I/O runtime
    auto *fileOpenTy = llvm::FunctionType::get(i8PtrTy, {i8PtrTy, i8PtrTy}, false);
    module_->getOrInsertFunction("liva_file_open", fileOpenTy);

    auto *fileCloseTy = llvm::FunctionType::get(builder_->getVoidTy(), {i8PtrTy}, false);
    module_->getOrInsertFunction("liva_file_close", fileCloseTy);

    auto *fileReadLineTy = llvm::FunctionType::get(i8PtrTy, {i8PtrTy}, false);
    module_->getOrInsertFunction("liva_file_read_line", fileReadLineTy);

    auto *fileReadAllTy = llvm::FunctionType::get(i8PtrTy, {i8PtrTy}, false);
    module_->getOrInsertFunction("liva_file_read_all", fileReadAllTy);

    auto *fileWriteTy = llvm::FunctionType::get(builder_->getVoidTy(), {i8PtrTy, i8PtrTy}, false);
    module_->getOrInsertFunction("liva_file_write", fileWriteTy);
    module_->getOrInsertFunction("liva_file_write_line", fileWriteTy);

    auto *readLineTy = llvm::FunctionType::get(i8PtrTy, {}, false);
    module_->getOrInsertFunction("liva_read_line", readLineTy);

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

    // liva_array_contains(ptr, i64, ptr, i64, i8) -> i8
    auto *arrayContainsTy = llvm::FunctionType::get(i8Ty,
        {i8PtrTy, i64Ty, i8PtrTy, i64Ty, i8Ty}, false);
    module_->getOrInsertFunction("liva_array_contains", arrayContainsTy);

    // liva_array_index_of(ptr, i64, ptr, i64, i8) -> i64
    auto *arrayIndexOfTy = llvm::FunctionType::get(i64Ty,
        {i8PtrTy, i64Ty, i8PtrTy, i64Ty, i8Ty}, false);
    module_->getOrInsertFunction("liva_array_index_of", arrayIndexOfTy);

    // liva_array_reverse(ptr, i64, i64) -> void
    auto *arrayReverseTy = llvm::FunctionType::get(builder_->getVoidTy(),
        {i8PtrTy, i64Ty, i64Ty}, false);
    module_->getOrInsertFunction("liva_array_reverse", arrayReverseTy);

    // Hash map runtime
    // liva_map_new(capacity, entry_stride) -> ptr
    auto *mapNewTy = llvm::FunctionType::get(i8PtrTy, {i64Ty, i64Ty}, false);
    module_->getOrInsertFunction("liva_map_new", mapNewTy);

    auto *mapFreeTy = llvm::FunctionType::get(builder_->getVoidTy(), {i8PtrTy}, false);
    module_->getOrInsertFunction("liva_map_free", mapFreeTy);

    // liva_map_insert(ptr*, i64*, i64*, ptr, ptr, i64, i64, i8)
    auto *mapInsertTy = llvm::FunctionType::get(builder_->getVoidTy(),
        {i8PtrTy, i8PtrTy, i8PtrTy, i8PtrTy, i8PtrTy, i64Ty, i64Ty, i8Ty}, false);
    module_->getOrInsertFunction("liva_map_insert", mapInsertTy);

    // liva_map_get(ptr, i64, ptr, i64, i64, i8) -> ptr
    auto *mapGetTy = llvm::FunctionType::get(i8PtrTy,
        {i8PtrTy, i64Ty, i8PtrTy, i64Ty, i64Ty, i8Ty}, false);
    module_->getOrInsertFunction("liva_map_get", mapGetTy);

    // liva_map_remove(ptr, i64*, i64, ptr, i64, i64, i8) -> i8
    auto *mapRemoveTy = llvm::FunctionType::get(i8Ty,
        {i8PtrTy, i8PtrTy, i64Ty, i8PtrTy, i64Ty, i64Ty, i8Ty}, false);
    module_->getOrInsertFunction("liva_map_remove", mapRemoveTy);

    // liva_map_contains(ptr, i64, ptr, i64, i64, i8) -> i8
    auto *mapContainsTy = llvm::FunctionType::get(i8Ty,
        {i8PtrTy, i64Ty, i8PtrTy, i64Ty, i64Ty, i8Ty}, false);
    module_->getOrInsertFunction("liva_map_contains", mapContainsTy);

    // Hash set runtime
    module_->getOrInsertFunction("liva_set_new", mapNewTy);
    module_->getOrInsertFunction("liva_set_free", mapFreeTy);

    // liva_set_insert(ptr*, i64*, i64*, ptr, i64, i8)
    auto *setInsertTy = llvm::FunctionType::get(builder_->getVoidTy(),
        {i8PtrTy, i8PtrTy, i8PtrTy, i8PtrTy, i64Ty, i8Ty}, false);
    module_->getOrInsertFunction("liva_set_insert", setInsertTy);

    // liva_set_contains(ptr, i64, ptr, i64, i8) -> i8
    auto *setContainsTy = llvm::FunctionType::get(i8Ty,
        {i8PtrTy, i64Ty, i8PtrTy, i64Ty, i8Ty}, false);
    module_->getOrInsertFunction("liva_set_contains", setContainsTy);

    // liva_set_remove(ptr, i64*, i64, ptr, i64, i8) -> i8
    auto *setRemoveTy = llvm::FunctionType::get(i8Ty,
        {i8PtrTy, i8PtrTy, i64Ty, i8PtrTy, i64Ty, i8Ty}, false);
    module_->getOrInsertFunction("liva_set_remove", setRemoveTy);

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

llvm::StructType *IRGen::getMapStructTy() {
    if (!mapStructTy_) {
        mapStructTy_ = llvm::StructType::create(*context_,
            {llvm::PointerType::getUnqual(*context_),
             builder_->getInt64Ty(),
             builder_->getInt64Ty()},
            "MapStruct");
    }
    return mapStructTy_;
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
        // Check type alias map
        auto aliasIt = typeAliases_.find(named->getName());
        if (aliasIt != typeAliases_.end()) {
            return toLLVMType(aliasIt->second);
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
    case TypeRepr::Kind::Tuple: {
        auto *tupType = static_cast<const TupleTypeRepr *>(type);
        std::vector<llvm::Type *> elemTypes;
        for (auto &e : tupType->getElements())
            elemTypes.push_back(toLLVMType(e.get()));
        return llvm::StructType::get(*context_, elemTypes);
    }
    case TypeRepr::Kind::Function:
        return getClosureObjTy();
    case TypeRepr::Kind::Reference:
        return llvm::PointerType::getUnqual(*context_);
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
    // Store FuncDecl for default arg lookup
    funcDecls_[node->getName()] = node;

    // Build function type
    std::vector<llvm::Type *> paramTypes;
    for (auto &param : node->getParams()) {
        if (param.isRef) {
            paramTypes.push_back(llvm::PointerType::getUnqual(*context_));
        } else {
            paramTypes.push_back(toLLVMType(param.type.get()));
        }
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
    auto oldVarMapTypes = varMapTypes_;
    auto oldVarSetTypes = varSetTypes_;
    auto oldVarOptionalTypes = varOptionalTypes_;
    auto oldVarFuncTypes = varFuncTypes_;
    auto oldVarProtocolTypes = varProtocolTypes_;
    auto oldVarResultTypes = varResultTypes_;
    auto oldVarRefTypes = varRefTypes_;
    auto oldVarFileTypes = varFileTypes_;
    auto oldVarFileOptionalTypes = varFileOptionalTypes_;
    auto oldVarTupleTypes = varTupleTypes_;
    auto *oldFuncResultInfo = currentFuncResultInfo_;
    namedValues_.clear();
    varStructTypes_.clear();
    varEnumTypes_.clear();
    varArrayTypes_.clear();
    varDynArrayTypes_.clear();
    varMapTypes_.clear();
    varSetTypes_.clear();
    varOptionalTypes_.clear();
    varFuncTypes_.clear();
    varProtocolTypes_.clear();
    varResultTypes_.clear();
    varRefTypes_.clear();
    varFileTypes_.clear();
    varFileOptionalTypes_.clear();
    varTupleTypes_.clear();
    currentFuncResultInfo_ = nullptr;

    // Track Result return type for try expressions (visitFuncDecl)
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

    // Populate varRefTypes_ for ref parameters
    for (auto &param : node->getParams()) {
        if (param.isRef && param.type) {
            llvm::Type *innerTy = toLLVMType(param.type.get());
            if (param.type->getKind() == TypeRepr::Kind::Reference) {
                auto *refTy = static_cast<const ReferenceTypeRepr *>(param.type.get());
                innerTy = toLLVMType(refTy->getInner());
            }
            varRefTypes_[param.name] = innerTy;
        }
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
    varMapTypes_ = oldVarMapTypes;
    varSetTypes_ = oldVarSetTypes;
    varOptionalTypes_ = oldVarOptionalTypes;
    varFuncTypes_ = oldVarFuncTypes;
    varProtocolTypes_ = oldVarProtocolTypes;
    varResultTypes_ = oldVarResultTypes;
    varRefTypes_ = oldVarRefTypes;
    varFileTypes_ = oldVarFileTypes;
    varFileOptionalTypes_ = oldVarFileOptionalTypes;
    varTupleTypes_ = oldVarTupleTypes;
    currentFuncResultInfo_ = oldFuncResultInfo;

    return func;
}

llvm::Value *IRGen::visitVarDecl(VarDecl *node) {
    auto *func = builder_->GetInsertBlock()->getParent();

    // Tuple destructuring: let (x, y) = expr
    if (node->isDestructured()) {
        if (!node->hasInit()) return nullptr;
        auto *initVal = visit(const_cast<Expr *>(node->getInit()));
        if (!initVal) return nullptr;

        // Get tuple element types from init's resolved type
        auto *initType = node->getInit()->getResolvedType();
        if (!initType || initType->getKind() != TypeRepr::Kind::Tuple)
            return nullptr;

        auto *tupleTypeRepr = static_cast<const TupleTypeRepr *>(initType);
        auto *tupleTy = toLLVMType(initType);

        // Store the whole tuple to a temp alloca
        auto *tupleAlloca = createEntryBlockAlloca(func, "tuple.dest", tupleTy);
        builder_->CreateStore(initVal, tupleAlloca);

        // Extract each element and assign to named variables
        for (size_t i = 0; i < node->getDestructuredNames().size(); ++i) {
            auto *elemTy = toLLVMType(tupleTypeRepr->getElements()[i].get());
            auto *gep = builder_->CreateStructGEP(tupleTy, tupleAlloca, i);
            auto *val = builder_->CreateLoad(elemTy, gep);

            auto *elemAlloca = createEntryBlockAlloca(func, node->getDestructuredNames()[i], elemTy);
            builder_->CreateStore(val, elemAlloca);
            namedValues_[node->getDestructuredNames()[i]] = elemAlloca;
        }
        return tupleAlloca;
    }

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

    // File.open() init: let f = File.open("path", "mode") → Optional<ptr>
    if (node->hasInit() &&
        node->getInit()->getKind() == ASTNode::NodeKind::CallExpr) {
        auto *callInit = static_cast<CallExpr *>(const_cast<Expr *>(node->getInit()));
        if (callInit->getCallee()->getKind() == ASTNode::NodeKind::MemberExpr) {
            auto *me = static_cast<MemberExpr *>(callInit->getCallee());
            if (me->getObject()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
                auto *id = static_cast<IdentifierExpr *>(me->getObject());
                if (id->getName() == "File" && me->getMember() == "open") {
                    auto *ptrTy = llvm::PointerType::getUnqual(*context_);
                    auto *optTy = getOptionalType(ptrTy);
                    auto *alloca = createEntryBlockAlloca(func, node->getName(), optTy);
                    auto *initVal = visit(const_cast<Expr *>(node->getInit()));
                    if (initVal) builder_->CreateStore(initVal, alloca);
                    namedValues_[node->getName()] = alloca;
                    varOptionalTypes_[node->getName()] = ptrTy;
                    varFileOptionalTypes_.insert(node->getName());
                    return alloca;
                }
            }
        }
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
        // Register struct type name for optional chaining support
        if (optTypeRepr->getInner()->getKind() == TypeRepr::Kind::Named) {
            auto *namedInner = static_cast<const NamedTypeRepr *>(optTypeRepr->getInner());
            varStructTypes_[node->getName()] = namedInner->getName();
        }
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

    // Map declaration: var m: Map<K, V>
    if (node->hasTypeAnnotation() && node->getType() &&
        node->getType()->getKind() == TypeRepr::Kind::Generic) {
        auto *genType = static_cast<const GenericTypeRepr *>(node->getType());
        if (genType->getBaseName() == "Map" && genType->getTypeArgs().size() >= 2) {
            auto *keyType = toLLVMType(genType->getTypeArgs()[0].get());
            auto *valType = toLLVMType(genType->getTypeArgs()[1].get());
            const llvm::DataLayout &dl = module_->getDataLayout();
            uint64_t keySize = dl.getTypeAllocSize(keyType);
            uint64_t valSize = dl.getTypeAllocSize(valType);
            int8_t keyKind = (genType->getTypeArgs()[0]->getKind() == TypeRepr::Kind::String) ? 1 : 0;

            int64_t initCap = 16;
            int64_t stride = 9 + (int64_t)keySize + (int64_t)valSize;

            auto *structTy = getMapStructTy();
            auto *alloca = createEntryBlockAlloca(func, node->getName(), structTy);

            auto *newFn = module_->getFunction("liva_map_new");
            auto *dataPtr = builder_->CreateCall(newFn,
                {builder_->getInt64(initCap), builder_->getInt64(stride)}, "map.entries");

            auto *dataField = builder_->CreateStructGEP(structTy, alloca, 0, "map.entries.ptr");
            builder_->CreateStore(dataPtr, dataField);
            auto *sizeField = builder_->CreateStructGEP(structTy, alloca, 1, "map.size.ptr");
            builder_->CreateStore(builder_->getInt64(0), sizeField);
            auto *capField = builder_->CreateStructGEP(structTy, alloca, 2, "map.cap.ptr");
            builder_->CreateStore(builder_->getInt64(initCap), capField);

            namedValues_[node->getName()] = alloca;
            varMapTypes_[node->getName()] = {keyType, valType, keySize, valSize, keyKind};
            return alloca;
        }

        if (genType->getBaseName() == "Set" && !genType->getTypeArgs().empty()) {
            auto *elemType = toLLVMType(genType->getTypeArgs()[0].get());
            const llvm::DataLayout &dl = module_->getDataLayout();
            uint64_t elemSize = dl.getTypeAllocSize(elemType);
            int8_t keyKind = (genType->getTypeArgs()[0]->getKind() == TypeRepr::Kind::String) ? 1 : 0;

            int64_t initCap = 16;
            int64_t stride = 9 + (int64_t)elemSize;  // val_size=0 for sets

            auto *structTy = getMapStructTy();
            auto *alloca = createEntryBlockAlloca(func, node->getName(), structTy);

            auto *newFn = module_->getFunction("liva_set_new");
            auto *dataPtr = builder_->CreateCall(newFn,
                {builder_->getInt64(initCap), builder_->getInt64(stride)}, "set.entries");

            auto *dataField = builder_->CreateStructGEP(structTy, alloca, 0, "set.entries.ptr");
            builder_->CreateStore(dataPtr, dataField);
            auto *sizeField = builder_->CreateStructGEP(structTy, alloca, 1, "set.size.ptr");
            builder_->CreateStore(builder_->getInt64(0), sizeField);
            auto *capField = builder_->CreateStructGEP(structTy, alloca, 2, "set.cap.ptr");
            builder_->CreateStore(builder_->getInt64(initCap), capField);

            namedValues_[node->getName()] = alloca;
            varSetTypes_[node->getName()] = {elemType, elemSize, keyKind};
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

    // Tuple variable: let pair = (1, "hello") or let r = divmod(10, 3)
    if (node->hasInit() && node->getInit()->getResolvedType() &&
        node->getInit()->getResolvedType()->getKind() == TypeRepr::Kind::Tuple) {
        auto *tupleTypeRepr = static_cast<const TupleTypeRepr *>(node->getInit()->getResolvedType());
        auto *tupleTy = toLLVMType(tupleTypeRepr);
        auto *alloca = createEntryBlockAlloca(func, node->getName(), tupleTy);
        auto *initVal = visit(const_cast<Expr *>(node->getInit()));
        if (initVal) builder_->CreateStore(initVal, alloca);
        namedValues_[node->getName()] = alloca;
        TupleInfo ti;
        for (auto &e : tupleTypeRepr->getElements())
            ti.elementTypes.push_back(toLLVMType(e.get()));
        varTupleTypes_[node->getName()] = ti;
        return alloca;
    }

    // Also handle tuple type annotation: let x: (i32, string) = ...
    if (node->hasTypeAnnotation() && node->getType() &&
        node->getType()->getKind() == TypeRepr::Kind::Tuple) {
        auto *tupleTypeRepr = static_cast<const TupleTypeRepr *>(node->getType());
        auto *tupleTy = toLLVMType(tupleTypeRepr);
        auto *alloca = createEntryBlockAlloca(func, node->getName(), tupleTy);
        if (node->hasInit()) {
            auto *initVal = visit(const_cast<Expr *>(node->getInit()));
            if (initVal) builder_->CreateStore(initVal, alloca);
        }
        namedValues_[node->getName()] = alloca;
        TupleInfo ti;
        for (auto &e : tupleTypeRepr->getElements())
            ti.elementTypes.push_back(toLLVMType(e.get()));
        varTupleTypes_[node->getName()] = ti;
        return alloca;
    }

    // Init returns DynArray (map/filter/split): let doubled = arr.map(...)
    if (node->hasInit() && node->getInit()->getResolvedType() &&
        node->getInit()->getResolvedType()->getKind() == TypeRepr::Kind::Array) {
        auto *arrReprType = static_cast<const ArrayTypeRepr *>(node->getInit()->getResolvedType());
        if (arrReprType->isDynamic()) {
            auto *elemType = toLLVMType(arrReprType->getElement());
            const llvm::DataLayout &dl = module_->getDataLayout();
            uint64_t elemSize = dl.getTypeAllocSize(elemType);
            auto *structTy = getDynArrayStructTy();
            auto *alloca = createEntryBlockAlloca(func, node->getName(), structTy);
            auto *initVal = visit(const_cast<Expr *>(node->getInit()));
            if (initVal) builder_->CreateStore(initVal, alloca);
            namedValues_[node->getName()] = alloca;
            varDynArrayTypes_[node->getName()] = {elemType, elemSize};
            return alloca;
        }
    }

    // Fallback: visit init first to determine correct type for inferred vars
    if (node->hasInit()) {
        auto *initVal = visit(const_cast<Expr *>(node->getInit()));
        auto *type = toLLVMType(node->getType());
        // Use init value's type when annotation is absent/inferred (avoids i32 default)
        if (initVal && type == builder_->getInt32Ty() &&
            initVal->getType() != builder_->getInt32Ty()) {
            type = initVal->getType();
        }
        auto *alloca = createEntryBlockAlloca(func, node->getName(), type);
        if (initVal)
            builder_->CreateStore(initVal, alloca);
        namedValues_[node->getName()] = alloca;
        return alloca;
    }

    auto *type = toLLVMType(node->getType());
    auto *alloca = createEntryBlockAlloca(func, node->getName(), type);
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
    auto savedFileTypes = varFileTypes_;
    namedValues_[node->getBindingName()] = bindAlloca;
    // If the optional expression was File.open, the unwrapped value is a File ptr
    if (node->getOptionalExpr()->getKind() == ASTNode::NodeKind::CallExpr) {
        auto *callExpr = static_cast<CallExpr *>(node->getOptionalExpr());
        if (callExpr->getCallee()->getKind() == ASTNode::NodeKind::MemberExpr) {
            auto *me = static_cast<MemberExpr *>(callExpr->getCallee());
            if (me->getObject()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
                auto *id = static_cast<IdentifierExpr *>(me->getObject());
                if (id->getName() == "File" && me->getMember() == "open") {
                    varFileTypes_.insert(node->getBindingName());
                }
            }
        }
    }
    // Also check if the optional source was a File-optional variable
    if (node->getOptionalExpr()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
        auto *ident = static_cast<IdentifierExpr *>(node->getOptionalExpr());
        if (varFileOptionalTypes_.count(ident->getName())) {
            varFileTypes_.insert(node->getBindingName());
        }
    }
    visit(node->getThenBody());
    namedValues_ = saved;
    varFileTypes_ = savedFileTypes;
    if (!builder_->GetInsertBlock()->getTerminator()) builder_->CreateBr(mergeBB2);

    // Else
    builder_->SetInsertPoint(elseBB);
    if (node->hasElse()) visit(node->getElseBody());
    if (!builder_->GetInsertBlock()->getTerminator()) builder_->CreateBr(mergeBB2);

    builder_->SetInsertPoint(mergeBB2);
    return nullptr;
}

llvm::Value *IRGen::visitWhileLetStmt(WhileLetStmt *node) {
    // while let x = optional { body }
    // Becomes: loop { check hasVal; if false → exit; unwrap; body; continue }
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

    auto *func = builder_->GetInsertBlock()->getParent();
    auto *optStructTy = getOptionalType(innerType);

    auto *condBB = llvm::BasicBlock::Create(*context_, "whilelet.cond", func);
    auto *bodyBB = llvm::BasicBlock::Create(*context_, "whilelet.body", func);
    auto *exitBB = llvm::BasicBlock::Create(*context_, "whilelet.exit", func);

    builder_->CreateBr(condBB);

    // Cond: check hasVal
    builder_->SetInsertPoint(condBB);
    auto *hasValPtr = builder_->CreateStructGEP(optStructTy, optAlloca, 0);
    auto *hasVal = builder_->CreateLoad(builder_->getInt1Ty(), hasValPtr, "whilelet.hasval");
    builder_->CreateCondBr(hasVal, bodyBB, exitBB);

    // Body: unwrap + bind + execute body
    builder_->SetInsertPoint(bodyBB);
    auto *valPtr = builder_->CreateStructGEP(optStructTy, optAlloca, 1);
    auto *unwrapped = builder_->CreateLoad(innerType, valPtr, "whilelet.val");
    auto *bindAlloca = createEntryBlockAlloca(func, node->getBindingName(), innerType);
    builder_->CreateStore(unwrapped, bindAlloca);

    auto saved = namedValues_;
    namedValues_[node->getBindingName()] = bindAlloca;
    loopStack_.push_back({exitBB, condBB});
    visit(node->getBody());
    loopStack_.pop_back();
    namedValues_ = saved;
    if (!builder_->GetInsertBlock()->getTerminator())
        builder_->CreateBr(condBB);

    builder_->SetInsertPoint(exitBB);
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

    // Check if iterable is an identifier referencing a collection
    if (node->getIterable()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
        auto *ident = static_cast<IdentifierExpr *>(
            const_cast<Expr *>(node->getIterable()));
        const std::string &iterName = ident->getName();

        // === DynArray iteration: for item in arr ===
        auto daIt = varDynArrayTypes_.find(iterName);
        if (daIt != varDynArrayTypes_.end()) {
            auto *elemType = daIt->second.elementType;
            auto *structTy = getDynArrayStructTy();
            auto *arrAlloca = namedValues_[iterName];

            // Load data pointer and length
            auto *dataField = builder_->CreateStructGEP(structTy, arrAlloca, 0, "da.data.ptr");
            auto *dataPtr = builder_->CreateLoad(builder_->getPtrTy(), dataField, "da.data");
            auto *lenField = builder_->CreateStructGEP(structTy, arrAlloca, 1, "da.len.ptr");
            auto *len = builder_->CreateLoad(builder_->getInt64Ty(), lenField, "da.len");

            // Index variable
            auto *idxVar = createEntryBlockAlloca(func, "for.idx", builder_->getInt64Ty());
            builder_->CreateStore(builder_->getInt64(0), idxVar);

            // Loop variable
            auto *loopVar = createEntryBlockAlloca(func, node->getVarName(), elemType);
            namedValues_[node->getVarName()] = loopVar;

            auto *condBB = llvm::BasicBlock::Create(*context_, "for.cond", func);
            auto *bodyBB = llvm::BasicBlock::Create(*context_, "for.body", func);
            auto *latchBB = llvm::BasicBlock::Create(*context_, "for.latch", func);
            auto *exitBB = llvm::BasicBlock::Create(*context_, "for.exit", func);

            builder_->CreateBr(condBB);

            // Condition: idx < len
            builder_->SetInsertPoint(condBB);
            auto *idx = builder_->CreateLoad(builder_->getInt64Ty(), idxVar, "idx");
            auto *cond = builder_->CreateICmpSLT(idx, len, "for.cmp");
            builder_->CreateCondBr(cond, bodyBB, exitBB);

            // Body: load element, store to loop var
            builder_->SetInsertPoint(bodyBB);
            auto *elemPtr = builder_->CreateGEP(elemType, dataPtr, idx, "elem.ptr");
            auto *elem = builder_->CreateLoad(elemType, elemPtr, "elem");
            builder_->CreateStore(elem, loopVar);

            loopStack_.push_back({exitBB, latchBB});
            visit(const_cast<ASTNode *>(node->getBody()));
            loopStack_.pop_back();
            if (!builder_->GetInsertBlock()->getTerminator())
                builder_->CreateBr(latchBB);

            // Latch: idx++
            builder_->SetInsertPoint(latchBB);
            auto *curIdx = builder_->CreateLoad(builder_->getInt64Ty(), idxVar, "idx");
            auto *nextIdx = builder_->CreateAdd(curIdx, builder_->getInt64(1), "idx.inc");
            builder_->CreateStore(nextIdx, idxVar);
            builder_->CreateBr(condBB);

            builder_->SetInsertPoint(exitBB);
            return nullptr;
        }

        // === Map iteration ===
        auto mapIt = varMapTypes_.find(iterName);
        if (mapIt != varMapTypes_.end()) {
            auto &info = mapIt->second;
            auto *structTy = getMapStructTy();
            auto *mapAlloca = namedValues_[iterName];

            int64_t stride = 9 + (int64_t)info.keySize + (int64_t)info.valSize;

            // Load entries pointer and capacity
            auto *entriesField = builder_->CreateStructGEP(structTy, mapAlloca, 0, "map.entries.ptr");
            auto *entriesPtr = builder_->CreateLoad(builder_->getPtrTy(), entriesField, "map.entries");
            auto *capField = builder_->CreateStructGEP(structTy, mapAlloca, 2, "map.cap.ptr");
            auto *cap = builder_->CreateLoad(builder_->getInt64Ty(), capField, "map.cap");

            // Index variable
            auto *idxVar = createEntryBlockAlloca(func, "for.idx", builder_->getInt64Ty());
            builder_->CreateStore(builder_->getInt64(0), idxVar);

            // Loop variable(s)
            auto *keyVar = createEntryBlockAlloca(func, node->getVarName(), info.keyType);
            namedValues_[node->getVarName()] = keyVar;

            llvm::AllocaInst *valVar = nullptr;
            if (node->hasTuplePattern()) {
                valVar = createEntryBlockAlloca(func, node->getVarName2(), info.valType);
                namedValues_[node->getVarName2()] = valVar;
            }

            auto *condBB = llvm::BasicBlock::Create(*context_, "for.cond", func);
            auto *bodyBB = llvm::BasicBlock::Create(*context_, "for.body", func);
            auto *processBB = llvm::BasicBlock::Create(*context_, "for.process", func);
            auto *latchBB = llvm::BasicBlock::Create(*context_, "for.latch", func);
            auto *exitBB = llvm::BasicBlock::Create(*context_, "for.exit", func);

            builder_->CreateBr(condBB);

            // Condition: idx < capacity
            builder_->SetInsertPoint(condBB);
            auto *idx = builder_->CreateLoad(builder_->getInt64Ty(), idxVar, "idx");
            auto *cond = builder_->CreateICmpSLT(idx, cap, "for.cmp");
            builder_->CreateCondBr(cond, bodyBB, exitBB);

            // Body: check if entry is occupied (state == 1)
            builder_->SetInsertPoint(bodyBB);
            auto *offset = builder_->CreateMul(idx, builder_->getInt64(stride), "entry.offset");
            auto *entryPtr = builder_->CreateGEP(builder_->getInt8Ty(), entriesPtr, offset, "entry.ptr");
            auto *state = builder_->CreateLoad(builder_->getInt8Ty(), entryPtr, "entry.state");
            auto *isOccupied = builder_->CreateICmpEQ(state, builder_->getInt8(1), "is.occupied");
            builder_->CreateCondBr(isOccupied, processBB, latchBB);

            // Process: extract key (and optionally value)
            builder_->SetInsertPoint(processBB);
            auto *keyRaw = builder_->CreateGEP(builder_->getInt8Ty(), entryPtr,
                builder_->getInt64(9), "key.raw");
            auto *key = builder_->CreateLoad(info.keyType, keyRaw, "key");
            builder_->CreateStore(key, keyVar);

            if (node->hasTuplePattern() && valVar) {
                auto *valRaw = builder_->CreateGEP(builder_->getInt8Ty(), entryPtr,
                    builder_->getInt64(9 + (int64_t)info.keySize), "val.raw");
                auto *val = builder_->CreateLoad(info.valType, valRaw, "val");
                builder_->CreateStore(val, valVar);
            }

            loopStack_.push_back({exitBB, latchBB});
            visit(const_cast<ASTNode *>(node->getBody()));
            loopStack_.pop_back();
            if (!builder_->GetInsertBlock()->getTerminator())
                builder_->CreateBr(latchBB);

            // Latch: idx++
            builder_->SetInsertPoint(latchBB);
            auto *curIdx = builder_->CreateLoad(builder_->getInt64Ty(), idxVar, "idx");
            auto *nextIdx = builder_->CreateAdd(curIdx, builder_->getInt64(1), "idx.inc");
            builder_->CreateStore(nextIdx, idxVar);
            builder_->CreateBr(condBB);

            builder_->SetInsertPoint(exitBB);
            return nullptr;
        }

        // === Set iteration: for item in set ===
        auto setIt = varSetTypes_.find(iterName);
        if (setIt != varSetTypes_.end()) {
            auto &info = setIt->second;
            auto *structTy = getMapStructTy();  // Set uses same struct as Map
            auto *setAlloca = namedValues_[iterName];

            int64_t stride = 9 + (int64_t)info.elemSize;  // val_size=0

            // Load entries pointer and capacity
            auto *entriesField = builder_->CreateStructGEP(structTy, setAlloca, 0, "set.entries.ptr");
            auto *entriesPtr = builder_->CreateLoad(builder_->getPtrTy(), entriesField, "set.entries");
            auto *capField = builder_->CreateStructGEP(structTy, setAlloca, 2, "set.cap.ptr");
            auto *cap = builder_->CreateLoad(builder_->getInt64Ty(), capField, "set.cap");

            // Index variable
            auto *idxVar = createEntryBlockAlloca(func, "for.idx", builder_->getInt64Ty());
            builder_->CreateStore(builder_->getInt64(0), idxVar);

            // Loop variable
            auto *loopVar = createEntryBlockAlloca(func, node->getVarName(), info.elemType);
            namedValues_[node->getVarName()] = loopVar;

            auto *condBB = llvm::BasicBlock::Create(*context_, "for.cond", func);
            auto *bodyBB = llvm::BasicBlock::Create(*context_, "for.body", func);
            auto *processBB = llvm::BasicBlock::Create(*context_, "for.process", func);
            auto *latchBB = llvm::BasicBlock::Create(*context_, "for.latch", func);
            auto *exitBB = llvm::BasicBlock::Create(*context_, "for.exit", func);

            builder_->CreateBr(condBB);

            // Condition: idx < capacity
            builder_->SetInsertPoint(condBB);
            auto *idx = builder_->CreateLoad(builder_->getInt64Ty(), idxVar, "idx");
            auto *cond = builder_->CreateICmpSLT(idx, cap, "for.cmp");
            builder_->CreateCondBr(cond, bodyBB, exitBB);

            // Body: check if entry is occupied (state == 1)
            builder_->SetInsertPoint(bodyBB);
            auto *offset = builder_->CreateMul(idx, builder_->getInt64(stride), "entry.offset");
            auto *entryPtr = builder_->CreateGEP(builder_->getInt8Ty(), entriesPtr, offset, "entry.ptr");
            auto *state = builder_->CreateLoad(builder_->getInt8Ty(), entryPtr, "entry.state");
            auto *isOccupied = builder_->CreateICmpEQ(state, builder_->getInt8(1), "is.occupied");
            builder_->CreateCondBr(isOccupied, processBB, latchBB);

            // Process: extract element
            builder_->SetInsertPoint(processBB);
            auto *elemRaw = builder_->CreateGEP(builder_->getInt8Ty(), entryPtr,
                builder_->getInt64(9), "elem.raw");
            auto *elem = builder_->CreateLoad(info.elemType, elemRaw, "elem");
            builder_->CreateStore(elem, loopVar);

            loopStack_.push_back({exitBB, latchBB});
            visit(const_cast<ASTNode *>(node->getBody()));
            loopStack_.pop_back();
            if (!builder_->GetInsertBlock()->getTerminator())
                builder_->CreateBr(latchBB);

            // Latch: idx++
            builder_->SetInsertPoint(latchBB);
            auto *curIdx = builder_->CreateLoad(builder_->getInt64Ty(), idxVar, "idx");
            auto *nextIdx = builder_->CreateAdd(curIdx, builder_->getInt64(1), "idx.inc");
            builder_->CreateStore(nextIdx, idxVar);
            builder_->CreateBr(condBB);

            builder_->SetInsertPoint(exitBB);
            return nullptr;
        }
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
        // Reference variable: double indirection (load ptr, then load through ptr)
        auto refIt = varRefTypes_.find(node->getName());
        if (refIt != varRefTypes_.end()) {
            auto *ptr = builder_->CreateLoad(
                llvm::PointerType::getUnqual(*context_), it->second, node->getName() + ".ptr");
            return builder_->CreateLoad(refIt->second, ptr, node->getName());
        }
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
    // Handle optional chain result: p?.x ?? default
    if (!optAlloca || !innerType) {
        if (node->getLHS()->getKind() == ASTNode::NodeKind::MemberExpr) {
            auto *memberExpr = static_cast<MemberExpr *>(node->getLHS());
            if (memberExpr->isOptionalChain()) {
                auto *lhsVal = visit(node->getLHS());
                if (!lhsVal) return visit(node->getRHS());
                // lhsVal is an Optional<T> struct value; store to alloca for GEP
                auto *optTy = lhsVal->getType();
                auto *func = builder_->GetInsertBlock()->getParent();
                auto *tmpAlloca = createEntryBlockAlloca(func, "coal.opt.tmp", optTy);
                builder_->CreateStore(lhsVal, tmpAlloca);
                auto *hasValPtr2 = builder_->CreateStructGEP(optTy, tmpAlloca, 0);
                auto *hasVal2 = builder_->CreateLoad(builder_->getInt1Ty(), hasValPtr2, "coal.hasval");
                auto *fieldInnerType = optTy->getStructElementType(1);
                auto *hasValBB2 = llvm::BasicBlock::Create(*context_, "coal.hasval", func);
                auto *nilBB2 = llvm::BasicBlock::Create(*context_, "coal.nil", func);
                auto *mergeBB2 = llvm::BasicBlock::Create(*context_, "coal.merge", func);
                builder_->CreateCondBr(hasVal2, hasValBB2, nilBB2);

                builder_->SetInsertPoint(hasValBB2);
                auto *valPtr2 = builder_->CreateStructGEP(optTy, tmpAlloca, 1);
                auto *optVal2 = builder_->CreateLoad(fieldInnerType, valPtr2, "coal.val");
                auto *hasValEnd2 = builder_->GetInsertBlock();
                builder_->CreateBr(mergeBB2);

                builder_->SetInsertPoint(nilBB2);
                auto *defaultVal2 = visit(node->getRHS());
                auto *nilEnd2 = builder_->GetInsertBlock();
                builder_->CreateBr(mergeBB2);

                builder_->SetInsertPoint(mergeBB2);
                auto *phi2 = builder_->CreatePHI(fieldInnerType, 2, "coal.result");
                phi2->addIncoming(optVal2, hasValEnd2);
                phi2->addIncoming(defaultVal2, nilEnd2);
                return phi2;
            }
        }
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

llvm::Value *IRGen::emitOptionalChainMember(MemberExpr *node) {
    // obj?.field — nil check, unwrap, field access, rewrap as Optional
    if (node->getObject()->getKind() != ASTNode::NodeKind::IdentifierExpr)
        return nullptr;

    auto *ident = static_cast<IdentifierExpr *>(node->getObject());
    auto optIt = varOptionalTypes_.find(ident->getName());
    if (optIt == varOptionalTypes_.end())
        return nullptr;
    auto nvIt = namedValues_.find(ident->getName());
    if (nvIt == namedValues_.end())
        return nullptr;

    auto *optAlloca = nvIt->second;
    auto *innerType = optIt->second; // The struct type inside Optional

    // Find the struct type name for field lookup
    auto stIt = varStructTypes_.find(ident->getName());
    if (stIt == varStructTypes_.end())
        return nullptr;
    const auto &structTypeName = stIt->second;

    auto stTyIt = structTypes_.find(structTypeName);
    if (stTyIt == structTypes_.end())
        return nullptr;
    auto *structTy = stTyIt->second;

    int fieldIdx = getStructFieldIndex(structTypeName, node->getMember());
    if (fieldIdx < 0)
        return nullptr;

    auto *fieldType = structTy->getElementType(fieldIdx);
    auto *optResultTy = getOptionalType(fieldType);

    // nil check
    auto *optStructTy = getOptionalType(innerType);
    auto *hasValPtr = builder_->CreateStructGEP(optStructTy, optAlloca, 0);
    auto *hasVal = builder_->CreateLoad(builder_->getInt1Ty(), hasValPtr, "optchain.hasval");

    auto *func = builder_->GetInsertBlock()->getParent();
    auto *hasValBB = llvm::BasicBlock::Create(*context_, "optchain.hasval", func);
    auto *nilBB = llvm::BasicBlock::Create(*context_, "optchain.nil", func);
    auto *mergeBB = llvm::BasicBlock::Create(*context_, "optchain.merge", func);
    builder_->CreateCondBr(hasVal, hasValBB, nilBB);

    // has_val path: unwrap struct, access field, rewrap as Optional<FieldType>
    builder_->SetInsertPoint(hasValBB);
    auto *valPtr = builder_->CreateStructGEP(optStructTy, optAlloca, 1);
    auto *structVal = builder_->CreateLoad(innerType, valPtr, "optchain.unwrap");
    // Store struct into temp alloca for GEP
    auto *tmpAlloca = createEntryBlockAlloca(func, "optchain.tmp", innerType);
    builder_->CreateStore(structVal, tmpAlloca);
    auto *fieldGEP = builder_->CreateStructGEP(structTy, tmpAlloca, fieldIdx, "optchain.field");
    auto *fieldVal = builder_->CreateLoad(fieldType, fieldGEP, "optchain.fieldval");
    // Build Optional<FieldType>{true, fieldVal}
    auto *resultAlloca = createEntryBlockAlloca(func, "optchain.result", optResultTy);
    auto *resHasPtr = builder_->CreateStructGEP(optResultTy, resultAlloca, 0);
    builder_->CreateStore(builder_->getTrue(), resHasPtr);
    auto *resValPtr = builder_->CreateStructGEP(optResultTy, resultAlloca, 1);
    builder_->CreateStore(fieldVal, resValPtr);
    auto *someResult = builder_->CreateLoad(optResultTy, resultAlloca, "optchain.some");
    auto *hasValEnd = builder_->GetInsertBlock();
    builder_->CreateBr(mergeBB);

    // nil path: return nil Optional<FieldType>
    builder_->SetInsertPoint(nilBB);
    auto *nilAlloca = createEntryBlockAlloca(func, "optchain.nil", optResultTy);
    auto *nilHasPtr = builder_->CreateStructGEP(optResultTy, nilAlloca, 0);
    builder_->CreateStore(builder_->getFalse(), nilHasPtr);
    // Zero-init the value field
    auto *nilValPtr = builder_->CreateStructGEP(optResultTy, nilAlloca, 1);
    builder_->CreateStore(llvm::Constant::getNullValue(fieldType), nilValPtr);
    auto *nilResult = builder_->CreateLoad(optResultTy, nilAlloca, "optchain.none");
    auto *nilEnd = builder_->GetInsertBlock();
    builder_->CreateBr(mergeBB);

    // merge
    builder_->SetInsertPoint(mergeBB);
    auto *phi = builder_->CreatePHI(optResultTy, 2, "optchain.phi");
    phi->addIncoming(someResult, hasValEnd);
    phi->addIncoming(nilResult, nilEnd);
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
            auto *fn = llvm::Intrinsic::getDeclaration(
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
            auto *fn = llvm::Intrinsic::getDeclaration(
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
            auto *fn = llvm::Intrinsic::getDeclaration(
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
        auto *fn = llvm::Intrinsic::getDeclaration(
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
        auto *fn = llvm::Intrinsic::getDeclaration(
            module_.get(), llvm::Intrinsic::pow, {llvm::Type::getDoubleTy(*context_)});
        return builder_->CreateCall(fn, {x, y}, "powtmp");
    }

    if (funcName == "floor" && !node->getArgs().empty()) {
        auto *x = visit(node->getArgs()[0].get());
        if (!x) return nullptr;
        if (x->getType()->isIntegerTy())
            x = builder_->CreateSIToFP(x, llvm::Type::getDoubleTy(*context_), "tofp");
        auto *fn = llvm::Intrinsic::getDeclaration(
            module_.get(), llvm::Intrinsic::floor, {llvm::Type::getDoubleTy(*context_)});
        return builder_->CreateCall(fn, {x}, "floortmp");
    }

    if (funcName == "ceil" && !node->getArgs().empty()) {
        auto *x = visit(node->getArgs()[0].get());
        if (!x) return nullptr;
        if (x->getType()->isIntegerTy())
            x = builder_->CreateSIToFP(x, llvm::Type::getDoubleTy(*context_), "tofp");
        auto *fn = llvm::Intrinsic::getDeclaration(
            module_.get(), llvm::Intrinsic::ceil, {llvm::Type::getDoubleTy(*context_)});
        return builder_->CreateCall(fn, {x}, "ceiltmp");
    }

    if (funcName == "log" && !node->getArgs().empty()) {
        auto *x = visit(node->getArgs()[0].get());
        if (!x) return nullptr;
        if (x->getType()->isIntegerTy())
            x = builder_->CreateSIToFP(x, llvm::Type::getDoubleTy(*context_), "tofp");
        auto *fn = llvm::Intrinsic::getDeclaration(
            module_.get(), llvm::Intrinsic::log, {llvm::Type::getDoubleTy(*context_)});
        return builder_->CreateCall(fn, {x}, "logtmp");
    }

    if (funcName == "log10" && !node->getArgs().empty()) {
        auto *x = visit(node->getArgs()[0].get());
        if (!x) return nullptr;
        if (x->getType()->isIntegerTy())
            x = builder_->CreateSIToFP(x, llvm::Type::getDoubleTy(*context_), "tofp");
        auto *fn = llvm::Intrinsic::getDeclaration(
            module_.get(), llvm::Intrinsic::log10, {llvm::Type::getDoubleTy(*context_)});
        return builder_->CreateCall(fn, {x}, "log10tmp");
    }

    if (funcName == "sin" && !node->getArgs().empty()) {
        auto *x = visit(node->getArgs()[0].get());
        if (!x) return nullptr;
        if (x->getType()->isIntegerTy())
            x = builder_->CreateSIToFP(x, llvm::Type::getDoubleTy(*context_), "tofp");
        auto *fn = llvm::Intrinsic::getDeclaration(
            module_.get(), llvm::Intrinsic::sin, {llvm::Type::getDoubleTy(*context_)});
        return builder_->CreateCall(fn, {x}, "sintmp");
    }

    if (funcName == "cos" && !node->getArgs().empty()) {
        auto *x = visit(node->getArgs()[0].get());
        if (!x) return nullptr;
        if (x->getType()->isIntegerTy())
            x = builder_->CreateSIToFP(x, llvm::Type::getDoubleTy(*context_), "tofp");
        auto *fn = llvm::Intrinsic::getDeclaration(
            module_.get(), llvm::Intrinsic::cos, {llvm::Type::getDoubleTy(*context_)});
        return builder_->CreateCall(fn, {x}, "costmp");
    }

    if (funcName == "tan" && !node->getArgs().empty()) {
        auto *x = visit(node->getArgs()[0].get());
        if (!x) return nullptr;
        if (x->getType()->isIntegerTy())
            x = builder_->CreateSIToFP(x, llvm::Type::getDoubleTy(*context_), "tofp");
        auto *fn = llvm::Intrinsic::getDeclaration(
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
            auto *powFn = llvm::Intrinsic::getDeclaration(
                module_.get(), llvm::Intrinsic::pow, {f64Ty});
            auto *factor = builder_->CreateCall(powFn, {ten, d}, "factor");
            auto *scaled = builder_->CreateFMul(x, factor, "scaled");
            auto *roundFn = llvm::Intrinsic::getDeclaration(
                module_.get(), llvm::Intrinsic::round, {f64Ty});
            auto *rounded = builder_->CreateCall(roundFn, {scaled}, "rounded");
            return builder_->CreateFDiv(rounded, factor, "roundtmp");
        }
        // round(x): round to nearest integer
        auto *fn = llvm::Intrinsic::getDeclaration(
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
    auto savedMapTypes2 = varMapTypes_;
    auto savedSetTypes2 = varSetTypes_;
    auto savedOptionalTypes2 = varOptionalTypes_;
    auto savedFuncTypes2 = varFuncTypes_;
    auto savedProtocolTypes2 = varProtocolTypes_;
    auto savedResultTypes2 = varResultTypes_;
    auto savedFileTypes2 = varFileTypes_;
    auto savedFileOptTypes2 = varFileOptionalTypes_;
    auto savedTupleTypes2 = varTupleTypes_;
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
    varMapTypes_.clear();
    varSetTypes_.clear();
    varOptionalTypes_.clear();
    varFuncTypes_.clear();
    varProtocolTypes_.clear();
    varResultTypes_.clear();
    varFileTypes_.clear();
    varFileOptionalTypes_.clear();
    varTupleTypes_.clear();
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
    varMapTypes_ = savedMapTypes2;
    varSetTypes_ = savedSetTypes2;
    varOptionalTypes_ = savedOptionalTypes2;
    varFuncTypes_ = savedFuncTypes2;
    varProtocolTypes_ = savedProtocolTypes2;
    varResultTypes_ = savedResultTypes2;
    varFileTypes_ = savedFileTypes2;
    varFileOptionalTypes_ = savedFileOptTypes2;
    varTupleTypes_ = savedTupleTypes2;
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
        auto oldVarMapTypes = varMapTypes_;
        auto oldVarSetTypes = varSetTypes_;
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
        varMapTypes_.clear();
        varSetTypes_.clear();
        varOptionalTypes_.clear();
        varFuncTypes_.clear();
        varProtocolTypes_.clear();
        varResultTypes_.clear();
        varFileTypes_.clear();
        currentFuncResultInfo_ = nullptr;

        // Create allocas for parameters
        i = 0;
        for (auto &arg : func->args()) {
            auto *alloca = createEntryBlockAlloca(func, std::string(arg.getName()),
                                                   arg.getType());
            builder_->CreateStore(&arg, alloca);
            namedValues_[std::string(arg.getName())] = alloca;
            if (method->getParams()[i].isSelf) {
                // Register self as struct or enum type
                auto etIt = enumTypes_.find(typeName);
                if (etIt != enumTypes_.end()) {
                    varEnumTypes_["self"] = typeName;
                } else {
                    varStructTypes_["self"] = typeName;
                }
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
        varMapTypes_ = oldVarMapTypes;
        varSetTypes_ = oldVarSetTypes;
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
                auto oldVarMapTypes = varMapTypes_;
                auto oldVarSetTypes = varSetTypes_;
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
                varMapTypes_.clear();
                varSetTypes_.clear();
                varOptionalTypes_.clear();
                varFuncTypes_.clear();
                varProtocolTypes_.clear();
                varResultTypes_.clear();
                varFileTypes_.clear();
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
                varMapTypes_ = oldVarMapTypes;
                varSetTypes_ = oldVarSetTypes;
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

llvm::Value *IRGen::visitTupleLiteralExpr(TupleLiteralExpr *node) {
    auto *tupleTypeRepr = node->getResolvedType();
    if (!tupleTypeRepr) return nullptr;
    auto *tupleTy = toLLVMType(tupleTypeRepr);
    auto *func = builder_->GetInsertBlock()->getParent();
    auto *alloca = createEntryBlockAlloca(func, "tuple.tmp", tupleTy);

    for (size_t i = 0; i < node->getElements().size(); ++i) {
        auto *val = visit(node->getElements()[i].get());
        if (!val) continue;
        auto *gep = builder_->CreateStructGEP(tupleTy, alloca, i);
        builder_->CreateStore(val, gep);
    }

    return builder_->CreateLoad(tupleTy, alloca, "tuple.val");
}

llvm::Value *IRGen::visitIndexExpr(IndexExpr *node) {
    if (node->getBase()->getKind() != ASTNode::NodeKind::IdentifierExpr)
        return nullptr;
    auto *ident = static_cast<const IdentifierExpr *>(node->getBase());

    // === Range slicing: arr[1..3] or s[1..3] ===
    if (node->getIndex()->getKind() == ASTNode::NodeKind::RangeExpr) {
        auto *rangeExpr = static_cast<RangeExpr *>(const_cast<Expr *>(node->getIndex()));
        auto *startVal = visit(const_cast<Expr *>(rangeExpr->getStart()));
        auto *endVal = visit(const_cast<Expr *>(rangeExpr->getEnd()));
        if (!startVal || !endVal) return nullptr;
        if (startVal->getType()->isIntegerTy(32))
            startVal = builder_->CreateSExt(startVal, builder_->getInt64Ty(), "slice.start");
        if (endVal->getType()->isIntegerTy(32))
            endVal = builder_->CreateSExt(endVal, builder_->getInt64Ty(), "slice.end");
        auto *sliceLen = builder_->CreateSub(endVal, startVal, "slice.len");

        // String slicing: s[1..3] -> liva_str_substring(s, start, end-start)
        auto daIt = varDynArrayTypes_.find(ident->getName());
        if (daIt == varDynArrayTypes_.end() && varArrayTypes_.find(ident->getName()) == varArrayTypes_.end()) {
            auto allocaIt = namedValues_.find(ident->getName());
            if (allocaIt == namedValues_.end()) return nullptr;
            auto *ptrTy = llvm::PointerType::getUnqual(*context_);
            auto *strVal = builder_->CreateLoad(ptrTy, allocaIt->second, "str.ptr");
            auto *subFn = module_->getOrInsertFunction(
                "liva_str_substring",
                llvm::FunctionType::get(ptrTy, {ptrTy, builder_->getInt64Ty(), builder_->getInt64Ty()}, false)).getCallee();
            return builder_->CreateCall(
                llvm::FunctionType::get(ptrTy, {ptrTy, builder_->getInt64Ty(), builder_->getInt64Ty()}, false),
                subFn, {strVal, startVal, sliceLen}, "str.slice");
        }

        // DynArray slicing: arr[1..3] -> new DynArray with copied elements
        if (daIt != varDynArrayTypes_.end()) {
            auto allocaIt = namedValues_.find(ident->getName());
            if (allocaIt == namedValues_.end()) return nullptr;
            auto *arrAlloca = allocaIt->second;
            auto *structTy = getDynArrayStructTy();
            auto *elemType = daIt->second.elementType;
            auto elemSize = builder_->getInt64(daIt->second.elemSize);
            auto *ptrTy = llvm::PointerType::getUnqual(*context_);
            auto *dataField = builder_->CreateStructGEP(structTy, arrAlloca, 0);
            auto *dataPtr = builder_->CreateLoad(ptrTy, dataField, "src.data");
            // Allocate new array: liva_array_new(elem_size, sliceLen)
            auto *newFn = module_->getOrInsertFunction(
                "liva_array_new",
                llvm::FunctionType::get(ptrTy, {builder_->getInt64Ty(), builder_->getInt64Ty()}, false)).getCallee();
            auto *newData = builder_->CreateCall(
                llvm::FunctionType::get(ptrTy, {builder_->getInt64Ty(), builder_->getInt64Ty()}, false),
                newFn, {elemSize, sliceLen}, "slice.data");
            // memcpy(newData, dataPtr + start * elemSize, sliceLen * elemSize)
            auto *srcOffset = builder_->CreateMul(startVal, elemSize, "src.off");
            auto *srcPtr = builder_->CreateGEP(builder_->getInt8Ty(), dataPtr, srcOffset, "src.ptr");
            auto *copySize = builder_->CreateMul(sliceLen, elemSize, "copy.sz");
            builder_->CreateMemCpy(newData, llvm::MaybeAlign(1), srcPtr, llvm::MaybeAlign(1), copySize);
            // Build DynArray struct {ptr, i64, i64}
            auto *func = builder_->GetInsertBlock()->getParent();
            auto *resultAlloca = createEntryBlockAlloca(func, "slice.arr", structTy);
            auto *f0 = builder_->CreateStructGEP(structTy, resultAlloca, 0);
            builder_->CreateStore(newData, f0);
            auto *f1 = builder_->CreateStructGEP(structTy, resultAlloca, 1);
            builder_->CreateStore(sliceLen, f1);
            auto *f2 = builder_->CreateStructGEP(structTy, resultAlloca, 2);
            builder_->CreateStore(sliceLen, f2);
            return builder_->CreateLoad(structTy, resultAlloca, "slice.result");
        }

        return nullptr;
    }

    // === Scalar indexing ===
    auto *indexVal = visit(const_cast<Expr *>(node->getIndex()));
    if (!indexVal) return nullptr;

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
    if (arrIt != varArrayTypes_.end()) {
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

    // String indexing: s[i] -> liva_str_substring(s, i, 1)
    auto allocaIt = namedValues_.find(ident->getName());
    if (allocaIt != namedValues_.end()) {
        auto *ptrTy = llvm::PointerType::getUnqual(*context_);
        auto *strVal = builder_->CreateLoad(ptrTy, allocaIt->second, "str.ptr");
        if (indexVal->getType()->isIntegerTy(32))
            indexVal = builder_->CreateSExt(indexVal, builder_->getInt64Ty(), "idx.ext");
        // Bounds check: liva_str_length(s)
        auto *lenFn = module_->getOrInsertFunction(
            "liva_str_length",
            llvm::FunctionType::get(builder_->getInt64Ty(), {ptrTy}, false)).getCallee();
        auto *strLen = builder_->CreateCall(
            llvm::FunctionType::get(builder_->getInt64Ty(), {ptrTy}, false),
            lenFn, {strVal}, "str.len");
        emitBoundsCheck(indexVal, strLen);
        // liva_str_substring(s, i, 1)
        auto *subFn = module_->getOrInsertFunction(
            "liva_str_substring",
            llvm::FunctionType::get(ptrTy, {ptrTy, builder_->getInt64Ty(), builder_->getInt64Ty()}, false)).getCallee();
        return builder_->CreateCall(
            llvm::FunctionType::get(ptrTy, {ptrTy, builder_->getInt64Ty(), builder_->getInt64Ty()}, false),
            subFn, {strVal, indexVal, builder_->getInt64(1)}, "str.char");
    }

    return nullptr;
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
    auto savedVarMapTypes = varMapTypes_;
    auto savedVarSetTypes = varSetTypes_;
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
    varMapTypes_.clear();
    varSetTypes_.clear();
    varOptionalTypes_.clear();
    varFuncTypes_.clear();
    varProtocolTypes_.clear();
    varResultTypes_.clear();
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
    varMapTypes_ = savedVarMapTypes;
    varSetTypes_ = savedVarSetTypes;
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
    auto savedVarMapTypes = varMapTypes_;
    auto savedVarSetTypes = varSetTypes_;
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
    varMapTypes_.clear();
    varSetTypes_.clear();
    varOptionalTypes_.clear();
    varFuncTypes_.clear();
    varProtocolTypes_.clear();
    varResultTypes_.clear();
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
    varMapTypes_ = savedVarMapTypes;
    varSetTypes_ = savedVarSetTypes;
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

llvm::Value *IRGen::visitTernaryExpr(TernaryExpr *node) {
    auto *condVal = visit(node->getCondition());
    if (!condVal) return nullptr;
    // Ensure condition is i1
    if (!condVal->getType()->isIntegerTy(1))
        condVal = builder_->CreateICmpNE(condVal, llvm::ConstantInt::get(condVal->getType(), 0), "tern.cond");

    auto *func = builder_->GetInsertBlock()->getParent();
    auto *thenBB = llvm::BasicBlock::Create(*context_, "tern.then", func);
    auto *elseBB = llvm::BasicBlock::Create(*context_, "tern.else", func);
    auto *mergeBB = llvm::BasicBlock::Create(*context_, "tern.merge", func);
    builder_->CreateCondBr(condVal, thenBB, elseBB);

    // Then branch
    builder_->SetInsertPoint(thenBB);
    auto *thenVal = visit(node->getThenExpr());
    if (!thenVal) return nullptr;
    auto *thenEndBB = builder_->GetInsertBlock();
    builder_->CreateBr(mergeBB);

    // Else branch
    builder_->SetInsertPoint(elseBB);
    auto *elseVal = visit(node->getElseExpr());
    if (!elseVal) return nullptr;
    auto *elseEndBB = builder_->GetInsertBlock();
    builder_->CreateBr(mergeBB);

    // Merge with PHI
    builder_->SetInsertPoint(mergeBB);
    auto *phi = builder_->CreatePHI(thenVal->getType(), 2, "tern.val");
    phi->addIncoming(thenVal, thenEndBB);
    phi->addIncoming(elseVal, elseEndBB);
    return phi;
}

llvm::Value *IRGen::visitRefExpr(RefExpr *node) {
    // ref x → return address of x (the alloca pointer)
    if (auto *ident = dynamic_cast<const IdentifierExpr *>(node->getExpr())) {
        auto it = namedValues_.find(ident->getName());
        if (it != namedValues_.end()) {
            // If x is itself a ref, load the pointer (pass-through)
            auto refIt = varRefTypes_.find(ident->getName());
            if (refIt != varRefTypes_.end()) {
                return builder_->CreateLoad(
                    llvm::PointerType::getUnqual(*context_), it->second,
                    ident->getName() + ".ref");
            }
            // Normal variable: return alloca address
            return it->second;
        }
    }
    return nullptr;
}

llvm::Value *IRGen::visitTypeAliasDecl(TypeAliasDecl *node) {
    // Record alias for toLLVMType resolution
    typeAliases_[node->getName()] = node->getTargetType();
    return nullptr;
}

} // namespace liva

#endif // LIVA_HAS_LLVM
