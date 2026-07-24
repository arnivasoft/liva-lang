#include "liva/IR/IRGen.h"

#ifdef LIVA_HAS_LLVM

#include "liva/IR/RuntimeTypeCodes.h"
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
    auto *ptrTy = llvm::PointerType::getUnqual(*context_);
    auto ty = [&](RtTypeCode c) -> llvm::Type * {
        switch (c) {
        case RT_VOID: return builder_->getVoidTy();
        case RT_PTR:  return ptrTy;
        case RT_I8:   return builder_->getInt8Ty();
        case RT_I32:  return builder_->getInt32Ty();
        case RT_I64:  return builder_->getInt64Ty();
        case RT_F64:  return builder_->getDoubleTy();
        }
        return ptrTy;
    };
    auto decl = [&](const char *name, RtTypeCode ret,
                    std::initializer_list<RtTypeCode> params, bool varargs) {
        std::vector<llvm::Type *> ps;
        for (auto p : params) ps.push_back(ty(p));
        module_->getOrInsertFunction(
            name, llvm::FunctionType::get(ty(ret), ps, varargs));
    };
    // RuntimeFunctions.def spells type codes without the RT_ prefix
    // (VOID/PTR/I8/I32/I64/F64); alias them onto RtTypeCode's enumerators
    // for the duration of the include. VOID collides with a WinAPI macro
    // (winnt.h: `#define VOID void`) — save/restore it around the include.
#pragma push_macro("VOID")
#undef VOID
#define VOID RT_VOID
#define PTR  RT_PTR
#define I8   RT_I8
#define I32  RT_I32
#define I64  RT_I64
#define F64  RT_F64
#define RT_ARGS(...) { __VA_ARGS__ }
#define LIVA_RT(name, ret, params)        decl(#name, ret, params, false);
#define LIVA_RT_JIT(name, ret, params)    decl(#name, ret, params, false);
#define LIVA_RT_VA(name, ret, params)     decl(#name, ret, params, true);
#define LIVA_RT_JIT_VA(name, ret, params) decl(#name, ret, params, true);
#include "RuntimeFunctions.def"
#undef LIVA_RT
#undef LIVA_RT_JIT
#undef LIVA_RT_VA
#undef LIVA_RT_JIT_VA
#undef RT_ARGS
#undef VOID
#pragma pop_macro("VOID")
#undef PTR
#undef I8
#undef I32
#undef I64
#undef F64

    // liva_panic / liva_exit are noreturn. RuntimeFunctions.def has no
    // attribute slot, so this is kept as a small hand-fixup here to
    // preserve pre-refactor IR exactly (see IR-diff equivalence check).
    for (const char *fn : {"liva_panic", "liva_exit"}) {
        if (auto *f = module_->getFunction(fn))
            f->addFnAttr(llvm::Attribute::NoReturn);
    }

    // Coroutine + async runtime (out of .def scope — declared separately)
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

llvm::Type *IRGen::dynArrayElemLLVMType(const TypeRepr *elemRepr) {
    if (elemRepr && elemRepr->getKind() == TypeRepr::Kind::Array &&
        static_cast<const ArrayTypeRepr *>(elemRepr)->isDynamic())
        return getDynArrayStructTy();
    return toLLVMType(elemRepr);
}

void IRGen::deriveNestedDynArrayInner(const ArrayTypeRepr *outerArrRepr,
                                       llvm::Type *&innerElemType,
                                       uint64_t &innerElemSize) {
    innerElemType = nullptr;
    innerElemSize = 0;
    if (!outerArrRepr) return;
    auto *elemRepr = outerArrRepr->getElement();
    if (!elemRepr || elemRepr->getKind() != TypeRepr::Kind::Array) return;
    auto *innerArrRepr = static_cast<const ArrayTypeRepr *>(elemRepr);
    if (!innerArrRepr->isDynamic()) return;
    innerElemType = dynArrayElemLLVMType(innerArrRepr->getElement());
    innerElemSize = module_->getDataLayout().getTypeAllocSize(innerElemType);
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
