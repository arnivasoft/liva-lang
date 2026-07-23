#pragma once

#include "liva/AST/ASTVisitor.h"
#include "liva/AST/Decl.h"
#include "liva/Common/Diagnostics.h"
#include <cassert>

#ifdef LIVA_HAS_LLVM
#include "liva/Sema/ModuleLoader.h"
#include <llvm/IR/DIBuilder.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/DebugInfoMetadata.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Value.h>
#include <algorithm>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#endif

namespace liva {

class ModuleLoader; // forward declaration for stub

#ifdef LIVA_HAS_LLVM

/// Monomorphization statistics
struct MonoStats {
    unsigned funcCount = 0;
    unsigned methodCount = 0;
    unsigned structCount = 0;
    unsigned funcCacheHits = 0;
    unsigned methodCacheHits = 0;
    unsigned structCacheHits = 0;
};

/// Generates LLVM IR from the AST
class IRGen : public ASTVisitor<IRGen, llvm::Value *> {
public:
    IRGen(const std::string &moduleName, DiagnosticsEngine &diag);

    /// Set module loader for cross-module IR generation
    void setModuleLoader(ModuleLoader *loader) { moduleLoader_ = loader; }

    /// Enable/disable debug info emission (DWARF/CodeView)
    void setDebugInfo(bool enable) { emitDebugInfo_ = enable; }

    /// Enable separate compilation mode (extern declarations for imports)
    void setSeparateCompilation(bool enable) { separateCompilation_ = enable; }

    /// Control whether main() is required
    void setRequireMain(bool require) { requireMain_ = require; }

    /// Set the target triple for the generated module
    void setTargetTriple(const std::string &triple) { targetTriple_ = triple; }

    /// Generate IR for a translation unit
    bool generate(TranslationUnit &tu);

    /// Get the generated module
    llvm::Module *getModule() { return module_.get(); }
    std::unique_ptr<llvm::Module> takeModule() { return std::move(module_); }
    std::unique_ptr<llvm::LLVMContext> takeContext() { return std::move(context_); }

    /// Get monomorphization statistics
    const MonoStats &getMonoStats() const { return monoStats_; }

    /// Dump IR to stderr
    void dump();

    /// Write IR to a file
    bool writeToFile(const std::string &filename);

    // Visitor methods
    llvm::Value *visitClassDecl(ClassDecl *node);
    /// Pre-declare a class (phase 1): register type/field/vtable maps and create
    /// all function PROTOTYPES (no bodies) so other classes can forward-reference it.
    /// Idempotent (guarded by preDeclared_); recursively ensures parent is done first.
    void preDeclareClass(ClassDecl *node);
    llvm::Value *visitTestDecl(TestDecl *node);
    llvm::Value *visitFuncDecl(FuncDecl *node);
    llvm::Value *visitVarDecl(VarDecl *node);
    llvm::Value *visitStructDecl(StructDecl *node);
    llvm::Value *visitBlockStmt(BlockStmt *node);
    llvm::Value *visitReturnStmt(ReturnStmt *node);
    llvm::Value *visitIfStmt(IfStmt *node);
    llvm::Value *visitIfLetStmt(IfLetStmt *node);
    llvm::Value *visitWhileLetStmt(WhileLetStmt *node);
    llvm::Value *visitWhileStmt(WhileStmt *node);
    llvm::Value *visitForStmt(ForStmt *node);
    llvm::Value *visitBreakStmt(BreakStmt *node);
    llvm::Value *visitContinueStmt(ContinueStmt *node);
    llvm::Value *visitExprStmt(ExprStmt *node);

    llvm::Value *visitIntegerLiteralExpr(IntegerLiteralExpr *node);
    llvm::Value *visitFloatLiteralExpr(FloatLiteralExpr *node);
    llvm::Value *visitBoolLiteralExpr(BoolLiteralExpr *node);
    llvm::Value *visitStringLiteralExpr(StringLiteralExpr *node);
    llvm::Value *visitIdentifierExpr(IdentifierExpr *node);
    llvm::Value *visitBinaryExpr(BinaryExpr *node);
    llvm::Value *visitUnaryExpr(UnaryExpr *node);
    llvm::Value *visitCallExpr(CallExpr *node);

