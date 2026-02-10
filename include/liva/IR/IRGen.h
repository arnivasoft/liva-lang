#pragma once

#include "liva/AST/ASTVisitor.h"
#include "liva/AST/Decl.h"
#include "liva/Common/Diagnostics.h"

#ifdef LIVA_HAS_LLVM
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Value.h>
#include <memory>
#include <string>
#include <unordered_map>
#endif

namespace liva {

#ifdef LIVA_HAS_LLVM

/// Generates LLVM IR from the AST
class IRGen : public ASTVisitor<IRGen, llvm::Value *> {
public:
    IRGen(const std::string &moduleName, DiagnosticsEngine &diag);

    /// Generate IR for a translation unit
    bool generate(TranslationUnit &tu);

    /// Get the generated module
    llvm::Module *getModule() { return module_.get(); }
    std::unique_ptr<llvm::Module> takeModule() { return std::move(module_); }

    /// Dump IR to stderr
    void dump();

    /// Write IR to a file
    bool writeToFile(const std::string &filename);

    // Visitor methods
    llvm::Value *visitFuncDecl(FuncDecl *node);
    llvm::Value *visitVarDecl(VarDecl *node);
    llvm::Value *visitStructDecl(StructDecl *node);
    llvm::Value *visitBlockStmt(BlockStmt *node);
    llvm::Value *visitReturnStmt(ReturnStmt *node);
    llvm::Value *visitIfStmt(IfStmt *node);
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
    llvm::Value *visitAssignExpr(AssignExpr *node);
    llvm::Value *visitGroupExpr(GroupExpr *node);
    llvm::Value *visitCastExpr(CastExpr *node);
    llvm::Value *visitMemberExpr(MemberExpr *node);
    llvm::Value *visitStructLiteralExpr(StructLiteralExpr *node);
    llvm::Value *visitImplDecl(ImplDecl *node);
    llvm::Value *visitEnumDecl(EnumDecl *node);
    llvm::Value *visitEnumCaseDecl(EnumCaseDecl *node);
    llvm::Value *visitMatchExpr(MatchExpr *node);
    llvm::Value *visitRangeExpr(RangeExpr *node);
    llvm::Value *visitArrayLiteralExpr(ArrayLiteralExpr *node);
    llvm::Value *visitIndexExpr(IndexExpr *node);
    llvm::Value *visitNilLiteralExpr(NilLiteralExpr *node);
    llvm::Value *visitUnwrapExpr(UnwrapExpr *node);

private:
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
    std::unique_ptr<llvm::LLVMContext> context_;
    std::unique_ptr<llvm::Module> module_;
    std::unique_ptr<llvm::IRBuilder<>> builder_;

    /// Get field index by name for a struct type
    int getStructFieldIndex(const std::string &structName, const std::string &fieldName);

    /// Named values in current scope
    std::unordered_map<std::string, llvm::AllocaInst *> namedValues_;

    /// Struct type layouts
    std::unordered_map<std::string, llvm::StructType *> structTypes_;

    /// Struct field names in order (struct name -> field names)
    std::unordered_map<std::string, std::vector<std::string>> structFieldNames_;

    /// Variable to struct type name mapping
    std::unordered_map<std::string, std::string> varStructTypes_;

    /// Enum case tag values: enumName -> {caseName -> tag}
    std::unordered_map<std::string, std::unordered_map<std::string, int>> enumCases_;

    /// Variable to enum type name mapping
    std::unordered_map<std::string, std::string> varEnumTypes_;

    /// Array variable tracking
    struct ArrayInfo {
        llvm::Type *elementType;
        uint64_t size;
    };
    std::unordered_map<std::string, ArrayInfo> varArrayTypes_;

    /// Dynamic array variable tracking
    struct DynArrayInfo {
        llvm::Type *elementType;
        uint64_t elemSize;
    };
    std::unordered_map<std::string, DynArrayInfo> varDynArrayTypes_;

    /// Dynamic array LLVM struct type: { ptr, i64, i64 }
    llvm::StructType *dynArrayStructTy_ = nullptr;

    /// Get or create the dynamic array struct type
    llvm::StructType *getDynArrayStructTy();

    /// String constant interning
    std::unordered_map<std::string, llvm::Constant *> stringConstants_;

    /// Monomorphization: active type substitution map ("T" -> TypeRepr*)
    std::unordered_map<std::string, const TypeRepr *> currentTypeSubst_;

    /// Cache of monomorphized functions: "identity_i32" -> Function*
    std::unordered_map<std::string, llvm::Function *> monomorphizedFuncs_;

    /// Generic function AST nodes: "identity" -> FuncDecl*
    std::unordered_map<std::string, const FuncDecl *> genericFuncDecls_;

    /// Generic struct AST nodes: "Box" -> StructDecl*
    std::unordered_map<std::string, const StructDecl *> genericStructDecls_;

    /// Cache of monomorphized structs: "Box_i32" -> true
    std::unordered_map<std::string, bool> monomorphizedStructs_;

    /// Generic impl AST nodes: "Box" -> ImplDecl*
    std::unordered_map<std::string, const ImplDecl *> genericImplDecls_;

    /// Struct type args for monomorphized structs: "Box_i32" -> [i32 TypeRepr*]
    std::unordered_map<std::string, std::vector<const TypeRepr *>> structTypeArgs_;

    /// Monomorphize a method from a generic impl block
    llvm::Function *monomorphizeMethod(const ImplDecl *implDecl,
                                        const FuncDecl *methodDecl,
                                        const std::string &mangledStructName,
                                        const std::vector<const TypeRepr *> &typeArgs);

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
    };

    PatternInfo parseMatchPattern(const std::string &pattern,
                                   const std::string &subjectEnumType);

    llvm::Value *emitEnumCaseConstruct(const std::string &enumName,
                                        const std::string &caseName, int tag,
                                        const std::vector<std::unique_ptr<Expr>> &args);

    /// Emit runtime bounds check: panics if index >= size
    void emitBoundsCheck(llvm::Value *indexVal, llvm::Value *sizeVal);

    /// Get or create an Optional<T> struct type: { i1, T }
    llvm::StructType *getOptionalType(llvm::Type *innerType);
    std::unordered_map<llvm::Type *, llvm::StructType *> optionalTypes_;

    /// Track which variables are optional and their inner LLVM type
    std::unordered_map<std::string, llvm::Type *> varOptionalTypes_;
};

#else

/// Stub IRGen when LLVM is not available
class IRGen {
public:
    IRGen(const std::string &, DiagnosticsEngine &diag) : diag_(diag) {}

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
