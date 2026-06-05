#include "liva/IR/IRGen.h"

#ifdef LIVA_HAS_LLVM

#include <llvm/BinaryFormat/Dwarf.h>
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

void IRGen::initDebugInfo(const std::string &filename) {
    if (!emitDebugInfo_) return;
    diBuilder_ = std::make_unique<llvm::DIBuilder>(*module_);
    diFile_ = diBuilder_->createFile(filename, ".");
    diCU_ = diBuilder_->createCompileUnit(
        llvm::dwarf::DW_LANG_C,  // closest available to Liva
        diFile_,
        "Liva Compiler",  // producer
        false,  // isOptimized
        "",     // flags
        0       // runtime version
    );
    module_->addModuleFlag(llvm::Module::Warning, "Debug Info Version",
                           llvm::DEBUG_METADATA_VERSION);
    // On Windows, emit CodeView debug info
    module_->addModuleFlag(llvm::Module::Warning, "CodeView", 1);
}

void IRGen::finalizeDebugInfo() {
    if (diBuilder_) {
        diBuilder_->finalize();
    }
}

llvm::DISubroutineType *IRGen::createFunctionDebugType() {
    // Minimal: unspecified return type (void-like)
    llvm::SmallVector<llvm::Metadata *, 1> types;
    types.push_back(nullptr);  // void return type
    return diBuilder_->createSubroutineType(diBuilder_->getOrCreateTypeArray(types));
}

void IRGen::emitDebugLocation(const SourceLocation &loc) {
    if (!diBuilder_ || !loc.isValid()) return;
    // Always use current function's subprogram as scope
    llvm::DIScope *scope = nullptr;
    auto *bb = builder_->GetInsertBlock();
    if (bb) {
        auto *func = bb->getParent();
        if (func)
            scope = func->getSubprogram();
    }
    if (!scope)
        return;  // DILocation requires DILocalScope, diCU_ is not valid
    builder_->SetCurrentDebugLocation(
        llvm::DILocation::get(*context_, loc.line, loc.column, scope));
}

llvm::DIType *IRGen::toDIType(const TypeRepr *type) {
    if (!type || !diBuilder_) return nullptr;
    switch (type->getKind()) {
    case TypeRepr::Kind::Bool:
        return diBuilder_->createBasicType("bool", 8, llvm::dwarf::DW_ATE_boolean);
    case TypeRepr::Kind::I8:
        return diBuilder_->createBasicType("i8", 8, llvm::dwarf::DW_ATE_signed);
    case TypeRepr::Kind::U8:
        return diBuilder_->createBasicType("u8", 8, llvm::dwarf::DW_ATE_unsigned);
    case TypeRepr::Kind::I16:
        return diBuilder_->createBasicType("i16", 16, llvm::dwarf::DW_ATE_signed);
    case TypeRepr::Kind::U16:
        return diBuilder_->createBasicType("u16", 16, llvm::dwarf::DW_ATE_unsigned);
    case TypeRepr::Kind::I32:
        return diBuilder_->createBasicType("i32", 32, llvm::dwarf::DW_ATE_signed);
    case TypeRepr::Kind::U32:
        return diBuilder_->createBasicType("u32", 32, llvm::dwarf::DW_ATE_unsigned);
    case TypeRepr::Kind::I64:
        return diBuilder_->createBasicType("i64", 64, llvm::dwarf::DW_ATE_signed);
    case TypeRepr::Kind::U64:
        return diBuilder_->createBasicType("u64", 64, llvm::dwarf::DW_ATE_unsigned);
    case TypeRepr::Kind::F32:
        return diBuilder_->createBasicType("f32", 32, llvm::dwarf::DW_ATE_float);
    case TypeRepr::Kind::F64:
        return diBuilder_->createBasicType("f64", 64, llvm::dwarf::DW_ATE_float);
    case TypeRepr::Kind::String:
        return diBuilder_->createPointerType(
            diBuilder_->createBasicType("char", 8, llvm::dwarf::DW_ATE_signed_char), 64);
    case TypeRepr::Kind::Optional: {
        auto *optType = static_cast<const OptionalTypeRepr *>(type);
        auto *innerDI = toDIType(optType->getInner());
        if (!innerDI) return nullptr;
        // Optional is {i1 hasValue, T value} struct
        auto *innerLLVM = toLLVMType(optType->getInner());
        if (!innerLLVM) return nullptr;
        auto *optStructTy = getOptionalType(innerLLVM);
        auto &dl = module_->getDataLayout();
        auto *layout = dl.getStructLayout(optStructTy);
        auto *boolDI = diBuilder_->createBasicType("bool", 8, llvm::dwarf::DW_ATE_boolean);
        llvm::SmallVector<llvm::Metadata *, 2> elements;
        elements.push_back(diBuilder_->createMemberType(
            diCU_, "hasValue", diFile_, 0, 8, 0,
            layout->getElementOffsetInBits(0),
            llvm::DINode::FlagZero, boolDI));
        elements.push_back(diBuilder_->createMemberType(
            diCU_, "value", diFile_, 0,
            dl.getTypeSizeInBits(innerLLVM), 0,
            layout->getElementOffsetInBits(1),
            llvm::DINode::FlagZero, innerDI));
        return diBuilder_->createStructType(
            diCU_, optType->toString(), diFile_, 0,
            dl.getTypeSizeInBits(optStructTy),
            0, llvm::DINode::FlagZero, nullptr,
            diBuilder_->getOrCreateArray(elements));
    }
    case TypeRepr::Kind::Array: {
        auto *arrType = static_cast<const ArrayTypeRepr *>(type);
        auto *elemDI = toDIType(arrType->getElement());
        if (!elemDI) return nullptr;
        if (arrType->isDynamic()) {
            // Dynamic array: {ptr data, i64 size, i64 capacity} — 192 bits
            auto *ptrDI = diBuilder_->createPointerType(elemDI, 64);
            auto *i64DI = diBuilder_->createBasicType("i64", 64, llvm::dwarf::DW_ATE_signed);
            llvm::SmallVector<llvm::Metadata *, 3> elements;
            elements.push_back(diBuilder_->createMemberType(
                diCU_, "data", diFile_, 0, 64, 0, 0,
                llvm::DINode::FlagZero, ptrDI));
            elements.push_back(diBuilder_->createMemberType(
                diCU_, "size", diFile_, 0, 64, 0, 64,
                llvm::DINode::FlagZero, i64DI));
            elements.push_back(diBuilder_->createMemberType(
                diCU_, "capacity", diFile_, 0, 64, 0, 128,
                llvm::DINode::FlagZero, i64DI));
            return diBuilder_->createStructType(
                diCU_, arrType->toString(), diFile_, 0,
                192, 0, llvm::DINode::FlagZero, nullptr,
                diBuilder_->getOrCreateArray(elements));
        } else {
            // Fixed array: [T; N]
            auto *elemLLVM = toLLVMType(arrType->getElement());
            if (!elemLLVM) return nullptr;
            auto &dl = module_->getDataLayout();
            uint64_t elemBits = dl.getTypeSizeInBits(elemLLVM);
            int64_t count = arrType->getSize();
            auto *subrange = diBuilder_->getOrCreateSubrange(0, count);
            return diBuilder_->createArrayType(
                static_cast<uint64_t>(count) * elemBits, 0, elemDI,
                diBuilder_->getOrCreateArray({subrange}));
        }
    }
    case TypeRepr::Kind::Named: {
        auto *named = static_cast<const NamedTypeRepr *>(type);
        auto it = diStructTypes_.find(named->getName());
        if (it != diStructTypes_.end()) return it->second;
        // Check if this is an enum type
        auto enumIt = enumTypes_.find(named->getName());
        if (enumIt != enumTypes_.end()) {
            auto *i32DI = diBuilder_->createBasicType("i32", 32, llvm::dwarf::DW_ATE_signed);
            llvm::SmallVector<llvm::Metadata *, 1> elements;
            elements.push_back(diBuilder_->createMemberType(
                diCU_, "tag", diFile_, 0, 32, 0, 0,
                llvm::DINode::FlagZero, i32DI));
            auto *enumDI = diBuilder_->createStructType(
                diCU_, named->getName(), diFile_, 0,
                32, 0, llvm::DINode::FlagZero, nullptr,
                diBuilder_->getOrCreateArray(elements));
            diStructTypes_[named->getName()] = enumDI;
            return enumDI;
        }
        return nullptr;
    }
    case TypeRepr::Kind::Void:
    default:
        return nullptr;
    }
}

llvm::DISubroutineType *IRGen::createFunctionDebugType(const FuncDecl *funcDecl) {
    if (!diBuilder_ || !funcDecl) return createFunctionDebugType();
    llvm::SmallVector<llvm::Metadata *, 8> types;
    // First element is return type
    types.push_back(toDIType(funcDecl->getReturnType()));
    // Then parameter types
    for (auto &param : funcDecl->getParams()) {
        if (param.isSelf) {
            // self is a pointer
            auto *charTy = diBuilder_->createBasicType("char", 8, llvm::dwarf::DW_ATE_signed_char);
            types.push_back(diBuilder_->createPointerType(charTy, 64));
        } else {
            types.push_back(toDIType(param.type.get()));
        }
    }
    return diBuilder_->createSubroutineType(diBuilder_->getOrCreateTypeArray(types));
}

void IRGen::emitLocalVarDebugInfo(const std::string &name, llvm::AllocaInst *alloca,
                                   llvm::DIType *diType, const SourceLocation &loc) {
    if (!diBuilder_ || !diType || !alloca) return;
    auto *bb = builder_->GetInsertBlock();
    if (!bb) return;
    auto *func = bb->getParent();
    if (!func) return;
    auto *sp = func->getSubprogram();
    if (!sp) return;
    unsigned line = loc.isValid() ? loc.line : 0;
    auto *varInfo = diBuilder_->createAutoVariable(sp, name, diFile_, line, diType);
    diBuilder_->insertDeclare(
        alloca, varInfo, diBuilder_->createExpression(),
        llvm::DILocation::get(*context_, line, 0, sp),
        bb);
}

void IRGen::emitParamDebugInfo(const std::string &name, unsigned argNo, llvm::AllocaInst *alloca,
                                llvm::DIType *diType, const SourceLocation &loc) {
    if (!diBuilder_ || !diType || !alloca) return;
    auto *bb = builder_->GetInsertBlock();
    if (!bb) return;
    auto *func = bb->getParent();
    if (!func) return;
    auto *sp = func->getSubprogram();
    if (!sp) return;
    unsigned line = loc.isValid() ? loc.line : 0;
    auto *varInfo = diBuilder_->createParameterVariable(sp, name, argNo, diFile_, line, diType);
    diBuilder_->insertDeclare(
        alloca, varInfo, diBuilder_->createExpression(),
        llvm::DILocation::get(*context_, line, 0, sp),
        bb);
}

llvm::DICompositeType *IRGen::getOrCreateStructDIType(const std::string &structName) {
    if (!diBuilder_) return nullptr;
    auto it = diStructTypes_.find(structName);
    if (it != diStructTypes_.end()) return it->second;

    auto stIt = structTypes_.find(structName);
    if (stIt == structTypes_.end()) return nullptr;
    auto fnIt = structFieldNames_.find(structName);
    if (fnIt == structFieldNames_.end()) return nullptr;
    auto ftrIt = structFieldTypeReprs_.find(structName);
    if (ftrIt == structFieldTypeReprs_.end()) return nullptr;

    auto *structTy = stIt->second;
    const auto &fieldNames = fnIt->second;
    const auto &fieldTypeReprs = ftrIt->second;
    const llvm::DataLayout &dl = module_->getDataLayout();
    const auto *layout = dl.getStructLayout(structTy);
    uint64_t totalSize = dl.getTypeAllocSize(structTy) * 8; // in bits

    llvm::SmallVector<llvm::Metadata *, 8> members;
    for (size_t i = 0; i < fieldNames.size(); ++i) {
        auto *fieldDITy = toDIType(fieldTypeReprs[i]);
        if (!fieldDITy) continue;
        uint64_t fieldSize = dl.getTypeAllocSize(structTy->getElementType(static_cast<unsigned>(i))) * 8;
        uint64_t fieldOffset = layout->getElementOffsetInBits(static_cast<unsigned>(i));
        auto *member = diBuilder_->createMemberType(
            diFile_, fieldNames[i], diFile_, 0, fieldSize, 0, fieldOffset,
            llvm::DINode::FlagZero, fieldDITy);
        members.push_back(member);
    }

    auto *diStructTy = diBuilder_->createStructType(
        diFile_, structName, diFile_, 0, totalSize, 0,
        llvm::DINode::FlagZero, nullptr,
        diBuilder_->getOrCreateArray(members));
    diStructTypes_[structName] = diStructTy;
    return diStructTy;
}