    // --- visitCallExpr domain helpers (IRGenCall*.cpp) ---
    // Dolu optional = çağrı işlendi (değer nullptr olabilir = hata);
    // std::nullopt = bu domain'in işi değil.
    std::optional<llvm::Value *> tryEmitMethodCall(CallExpr *node);
    std::optional<llvm::Value *> tryEmitCoreBuiltin(CallExpr *node,
                                                    const std::string &funcName);
    std::optional<llvm::Value *> tryEmitSysBuiltin(CallExpr *node,
                                                    const std::string &funcName);
    std::optional<llvm::Value *> tryEmitStringBuiltin(CallExpr *node,
                                                    const std::string &funcName);
    std::optional<llvm::Value *> tryEmitConcurrencyBuiltin(CallExpr *node,
                                                    const std::string &funcName);
    std::optional<llvm::Value *> tryEmitDataBuiltin(CallExpr *node,
                                                    const std::string &funcName);
    std::optional<llvm::Value *> tryEmitNetBuiltin(CallExpr *node,
                                                    const std::string &funcName);
    std::optional<llvm::Value *> tryEmitUIBuiltin(CallExpr *node,
                                                    const std::string &funcName);
    // Ensure an integer-typed value is i64 (sign-extend from smaller widths).
    // Liva integer literals default to i32, but most stdlib runtime functions
    // expect i64. Shared by the Concurrency and Data helpers.
    llvm::Value *toI64(llvm::Value *v);

    llvm::Value *visitAssignExpr(AssignExpr *node);
    llvm::Value *visitGroupExpr(GroupExpr *node);
    llvm::Value *visitCastExpr(CastExpr *node);
    llvm::Value *visitIsExpr(IsExpr *node);
    llvm::Value *visitMemberExpr(MemberExpr *node);
    llvm::Value *visitStructLiteralExpr(StructLiteralExpr *node);
    llvm::Value *visitImplDecl(ImplDecl *node);
    llvm::Value *visitEnumDecl(EnumDecl *node);
    llvm::Value *visitEnumCaseDecl(EnumCaseDecl *node);
    llvm::Value *visitMatchExpr(MatchExpr *node);
    llvm::Value *visitRangeExpr(RangeExpr *node);
    llvm::Value *visitArrayLiteralExpr(ArrayLiteralExpr *node);
    llvm::Value *visitTupleLiteralExpr(TupleLiteralExpr *node);
    llvm::Value *visitIndexExpr(IndexExpr *node);
    llvm::Value *visitNilLiteralExpr(NilLiteralExpr *node);
    llvm::Value *visitUnwrapExpr(UnwrapExpr *node);
    llvm::Value *visitClosureExpr(ClosureExpr *node);
    llvm::Value *visitProtocolDecl(ProtocolDecl *node);
    llvm::Value *visitImportDecl(ImportDecl *node);
    llvm::Value *visitTryExpr(TryExpr *node);
    llvm::Value *visitTernaryExpr(TernaryExpr *node);
    llvm::Value *visitRefExpr(RefExpr *node);
    llvm::Value *visitTypeAliasDecl(TypeAliasDecl *node);
    llvm::Value *visitAwaitExpr(AwaitExpr *node);
    llvm::Value *visitYieldExpr(YieldExpr *node);
    llvm::Value *visitComptimeExpr(ComptimeExpr *node);

private:
    /// Lookup a runtime function by name, asserting it was declared
    llvm::Function *getOrPanic(const char *name) {
        auto *fn = module_->getFunction(name);
        assert(fn && "Missing runtime function declaration — check createRuntimeDecls()");
        return fn;
    }

