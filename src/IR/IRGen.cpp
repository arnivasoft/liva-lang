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

llvm::StructType *IRGen::getTaskType(llvm::Type *innerType) {
    auto it = taskTypes_.find(innerType);
    if (it != taskTypes_.end()) return it->second;
    auto *ty = llvm::StructType::create(*context_,
        {builder_->getInt1Ty(), innerType}, "Task");
    taskTypes_[innerType] = ty;
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
    case TypeRepr::Kind::Generic: {
        auto *genType = static_cast<const GenericTypeRepr *>(type);
        if (genType->getBaseName() == "Task" && !genType->getTypeArgs().empty()) {
            return getTaskType(toLLVMType(genType->getTypeArgs()[0].get()));
        }
        // Other generics (Map, Set, etc.) are handled elsewhere
        return llvm::PointerType::getUnqual(*context_);
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

} // namespace liva

#endif // LIVA_HAS_LLVM