bool IRGen::generate(TranslationUnit &tu) {
    // Override module target triple if cross-compiling
    if (!targetTriple_.empty()) {
        module_->setTargetTriple(llvm::Triple(targetTriple_));
    }

    initDebugInfo(module_->getModuleIdentifier());
    createRuntimeDecls();

    // Enum pre-pass: register every enum's case/payload maps BEFORE class method
    // prototypes are built, so a method param typed by an enum (e.g.
    // `setAlign(a: Align)`) lowers to the correct LLVM type (i32 for simple
    // enums) regardless of source order.
    for (auto &decl : tu.getDeclarations()) {
        if (decl->getKind() == ASTNode::NodeKind::EnumDecl) {
            auto *enumDecl = static_cast<EnumDecl *>(decl.get());
            if (preDeclaredEnums_.insert(enumDecl->getName()).second)
                visitEnumDecl(enumDecl);
        }
    }

    // Class pre-pass (phase 1): register every class's type/field/vtable maps and
    // create all method/init/accessor PROTOTYPES (no bodies) BEFORE any body is
    // emitted, so a class can forward-reference a class declared later in the file.
    // First index all class decls so preDeclareClass can recursively ensure a
    // parent is pre-declared before its children regardless of source order.
    for (auto &decl : tu.getDeclarations()) {
        if (decl->getKind() == ASTNode::NodeKind::ClassDecl) {
            auto *classDecl = static_cast<ClassDecl *>(decl.get());
            classDecls_[classDecl->getName()] = classDecl;
        }
    }
    for (auto &decl : tu.getDeclarations()) {
        if (decl->getKind() == ASTNode::NodeKind::ClassDecl) {
            preDeclareClass(static_cast<ClassDecl *>(decl.get()));
        }
    }

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

        // Enums already registered in the enum pre-pass — don't re-emit (would
        // recreate the named tagged-union struct type).
        if (decl->getKind() == ASTNode::NodeKind::EnumDecl) {
            auto *enumDecl = static_cast<EnumDecl *>(decl.get());
            if (preDeclaredEnums_.count(enumDecl->getName()))
                continue;
        }

        visit(decl.get());
        if (diag_.hasErrors())
            return false;
    }

    finalizeDebugInfo();

    // Generate synthetic test main if we have test entries and no user-defined main
    if (!testEntries_.empty() && !module_->getFunction("main")) {
        generateTestMain();
    }

    // Check for main function (skip if not required, e.g. non-entry separate compilation)
    if (requireMain_ && !module_->getFunction("main")) {
        diag_.report(SourceLocation{}, DiagID::err_main_not_found);
        return false;
    }

    // Verify the module
    std::string errStr;
    llvm::raw_string_ostream errStream(errStr);
    if (llvm::verifyModule(*module_, &errStream)) {
        errStream.flush();
        diag_.report(SourceLocation{}, DiagID::err_module_verify, errStr);
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
    auto *strDupTy = llvm::FunctionType::get(i8PtrTy, {i8PtrTy}, false);
    module_->getOrInsertFunction("liva_str_dup", strDupTy);

    auto *concatTy = llvm::FunctionType::get(i8PtrTy, {i8PtrTy, i8PtrTy}, false);
    module_->getOrInsertFunction("liva_str_concat", concatTy);

    auto *equalTy = llvm::FunctionType::get(i32Ty, {i8PtrTy, i8PtrTy}, false);
    module_->getOrInsertFunction("liva_str_equal", equalTy);
    module_->getOrInsertFunction("liva_str_compare", equalTy);

    auto *lenTy = llvm::FunctionType::get(i64Ty, {i8PtrTy}, false);
    module_->getOrInsertFunction("liva_str_length", lenTy);
    module_->getOrInsertFunction("liva_str_byte_length", lenTy);

    auto *i32ToStrTy = llvm::FunctionType::get(i8PtrTy, {i32Ty}, false);
    module_->getOrInsertFunction("liva_i32_to_str", i32ToStrTy);

    // liva_char_to_str(i32) -> i8* (char code point to string)
    module_->getOrInsertFunction("liva_char_to_str", i32ToStrTy);

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

    // P1-8 alt-spec 2: built-in Hashable. `expr.hash()` on primitive receivers
    // (i8/i16/i32/i64/u8/u16/u32/u64/string/bool/Char) lowers to one of these.
    auto *hashI64Ty = llvm::FunctionType::get(i64Ty, {i64Ty}, false);
    module_->getOrInsertFunction("liva_hash_i64", hashI64Ty);

    auto *hashI32Ty = llvm::FunctionType::get(i64Ty, {i32Ty}, false);
    module_->getOrInsertFunction("liva_hash_i32", hashI32Ty);

    auto *hashStringTy = llvm::FunctionType::get(i64Ty, {i8PtrTy}, false);
    module_->getOrInsertFunction("liva_hash_string", hashStringTy);

    auto *hashBoolTy = llvm::FunctionType::get(i64Ty, {i8Ty}, false);
    module_->getOrInsertFunction("liva_hash_bool", hashBoolTy);

    auto *hashCharTy = llvm::FunctionType::get(i64Ty, {i32Ty}, false);
    module_->getOrInsertFunction("liva_hash_char", hashCharTy);

    // liva_init_args(i32 argc, ptr argv) -> void
    auto *initArgsTy = llvm::FunctionType::get(builder_->getVoidTy(), {i32Ty, i8PtrTy}, false);
    module_->getOrInsertFunction("liva_init_args", initArgsTy);

    // === Stdlib: Random ===
    auto *randIntTy = llvm::FunctionType::get(i32Ty, {i32Ty, i32Ty}, false);
    module_->getOrInsertFunction("liva_rand_int", randIntTy);

    auto *randFloatTy = llvm::FunctionType::get(f64Ty, {}, false);
    module_->getOrInsertFunction("liva_rand_float", randFloatTy);

    auto *randSeedTy = llvm::FunctionType::get(builder_->getVoidTy(), {i64Ty}, false);
    module_->getOrInsertFunction("liva_rand_seed", randSeedTy);

    auto *randI64Ty = llvm::FunctionType::get(i64Ty, {}, false);
    module_->getOrInsertFunction("liva_rand_i64", randI64Ty);

    auto *randUuidTy = llvm::FunctionType::get(i8PtrTy, {}, false);
    module_->getOrInsertFunction("liva_rand_uuid", randUuidTy);
    module_->getOrInsertFunction("liva_rand_uuid_v7", randUuidTy);

    // === Stdlib: Process/Env ===
    auto *envGetTy = llvm::FunctionType::get(i8PtrTy, {i8PtrTy}, false);
    module_->getOrInsertFunction("liva_env_get", envGetTy);

    auto *exitTy = llvm::FunctionType::get(builder_->getVoidTy(), {i32Ty}, false);
    auto exitFn = module_->getOrInsertFunction("liva_exit", exitTy);
    if (auto *f = llvm::dyn_cast<llvm::Function>(exitFn.getCallee()))
        f->addFnAttr(llvm::Attribute::NoReturn);

    auto *argsTy = llvm::FunctionType::get(i8PtrTy, {i8PtrTy}, false);
    module_->getOrInsertFunction("liva_args", argsTy);

    // === Stdlib: Date/Time ===
    auto *clockTy = llvm::FunctionType::get(f64Ty, {}, false);
    module_->getOrInsertFunction("liva_clock", clockTy);

    auto *clockMsTy = llvm::FunctionType::get(i64Ty, {}, false);
    module_->getOrInsertFunction("liva_clock_ms", clockMsTy);

    auto *sleepTy = llvm::FunctionType::get(builder_->getVoidTy(), {i64Ty}, false);
    module_->getOrInsertFunction("liva_sleep", sleepTy);

    // === Stdlib: Benchmarking ===
    auto *benchStartTy = llvm::FunctionType::get(i64Ty, {}, false);
    module_->getOrInsertFunction("liva_bench_start", benchStartTy);

    auto *benchIterTy = llvm::FunctionType::get(i64Ty, {i64Ty}, false);
    module_->getOrInsertFunction("liva_bench_iter", benchIterTy);

    auto *benchDoneTy = llvm::FunctionType::get(i64Ty, {i64Ty}, false);
    module_->getOrInsertFunction("liva_bench_done", benchDoneTy);

    auto *benchReportTy = llvm::FunctionType::get(builder_->getVoidTy(), {i8PtrTy, i64Ty}, false);
    module_->getOrInsertFunction("liva_bench_report", benchReportTy);

    auto *benchResetTy = llvm::FunctionType::get(builder_->getVoidTy(), {i64Ty}, false);
    module_->getOrInsertFunction("liva_bench_reset", benchResetTy);

    // === Stdlib: Regex ===
    auto *regexMatchTy = llvm::FunctionType::get(i8Ty, {i8PtrTy, i8PtrTy}, false);
    module_->getOrInsertFunction("liva_regex_match", regexMatchTy);

    auto *regexFindTy = llvm::FunctionType::get(i8PtrTy, {i8PtrTy, i8PtrTy}, false);
    module_->getOrInsertFunction("liva_regex_find", regexFindTy);

    auto *regexFindAllTy = llvm::FunctionType::get(i8PtrTy, {i8PtrTy, i8PtrTy, i8PtrTy}, false);
    module_->getOrInsertFunction("liva_regex_find_all", regexFindAllTy);

    // regexSplit(str, pattern, &count) -> char**
    module_->getOrInsertFunction("liva_regex_split", regexFindAllTy);

    auto *regexReplaceTy = llvm::FunctionType::get(i8PtrTy, {i8PtrTy, i8PtrTy, i8PtrTy}, false);
    module_->getOrInsertFunction("liva_regex_replace", regexReplaceTy);

    // regexFindGroups(str, pattern, &count) -> char**
    module_->getOrInsertFunction("liva_regex_find_groups", regexFindAllTy);

    // regexCompile(pattern) -> i64 handle
    auto *regexCompileTy = llvm::FunctionType::get(i64Ty, {i8PtrTy}, false);
    module_->getOrInsertFunction("liva_regex_compile", regexCompileTy);

    // regexTest(handle, str) -> i8 (bool)
    auto *regexTestTy = llvm::FunctionType::get(i8Ty, {i64Ty, i8PtrTy}, false);
    module_->getOrInsertFunction("liva_regex_test", regexTestTy);

    // regexExec(handle, str) -> char*
    auto *regexExecTy = llvm::FunctionType::get(i8PtrTy, {i64Ty, i8PtrTy}, false);
    module_->getOrInsertFunction("liva_regex_exec", regexExecTy);

    // regexExecGroups(handle, str, &count) -> char**
    auto *regexExecGroupsTy = llvm::FunctionType::get(i8PtrTy, {i64Ty, i8PtrTy, i8PtrTy}, false);
    module_->getOrInsertFunction("liva_regex_exec_groups", regexExecGroupsTy);

    // regexReplaceCompiled(handle, str, replacement) -> char*
    auto *regexReplCompTy = llvm::FunctionType::get(i8PtrTy, {i64Ty, i8PtrTy, i8PtrTy}, false);
    module_->getOrInsertFunction("liva_regex_replace_compiled", regexReplCompTy);

    // regexFree(handle) -> void
    auto *voidTy = builder_->getVoidTy();
    auto *regexFreeTy = llvm::FunctionType::get(voidTy, {i64Ty}, false);
    module_->getOrInsertFunction("liva_regex_free", regexFreeTy);

    // === Stdlib: Networking ===
    // httpStatus(handle) -> i32
    auto *httpStatusTy = llvm::FunctionType::get(i32Ty, {i64Ty}, false);
    module_->getOrInsertFunction("liva_http_req_status", httpStatusTy);

    // httpBody(handle) -> char*
    auto *httpBodyTy = llvm::FunctionType::get(i8PtrTy, {i64Ty}, false);
    module_->getOrInsertFunction("liva_http_req_body", httpBodyTy);

    // httpClose(handle) -> void
    auto *httpCloseTy = llvm::FunctionType::get(builder_->getVoidTy(), {i64Ty}, false);
    module_->getOrInsertFunction("liva_http_req_close", httpCloseTy);

    // httpRequestEx(method, url, body, headersBlob, timeout) -> i64 handle
    auto *httpReqExTy = llvm::FunctionType::get(i64Ty,
        {i8PtrTy, i8PtrTy, i8PtrTy, i8PtrTy, i64Ty}, false);
    module_->getOrInsertFunction("liva_http_req_ex", httpReqExTy);
    // httpRawHeaders(handle) -> char*
    module_->getOrInsertFunction("liva_http_raw_headers", httpBodyTy);
    // httpHeaderLookup(blob, name) -> char* (nullable)
    auto *httpHdrLookupTy = llvm::FunctionType::get(i8PtrTy, {i8PtrTy, i8PtrTy}, false);
    module_->getOrInsertFunction("liva_http_header_lookup", httpHdrLookupTy);

    // URL component accessors: (ptr) -> ptr, except port (ptr) -> i32
    auto *urlStrTy = llvm::FunctionType::get(i8PtrTy, {i8PtrTy}, false);
    module_->getOrInsertFunction("liva_url_scheme", urlStrTy);
    module_->getOrInsertFunction("liva_url_host", urlStrTy);
    module_->getOrInsertFunction("liva_url_path", urlStrTy);
    module_->getOrInsertFunction("liva_url_query", urlStrTy);
    module_->getOrInsertFunction("liva_url_fragment", urlStrTy);
    auto *urlPortTy = llvm::FunctionType::get(i32Ty, {i8PtrTy}, false);
    module_->getOrInsertFunction("liva_url_port", urlPortTy);

    // === Stdlib: WebSocket ===
    // wsSend(handle, msg) -> i32
    auto *wsSendTy = llvm::FunctionType::get(i32Ty, {i64Ty, i8PtrTy}, false);
    module_->getOrInsertFunction("liva_ws_send_text", wsSendTy);

    // wsClose(handle, status, reason) -> i32
    auto *wsCloseTy = llvm::FunctionType::get(i32Ty, {i64Ty, i32Ty, i8PtrTy}, false);
    module_->getOrInsertFunction("liva_ws_close", wsCloseTy);

    // wsIsOpen(handle) -> i32 (treated as bool by Liva)
    auto *wsIsOpenTy = llvm::FunctionType::get(i32Ty, {i64Ty}, false);
    module_->getOrInsertFunction("liva_ws_is_open", wsIsOpenTy);

    // wsConnectEx(url, headersBlob, subprotocol, keepAliveMs) -> i64
    auto *wsConnectExTy = llvm::FunctionType::get(i64Ty,
        {i8PtrTy, i8PtrTy, i8PtrTy, i64Ty}, false);
    module_->getOrInsertFunction("liva_ws_connect_ex", wsConnectExTy);
    // wsRecvKind(handle) -> i32
    auto *wsRecvKindTy = llvm::FunctionType::get(i32Ty, {i64Ty}, false);
    module_->getOrInsertFunction("liva_ws_recv", wsRecvKindTy);
    // wsMsgText(handle) -> i8*
    auto *wsMsgTextTy = llvm::FunctionType::get(i8PtrTy, {i64Ty}, false);
    module_->getOrInsertFunction("liva_ws_msg_text", wsMsgTextTy);
    // wsMsgBytes(handle, out_len*) -> i8*
    auto *wsMsgBytesTy = llvm::FunctionType::get(i8PtrTy,
        {i64Ty, llvm::PointerType::getUnqual(*context_)}, false);
    module_->getOrInsertFunction("liva_ws_msg_bytes", wsMsgBytesTy);
    // wsSendBinary(handle, data*, len) -> i32
    auto *wsSendBinTy = llvm::FunctionType::get(i32Ty,
        {i64Ty, i8PtrTy, i64Ty}, false);
    module_->getOrInsertFunction("liva_ws_send_binary", wsSendBinTy);

    // === Stdlib: SQLite ===
    // sqliteOpen(path) -> i64
    auto *sqliteOpenTy = llvm::FunctionType::get(i64Ty, {i8PtrTy}, false);
    module_->getOrInsertFunction("liva_sqlite_open", sqliteOpenTy);

    // sqliteClose(handle) -> void
    auto *sqliteCloseTy = llvm::FunctionType::get(builder_->getVoidTy(), {i64Ty}, false);
    module_->getOrInsertFunction("liva_sqlite_close", sqliteCloseTy);

    // sqliteExec(handle, sql) -> i32
    auto *sqliteExecTy = llvm::FunctionType::get(i32Ty, {i64Ty, i8PtrTy}, false);
    module_->getOrInsertFunction("liva_sqlite_exec", sqliteExecTy);

    // sqliteQueryFirst(handle, sql) -> char* (nullable)
    auto *sqliteQFirstTy = llvm::FunctionType::get(i8PtrTy, {i64Ty, i8PtrTy}, false);
    module_->getOrInsertFunction("liva_sqlite_query_first", sqliteQFirstTy);

    // sqliteQueryInt(handle, sql, *ok) -> i64
    auto *sqliteQIntTy = llvm::FunctionType::get(i64Ty, {i64Ty, i8PtrTy, i8PtrTy}, false);
    module_->getOrInsertFunction("liva_sqlite_query_int", sqliteQIntTy);

    // sqliteQueryAllFirstCol(handle, sql) -> char*
    auto *sqliteQAllTy = llvm::FunctionType::get(i8PtrTy, {i64Ty, i8PtrTy}, false);
    module_->getOrInsertFunction("liva_sqlite_query_all_first_col", sqliteQAllTy);

    // sqliteLastInsertRowid(handle) -> i64
    auto *sqliteLastIdTy = llvm::FunctionType::get(i64Ty, {i64Ty}, false);
    module_->getOrInsertFunction("liva_sqlite_last_insert_rowid", sqliteLastIdTy);

    // sqliteChanges(handle) -> i32
    auto *sqliteChangesTy = llvm::FunctionType::get(i32Ty, {i64Ty}, false);
    module_->getOrInsertFunction("liva_sqlite_changes", sqliteChangesTy);

    // sqliteErrmsg(handle) -> char*
    auto *sqliteErrTy = llvm::FunctionType::get(i8PtrTy, {i64Ty}, false);
    module_->getOrInsertFunction("liva_sqlite_errmsg", sqliteErrTy);

    // --- Prepared statements ---
    // sqlitePrepare(db, sql) -> i64
    auto *sqlitePrepTy = llvm::FunctionType::get(i64Ty, {i64Ty, i8PtrTy}, false);
    module_->getOrInsertFunction("liva_sqlite_prepare", sqlitePrepTy);

    // sqliteBindText(stmt, idx, val) -> i32
    auto *sqliteBindTextTy = llvm::FunctionType::get(i32Ty, {i64Ty, i32Ty, i8PtrTy}, false);
    module_->getOrInsertFunction("liva_sqlite_bind_text", sqliteBindTextTy);

    // sqliteBindInt(stmt, idx, val) -> i32
    auto *sqliteBindIntTy = llvm::FunctionType::get(i32Ty, {i64Ty, i32Ty, i64Ty}, false);
    module_->getOrInsertFunction("liva_sqlite_bind_int", sqliteBindIntTy);

    // sqliteBindDouble(stmt, idx, val) -> i32
    auto *sqliteF64Ty = builder_->getDoubleTy();
    auto *sqliteBindDblTy = llvm::FunctionType::get(i32Ty, {i64Ty, i32Ty, sqliteF64Ty}, false);
    module_->getOrInsertFunction("liva_sqlite_bind_double", sqliteBindDblTy);

    // sqliteBindNull(stmt, idx) -> i32
    auto *sqliteBindNullTy = llvm::FunctionType::get(i32Ty, {i64Ty, i32Ty}, false);
    module_->getOrInsertFunction("liva_sqlite_bind_null", sqliteBindNullTy);

    // sqliteStep(stmt) -> i32 (1=row, 2=done, 0=error)
    auto *sqliteStepTy = llvm::FunctionType::get(i32Ty, {i64Ty}, false);
    module_->getOrInsertFunction("liva_sqlite_step", sqliteStepTy);

    // sqliteReset(stmt) -> i32
    module_->getOrInsertFunction("liva_sqlite_reset", sqliteStepTy);

    // sqliteColumnCount(stmt) -> i32
    module_->getOrInsertFunction("liva_sqlite_column_count", sqliteStepTy);

    // sqliteColumnText(stmt, col) -> char*
    auto *sqliteColTextTy = llvm::FunctionType::get(i8PtrTy, {i64Ty, i32Ty}, false);
    module_->getOrInsertFunction("liva_sqlite_column_text", sqliteColTextTy);

    // sqliteColumnInt(stmt, col) -> i64
    auto *sqliteColIntTy = llvm::FunctionType::get(i64Ty, {i64Ty, i32Ty}, false);
    module_->getOrInsertFunction("liva_sqlite_column_int", sqliteColIntTy);

    // sqliteColumnDouble(stmt, col) -> f64
    auto *sqliteColDblTy = llvm::FunctionType::get(sqliteF64Ty, {i64Ty, i32Ty}, false);
    module_->getOrInsertFunction("liva_sqlite_column_double", sqliteColDblTy);

    // sqliteColumnName(stmt, col) -> char*
    auto *sqliteColNameTy = llvm::FunctionType::get(i8PtrTy, {i64Ty, i32Ty}, false);
    module_->getOrInsertFunction("liva_sqlite_column_name", sqliteColNameTy);

    // sqliteColumnType(stmt, col) -> i32
    auto *sqliteColTypeTy = llvm::FunctionType::get(i32Ty, {i64Ty, i32Ty}, false);
    module_->getOrInsertFunction("liva_sqlite_column_type", sqliteColTypeTy);

    // sqliteBindByName(stmt, name, val) -> i32
    auto *sqliteBindNameTy = llvm::FunctionType::get(i32Ty, {i64Ty, i8PtrTy, i8PtrTy}, false);
    module_->getOrInsertFunction("liva_sqlite_bind_by_name", sqliteBindNameTy);

    // sqliteBindBlob(stmt, idx, data, len) -> i32
    auto *sqliteBindBlobTy = llvm::FunctionType::get(i32Ty, {i64Ty, i32Ty, i8PtrTy, i64Ty}, false);
    module_->getOrInsertFunction("liva_sqlite_bind_blob", sqliteBindBlobTy);
    // sqliteColumnBlob(stmt, col, out_len) -> i8*
    auto *sqliteColBlobTy = llvm::FunctionType::get(i8PtrTy, {i64Ty, i32Ty, i8PtrTy}, false);
    module_->getOrInsertFunction("liva_sqlite_column_blob", sqliteColBlobTy);

    // sqliteFinalize(stmt) -> void
    auto *sqliteFinTy = llvm::FunctionType::get(builder_->getVoidTy(), {i64Ty}, false);
    module_->getOrInsertFunction("liva_sqlite_finalize", sqliteFinTy);

    // === Stdlib: PostgreSQL ===
    // pgNormalizeParams(sql) -> char*
    auto *pgNormParamsTy = llvm::FunctionType::get(i8PtrTy, {i8PtrTy}, false);
    module_->getOrInsertFunction("liva_pg_normalize_params", pgNormParamsTy);

    // pgConnect(conninfo) -> i64
    module_->getOrInsertFunction("liva_pg_connect",
        llvm::FunctionType::get(i64Ty, {i8PtrTy}, false));
    // pgClose(handle) -> void
    module_->getOrInsertFunction("liva_pg_close",
        llvm::FunctionType::get(builder_->getVoidTy(), {i64Ty}, false));
    // pgExec(handle, sql) -> i32
    module_->getOrInsertFunction("liva_pg_exec",
        llvm::FunctionType::get(i32Ty, {i64Ty, i8PtrTy}, false));
    // pgErrmsg(handle) -> i8*
    module_->getOrInsertFunction("liva_pg_errmsg",
        llvm::FunctionType::get(i8PtrTy, {i64Ty}, false));

    // pgQuery(handle, sql) -> i64 (PGresult* handle)
    module_->getOrInsertFunction("liva_pg_query",
        llvm::FunctionType::get(i64Ty, {i64Ty, i8PtrTy}, false));
    // pgClear(result) -> void
    module_->getOrInsertFunction("liva_pg_clear",
        llvm::FunctionType::get(builder_->getVoidTy(), {i64Ty}, false));
    // pgNtuples(result) -> i32
    module_->getOrInsertFunction("liva_pg_ntuples",
        llvm::FunctionType::get(i32Ty, {i64Ty}, false));
    // pgNfields(result) -> i32
    module_->getOrInsertFunction("liva_pg_nfields",
        llvm::FunctionType::get(i32Ty, {i64Ty}, false));
    // pgGetvalue(result, row, col) -> i8*
    module_->getOrInsertFunction("liva_pg_getvalue",
        llvm::FunctionType::get(i8PtrTy, {i64Ty, i32Ty, i32Ty}, false));
    // pgGetisnull(result, row, col) -> i32
    module_->getOrInsertFunction("liva_pg_getisnull",
        llvm::FunctionType::get(i32Ty, {i64Ty, i32Ty, i32Ty}, false));
    // pgFname(result, col) -> i8*
    module_->getOrInsertFunction("liva_pg_fname",
        llvm::FunctionType::get(i8PtrTy, {i64Ty, i32Ty}, false));
    // pgQueryParams(handle, sql, values, nparams) -> i64
    module_->getOrInsertFunction("liva_pg_query_params",
        llvm::FunctionType::get(i64Ty, {i64Ty, i8PtrTy, i8PtrTy, i64Ty}, false));

    // === File I/O: seek/tell/size ===
    // liva_file_seek(fp, offset, whence) -> i32
    auto *fileSeekTy = llvm::FunctionType::get(i32Ty, {i8PtrTy, i64Ty, i32Ty}, false);
    module_->getOrInsertFunction("liva_file_seek", fileSeekTy);

    // liva_file_tell(fp) -> i64
    auto *fileTellTy = llvm::FunctionType::get(i64Ty, {i8PtrTy}, false);
    module_->getOrInsertFunction("liva_file_tell", fileTellTy);

    // liva_file_size(fp) -> i64
    module_->getOrInsertFunction("liva_file_size", fileTellTy);

    // === Directory operations ===
    // liva_dir_list(path, &count) -> char**
    auto *dirListTy = llvm::FunctionType::get(i8PtrTy, {i8PtrTy, i8PtrTy}, false);
    module_->getOrInsertFunction("liva_dir_list", dirListTy);

    // liva_dir_create(path) -> i8
    auto *dirBoolTy = llvm::FunctionType::get(i8Ty, {i8PtrTy}, false);
    module_->getOrInsertFunction("liva_dir_create", dirBoolTy);
    module_->getOrInsertFunction("liva_dir_remove", dirBoolTy);
    module_->getOrInsertFunction("liva_dir_exists", dirBoolTy);

    // === Path operations ===
    // liva_path_join(a, b) -> char*
    auto *pathJoinTy = llvm::FunctionType::get(i8PtrTy, {i8PtrTy, i8PtrTy}, false);
    module_->getOrInsertFunction("liva_path_join", pathJoinTy);

    // liva_path_dirname(path) -> char*
    module_->getOrInsertFunction("liva_path_dirname", strNoArgTy);
    module_->getOrInsertFunction("liva_path_basename", strNoArgTy);
    module_->getOrInsertFunction("liva_path_extension", strNoArgTy);

    // liva_path_exists(path) -> i8
    module_->getOrInsertFunction("liva_path_exists", dirBoolTy);

    // liva_file_is_file(path) -> i8
    module_->getOrInsertFunction("liva_file_is_file", dirBoolTy);

    // liva_path_is_dir(path) -> i8
    module_->getOrInsertFunction("liva_path_is_dir", dirBoolTy);

    // liva_path_size(path) -> i64
    auto *pathI64Ty = llvm::FunctionType::get(i64Ty, {i8PtrTy}, false);
    module_->getOrInsertFunction("liva_path_size", pathI64Ty);

    // liva_path_modified_time(path) -> i64
    module_->getOrInsertFunction("liva_path_modified_time", pathI64Ty);

    // liva_file_read(path) -> char* (nullable)
    module_->getOrInsertFunction("liva_file_read", strNoArgTy);
    // liva_file_write_path(path, content) -> i8
    module_->getOrInsertFunction("liva_file_write_path", strBoolTy);
    // liva_file_append(path, content) -> i8
    module_->getOrInsertFunction("liva_file_append", strBoolTy);
    // liva_file_remove(path) -> i8
    module_->getOrInsertFunction("liva_file_remove", dirBoolTy);
    // liva_file_copy(src, dst) -> i8
    module_->getOrInsertFunction("liva_file_copy", strBoolTy);
    // liva_path_absolute(path) -> char*
    module_->getOrInsertFunction("liva_path_absolute", strNoArgTy);

    // liva_str_array_free(arr, count) -> void
    auto *strArrayFreeTy = llvm::FunctionType::get(builder_->getVoidTy(), {i8PtrTy, i64Ty}, false);
    module_->getOrInsertFunction("liva_str_array_free", strArrayFreeTy);

    // === Subprocess ===
    // liva_exec(command) -> i32
    auto *execTy = llvm::FunctionType::get(i32Ty, {i8PtrTy}, false);
    module_->getOrInsertFunction("liva_exec", execTy);

    // liva_exec_output(command) -> char*
    module_->getOrInsertFunction("liva_exec_output", strNoArgTy);

    // liva_process_start(command) -> i64
    auto *procStartTy = llvm::FunctionType::get(i64Ty, {i8PtrTy}, false);
    module_->getOrInsertFunction("liva_process_start", procStartTy);

    // liva_process_wait(handle) -> i32
    auto *procWaitTy = llvm::FunctionType::get(i32Ty, {i64Ty}, false);
    module_->getOrInsertFunction("liva_process_wait", procWaitTy);

    // liva_process_kill(handle) -> i8
    auto *procKillTy = llvm::FunctionType::get(i8Ty, {i64Ty}, false);
    module_->getOrInsertFunction("liva_process_kill", procKillTy);

    // liva_process_read(handle) -> char*
    auto *procReadTy = llvm::FunctionType::get(i8PtrTy, {i64Ty}, false);
    module_->getOrInsertFunction("liva_process_read", procReadTy);

    // liva_process_close(handle) -> void
    auto *procCloseTy = llvm::FunctionType::get(builder_->getVoidTy(), {i64Ty}, false);
    module_->getOrInsertFunction("liva_process_close", procCloseTy);

    // === JSON DOM (parse-tree) natives ===
    // liva_json_parse(s) -> i64
    module_->getOrInsertFunction("liva_json_parse", llvm::FunctionType::get(i64Ty, {i8PtrTy}, false));
    // liva_json_free_doc(docH) -> void
    module_->getOrInsertFunction("liva_json_free_doc", llvm::FunctionType::get(builder_->getVoidTy(), {i64Ty}, false));
    // liva_json_root(docH) -> i64
    module_->getOrInsertFunction("liva_json_root", llvm::FunctionType::get(i64Ty, {i64Ty}, false));
    // liva_json_node_kind(nodeH) -> i32
    module_->getOrInsertFunction("liva_json_node_kind", llvm::FunctionType::get(i32Ty, {i64Ty}, false));
    // liva_json_node_as_int(nodeH) -> i64
    module_->getOrInsertFunction("liva_json_node_as_int", llvm::FunctionType::get(i64Ty, {i64Ty}, false));
    // liva_json_node_as_float(nodeH) -> f64
    module_->getOrInsertFunction("liva_json_node_as_float", llvm::FunctionType::get(f64Ty, {i64Ty}, false));
    // liva_json_node_as_bool(nodeH) -> i8
    module_->getOrInsertFunction("liva_json_node_as_bool", llvm::FunctionType::get(i8Ty, {i64Ty}, false));
    // liva_json_node_as_string(nodeH) -> char*
    module_->getOrInsertFunction("liva_json_node_as_string", llvm::FunctionType::get(i8PtrTy, {i64Ty}, false));
    // liva_json_to_string(nodeH) -> char*
    module_->getOrInsertFunction("liva_json_to_string", llvm::FunctionType::get(i8PtrTy, {i64Ty}, false));
    // liva_json_to_string_pretty(nodeH, indent) -> char*
    module_->getOrInsertFunction("liva_json_to_string_pretty", llvm::FunctionType::get(i8PtrTy, {i64Ty, i32Ty}, false));
    // liva_json_obj_get(nodeH, key) -> i64
    module_->getOrInsertFunction("liva_json_obj_get", llvm::FunctionType::get(i64Ty, {i64Ty, i8PtrTy}, false));
    // liva_json_obj_has(nodeH, key) -> i8
    module_->getOrInsertFunction("liva_json_obj_has", llvm::FunctionType::get(i8Ty, {i64Ty, i8PtrTy}, false));
    // liva_json_obj_count(nodeH) -> i32
    module_->getOrInsertFunction("liva_json_obj_count", llvm::FunctionType::get(i32Ty, {i64Ty}, false));
    // liva_json_arr_count(nodeH) -> i32
    module_->getOrInsertFunction("liva_json_arr_count", llvm::FunctionType::get(i32Ty, {i64Ty}, false));
    // liva_json_arr_at(nodeH, idx) -> i64
    module_->getOrInsertFunction("liva_json_arr_at", llvm::FunctionType::get(i64Ty, {i64Ty, i64Ty}, false));
    // liva_json_obj_keys(nodeH, &count) -> char**
    module_->getOrInsertFunction("liva_json_obj_keys", llvm::FunctionType::get(i8PtrTy, {i64Ty, i8PtrTy}, false));

    // === JSON DOM Building / Mutation ===
    module_->getOrInsertFunction("liva_json_new_object", llvm::FunctionType::get(i64Ty, {}, false));
    module_->getOrInsertFunction("liva_json_new_array", llvm::FunctionType::get(i64Ty, {}, false));
    module_->getOrInsertFunction("liva_json_obj_set_string", llvm::FunctionType::get(builder_->getVoidTy(), {i64Ty, i64Ty, i8PtrTy, i8PtrTy}, false));
    module_->getOrInsertFunction("liva_json_obj_set_int", llvm::FunctionType::get(builder_->getVoidTy(), {i64Ty, i64Ty, i8PtrTy, i64Ty}, false));
    module_->getOrInsertFunction("liva_json_obj_set_float", llvm::FunctionType::get(builder_->getVoidTy(), {i64Ty, i64Ty, i8PtrTy, f64Ty}, false));
    module_->getOrInsertFunction("liva_json_obj_set_bool", llvm::FunctionType::get(builder_->getVoidTy(), {i64Ty, i64Ty, i8PtrTy, i8Ty}, false));
    module_->getOrInsertFunction("liva_json_obj_set_null", llvm::FunctionType::get(builder_->getVoidTy(), {i64Ty, i64Ty, i8PtrTy}, false));
    module_->getOrInsertFunction("liva_json_obj_set_object", llvm::FunctionType::get(i64Ty, {i64Ty, i64Ty, i8PtrTy}, false));
    module_->getOrInsertFunction("liva_json_obj_set_array", llvm::FunctionType::get(i64Ty, {i64Ty, i64Ty, i8PtrTy}, false));
    module_->getOrInsertFunction("liva_json_obj_remove", llvm::FunctionType::get(builder_->getVoidTy(), {i64Ty, i8PtrTy}, false));
    module_->getOrInsertFunction("liva_json_arr_add_string", llvm::FunctionType::get(builder_->getVoidTy(), {i64Ty, i64Ty, i8PtrTy}, false));
    module_->getOrInsertFunction("liva_json_arr_add_int", llvm::FunctionType::get(builder_->getVoidTy(), {i64Ty, i64Ty, i64Ty}, false));
    module_->getOrInsertFunction("liva_json_arr_add_float", llvm::FunctionType::get(builder_->getVoidTy(), {i64Ty, i64Ty, f64Ty}, false));
    module_->getOrInsertFunction("liva_json_arr_add_bool", llvm::FunctionType::get(builder_->getVoidTy(), {i64Ty, i64Ty, i8Ty}, false));
    module_->getOrInsertFunction("liva_json_arr_add_null", llvm::FunctionType::get(builder_->getVoidTy(), {i64Ty, i64Ty}, false));
    module_->getOrInsertFunction("liva_json_arr_add_object", llvm::FunctionType::get(i64Ty, {i64Ty, i64Ty}, false));
    module_->getOrInsertFunction("liva_json_arr_add_array", llvm::FunctionType::get(i64Ty, {i64Ty, i64Ty}, false));
    // liva_json_path_get(nodeH, path) -> i64
    module_->getOrInsertFunction("liva_json_path_get", llvm::FunctionType::get(i64Ty, {i64Ty, i8PtrTy}, false));
    // liva_json_path_set_*(docH, nodeH, path, val) -> void
    module_->getOrInsertFunction("liva_json_path_set_string", llvm::FunctionType::get(builder_->getVoidTy(), {i64Ty, i64Ty, i8PtrTy, i8PtrTy}, false));
    module_->getOrInsertFunction("liva_json_path_set_int", llvm::FunctionType::get(builder_->getVoidTy(), {i64Ty, i64Ty, i8PtrTy, i64Ty}, false));
    module_->getOrInsertFunction("liva_json_path_set_float", llvm::FunctionType::get(builder_->getVoidTy(), {i64Ty, i64Ty, i8PtrTy, f64Ty}, false));
    module_->getOrInsertFunction("liva_json_path_set_bool", llvm::FunctionType::get(builder_->getVoidTy(), {i64Ty, i64Ty, i8PtrTy, i8Ty}, false));

    // === Logging ===
    auto *logMsgTy = llvm::FunctionType::get(builder_->getVoidTy(), {i8PtrTy}, false);
    module_->getOrInsertFunction("liva_log_debug", logMsgTy);
    module_->getOrInsertFunction("liva_log_info", logMsgTy);
    module_->getOrInsertFunction("liva_log_warn", logMsgTy);
    module_->getOrInsertFunction("liva_log_error", logMsgTy);
    auto *logSetLevelTy = llvm::FunctionType::get(builder_->getVoidTy(), {i32Ty}, false);
    module_->getOrInsertFunction("liva_log_set_level", logSetLevelTy);

    // === Testing ===
    auto *assertTy = llvm::FunctionType::get(builder_->getVoidTy(), {i8Ty}, false);
    module_->getOrInsertFunction("liva_assert", assertTy);
    auto *assertMsgTy = llvm::FunctionType::get(builder_->getVoidTy(), {i8Ty, i8PtrTy}, false);
    module_->getOrInsertFunction("liva_assert_msg", assertMsgTy);
    auto *assertEqTy = llvm::FunctionType::get(builder_->getVoidTy(), {i64Ty, i64Ty}, false);
    module_->getOrInsertFunction("liva_assert_eq", assertEqTy);
    auto *assertEqStrTy = llvm::FunctionType::get(builder_->getVoidTy(), {i8PtrTy, i8PtrTy}, false);
    module_->getOrInsertFunction("liva_assert_eq_str", assertEqStrTy);
    auto *assertEqFloatTy = llvm::FunctionType::get(builder_->getVoidTy(), {f64Ty, f64Ty}, false);
    module_->getOrInsertFunction("liva_assert_eq_float", assertEqFloatTy);

    // === Test Runner ===
    auto *testBeginTy = llvm::FunctionType::get(builder_->getVoidTy(), {}, false);
    module_->getOrInsertFunction("liva_test_begin", testBeginTy);

    // liva_test_run(name, fn_ptr) — fn_ptr is void(*)()
    auto *testFnPtrTy = llvm::PointerType::getUnqual(*context_);
    auto *testRunTy = llvm::FunctionType::get(builder_->getVoidTy(), {i8PtrTy, testFnPtrTy}, false);
    module_->getOrInsertFunction("liva_test_run", testRunTy);

    // liva_test_run_closure(name, fn_ptr, env_ptr) -> i8
    auto *testRunClosureTy = llvm::FunctionType::get(
        i8Ty, {i8PtrTy, testFnPtrTy, testFnPtrTy}, false);
    module_->getOrInsertFunction("liva_test_run_closure", testRunClosureTy);

    auto *testEndTy = llvm::FunctionType::get(i32Ty, {}, false);
    module_->getOrInsertFunction("liva_test_end", testEndTy);

    auto *testFailTy = llvm::FunctionType::get(builder_->getVoidTy(), {i8PtrTy}, false);
    module_->getOrInsertFunction("liva_test_fail", testFailTy);

    // === DateTime ===
    auto *dateNowTy = llvm::FunctionType::get(i8PtrTy, {}, false);
    module_->getOrInsertFunction("liva_date_now", dateNowTy);
    module_->getOrInsertFunction("liva_time_now", dateNowTy);
    module_->getOrInsertFunction("liva_datetime_now", dateNowTy);
    auto *dateFormatTy = llvm::FunctionType::get(i8PtrTy, {f64Ty, i8PtrTy}, false);
    module_->getOrInsertFunction("liva_date_format", dateFormatTy);
    auto *datePartTy = llvm::FunctionType::get(i32Ty, {f64Ty}, false);
    module_->getOrInsertFunction("liva_date_year", datePartTy);
    module_->getOrInsertFunction("liva_date_month", datePartTy);
    module_->getOrInsertFunction("liva_date_day", datePartTy);
    module_->getOrInsertFunction("liva_date_weekday", datePartTy);
    // liva_date_timestamp() -> f64
    auto *dateTimestampTy = llvm::FunctionType::get(f64Ty, {}, false);
    module_->getOrInsertFunction("liva_date_timestamp", dateTimestampTy);
    // liva_date_parse(str, fmt) -> f64
    auto *dateParseTy = llvm::FunctionType::get(f64Ty, {i8PtrTy, i8PtrTy}, false);
    module_->getOrInsertFunction("liva_date_parse", dateParseTy);
    // liva_date_add(ts, secs) -> f64
    auto *dateArithTy = llvm::FunctionType::get(f64Ty, {f64Ty, f64Ty}, false);
    module_->getOrInsertFunction("liva_date_add", dateArithTy);
    // liva_date_diff(ts1, ts2) -> f64
    module_->getOrInsertFunction("liva_date_diff", dateArithTy);
    // liva_date_hour/minute/second(ts) -> i32
    module_->getOrInsertFunction("liva_date_hour", datePartTy);
    module_->getOrInsertFunction("liva_date_minute", datePartTy);
    module_->getOrInsertFunction("liva_date_second", datePartTy);

    // === Encoding/Compression ===
    // base64Encode/hexEncode: (ptr) -> ptr (same as strNoArgTy)
    module_->getOrInsertFunction("liva_base64_encode", strNoArgTy);
    module_->getOrInsertFunction("liva_base64_decode", strNoArgTy);
    module_->getOrInsertFunction("liva_hex_encode", strNoArgTy);
    module_->getOrInsertFunction("liva_hex_decode", strNoArgTy);
    // urlEncode/urlDecode: (ptr) -> ptr
    module_->getOrInsertFunction("liva_url_encode", strNoArgTy);
    module_->getOrInsertFunction("liva_url_decode", strNoArgTy);
    // base64UrlEncode/base64UrlDecode: (ptr) -> ptr (RFC 4648 §5)
    module_->getOrInsertFunction("liva_base64_url_encode", strNoArgTy);
    module_->getOrInsertFunction("liva_base64_url_decode", strNoArgTy);
    // jwtHS256Sig/jwtHS512Sig: (ptr, ptr) -> ptr (HMAC + base64url, binary-safe)
    module_->getOrInsertFunction("liva_jwt_hs256_sig", concatTy);
    module_->getOrInsertFunction("liva_jwt_hs512_sig", concatTy);
    // constTimeEq(a, b) -> i8 (1 if equal, 0 otherwise)
    auto *constEqTy = llvm::FunctionType::get(builder_->getInt8Ty(),
        {i8PtrTy, i8PtrTy}, false);
    module_->getOrInsertFunction("liva_const_time_eq", constEqTy);
    // arrayClone(data, count, elem_size) -> ptr  (deep-copy buffer)
    auto *arrayCloneTy = llvm::FunctionType::get(i8PtrTy,
        {i8PtrTy, builder_->getInt64Ty(), builder_->getInt64Ty()}, false);
    module_->getOrInsertFunction("liva_array_clone", arrayCloneTy);

    // === Bytes <-> String / hex / base64url with explicit byte length ===
    // strToBytes(s, *out_len) -> ptr
    auto *strToBytesTy = llvm::FunctionType::get(i8PtrTy,
        {i8PtrTy, llvm::PointerType::getUnqual(*context_)}, false);
    module_->getOrInsertFunction("liva_str_to_bytes", strToBytesTy);
    // bytesToStr(data, len) -> ptr
    auto *bytesToStrTy = llvm::FunctionType::get(i8PtrTy,
        {i8PtrTy, builder_->getInt64Ty()}, false);
    module_->getOrInsertFunction("liva_bytes_to_str", bytesToStrTy);
    // hexDecodeBytes(s, *out_len, *ok) -> ptr
    auto *hexDecBytesTy = llvm::FunctionType::get(i8PtrTy,
        {i8PtrTy, llvm::PointerType::getUnqual(*context_),
         llvm::PointerType::getUnqual(*context_)}, false);
    module_->getOrInsertFunction("liva_hex_decode_bytes", hexDecBytesTy);
    module_->getOrInsertFunction("liva_base64_url_decode_bytes", hexDecBytesTy);
    // hexEncodeBytes(data, len) -> ptr
    module_->getOrInsertFunction("liva_hex_encode_bytes", bytesToStrTy);
    module_->getOrInsertFunction("liva_base64_url_encode_bytes", bytesToStrTy);
    // gzipEncodeBytes(data, len, *out_len) -> ptr
    auto *gzipEncTy = llvm::FunctionType::get(i8PtrTy,
        {i8PtrTy, builder_->getInt64Ty(),
         llvm::PointerType::getUnqual(*context_)}, false);
    module_->getOrInsertFunction("liva_gzip_encode_bytes", gzipEncTy);
    // gzipDecodeBytes(data, len, *out_len, *ok) -> ptr
    auto *gzipDecTy = llvm::FunctionType::get(i8PtrTy,
        {i8PtrTy, builder_->getInt64Ty(),
         llvm::PointerType::getUnqual(*context_),
         llvm::PointerType::getUnqual(*context_)}, false);
    module_->getOrInsertFunction("liva_gzip_decode_bytes", gzipDecTy);
    // isoFormatUtc(ts: f64) -> ptr; isoParse(str, *ok) -> f64
    auto *isoFmtTy = llvm::FunctionType::get(i8PtrTy, {builder_->getDoubleTy()}, false);
    module_->getOrInsertFunction("liva_iso_format_utc", isoFmtTy);
    auto *isoParseTy = llvm::FunctionType::get(builder_->getDoubleTy(),
        {i8PtrTy, i8PtrTy}, false);
    module_->getOrInsertFunction("liva_iso_parse", isoParseTy);
    // crc32: (ptr) -> i64
    module_->getOrInsertFunction("liva_crc32", lenTy); // (ptr) -> i64

    // === Crypto ===
    // sha256(data) -> hex string, md5(data) -> hex string
    module_->getOrInsertFunction("liva_sha256", strNoArgTy);  // (ptr) -> ptr
    module_->getOrInsertFunction("liva_md5", strNoArgTy);     // (ptr) -> ptr
    // hmacSha256(key, data) -> hex string
    module_->getOrInsertFunction("liva_hmac_sha256", concatTy); // (ptr, ptr) -> ptr
    // sha1(data), sha512(data) -> hex string
    module_->getOrInsertFunction("liva_sha1", strNoArgTy);    // (ptr) -> ptr
    module_->getOrInsertFunction("liva_sha512", strNoArgTy);  // (ptr) -> ptr
    // hmacSha1(key, data), hmacSha512(key, data) -> hex string
    module_->getOrInsertFunction("liva_hmac_sha1", concatTy);   // (ptr, ptr) -> ptr
    module_->getOrInsertFunction("liva_hmac_sha512", concatTy); // (ptr, ptr) -> ptr

    // === Stdlib: Synchronization ===

    // mutexCreate() -> i64
    auto *noArgI64Ty = llvm::FunctionType::get(i64Ty, {}, false);
    module_->getOrInsertFunction("liva_mutex_create", noArgI64Ty);

    // mutexLock(handle), mutexUnlock(handle), mutexFree(handle) -> void
    auto *voidI64Ty = llvm::FunctionType::get(voidTy, {i64Ty}, false);
    module_->getOrInsertFunction("liva_mutex_lock", voidI64Ty);
    module_->getOrInsertFunction("liva_mutex_unlock", voidI64Ty);
    module_->getOrInsertFunction("liva_mutex_free", voidI64Ty);

    // mutexTryLock(handle) -> i8
    auto *i8FromI64Ty = llvm::FunctionType::get(i8Ty, {i64Ty}, false);
    module_->getOrInsertFunction("liva_mutex_try_lock", i8FromI64Ty);

    // atomicCreate(initial) -> i64
    auto *i64FromI64Ty = llvm::FunctionType::get(i64Ty, {i64Ty}, false);
    module_->getOrInsertFunction("liva_atomic_create", i64FromI64Ty);

    // atomicLoad(handle) -> i64
    module_->getOrInsertFunction("liva_atomic_load", i64FromI64Ty);

    // atomicStore(handle, value) -> void
    auto *voidI64I64Ty = llvm::FunctionType::get(voidTy, {i64Ty, i64Ty}, false);
    module_->getOrInsertFunction("liva_atomic_store", voidI64I64Ty);

    // atomicAdd(handle, value) -> i64, atomicSub(handle, value) -> i64
    auto *i64FromI64I64Ty = llvm::FunctionType::get(i64Ty, {i64Ty, i64Ty}, false);
    module_->getOrInsertFunction("liva_atomic_add", i64FromI64I64Ty);
    module_->getOrInsertFunction("liva_atomic_sub", i64FromI64I64Ty);

    // atomicCas(handle, expected, desired) -> i8
    auto *casArgTy = llvm::FunctionType::get(i8Ty, {i64Ty, i64Ty, i64Ty}, false);
    module_->getOrInsertFunction("liva_atomic_cas", casArgTy);

    // atomicFree(handle) -> void
    module_->getOrInsertFunction("liva_atomic_free", voidI64Ty);

    // === Stdlib: RWLock ===

    // rwlockCreate() -> i64
    module_->getOrInsertFunction("liva_rwlock_create", noArgI64Ty);

    // rwlock*Lock/*Unlock/Free(handle) -> void
    module_->getOrInsertFunction("liva_rwlock_read_lock", voidI64Ty);
    module_->getOrInsertFunction("liva_rwlock_read_unlock", voidI64Ty);
    module_->getOrInsertFunction("liva_rwlock_write_lock", voidI64Ty);
    module_->getOrInsertFunction("liva_rwlock_write_unlock", voidI64Ty);
    module_->getOrInsertFunction("liva_rwlock_free", voidI64Ty);

    // rwlockTry*Lock(handle) -> i8
    module_->getOrInsertFunction("liva_rwlock_try_read_lock", i8FromI64Ty);
    module_->getOrInsertFunction("liva_rwlock_try_write_lock", i8FromI64Ty);

    // === Stdlib: ConditionVariable ===

    // condVarCreate() -> i64
    module_->getOrInsertFunction("liva_condvar_create", noArgI64Ty);

    // condVarWait(cv, mtx) -> void
    module_->getOrInsertFunction("liva_condvar_wait", voidI64I64Ty);

    // condVarNotifyOne/All/Free(handle) -> void
    module_->getOrInsertFunction("liva_condvar_notify_one", voidI64Ty);
    module_->getOrInsertFunction("liva_condvar_notify_all", voidI64Ty);
    module_->getOrInsertFunction("liva_condvar_free", voidI64Ty);

    // === Stdlib: Channel ===

    // channelCreate(capacity) -> i64
    module_->getOrInsertFunction("liva_channel_create", i64FromI64Ty);

    // channelSend(handle, value) -> void
    module_->getOrInsertFunction("liva_channel_send", voidI64I64Ty);

    // channelReceive(handle, &ok) -> i64
    auto *chRecvTy = llvm::FunctionType::get(i64Ty, {i64Ty, i8PtrTy}, false);
    module_->getOrInsertFunction("liva_channel_receive", chRecvTy);

    // channelClose(handle) -> void
    module_->getOrInsertFunction("liva_channel_close", voidI64Ty);

    // channelLen(handle) -> i64
    module_->getOrInsertFunction("liva_channel_len", i64FromI64Ty);

    // channelFree(handle) -> void
    module_->getOrInsertFunction("liva_channel_free", voidI64Ty);

    // channelTrySend(handle, value) -> i8
    auto *chTrySendTy = llvm::FunctionType::get(i8Ty, {i64Ty, i64Ty}, false);
    module_->getOrInsertFunction("liva_channel_try_send", chTrySendTy);

    // channelTryReceive(handle, &ok) -> i64
    module_->getOrInsertFunction("liva_channel_try_receive", chRecvTy);

    // === Stdlib: TOML ===

    // tomlParse(text) -> i64
    auto *tomlParseTy = llvm::FunctionType::get(i64Ty, {i8PtrTy}, false);
    module_->getOrInsertFunction("liva_toml_parse", tomlParseTy);

    // tomlGetString(handle, section, key) -> char*
    auto *tomlGetStrTy = llvm::FunctionType::get(i8PtrTy, {i64Ty, i8PtrTy, i8PtrTy}, false);
    module_->getOrInsertFunction("liva_toml_get_string", tomlGetStrTy);

    // tomlGetInt(handle, section, key, &ok) -> i64
    auto *tomlGetIntTy = llvm::FunctionType::get(i64Ty, {i64Ty, i8PtrTy, i8PtrTy, i8PtrTy}, false);
    module_->getOrInsertFunction("liva_toml_get_int", tomlGetIntTy);

    // tomlGetBool(handle, section, key, &ok) -> i8
    auto *tomlGetBoolTy = llvm::FunctionType::get(i8Ty, {i64Ty, i8PtrTy, i8PtrTy, i8PtrTy}, false);
    module_->getOrInsertFunction("liva_toml_get_bool", tomlGetBoolTy);

    // tomlHasKey(handle, section, key) -> i8
    auto *tomlHasKeyTy = llvm::FunctionType::get(i8Ty, {i64Ty, i8PtrTy, i8PtrTy}, false);
    module_->getOrInsertFunction("liva_toml_has_key", tomlHasKeyTy);

    // tomlFree(handle) -> void
    module_->getOrInsertFunction("liva_toml_free", voidI64Ty);

    // === Stdlib: TaskGroup ===

    // taskGroupCreate() -> i64
    module_->getOrInsertFunction("liva_task_group_create", noArgI64Ty);

    // taskGroupSpawn(group, task*) -> void
    auto *tgSpawnTy = llvm::FunctionType::get(voidTy, {i64Ty, i8PtrTy}, false);
    module_->getOrInsertFunction("liva_task_group_spawn", tgSpawnTy);

    // taskGroupAwaitAll(group) -> void
    module_->getOrInsertFunction("liva_task_group_await_all", voidI64Ty);

    // taskGroupCancelAll(group) -> void
    module_->getOrInsertFunction("liva_task_group_cancel_all", voidI64Ty);

    // taskGroupCount(group) -> i64
    module_->getOrInsertFunction("liva_task_group_count", i64FromI64Ty);

    // taskGroupFree(group) -> void
    module_->getOrInsertFunction("liva_task_group_free", voidI64Ty);

    // === Stdlib: String utility functions ===

    // liva_str_repeat(ptr, i64) -> ptr
    auto *strRepeatTy = llvm::FunctionType::get(i8PtrTy, {i8PtrTy, i64Ty}, false);
    module_->getOrInsertFunction("liva_str_repeat", strRepeatTy);

    // liva_str_pad_left(ptr, i64, ptr) -> ptr, liva_str_pad_right(ptr, i64, ptr) -> ptr
    auto *strPadTy = llvm::FunctionType::get(i8PtrTy, {i8PtrTy, i64Ty, i8PtrTy}, false);
    module_->getOrInsertFunction("liva_str_pad_left", strPadTy);
    module_->getOrInsertFunction("liva_str_pad_right", strPadTy);

    // liva_str_join(ptr*, i64, ptr) -> ptr
    auto *strJoinTy = llvm::FunctionType::get(i8PtrTy, {i8PtrTy, i64Ty, i8PtrTy}, false);
    module_->getOrInsertFunction("liva_str_join", strJoinTy);

    // liva_str_trim_left(ptr) -> ptr, liva_str_trim_right(ptr) -> ptr
    // liva_str_reverse(ptr) -> ptr — reuse strNoArgTy: (ptr) -> ptr
    module_->getOrInsertFunction("liva_str_trim_left", strNoArgTy);
    module_->getOrInsertFunction("liva_str_trim_right", strNoArgTy);
    module_->getOrInsertFunction("liva_str_reverse", strNoArgTy);

    // liva_str_chars(ptr, ptr) -> ptr, liva_str_lines(ptr, ptr) -> ptr
    auto *strCharsTy = llvm::FunctionType::get(i8PtrTy, {i8PtrTy, i8PtrTy}, false);
    module_->getOrInsertFunction("liva_str_chars", strCharsTy);
    module_->getOrInsertFunction("liva_str_lines", strCharsTy);

    // === Stdlib: UTF-8 helpers ===

    // liva_str_char_count(ptr) -> i64
    auto *strCountTy = llvm::FunctionType::get(i64Ty, {i8PtrTy}, false);
    module_->getOrInsertFunction("liva_str_char_count", strCountTy);

    // liva_str_codepoint_at(ptr, i64) -> i32
    auto *strCpAtTy = llvm::FunctionType::get(i32Ty, {i8PtrTy, i64Ty}, false);
    module_->getOrInsertFunction("liva_str_codepoint_at", strCpAtTy);

    // liva_str_is_ascii(ptr) -> i8
    auto *strIsAsciiTy = llvm::FunctionType::get(i8Ty, {i8PtrTy}, false);
    module_->getOrInsertFunction("liva_str_is_ascii", strIsAsciiTy);

    // char predicates: (i32) -> i8
    auto *charPredTy = llvm::FunctionType::get(i8Ty, {i32Ty}, false);
    module_->getOrInsertFunction("liva_char_is_alpha", charPredTy);
    module_->getOrInsertFunction("liva_char_is_digit", charPredTy);
    module_->getOrInsertFunction("liva_char_is_alnum", charPredTy);
    module_->getOrInsertFunction("liva_char_is_space", charPredTy);
    module_->getOrInsertFunction("liva_char_is_upper", charPredTy);
    module_->getOrInsertFunction("liva_char_is_lower", charPredTy);

    // case conversion: (i32) -> i32
    auto *charCaseTy = llvm::FunctionType::get(i32Ty, {i32Ty}, false);
    module_->getOrInsertFunction("liva_char_to_upper", charCaseTy);
    module_->getOrInsertFunction("liva_char_to_lower", charCaseTy);

    // === Stdlib: Collection utility functions ===

    auto *voidTy2 = builder_->getVoidTy();

    // liva_array_reversed(ptr, i64, i64, ptr, ptr, ptr) -> void
    auto *arrReversedTy = llvm::FunctionType::get(voidTy2,
        {i8PtrTy, i64Ty, i64Ty, i8PtrTy, i8PtrTy, i8PtrTy}, false);
    module_->getOrInsertFunction("liva_array_reversed", arrReversedTy);

    // liva_array_sorted(ptr, i64, i64, ptr, ptr, ptr, ptr) -> void
    auto *arrSortedTy = llvm::FunctionType::get(voidTy2,
        {i8PtrTy, i64Ty, i64Ty, i8PtrTy, i8PtrTy, i8PtrTy, i8PtrTy}, false);
    module_->getOrInsertFunction("liva_array_sorted", arrSortedTy);

    // liva_array_any(ptr, i64, i64, ptr) -> i8
    auto *arrPredTy = llvm::FunctionType::get(i8Ty,
        {i8PtrTy, i64Ty, i64Ty, i8PtrTy}, false);
    module_->getOrInsertFunction("liva_array_any", arrPredTy);
    module_->getOrInsertFunction("liva_array_all", arrPredTy);

    // liva_array_count(ptr, i64, i64, ptr) -> i64
    auto *arrCountTy = llvm::FunctionType::get(i64Ty,
        {i8PtrTy, i64Ty, i64Ty, i8PtrTy}, false);
    module_->getOrInsertFunction("liva_array_count", arrCountTy);

    // === Stdlib: UI (wxWidgets wrapper) ===

    auto *ptrTy = llvm::PointerType::getUnqual(*context_);

    // () -> void
    auto *uiNoArgVoidTy = llvm::FunctionType::get(voidTy, {}, false);
    module_->getOrInsertFunction("liva_ui_app_init", uiNoArgVoidTy);
    module_->getOrInsertFunction("liva_ui_app_run", uiNoArgVoidTy);
    module_->getOrInsertFunction("liva_ui_app_quit", uiNoArgVoidTy);

    // () -> i32
    auto *uiNoArgI32Ty = llvm::FunctionType::get(i32Ty, {}, false);

    // create_window(i32 w, i32 h, ptr title) -> i32
    auto *uiCreateWinTy = llvm::FunctionType::get(i32Ty, {i32Ty, i32Ty, i8PtrTy}, false);
    module_->getOrInsertFunction("liva_ui_create_window", uiCreateWinTy);

    // window_show(i32 handle, i32 show) -> void
    auto *uiHandleI32VoidTy = llvm::FunctionType::get(voidTy, {i32Ty, i32Ty}, false);
    module_->getOrInsertFunction("liva_ui_window_show", uiHandleI32VoidTy);

    // window_set_title(i32 handle, ptr title) -> void
    auto *uiHandleStrTy = llvm::FunctionType::get(voidTy, {i32Ty, i8PtrTy}, false);
    module_->getOrInsertFunction("liva_ui_window_set_title", uiHandleStrTy);

    // window_get_width/height(i32 handle) -> i32
    auto *uiHandleRetI32Ty = llvm::FunctionType::get(i32Ty, {i32Ty}, false);
    module_->getOrInsertFunction("liva_ui_window_get_width", uiHandleRetI32Ty);
    module_->getOrInsertFunction("liva_ui_window_get_height", uiHandleRetI32Ty);

    // Callback type: (i32 handle, ptr func, ptr env, i32 env_size) -> void.
    // env_size > 0 tells the runtime to heap-copy the (stack) env and free it
    // when the widget is destroyed; 0 keeps the caller-owned (stack) env.
    auto *uiCallbackTy = llvm::FunctionType::get(voidTy, {i32Ty, ptrTy, ptrTy, i32Ty}, false);
    module_->getOrInsertFunction("liva_ui_window_on_close", uiCallbackTy);
    module_->getOrInsertFunction("liva_ui_on_click", uiCallbackTy);
    module_->getOrInsertFunction("liva_ui_on_change", uiCallbackTy);
    module_->getOrInsertFunction("liva_ui_on_select", uiCallbackTy);
    module_->getOrInsertFunction("liva_ui_on_key", uiCallbackTy);
    module_->getOrInsertFunction("liva_ui_canvas_on_paint", uiCallbackTy);

    // ── Phase 2: menu / statusbar / toolbar ──────────────────────────
    auto *uiRetI32Ty        = llvm::FunctionType::get(i32Ty, {}, false);
    auto *uiStrRetI32Ty     = llvm::FunctionType::get(i32Ty, {i8PtrTy}, false);
    auto *uiI32StrRetI32Ty  = llvm::FunctionType::get(i32Ty, {i32Ty, i8PtrTy}, false);
    auto *uiI32I32RetI32Ty  = llvm::FunctionType::get(i32Ty, {i32Ty, i32Ty}, false);
    auto *uiI32RetI32Ty     = llvm::FunctionType::get(i32Ty, {i32Ty}, false);
    auto *uiI32VoidTy       = llvm::FunctionType::get(voidTy, {i32Ty}, false);
    auto *uiI32StrVoidTy    = llvm::FunctionType::get(voidTy, {i32Ty, i8PtrTy}, false);
    auto *uiI32StrI32VoidTy = llvm::FunctionType::get(voidTy, {i32Ty, i8PtrTy, i32Ty}, false);
    auto *uiI32I32StrVoidTy = llvm::FunctionType::get(voidTy, {i32Ty, i32Ty, i8PtrTy}, false);

    module_->getOrInsertFunction("liva_ui_create_menu_bar", uiRetI32Ty);
    module_->getOrInsertFunction("liva_ui_create_menu", uiStrRetI32Ty);
    module_->getOrInsertFunction("liva_ui_menu_add_item", uiI32StrRetI32Ty);
    module_->getOrInsertFunction("liva_ui_menu_add_check_item", uiI32StrRetI32Ty);
    module_->getOrInsertFunction("liva_ui_menu_add_separator", uiI32VoidTy);
    module_->getOrInsertFunction("liva_ui_menu_add_submenu", uiI32StrI32VoidTy);
    module_->getOrInsertFunction("liva_ui_menu_bar_add_menu", uiHandleI32VoidTy);
    module_->getOrInsertFunction("liva_ui_window_set_menu_bar", uiHandleI32VoidTy);
    module_->getOrInsertFunction("liva_ui_menu_item_set_enabled", uiHandleI32VoidTy);
    module_->getOrInsertFunction("liva_ui_menu_item_set_checked", uiHandleI32VoidTy);
    module_->getOrInsertFunction("liva_ui_menu_item_on_click", uiCallbackTy);
    module_->getOrInsertFunction("liva_ui_menu_popup", uiHandleI32VoidTy);
    module_->getOrInsertFunction("liva_ui_on_right_click", uiCallbackTy);
    module_->getOrInsertFunction("liva_ui_create_status_bar", uiI32I32RetI32Ty);
    module_->getOrInsertFunction("liva_ui_status_bar_set_text", uiI32I32StrVoidTy);
    module_->getOrInsertFunction("liva_ui_create_toolbar", uiI32RetI32Ty);
    module_->getOrInsertFunction("liva_ui_toolbar_add_tool", uiI32StrRetI32Ty);
    module_->getOrInsertFunction("liva_ui_toolbar_add_separator", uiI32VoidTy);
    module_->getOrInsertFunction("liva_ui_toolbar_realize", uiI32VoidTy);
    module_->getOrInsertFunction("liva_ui_tool_item_set_enabled", uiHandleI32VoidTy);
    module_->getOrInsertFunction("liva_ui_tool_item_on_click", uiCallbackTy);

    // create_widget(i32 parent) -> i32
    auto *uiCreateParentTy = llvm::FunctionType::get(i32Ty, {i32Ty}, false);
    module_->getOrInsertFunction("liva_ui_create_panel", uiCreateParentTy);
    module_->getOrInsertFunction("liva_ui_create_listbox", uiCreateParentTy);
    module_->getOrInsertFunction("liva_ui_create_tabview", uiCreateParentTy);
    module_->getOrInsertFunction("liva_ui_create_scrollview", uiCreateParentTy);
    module_->getOrInsertFunction("liva_ui_create_divider", uiCreateParentTy);
    module_->getOrInsertFunction("liva_ui_create_canvas", uiCreateParentTy);

    // create_widget(i32 parent, ptr text) -> i32
    auto *uiCreateParentStrTy = llvm::FunctionType::get(i32Ty, {i32Ty, i8PtrTy}, false);
    module_->getOrInsertFunction("liva_ui_create_button", uiCreateParentStrTy);
    module_->getOrInsertFunction("liva_ui_create_label", uiCreateParentStrTy);
    module_->getOrInsertFunction("liva_ui_create_textinput", uiCreateParentStrTy);
    module_->getOrInsertFunction("liva_ui_create_checkbox", uiCreateParentStrTy);
    module_->getOrInsertFunction("liva_ui_create_textarea", uiCreateParentStrTy);
    module_->getOrInsertFunction("liva_ui_create_radiogroup", uiCreateParentStrTy);
    module_->getOrInsertFunction("liva_ui_create_dropdown", uiCreateParentStrTy);
    module_->getOrInsertFunction("liva_ui_create_imageview", uiCreateParentStrTy);

    // create_slider(i32 parent, i32 min, i32 max, i32 val) -> i32
    auto *uiCreateSliderTy = llvm::FunctionType::get(i32Ty, {i32Ty, i32Ty, i32Ty, i32Ty}, false);
    module_->getOrInsertFunction("liva_ui_create_slider", uiCreateSliderTy);

    // create_progressbar(i32 parent, i32 range) -> i32
    auto *uiCreateProgTy = llvm::FunctionType::get(i32Ty, {i32Ty, i32Ty}, false);
    module_->getOrInsertFunction("liva_ui_create_progressbar", uiCreateProgTy);

    // set_text(i32 handle, ptr text) -> void
    module_->getOrInsertFunction("liva_ui_set_text", uiHandleStrTy);
    module_->getOrInsertFunction("liva_ui_set_tooltip", uiHandleStrTy);

    // get_text(i32 handle) -> ptr
    auto *uiGetTextTy = llvm::FunctionType::get(i8PtrTy, {i32Ty}, false);
    module_->getOrInsertFunction("liva_ui_get_text", uiGetTextTy);

    // set_value(i32 handle, i32 val) -> void
    auto *uiSetValTy = llvm::FunctionType::get(voidTy, {i32Ty, i32Ty}, false);
    module_->getOrInsertFunction("liva_ui_set_value", uiSetValTy);

    // get_value(i32 handle) -> i32
    module_->getOrInsertFunction("liva_ui_get_value", uiHandleRetI32Ty);

    // set_enabled/visible(i32 handle, i32 flag) -> void
    module_->getOrInsertFunction("liva_ui_set_enabled", uiHandleI32VoidTy);
    module_->getOrInsertFunction("liva_ui_set_visible", uiHandleI32VoidTy);

    // set_size(i32 handle, i32 w, i32 h) -> void
    auto *uiSetSizeTy = llvm::FunctionType::get(voidTy, {i32Ty, i32Ty, i32Ty}, false);
    module_->getOrInsertFunction("liva_ui_set_size", uiSetSizeTy);

    // set_bounds(i32 handle, i32 x, i32 y, i32 w, i32 h) -> void
    {
        llvm::Type *i32 = llvm::Type::getInt32Ty(*context_);
        auto *fnTy = llvm::FunctionType::get(llvm::Type::getVoidTy(*context_),
                                             {i32, i32, i32, i32, i32}, false);
        module_->getOrInsertFunction("liva_ui_set_bounds", fnTy);
    }

    // set_font(i32 handle, i32 size, i32 bold) -> void
    auto *uiSetFontTy = llvm::FunctionType::get(voidTy, {i32Ty, i32Ty, i32Ty}, false);
    module_->getOrInsertFunction("liva_ui_set_font", uiSetFontTy);

    // set_bg_color / set_fg_color(i32 handle, i32 r, i32 g, i32 b) -> void
    auto *uiColorTy = llvm::FunctionType::get(voidTy, {i32Ty, i32Ty, i32Ty, i32Ty}, false);
    module_->getOrInsertFunction("liva_ui_set_bg_color", uiColorTy);
    module_->getOrInsertFunction("liva_ui_set_fg_color", uiColorTy);

    // destroy_widget(i32 handle) -> void
    auto *uiHandleVoidTy = llvm::FunctionType::get(voidTy, {i32Ty}, false);
    module_->getOrInsertFunction("liva_ui_destroy_widget", uiHandleVoidTy);

    // Sizer creation: () -> i32
    module_->getOrInsertFunction("liva_ui_create_vbox_sizer", uiNoArgI32Ty);
    module_->getOrInsertFunction("liva_ui_create_hbox_sizer", uiNoArgI32Ty);

    // create_grid_sizer / create_flex_grid_sizer(rows, cols, hgap, vgap) -> i32
    auto *uiGridSizerTy = llvm::FunctionType::get(i32Ty, {i32Ty, i32Ty, i32Ty, i32Ty}, false);
    module_->getOrInsertFunction("liva_ui_create_grid_sizer", uiGridSizerTy);
    module_->getOrInsertFunction("liva_ui_create_flex_grid_sizer", uiGridSizerTy);

    // sizer_add(i32 sizer, i32 widget, i32 proportion, i32 flags, i32 border) -> void
    auto *uiSizerAddTy = llvm::FunctionType::get(voidTy,
        {i32Ty, i32Ty, i32Ty, i32Ty, i32Ty}, false);
    module_->getOrInsertFunction("liva_ui_sizer_add", uiSizerAddTy);

    // set_sizer(i32 parent, i32 sizer) -> void
    auto *uiSetSizerTy = llvm::FunctionType::get(voidTy, {i32Ty, i32Ty}, false);
    module_->getOrInsertFunction("liva_ui_set_sizer", uiSetSizerTy);

    // list_add_item(i32 handle, ptr item) -> void
    module_->getOrInsertFunction("liva_ui_list_add_item", uiHandleStrTy);

    // list_clear(i32 handle) -> void
    module_->getOrInsertFunction("liva_ui_list_clear", uiHandleVoidTy);

    // list_get_selection(i32 handle) -> i32
    module_->getOrInsertFunction("liva_ui_list_get_selection", uiHandleRetI32Ty);

    // tab_add_page(i32 tab, i32 page, ptr title) -> void
    auto *uiTabAddTy = llvm::FunctionType::get(voidTy, {i32Ty, i32Ty, i8PtrTy}, false);
    module_->getOrInsertFunction("liva_ui_tab_add_page", uiTabAddTy);

    // tab_get_selection(i32 handle) -> i32
    module_->getOrInsertFunction("liva_ui_tab_get_selection", uiHandleRetI32Ty);

    // message_box(ptr title, ptr message, i32 style) -> void
    auto *uiMsgBoxTy = llvm::FunctionType::get(voidTy, {i8PtrTy, i8PtrTy, i32Ty}, false);
    module_->getOrInsertFunction("liva_ui_message_box", uiMsgBoxTy);

    // file_dialog(i32 parent, ptr title, ptr wildcard, i32 style) -> ptr
    auto *uiFileDlgTy = llvm::FunctionType::get(i8PtrTy, {i32Ty, i8PtrTy, i8PtrTy, i32Ty}, false);
    module_->getOrInsertFunction("liva_ui_file_dialog", uiFileDlgTy);

    // color_dialog(i32 parent) -> i32
    module_->getOrInsertFunction("liva_ui_color_dialog", uiCreateParentTy);

    // create_timer(i32 intervalMs, ptr func, ptr env) -> i32
    auto *uiCreateTimerTy = llvm::FunctionType::get(i32Ty, {i32Ty, ptrTy, ptrTy}, false);
    module_->getOrInsertFunction("liva_ui_create_timer", uiCreateTimerTy);

    // stop_timer(i32 handle) -> void
    module_->getOrInsertFunction("liva_ui_stop_timer", uiHandleVoidTy);

    // get_clipboard_text() -> ptr
    auto *uiGetClipTy = llvm::FunctionType::get(i8PtrTy, {}, false);
    module_->getOrInsertFunction("liva_ui_get_clipboard_text", uiGetClipTy);

    // set_clipboard_text(ptr) -> void
    auto *uiSetClipTy = llvm::FunctionType::get(voidTy, {i8PtrTy}, false);
    module_->getOrInsertFunction("liva_ui_set_clipboard_text", uiSetClipTy);

    // canvas_refresh(i32 handle) -> void
    module_->getOrInsertFunction("liva_ui_canvas_refresh", uiHandleVoidTy);

    // dc_clear(i32 dc, i32 r, i32 g, i32 b) -> void
    module_->getOrInsertFunction("liva_ui_dc_clear", uiColorTy);

    // dc_draw_rect(i32 dc, i32 x, i32 y, i32 w, i32 h, i32 r, i32 g, i32 b) -> void
    auto *uiDcRect8Ty = llvm::FunctionType::get(voidTy,
        {i32Ty, i32Ty, i32Ty, i32Ty, i32Ty, i32Ty, i32Ty, i32Ty}, false);
    module_->getOrInsertFunction("liva_ui_dc_draw_rect", uiDcRect8Ty);

    // dc_draw_text(i32 dc, ptr text, i32 x, i32 y, i32 r, i32 g, i32 b) -> void
    auto *uiDcTextTy = llvm::FunctionType::get(voidTy,
        {i32Ty, i8PtrTy, i32Ty, i32Ty, i32Ty, i32Ty, i32Ty}, false);
    module_->getOrInsertFunction("liva_ui_dc_draw_text", uiDcTextTy);

    // dc_draw_line(i32 dc, i32 x1, i32 y1, i32 x2, i32 y2, i32 r, i32 g, i32 b) -> void
    module_->getOrInsertFunction("liva_ui_dc_draw_line", uiDcRect8Ty);

    // dc_draw_circle(i32 dc, i32 cx, i32 cy, i32 radius, i32 r, i32 g, i32 b) -> void
    auto *uiDcCircleTy = llvm::FunctionType::get(voidTy,
        {i32Ty, i32Ty, i32Ty, i32Ty, i32Ty, i32Ty, i32Ty}, false);
    module_->getOrInsertFunction("liva_ui_dc_draw_circle", uiDcCircleTy);

    // ── Phase 3: new widgets ─────────────────────────────────────────
    // create_spin_ctrl(i32 parent, i32 min, i32 max, i32 val) -> i32
    module_->getOrInsertFunction("liva_ui_create_spin_ctrl", uiCreateSliderTy);
    // create_date_picker(i32 parent) -> i32 ; date_get_value(i32) -> ptr
    module_->getOrInsertFunction("liva_ui_create_date_picker", uiCreateParentTy);
    module_->getOrInsertFunction("liva_ui_date_get_value", uiGetTextTy);
    // create_combo_box(i32, ptr) -> i32 ; combo_add_item(i32, ptr) -> void
    module_->getOrInsertFunction("liva_ui_create_combo_box", uiCreateParentStrTy);
    module_->getOrInsertFunction("liva_ui_combo_add_item", uiHandleStrTy);
    // TreeView: create(i32)->i32, add_root(i32,ptr)->i32,
    //           add_node(i32,i32,ptr)->i32, get_selection(i32)->i32
    auto *uiI32I32StrRetI32Ty =
        llvm::FunctionType::get(i32Ty, {i32Ty, i32Ty, i8PtrTy}, false);
    module_->getOrInsertFunction("liva_ui_create_tree_view", uiCreateParentTy);
    module_->getOrInsertFunction("liva_ui_tree_add_root", uiI32StrRetI32Ty);
    module_->getOrInsertFunction("liva_ui_tree_add_node", uiI32I32StrRetI32Ty);
    module_->getOrInsertFunction("liva_ui_tree_get_selection", uiHandleRetI32Ty);
    // DataGrid: create(i32,i32,i32)->i32, set_cell(i32,i32,i32,ptr)->void,
    //           get_cell(i32,i32,i32)->ptr, set_col_label(i32,i32,ptr)->void
    auto *uiI32I32I32RetI32Ty =
        llvm::FunctionType::get(i32Ty, {i32Ty, i32Ty, i32Ty}, false);
    auto *uiI32I32I32StrVoidTy =
        llvm::FunctionType::get(voidTy, {i32Ty, i32Ty, i32Ty, i8PtrTy}, false);
    auto *uiI32I32I32RetStrTy =
        llvm::FunctionType::get(i8PtrTy, {i32Ty, i32Ty, i32Ty}, false);
    module_->getOrInsertFunction("liva_ui_create_data_grid", uiI32I32I32RetI32Ty);
    module_->getOrInsertFunction("liva_ui_grid_set_cell", uiI32I32I32StrVoidTy);
    module_->getOrInsertFunction("liva_ui_grid_get_cell", uiI32I32I32RetStrTy);
    module_->getOrInsertFunction("liva_ui_grid_set_col_label", uiI32I32StrVoidTy);
    // Splitter: create(i32)->i32, split_v/h(i32,i32,i32)->void, set_sash(i32,i32)->void
    module_->getOrInsertFunction("liva_ui_create_splitter", uiCreateParentTy);
    module_->getOrInsertFunction("liva_ui_splitter_split_v", uiSetSizeTy);
    module_->getOrInsertFunction("liva_ui_splitter_split_h", uiSetSizeTy);
    module_->getOrInsertFunction("liva_ui_splitter_set_sash", uiSetValTy);

    // ── Phase 4: Align/Anchors layout ────────────────────────────────
    // set_align(i32 handle, i32 align) -> void
    module_->getOrInsertFunction("liva_ui_set_align", uiSetValTy);

    // set_anchors(i32 handle, i32 l, i32 t, i32 r, i32 b) -> void
    auto *uiI32x5VoidTy =
        llvm::FunctionType::get(voidTy, {i32Ty, i32Ty, i32Ty, i32Ty, i32Ty}, false);
    module_->getOrInsertFunction("liva_ui_set_anchors", uiI32x5VoidTy);

    // ── Phase 5: data binding ────────────────────────────────────────
    auto *uiI32StrStrVoidTy =
        llvm::FunctionType::get(voidTy, {i32Ty, i8PtrTy, i8PtrTy}, false);
    auto *uiI32StrRetStrTy =
        llvm::FunctionType::get(i8PtrTy, {i32Ty, i8PtrTy}, false);
    module_->getOrInsertFunction("liva_ui_model_create", uiRetI32Ty);
    module_->getOrInsertFunction("liva_ui_model_set_text", uiI32StrStrVoidTy);
    module_->getOrInsertFunction("liva_ui_model_get_text", uiI32StrRetStrTy);
    module_->getOrInsertFunction("liva_ui_model_bind_text", uiI32StrI32VoidTy);
    module_->getOrInsertFunction("liva_ui_model_set_int", uiI32StrI32VoidTy);
    module_->getOrInsertFunction("liva_ui_model_get_int", uiI32StrRetI32Ty);
    module_->getOrInsertFunction("liva_ui_model_bind_int", uiI32StrI32VoidTy);

    // Coroutine + async runtime
    declareCoroutineIntrinsics();
    declareAsyncRuntimeFuncs();
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

void IRGen::declareCoroutineIntrinsics() {
    auto *ptrTy = llvm::PointerType::getUnqual(*context_);
    auto *i64Ty = builder_->getInt64Ty();
    auto *voidTy = builder_->getVoidTy();

    // malloc/free for coroutine frame allocation
    auto *mallocTy = llvm::FunctionType::get(ptrTy, {i64Ty}, false);
    module_->getOrInsertFunction("malloc", mallocTy);

    auto *freeTy = llvm::FunctionType::get(voidTy, {ptrTy}, false);
    module_->getOrInsertFunction("free", freeTy);
}

void IRGen::declareAsyncRuntimeFuncs() {
    auto *ptrTy = llvm::PointerType::getUnqual(*context_);
    auto *i8Ty = builder_->getInt8Ty();
    auto *voidTy = builder_->getVoidTy();

    // LivaTask *liva_task_create(void *coro_handle)
    auto *createTy = llvm::FunctionType::get(ptrTy, {ptrTy}, false);
    module_->getOrInsertFunction("liva_task_create", createTy);

    // void liva_task_complete(LivaTask *task)
    auto *voidPtrTy = llvm::FunctionType::get(voidTy, {ptrTy}, false);
    module_->getOrInsertFunction("liva_task_complete", voidPtrTy);

    // int8_t liva_task_is_done(LivaTask *task)
    auto *isDoneTy = llvm::FunctionType::get(i8Ty, {ptrTy}, false);
    module_->getOrInsertFunction("liva_task_is_done", isDoneTy);

    // void *liva_task_get_handle(LivaTask *task)
    auto *getHandleTy = llvm::FunctionType::get(ptrTy, {ptrTy}, false);
    module_->getOrInsertFunction("liva_task_get_handle", getHandleTy);

    // void liva_task_set_parent(LivaTask *child, LivaTask *parent)
    auto *setParentTy = llvm::FunctionType::get(voidTy, {ptrTy, ptrTy}, false);
    module_->getOrInsertFunction("liva_task_set_parent", setParentTy);

    // void liva_task_destroy(LivaTask *task)
    module_->getOrInsertFunction("liva_task_destroy", voidPtrTy);

    // void liva_coro_resume(void *handle)
    module_->getOrInsertFunction("liva_coro_resume", voidPtrTy);

    // void liva_coro_destroy(void *handle)
    module_->getOrInsertFunction("liva_coro_destroy", voidPtrTy);

    // void liva_scheduler_run(LivaTask *root)
    module_->getOrInsertFunction("liva_scheduler_run", voidPtrTy);

    // void liva_async_sleep(LivaTask *task, int64_t ms)
    auto *asyncSleepTy = llvm::FunctionType::get(voidTy, {ptrTy, builder_->getInt64Ty()}, false);
    module_->getOrInsertFunction("liva_async_sleep", asyncSleepTy);

    // void liva_task_cancel(LivaTask *task)
    module_->getOrInsertFunction("liva_task_cancel", voidPtrTy);

    // int8_t liva_task_is_cancelled(LivaTask *task)
    auto *isCancelledTy = llvm::FunctionType::get(i8Ty, {ptrTy}, false);
    module_->getOrInsertFunction("liva_task_is_cancelled", isCancelledTy);

    // int64_t liva_task_select(LivaTask **tasks, int64_t count)
    auto *i64Ty = builder_->getInt64Ty();
    auto *selectTy = llvm::FunctionType::get(i64Ty, {ptrTy, i64Ty}, false);
    module_->getOrInsertFunction("liva_task_select", selectTy);

    // int8_t liva_task_with_timeout(LivaTask *task, int64_t timeout_ms)
    auto *withTimeoutTy = llvm::FunctionType::get(i8Ty, {ptrTy, i64Ty}, false);
    module_->getOrInsertFunction("liva_task_with_timeout", withTimeoutTy);

    // void liva_scheduler_init(int32_t num_workers)
    auto *i32Ty = builder_->getInt32Ty();
    auto *schedInitTy = llvm::FunctionType::get(voidTy, {i32Ty}, false);
    module_->getOrInsertFunction("liva_scheduler_init", schedInitTy);

    // void liva_scheduler_shutdown()
    auto *schedShutdownTy = llvm::FunctionType::get(voidTy, {}, false);
    module_->getOrInsertFunction("liva_scheduler_shutdown", schedShutdownTy);

    // int32_t liva_scheduler_worker_count()
    auto *workerCountTy = llvm::FunctionType::get(i32Ty, {}, false);
    module_->getOrInsertFunction("liva_scheduler_worker_count", workerCountTy);

    // char *liva_async_file_read(const char *path)
    auto *asyncReadTy = llvm::FunctionType::get(ptrTy, {ptrTy}, false);
    module_->getOrInsertFunction("liva_async_file_read", asyncReadTy);

    // int8_t liva_async_file_write(const char *path, const char *content)
    auto *asyncWriteTy = llvm::FunctionType::get(i8Ty, {ptrTy, ptrTy}, false);
    module_->getOrInsertFunction("liva_async_file_write", asyncWriteTy);
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
                                          llvm::GlobalValue::LinkOnceODRLinkage, init, vtableName);
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
        // Enum types: payload-carrying enums lower to their tagged-union struct;
        // simple (payload-less) enums are a plain i32 discriminant.
        auto enumStructIt = enumTypes_.find(named->getName());
        if (enumStructIt != enumTypes_.end())
            return enumStructIt->second;
        if (enumCases_.count(named->getName()))
            return builder_->getInt32Ty();
        return llvm::PointerType::getUnqual(*context_);
    }
    case TypeRepr::Kind::Array: {
        auto *arrayType = static_cast<const ArrayTypeRepr *>(type);
        auto *elemType = toLLVMType(arrayType->getElement());
        if (arrayType->isDynamic())
            return llvm::PointerType::getUnqual(*context_);
        // Resolve const generic param name to value
        int64_t size = arrayType->getSize();
        if (arrayType->hasSizeParam()) {
            auto it = currentConstSubst_.find(arrayType->getSizeParamName());
            if (it != currentConstSubst_.end())
                size = it->second;
        }
        return llvm::ArrayType::get(elemType, static_cast<uint64_t>(size));
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
        if (genType->getBaseName() == "Task") {
            // Phase 2: Task<T> is opaque ptr (LivaTask*)
            return llvm::PointerType::getUnqual(*context_);
        }
        if (genType->getBaseName() == "Generator") {
            // Generator<T> is also a LivaTask* wrapper around a coroutine
            // handle whose promise slot stores T.
            return llvm::PointerType::getUnqual(*context_);
        }
        // User-defined generic struct (Stream<T>, Pair<A,B>, ...): resolve
        // to the monomorphized struct type using currentTypeSubst_ for any
        // type-param refs in the type args.
        if (genericStructDecls_.count(genType->getBaseName())) {
            std::vector<const TypeRepr *> resolvedArgs;
            resolvedArgs.reserve(genType->getTypeArgs().size());
            for (auto &ta : genType->getTypeArgs()) {
                const TypeRepr *resolved = ta.get();
                if (ta->getKind() == TypeRepr::Kind::Named) {
                    auto *nt = static_cast<const NamedTypeRepr *>(ta.get());
                    auto subIt = currentTypeSubst_.find(nt->getName());
                    if (subIt != currentTypeSubst_.end())
                        resolved = subIt->second;
                }
                resolvedArgs.push_back(resolved);
            }
            std::string mangled = mangleGenericStruct(genType->getBaseName(), resolvedArgs);
            auto stIt = structTypes_.find(mangled);
            if (stIt != structTypes_.end())
                return stIt->second;
        }
        // Other generics (Map, Set, etc.) are handled elsewhere
        return llvm::PointerType::getUnqual(*context_);
    }
    case TypeRepr::Kind::Function:
        return getClosureObjTy();
    case TypeRepr::Kind::Reference:
        return llvm::PointerType::getUnqual(*context_);
    case TypeRepr::Kind::DynProtocol:
        return getTraitObjectTy();
    case TypeRepr::Kind::ConstValue:
        // Const generic values resolve to i64 at IR level
        return builder_->getInt64Ty();
    case TypeRepr::Kind::AssociatedType: {
        // Resolve T.Item → concrete type via type substitution + conformance
        auto *assocType = static_cast<const AssociatedTypeRepr *>(type);
        // First resolve the base type (e.g., T → i32)
        auto substIt = currentTypeSubst_.find(assocType->getBaseName());
        if (substIt != currentTypeSubst_.end()) {
            // Look up the associated type resolution for the concrete type
            std::string concreteBase = substIt->second->toString();
            // Search implAssociatedTypeResolutions from type checker
            // For now, try direct named type lookup as fallback
            auto stIt = structTypes_.find(concreteBase);
            if (stIt != structTypes_.end())
                return stIt->second;
        }
        // Fallback: treat as opaque pointer
        return builder_->getInt32Ty();
    }
    default:
        return builder_->getInt32Ty();
    }
}