    // === Debug info ===
    std::unique_ptr<llvm::DIBuilder> diBuilder_;
    llvm::DICompileUnit *diCU_ = nullptr;
    llvm::DIFile *diFile_ = nullptr;
    bool emitDebugInfo_ = false;

    void initDebugInfo(const std::string &filename);
    void finalizeDebugInfo();
    llvm::DISubroutineType *createFunctionDebugType();
    llvm::DISubroutineType *createFunctionDebugType(const FuncDecl *funcDecl);
    void emitDebugLocation(const SourceLocation &loc);
    llvm::DIType *toDIType(const TypeRepr *type);
    llvm::DICompositeType *getOrCreateStructDIType(const std::string &structName);
    void emitLocalVarDebugInfo(const std::string &name, llvm::AllocaInst *alloca,
                               llvm::DIType *diType, const SourceLocation &loc);
    void emitParamDebugInfo(const std::string &name, unsigned argNo, llvm::AllocaInst *alloca,
                            llvm::DIType *diType, const SourceLocation &loc);
    std::unordered_map<std::string, llvm::DICompositeType *> diStructTypes_;

    /// Convert Liva type to LLVM type
    llvm::Type *toLLVMType(const TypeRepr *type);

    /// Create runtime function declarations (print, println)
    void createRuntimeDecls();

    /// Get or create a named value (variable)
    llvm::AllocaInst *createEntryBlockAlloca(llvm::Function *func,
                                              const std::string &name,
                                              llvm::Type *type);

    struct LoopContext {
        llvm::BasicBlock *breakBB;     // break → buraya dallan
        llvm::BasicBlock *continueBB;  // continue → buraya dallan
    };
    std::vector<LoopContext> loopStack_;

    DiagnosticsEngine &diag_;
    ModuleLoader *moduleLoader_ = nullptr;
    bool separateCompilation_ = false;
    bool requireMain_ = true;
    std::string targetTriple_;
    std::set<std::string> processedModules_;

    /// Extern declaration helpers for separate compilation
    void declareExternFunction(FuncDecl *funcDecl);
    void declareExternStruct(StructDecl *structDecl);
    void declareExternImpl(ImplDecl *implDecl);
    std::unique_ptr<llvm::LLVMContext> context_;
    std::unique_ptr<llvm::Module> module_;
    std::unique_ptr<llvm::IRBuilder<>> builder_;

    /// Get field index by name for a struct type
    int getStructFieldIndex(const std::string &structName, const std::string &fieldName);

    /// Info for a struct member that is a DynArray field
    struct MemberDynArrayInfo {
        llvm::Value *arrGEP;      // GEP pointing to the DynArray struct within the parent struct
        llvm::Type *elementType;
        uint64_t elemSize;
    };

    /// Resolve a MemberExpr to a DynArray field GEP if it is a struct.dynArrayField
    std::optional<MemberDynArrayInfo> resolveMemberDynArray(MemberExpr *memberExpr);

    /// Struct type layouts
    std::unordered_map<std::string, llvm::StructType *> structTypes_;

    /// Struct field names in order (struct name -> field names)
    std::unordered_map<std::string, std::vector<std::string>> structFieldNames_;

    /// Struct field TypeRepr* (struct name -> field TypeReprs for DynArray detection)
    std::unordered_map<std::string, std::vector<const TypeRepr *>> structFieldTypeReprs_;

    /// Enum case tag values: enumName -> {caseName -> tag}
    std::unordered_map<std::string, std::unordered_map<std::string, int>> enumCases_;

    /// Array variable tracking
    struct ArrayInfo {
        llvm::Type *elementType;
        uint64_t size;
    };

    /// Dynamic array variable tracking
    struct DynArrayInfo {
        llvm::Type *elementType;
        uint64_t elemSize;
    };

    /// Tuple variable tracking
    struct TupleInfo {
        std::vector<llvm::Type *> elementTypes;
    };

