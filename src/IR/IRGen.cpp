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
            return it->second;
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
    auto oldVarStructTypes = varStructTypes_;
    auto oldVarEnumTypes = varEnumTypes_;
    namedValues_.clear();
    varStructTypes_.clear();
    varEnumTypes_.clear();

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
    varStructTypes_ = oldVarStructTypes;
    varEnumTypes_ = oldVarEnumTypes;

    return func;
}

llvm::Value *IRGen::visitVarDecl(VarDecl *node) {
    auto *func = builder_->GetInsertBlock()->getParent();

    // Check if init is a struct literal - reuse its alloca
    if (node->hasInit() &&
        node->getInit()->getKind() == ASTNode::NodeKind::StructLiteralExpr) {
        auto *structLit = static_cast<StructLiteralExpr *>(
            const_cast<Expr *>(node->getInit()));
        const auto &typeName = structLit->getTypeName();

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

    // Check if init is an enum case reference: let c = Color.Green
    if (node->hasInit() &&
        node->getInit()->getKind() == ASTNode::NodeKind::MemberExpr) {
        auto *memberExpr = static_cast<MemberExpr *>(
            const_cast<Expr *>(node->getInit()));
        if (memberExpr->getObject()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
            auto *ident = static_cast<IdentifierExpr *>(memberExpr->getObject());
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
        visit(const_cast<ASTNode *>(node->getBody()));

        // Increment
        if (!builder_->GetInsertBlock()->getTerminator()) {
            auto *cur = builder_->CreateLoad(builder_->getInt32Ty(), loopVar,
                                              node->getVarName());
            auto *next = builder_->CreateAdd(cur, builder_->getInt32(1), "for.inc");
            builder_->CreateStore(next, loopVar);
            builder_->CreateBr(condBB);
        }

        // Exit
        builder_->SetInsertPoint(exitBB);
        return nullptr;
    }

    // Fallback: just emit body once
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
    // Check for method call: obj.method(args)
    if (node->getCallee()->getKind() == ASTNode::NodeKind::MemberExpr) {
        auto *memberExpr = static_cast<MemberExpr *>(node->getCallee());
        const auto &methodName = memberExpr->getMember();

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
        }

        if (objAlloca && !structTypeName.empty()) {
            std::string mangledName = structTypeName + "_" + methodName;
            auto *callee = module_->getFunction(mangledName);
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

llvm::Value *IRGen::visitImplDecl(ImplDecl *node) {
    const auto &typeName = node->getTypeName();

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
        namedValues_.clear();
        varStructTypes_.clear();
        varEnumTypes_.clear();

        // Create allocas for parameters
        i = 0;
        for (auto &arg : func->args()) {
            auto *alloca = createEntryBlockAlloca(func, std::string(arg.getName()),
                                                   arg.getType());
            builder_->CreateStore(&arg, alloca);
            namedValues_[std::string(arg.getName())] = alloca;
            if (method->getParams()[i].isSelf) {
                varStructTypes_["self"] = typeName;
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
    }

    return nullptr;
}

llvm::Value *IRGen::visitEnumDecl(EnumDecl *node) {
    auto &caseMap = enumCases_[node->getName()];
    int tag = 0;
    for (auto &c : node->getCases()) {
        caseMap[c->getName()] = tag++;
    }
    return nullptr;
}

llvm::Value *IRGen::visitEnumCaseDecl(EnumCaseDecl *) {
    // Handled by visitEnumDecl
    return nullptr;
}

llvm::Value *IRGen::visitMatchExpr(MatchExpr *node) {
    auto *func = builder_->GetInsertBlock()->getParent();

    // Evaluate the subject
    auto *subjectVal = visit(const_cast<Expr *>(node->getSubject()));
    if (!subjectVal)
        return nullptr;

    // Find enum type name from subject (if it's an identifier)
    std::string enumTypeName;
    if (node->getSubject()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
        auto *ident = static_cast<const IdentifierExpr *>(node->getSubject());
        auto it = varEnumTypes_.find(ident->getName());
        if (it != varEnumTypes_.end())
            enumTypeName = it->second;
    }

    auto *mergeBB = llvm::BasicBlock::Create(*context_, "match.end", func);

    // Build arm basic blocks and determine case values
    struct ArmInfo {
        llvm::BasicBlock *bb;
        int tag;
        bool isDefault;
    };
    std::vector<ArmInfo> armInfos;
    int defaultIdx = -1;

    for (size_t i = 0; i < node->getArms().size(); ++i) {
        auto &arm = node->getArms()[i];
        auto *armBB = llvm::BasicBlock::Create(*context_,
            "match.arm." + std::to_string(i), func);

        if (arm.pattern == "_") {
            armInfos.push_back({armBB, 0, true});
            defaultIdx = static_cast<int>(i);
        } else {
            int tag = -1;
            // Try "EnumName.CaseName" pattern
            auto dotPos = arm.pattern.find('.');
            if (dotPos != std::string::npos) {
                auto eName = arm.pattern.substr(0, dotPos);
                auto cName = arm.pattern.substr(dotPos + 1);
                auto eIt = enumCases_.find(eName);
                if (eIt != enumCases_.end()) {
                    auto cIt = eIt->second.find(cName);
                    if (cIt != eIt->second.end())
                        tag = cIt->second;
                }
            } else {
                // Try integer literal
                char *end = nullptr;
                long val = std::strtol(arm.pattern.c_str(), &end, 10);
                if (end != arm.pattern.c_str() && *end == '\0') {
                    tag = static_cast<int>(val);
                } else {
                    // Try bare case name using subject's enum type
                    if (!enumTypeName.empty()) {
                        auto eIt = enumCases_.find(enumTypeName);
                        if (eIt != enumCases_.end()) {
                            auto cIt = eIt->second.find(arm.pattern);
                            if (cIt != eIt->second.end())
                                tag = cIt->second;
                        }
                    }
                }
            }
            armInfos.push_back({armBB, tag, false});
        }
    }

    // Create default block if no explicit wildcard
    llvm::BasicBlock *defaultBB;
    if (defaultIdx >= 0) {
        defaultBB = armInfos[defaultIdx].bb;
    } else {
        defaultBB = mergeBB;
    }

    // Count non-default cases
    unsigned numCases = 0;
    for (auto &info : armInfos) {
        if (!info.isDefault && info.tag >= 0)
            ++numCases;
    }

    auto *switchInst = builder_->CreateSwitch(subjectVal, defaultBB, numCases);

    for (auto &info : armInfos) {
        if (!info.isDefault && info.tag >= 0) {
            switchInst->addCase(builder_->getInt32(info.tag), info.bb);
        }
    }

    // Generate arm bodies
    for (size_t i = 0; i < node->getArms().size(); ++i) {
        auto &arm = node->getArms()[i];
        builder_->SetInsertPoint(armInfos[i].bb);
        visit(arm.body.get());
        if (!builder_->GetInsertBlock()->getTerminator())
            builder_->CreateBr(mergeBB);
    }

    builder_->SetInsertPoint(mergeBB);
    return nullptr;
}

llvm::Value *IRGen::visitRangeExpr(RangeExpr *) {
    // Range expressions are handled inline by visitForStmt
    return nullptr;
}

} // namespace liva

#endif // LIVA_HAS_LLVM