llvm::AllocaInst *IRGen::createEntryBlockAlloca(llvm::Function *func,
                                                  const std::string &name,
                                                  llvm::Type *type) {
    llvm::IRBuilder<> tmpBuilder(&func->getEntryBlock(),
                                  func->getEntryBlock().begin());
    auto *alloca = tmpBuilder.CreateAlloca(type, nullptr, name);
    // Null-initialize pointer allocas so that free(ptr) at scope cleanup
    // is safe even if the variable was never written (conditional branches).
    if (type->isPointerTy()) {
        tmpBuilder.CreateStore(
            llvm::ConstantPointerNull::get(
                llvm::cast<llvm::PointerType>(type)),
            alloca);
    }
    return alloca;
}

void IRGen::trackStringTemp(llvm::Value *val) {
    if (val) vars_.tempStrings.push_back(val);
}

void IRGen::transferStringOwnership(llvm::Value *val, const std::string &varName) {
    if (!val) return;
    auto it = std::find(vars_.tempStrings.begin(), vars_.tempStrings.end(), val);
    if (it != vars_.tempStrings.end()) {
        vars_.tempStrings.erase(it);
        vars_.heapStringVars.insert(varName);
    }
    // If val is not in vars_.tempStrings (e.g., string literal), don't add to vars_.heapStringVars
}

void IRGen::removeFromTempStrings(llvm::Value *val) {
    if (!val) return;
    auto it = std::find(vars_.tempStrings.begin(), vars_.tempStrings.end(), val);
    if (it != vars_.tempStrings.end())
        vars_.tempStrings.erase(it);
}

bool IRGen::isStringTypeRepr(const TypeRepr *tr) {
    if (!tr) return false;
    if (tr->getKind() == TypeRepr::Kind::String) return true;
    if (tr->getKind() == TypeRepr::Kind::Named) {
        auto *named = static_cast<const NamedTypeRepr *>(tr);
        return named->getName() == "String";
    }
    return false;
}

} // namespace liva

#endif // LIVA_HAS_LLVM