    /// Map variable tracking
    struct MapInfo {
        llvm::Type *keyType;
        llvm::Type *valType;
        uint64_t keySize;
        uint64_t valSize;
        int8_t keyKind;  // 0=bytes, 1=string
    };

    /// Set variable tracking
    struct SetInfo {
        llvm::Type *elemType;
        uint64_t elemSize;
        int8_t keyKind;
    };

    /// Result type support
    struct ResultInfo { llvm::Type *okType; llvm::Type *errType; };

    /// Scope-bound IRGen state. Snapshotted/restored at scope boundaries
    /// (closures, monomorphization, if-let, etc.) via VarStateGuard.
    /// Fields here are migrated incrementally from previously top-level
    /// IRGen members; access is via `vars_.<name>`.
    struct VarState {
        /// Named values in current scope
        std::unordered_map<std::string, llvm::AllocaInst *> namedValues;

        /// Variable to struct type name mapping
        std::unordered_map<std::string, std::string> varStructTypes;

        /// Variable to enum type name mapping
        std::unordered_map<std::string, std::string> varEnumTypes;

        /// Array variable tracking
        std::unordered_map<std::string, ArrayInfo> varArrayTypes;

        /// Dynamic array variable tracking
        std::unordered_map<std::string, DynArrayInfo> varDynArrayTypes;

        /// Tuple variable tracking
        std::unordered_map<std::string, TupleInfo> varTupleTypes;

        /// Map variable tracking
        std::unordered_map<std::string, MapInfo> varMapTypes;

        /// Set variable tracking
        std::unordered_map<std::string, SetInfo> varSetTypes;

        /// Track which variables are optional and their inner LLVM type
        std::unordered_map<std::string, llvm::Type *> varOptionalTypes;

        /// Track which variables are references and their inner LLVM type
        std::unordered_map<std::string, llvm::Type *> varRefTypes;

        /// Track File-typed variables (opaque ptr = FILE*)
        std::set<std::string> varFileTypes;

        /// Track Optional<File> variables (for if-let unwrap → varFileTypes)
        std::set<std::string> varFileOptionalTypes;

        /// Variable to class type name mapping
        std::unordered_map<std::string, std::string> varClassTypes;

        /// Track variables that have been moved (passed by value to functions)
        std::set<std::string> movedVars;

        /// Heap-allocated string temps in current statement (freed after statement)
        std::vector<llvm::Value *> tempStrings;

        /// String variables holding heap-allocated memory (freed at scope exit)
        std::set<std::string> heapStringVars;

        /// Optional<string> variables that own their inner heap string. Cleaned
        /// up at scope exit by checking the hasVal flag and freeing only the
        /// inner ptr when set. Populated when an Optional-returning builtin
        /// (hexDecode, base64UrlDecode, tomlGetString, ...) is bound to a let.
        std::set<std::string> heapOptionalStringVars;

        /// Result type tracking per variable
        std::unordered_map<std::string, ResultInfo> varResultTypes;

        /// Current function's Result info (active when emitting a function body)
        ResultInfo *currentFuncResultInfo = nullptr;

        /// Function-typed variable tracking (for indirect calls)
        std::unordered_map<std::string, llvm::FunctionType *> varFuncTypes;

        /// Variables that are protocol trait objects: varName → protocolName
        std::unordered_map<std::string, std::string> varProtocolTypes;

        /// Concrete type behind dyn Protocol vars (let bindings only)
        std::unordered_map<std::string, std::string> varConcreteProtocolTypes;

        /// DynArray vars with dyn Protocol elements: arrayVarName → protocolName
        std::unordered_map<std::string, std::string> varDynArrayProtocol;
    };

    VarState vars_;

