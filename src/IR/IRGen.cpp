#include "liva/IR/IRGen.h"

#ifdef LIVA_HAS_LLVM

#include <llvm/IR/Verifier.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/TargetParser/Host.h>
#include <llvm/TargetParser/Triple.h>
#include <fstream>

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
    // printf declaration
    auto *printfType = llvm::FunctionType::get(
        builder_->getInt32Ty(), {llvm::PointerType::getUnqual(*context_)}, true);
    module_->getOrInsertFunction("printf", printfType);
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
        auto it = structTypes_.find(named->getName());
        if (it != structTypes_.end())
            return llvm::PointerType::getUnqual(*context_);
        return llvm::PointerType::getUnqual(*context_);
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
    return tmpBuilder.CreateAlloca(type, nullptr, name);
}

llvm::Value *IRGen::visitFuncDecl(FuncDecl *node) {
    // Build function type
    std::vector<llvm::Type *> paramTypes;
    for (auto &param : node->getParams()) {
        paramTypes.push_back(toLLVMType(param.type.get()));
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
    namedValues_.clear();

    // Create allocas for parameters
    i = 0;
    for (auto &arg : func->args()) {
        auto *alloca = createEntryBlockAlloca(func, std::string(arg.getName()), arg.getType());
        builder_->CreateStore(&arg, alloca);
        namedValues_[std::string(arg.getName())] = alloca;
        ++i;
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

    return func;
}

llvm::Value *IRGen::visitVarDecl(VarDecl *node) {
    auto *func = builder_->GetInsertBlock()->getParent();
    auto *type = toLLVMType(node->getType());
    auto *alloca = createEntryBlockAlloca(func, node->getName(), type);

    if (node->hasInit()) {
        auto *initVal = visit(const_cast<Expr *>(node->getInit()));
        if (initVal)
            builder_->CreateStore(initVal, alloca);
    }

    namedValues_[node->getName()] = alloca;
    return alloca;
}

llvm::Value *IRGen::visitStructDecl(StructDecl *node) {
    std::vector<llvm::Type *> fieldTypes;
    for (auto &field : node->getFields()) {
        fieldTypes.push_back(toLLVMType(field->getType()));
    }

    auto *structType = llvm::StructType::create(*context_, fieldTypes, node->getName());
    structTypes_[node->getName()] = structType;
    return nullptr;
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
    visit(const_cast<ASTNode *>(node->getBody()));
    if (!builder_->GetInsertBlock()->getTerminator())
        builder_->CreateBr(condBB);

    // Exit
    builder_->SetInsertPoint(exitBB);
    return nullptr;
}

llvm::Value *IRGen::visitForStmt(ForStmt *node) {
    // For-in is syntactic sugar; generate a while loop pattern
    // For MVP, we support range-like patterns: for i in start..end
    auto *func = builder_->GetInsertBlock()->getParent();

    // TODO: implement proper for-in with iterators
    // For now, just emit the body once as a placeholder
    visit(const_cast<ASTNode *>(node->getBody()));
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
    return builder_->CreateGlobalString(node->getValue());
}

llvm::Value *IRGen::visitIdentifierExpr(IdentifierExpr *node) {
    auto it = namedValues_.find(node->getName());
    if (it != namedValues_.end()) {
        return builder_->CreateLoad(it->second->getAllocatedType(), it->second,
                                     node->getName());
    }
    return nullptr;
}

llvm::Value *IRGen::visitBinaryExpr(BinaryExpr *node) {
    auto *lhs = visit(node->getLHS());
    auto *rhs = visit(node->getRHS());
    if (!lhs || !rhs)
        return nullptr;

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
    }
    return nullptr;
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
    // Get function name
    std::string funcName;
    if (node->getCallee()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
        auto *ident = static_cast<IdentifierExpr *>(node->getCallee());
        funcName = ident->getName();
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

        auto *arg = visit(node->getArgs()[0].get());
        if (!arg)
            return nullptr;

        // Determine format string based on argument type
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

        if (funcName == "println")
            fmt += "\n";

        auto *fmtStr = builder_->CreateGlobalString(fmt);
        return builder_->CreateCall(printfFunc, {fmtStr, arg});
    }

    // Look up the function
    auto *callee = module_->getFunction(funcName);
    if (!callee)
        return nullptr;

    std::vector<llvm::Value *> args;
    for (auto &arg : node->getArgs()) {
        auto *val = visit(arg.get());
        if (!val)
            return nullptr;
        args.push_back(val);
    }

    if (callee->getReturnType()->isVoidTy())
        return builder_->CreateCall(callee, args);
    return builder_->CreateCall(callee, args, "calltmp");
}

llvm::Value *IRGen::visitAssignExpr(AssignExpr *node) {
    auto *val = visit(node->getValue());
    if (!val)
        return nullptr;

    if (node->getTarget()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
        auto *ident = static_cast<IdentifierExpr *>(node->getTarget());
        auto it = namedValues_.find(ident->getName());
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
    // TODO: implement struct member access
    return nullptr;
}

} // namespace liva

#endif // LIVA_HAS_LLVM
