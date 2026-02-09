#pragma once

#include "liva/AST/ASTVisitor.h"
#include "liva/AST/Decl.h"
#include "liva/Common/Diagnostics.h"

#ifdef LIVA_HAS_LLVM
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
    llvm::Value *visitRangeExpr(RangeExpr *node);

private:
    /// Convert Liva type to LLVM type
    llvm::Type *toLLVMType(const TypeRepr *type);

    /// Create runtime function declarations (print, println)
    void createRuntimeDecls();

    /// Get or create a named value (variable)
    llvm::AllocaInst *createEntryBlockAlloca(llvm::Function *func,
                                              const std::string &name,
                                              llvm::Type *type);

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