    /// RAII guard that snapshots `vars_` on construction and restores it on
    /// destruction. Replaces the manual `auto saved = vars_.X; ...; vars_.X = saved;`
    /// idiom used at scope boundaries (closures, monomorphization, if/while-let).
    /// Access the previous snapshot via `guard.saved()` when partial restore is
    /// needed (e.g. captured-variable lookup in closures).
    class VarStateGuard {
        IRGen *gen_;
        VarState saved_;
    public:
        explicit VarStateGuard(IRGen *g) : gen_(g), saved_(g->vars_) {}
        ~VarStateGuard() { gen_->vars_ = std::move(saved_); }
        VarStateGuard(const VarStateGuard &) = delete;
        VarStateGuard &operator=(const VarStateGuard &) = delete;
        VarStateGuard(VarStateGuard &&) = delete;
        VarStateGuard &operator=(VarStateGuard &&) = delete;

        const VarState &saved() const { return saved_; }
    };

    /// Snapshot current `vars_`; the returned guard restores it on scope exit.
    /// Mandatory-RVO ensures no copy/move of the guard occurs at the call site.
    [[nodiscard]] VarStateGuard pushVarState() { return VarStateGuard(this); }

    /// Map/Set LLVM struct type: { ptr, i64, i64 }
    llvm::StructType *mapStructTy_ = nullptr;
    llvm::StructType *getMapStructTy();

    /// Dynamic array LLVM struct type: { ptr, i64, i64 }
    llvm::StructType *dynArrayStructTy_ = nullptr;

    /// Get or create the dynamic array struct type
    llvm::StructType *getDynArrayStructTy();

    /// Compile-time constant values (no alloca needed)
    std::unordered_map<std::string, llvm::Constant *> constValues_;

    /// String constant interning
    std::unordered_map<std::string, llvm::Constant *> stringConstants_;

    /// Monomorphization: active type substitution map ("T" -> TypeRepr*)
    std::unordered_map<std::string, const TypeRepr *> currentTypeSubst_;
    /// Monomorphization: active const substitution map ("N" -> 42)
    std::unordered_map<std::string, int64_t> currentConstSubst_;

    /// Cache of monomorphized functions: "identity_i32" -> Function*
    std::unordered_map<std::string, llvm::Function *> monomorphizedFuncs_;

    /// Generic function AST nodes: "identity" -> FuncDecl*
    std::unordered_map<std::string, const FuncDecl *> genericFuncDecls_;

    /// All function AST nodes: "greet" -> FuncDecl* (for default args)
    std::unordered_map<std::string, const FuncDecl *> funcDecls_;

    /// Type alias resolution: aliasName -> targetTypeRepr*
    std::unordered_map<std::string, const TypeRepr *> typeAliases_;

    /// Generic struct AST nodes: "Box" -> StructDecl*
    std::unordered_map<std::string, const StructDecl *> genericStructDecls_;

    /// Cache of monomorphized structs: "Box_i32"
    std::unordered_set<std::string> monomorphizedStructs_;

    /// Generic impl AST nodes: "Box" -> ImplDecl*
    std::unordered_map<std::string, const ImplDecl *> genericImplDecls_;

    /// Struct type args for monomorphized structs: "Box_i32" -> [i32 TypeRepr*]
    std::unordered_map<std::string, std::vector<const TypeRepr *>> structTypeArgs_;

    /// Monomorphize a method from a generic impl block. `structTypeArgs`
    /// substitute the impl's struct-level params (T from `impl Stream<T>`),
    /// `methodTypeArgs` the method's own type params (U from `func map<U>`).
    /// Both contribute to the mangled function name.
    llvm::Function *monomorphizeMethod(const ImplDecl *implDecl,
                                        const FuncDecl *methodDecl,
                                        const std::string &mangledStructName,
                                        const std::vector<const TypeRepr *> &structTypeArgs,
                                        const std::vector<const TypeRepr *> &methodTypeArgs = {});

    /// Lifetime management for inferred TypeRepr objects
    std::vector<std::unique_ptr<TypeRepr>> inferredTypes_;

    /// Monomorphize a generic function with concrete type arguments
    llvm::Function *monomorphize(const FuncDecl *funcDecl,
                                  const std::vector<const TypeRepr *> &typeArgs);

    /// Generate mangled name for a monomorphized function
    std::string mangleGenericFunc(const std::string &baseName,
                                   const std::vector<const TypeRepr *> &typeArgs);

    /// Monomorphize a generic struct with concrete type arguments
    void monomorphizeStruct(const StructDecl *structDecl,
                            const std::vector<const TypeRepr *> &typeArgs);

    /// Generate mangled name for a monomorphized struct
    std::string mangleGenericStruct(const std::string &baseName,
                                     const std::vector<const TypeRepr *> &typeArgs);

    /// Infer type arguments for a generic struct from field init values
    std::vector<const TypeRepr *> inferStructTypeArgs(
        const StructDecl *structDecl,
        const std::vector<StructLiteralExpr::FieldInit> &fieldInits,
        const std::vector<llvm::Value *> &fieldValues);

    /// Walk a TypeRepr and replace NamedType refs that appear in `subst`
    /// with their substitutes. Cloned-with-subst nodes are stored in
    /// inferredTypes_ so the returned pointer outlives the call.
    const TypeRepr *substituteTypeRepr(
        const TypeRepr *type,
        const std::unordered_map<std::string, const TypeRepr *> &subst);

    /// Enum LLVM StructType for payload enums: enumName -> StructType
    std::unordered_map<std::string, llvm::StructType *> enumTypes_;

    /// Per-case payload types: enumName -> caseName -> [llvm::Type*]
    std::unordered_map<std::string, std::unordered_map<std::string, std::vector<llvm::Type *>>>
        enumCasePayloads_;

    /// Pattern info for match expressions
    struct PatternInfo {
        std::string enumName;
        std::string caseName;
        std::vector<std::string> bindings;
        int tag = -1;
        bool isWildcard = false;
        std::vector<PatternInfo> nestedPatterns; // one per binding slot
    };

    PatternInfo parseMatchPattern(const std::string &pattern,
                                   const std::string &subjectEnumType);

    /// Emit nested pattern check: verify inner enum tag and extract bindings
    void emitNestedPatternMatch(llvm::Value *fieldPtr, const PatternInfo &nested,
                                llvm::BasicBlock *failBB, llvm::Function *func);

    llvm::Value *emitEnumCaseConstruct(const std::string &enumName,
                                        const std::string &caseName, int tag,
                                        const std::vector<std::unique_ptr<Expr>> &args);

    /// Emit runtime bounds check: panics if index >= size
    void emitBoundsCheck(llvm::Value *indexVal, llvm::Value *sizeVal);

    /// Emit runtime slice bounds check: panics if start<0, end<start, or end>len
    void emitSliceBoundsCheck(llvm::Value *startVal, llvm::Value *endVal, llvm::Value *lenVal);

    /// Emit nil coalescing operator (??) IR
    llvm::Value *emitNilCoalesce(BinaryExpr *node);

    /// Emit optional chaining member access (?.) IR
    llvm::Value *emitOptionalChainMember(MemberExpr *node);

    /// Get or create an Optional<T> struct type: { i1, T }
    llvm::StructType *getOptionalType(llvm::Type *innerType);
    std::unordered_map<llvm::Type *, llvm::StructType *> optionalTypes_;

    /// Types that implement the Drop protocol
    std::set<std::string> dropImplementors_;

    /// Async function tracking
    std::set<std::string> asyncFuncNames_;

    /// Generator-flagged functions and the LLVM type of their yielded value.
    /// Populated during visitFuncDecl so visitForStmt can recognise a
    /// generator-call iterable and emit a coro.resume / coro.done / coro.promise
    /// loop instead of the standard array iteration.
    std::unordered_map<std::string, llvm::Type *> generatorFuncs_;

    bool currentIsAsync_ = false;
    llvm::Type *asyncDeclaredRetType_ = nullptr;

    /// Phase 2 Coroutine State
    llvm::AllocaInst *currentCoroTask_ = nullptr;     // LivaTask* alloca
    llvm::Value *currentCoroHandle_ = nullptr;          // coro.begin handle
    llvm::Value *currentCoroId_ = nullptr;              // coro.id token
    llvm::AllocaInst *currentCoroPromise_ = nullptr;    // Promise alloca (return value)
    llvm::BasicBlock *currentCoroFinalBB_ = nullptr;    // Final suspend block
    llvm::BasicBlock *currentCoroCleanupBB_ = nullptr;  // Cleanup block
    llvm::BasicBlock *currentCoroSuspendBB_ = nullptr;  // Suspend/ret block

    void declareCoroutineIntrinsics();
    void declareAsyncRuntimeFuncs();

    /// Helper: emit free for all temp strings and clear list
    void emitTempStringCleanup();

    /// Helper: register a malloc'd string temp for cleanup
    void trackStringTemp(llvm::Value *val);

    /// Helper: transfer ownership from temp to named variable
    void transferStringOwnership(llvm::Value *val, const std::string &varName);

    /// Helper: remove a value from tempStrings_ without adding to heapStringVars_
    void removeFromTempStrings(llvm::Value *val);

    /// Helper: check if a TypeRepr represents a String type
    /// Handles both Kind::String and NamedTypeRepr("String")
    static bool isStringTypeRepr(const TypeRepr *tr);

    /// If `structName.field[idx]` is a dynamic-array typed field and `val`
    /// is a DynArray struct value, return a deep-cloned DynArray value so
    /// the source variable and the new struct can be freed independently.
    /// Otherwise returns `val` unchanged.
    llvm::Value *cloneIfDynArrayField(const std::string &structName, int idx,
                                       llvm::Value *val,
                                       const std::string &nameHint);

    /// Emit cleanup calls for heap-allocated resources before scope exit
    void emitScopeCleanup();

    /// Emit cleanup for struct fields that are heap-allocated (DynArray, Map, Set, string)
    /// Called when a struct var goes out of scope without implementing Drop
    void emitStructFieldCleanup(const std::string &varName, const std::string &structTypeName);

    /// If the field at `idx` in `structName` is a string, wrap `val` with liva_str_dup
    llvm::Value *dupIfStringField(const std::string &structName, int idx, llvm::Value *val);


    /// Optional return type support — non-null when the current function returns T?
    llvm::Type *currentFuncOptionalInner_ = nullptr;

    /// True when currently generating a class init (so 'return nil' yields null ptr)
    bool currentIsClassInit_ = false;

    /// Result type support
    std::map<std::pair<llvm::Type *, llvm::Type *>, llvm::StructType *> resultTypes_;
    ResultInfo currentFuncResultInfoStorage_;

    llvm::StructType *getResultType(llvm::Type *okType, llvm::Type *errType);
    llvm::Value *emitResultOk(llvm::Type *okType, llvm::Type *errType, llvm::Value *value);
    llvm::Value *emitResultErr(llvm::Type *okType, llvm::Type *errType, llvm::Value *value);

    /// Function-typed struct field tracking: structName -> {fieldName -> FunctionType*}
    std::unordered_map<std::string,
        std::unordered_map<std::string, llvm::FunctionType *>> structFieldFuncTypes_;

    /// Counter for generating unique closure names
    int closureCounter_ = 0;

    /// Alloc size (bytes) of the most recently visited closure literal's env
    /// struct; 0 when it captured nothing. Read by the UI event-method fast
    /// path immediately after visiting the closure to heap-own the env.
    uint64_t lastClosureEnvSize_ = 0;

    /// Closure object type: { ptr func_ptr, ptr env_ptr }
    llvm::StructType *closureObjTy_ = nullptr;

    /// Get or create the closure object type
    llvm::StructType *getClosureObjTy();

    /// Fat pointer (trait object) type: { ptr, ptr } = { data_ptr, vtable_ptr }
    llvm::StructType *traitObjectTy_ = nullptr;

    /// Get or create the trait object type
    llvm::StructType *getTraitObjectTy();

    /// Protocol method names in order: protocolName → [methodName...]
    std::unordered_map<std::string, std::vector<std::string>> protocolMethodNames_;

    /// Protocol method indices: protocolName → {methodName → index}
    std::unordered_map<std::string, std::unordered_map<std::string, int>> protocolMethodIndices_;

    /// Protocol declarations for default method lookup
    std::unordered_map<std::string, const ProtocolDecl *> protocolDecls_;

    /// Vtable globals: "vtable_protocolName_typeName" → GlobalVariable*
    std::unordered_map<std::string, llvm::GlobalVariable *> vtableGlobals_;

    /// Protocol conformances: protocolName → [typeName...]
    std::unordered_map<std::string, std::vector<std::string>> protocolConformances_;

    /// Class support
    std::unordered_map<std::string, llvm::StructType *> classTypes_;
    std::unordered_map<std::string, std::vector<std::string>> classFieldNames_;
    std::unordered_map<std::string, std::vector<const TypeRepr *>> classFieldTypeReprs_;
    std::unordered_map<std::string, std::string> classParent_;
    std::set<std::string> classNames_;
    /// Classes that have completed phase-1 pre-declaration (guards idempotency/cycles)
    std::set<std::string> preDeclared_;
    /// Enums registered in the enum pre-pass (guards re-emission in main decl loop)
    std::set<std::string> preDeclaredEnums_;
    /// Map of class name → ClassDecl node, populated during the pre-declare pass so
    /// preDeclareClass can recursively ensure a parent is pre-declared first.
    std::unordered_map<std::string, ClassDecl *> classDecls_;
    std::unordered_map<std::string, std::vector<std::string>> classVtableMethods_;
    std::unordered_map<std::string, llvm::GlobalVariable *> classVtables_;
    std::unordered_map<std::string, std::unordered_map<std::string, int>> classMethodIndices_;
    std::string currentClassContext_;
    /// Computed properties: className → set of computed field names
    std::unordered_map<std::string, std::set<std::string>> classComputedFields_;
    /// Static fields: "ClassName.fieldName" → global variable
    std::unordered_map<std::string, llvm::GlobalVariable *> classStaticFields_;
    /// Fields with property observers: className → set of field names
    std::unordered_map<std::string, std::set<std::string>> classObserverFields_;
    /// Lazy fields with initializers: className → set of field names (have companion __lazy_init_ bool)
    std::unordered_map<std::string, std::set<std::string>> classLazyFields_;

    /// Get or create a vtable for type conforming to protocol
    llvm::GlobalVariable *getOrCreateVtable(const std::string &protocolName,
                                              const std::string &typeName);

    /// Walk an expression and determine its class type name using IRGen maps.
    /// Returns empty string if the expression does not resolve to a known class
    /// type.  Handles IdentifierExpr (varClassTypes) and MemberExpr chains
    /// (classFieldTypeReprs_) without requiring Sema to have set resolvedType.
    std::string resolveExprClassTypeName(Expr *expr);

    /// Monomorphization statistics
    MonoStats monoStats_;

    /// Test framework support
    struct TestEntry { std::string name; llvm::Function *func; };
    std::vector<TestEntry> testEntries_;
    int testCounter_ = 0;
    void generateTestMain();
};

#else

/// Stub IRGen when LLVM is not available
class IRGen {
public:
    IRGen(const std::string &, DiagnosticsEngine &diag) : diag_(diag) {}

    void setModuleLoader(ModuleLoader *) {}
    void setDebugInfo(bool) {}
    void setSeparateCompilation(bool) {}
    void setRequireMain(bool) {}
    void setTargetTriple(const std::string &) {}

    bool generate(TranslationUnit &) {
        diag_.report(SourceLocation{}, DiagID::err_main_not_found);
        return false;
    }

    void dump() {}
    bool writeToFile(const std::string &) { return false; }

private:
    DiagnosticsEngine &diag_;
};

#endif // LIVA_HAS_LLVM

} // namespace liva
