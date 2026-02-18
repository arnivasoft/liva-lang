#include "liva/IR/IRGen.h"

#ifdef LIVA_HAS_LLVM

#include <llvm/IR/Intrinsics.h>

namespace liva {

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
    auto *panicFn = getOrPanic("liva_panic");
    auto *msg = builder_->CreateGlobalString("unwrap of nil optional value");
    builder_->CreateCall(panicFn, {msg});
    builder_->CreateUnreachable();

    builder_->SetInsertPoint(okBB);
    auto *valPtr = builder_->CreateStructGEP(optStructTy, optAlloca, 1);
    return builder_->CreateLoad(innerType, valPtr, "unwrap.val");
}

llvm::Value *IRGen::visitIdentifierExpr(IdentifierExpr *node) {
    // Check const values first (no alloca, direct constant)
    auto constIt = constValues_.find(node->getName());
    if (constIt != constValues_.end()) {
        return constIt->second;
    }

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

    // Struct operator overload dispatch
    if (node->getLHS()->getResolvedType() &&
        node->getLHS()->getResolvedType()->getKind() == TypeRepr::Kind::Named) {
        auto *namedTy = static_cast<const NamedTypeRepr *>(node->getLHS()->getResolvedType());
        const std::string &sName = namedTy->getName();
        const char *mName = nullptr;
        switch (node->getOp()) {
        case BinaryExpr::Op::Add: mName = "add"; break;
        case BinaryExpr::Op::Sub: mName = "sub"; break;
        case BinaryExpr::Op::Mul: mName = "mul"; break;
        case BinaryExpr::Op::Div: mName = "div"; break;
        case BinaryExpr::Op::Mod: mName = "mod"; break;
        case BinaryExpr::Op::Eq: case BinaryExpr::Op::NotEq: mName = "eq"; break;
        case BinaryExpr::Op::Less: case BinaryExpr::Op::LessEq:
        case BinaryExpr::Op::Greater: case BinaryExpr::Op::GreaterEq: mName = "less"; break;
        default: break;
        }
        if (mName) {
            std::string mangled = sName + "_" + mName;
            auto *callee = module_->getFunction(mangled);
            if (callee) {
                auto *func = builder_->GetInsertBlock()->getParent();
                auto *lhsAlloca = createEntryBlockAlloca(func, "op.lhs", lhs->getType());
                builder_->CreateStore(lhs, lhsAlloca);
                auto *result = builder_->CreateCall(callee, {lhsAlloca, rhs}, "op.result");
                switch (node->getOp()) {
                case BinaryExpr::Op::NotEq:
                    return builder_->CreateNot(result, "neq");
                case BinaryExpr::Op::GreaterEq:
                    return builder_->CreateNot(result, "geq");
                case BinaryExpr::Op::LessEq: {
                    auto *eqFn = module_->getFunction(sName + "_eq");
                    if (eqFn) {
                        auto *a2 = createEntryBlockAlloca(func, "op.lhs2", lhs->getType());
                        builder_->CreateStore(lhs, a2);
                        auto *eqR = builder_->CreateCall(eqFn, {a2, rhs}, "eq.r");
                        return builder_->CreateOr(result, eqR, "leq");
                    }
                    return result;
                }
                case BinaryExpr::Op::Greater: {
                    auto *eqFn = module_->getFunction(sName + "_eq");
                    if (eqFn) {
                        auto *a2 = createEntryBlockAlloca(func, "op.lhs2", lhs->getType());
                        builder_->CreateStore(lhs, a2);
                        auto *eqR = builder_->CreateCall(eqFn, {a2, rhs}, "eq.r");
                        auto *orR = builder_->CreateOr(result, eqR, "leq.tmp");
                        return builder_->CreateNot(orR, "gt");
                    }
                    return builder_->CreateNot(result, "gt");
                }
                default:
                    return result;
                }
            }
        }
    }

    // Check for string operations via resolved type
    bool isString = node->getLHS()->getResolvedType() &&
        node->getLHS()->getResolvedType()->getKind() == TypeRepr::Kind::String;

    if (isString) {
        switch (node->getOp()) {
        case BinaryExpr::Op::Add: {
            auto *concatFn = getOrPanic("liva_str_concat");
            auto *r = builder_->CreateCall(concatFn, {lhs, rhs}, "str.concat");
            trackStringTemp(r);
            return r;
        }
        case BinaryExpr::Op::Eq: {
            auto *equalFn = getOrPanic("liva_str_equal");
            auto *result = builder_->CreateCall(equalFn, {lhs, rhs});
            return builder_->CreateICmpNE(result,
                llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context_), 0));
        }
        case BinaryExpr::Op::NotEq: {
            auto *equalFn = getOrPanic("liva_str_equal");
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

// --- Free variable collection for closure capture ---

struct CapturedVar {
    std::string name;
    bool byRef;  // true if assigned inside closure body
};

static void collectFreeVarsImpl(const ASTNode *node,
                                const std::set<std::string> &params,
                                std::set<std::string> &locals,
                                std::set<std::string> &freeVars,
                                std::set<std::string> &mutatedVars) {
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
        collectFreeVarsImpl(e->getLHS(), params, locals, freeVars, mutatedVars);
        collectFreeVarsImpl(e->getRHS(), params, locals, freeVars, mutatedVars);
        break;
    }
    case NK::UnaryExpr: {
        auto *e = static_cast<const UnaryExpr *>(node);
        collectFreeVarsImpl(e->getOperand(), params, locals, freeVars, mutatedVars);
        break;
    }
    case NK::CallExpr: {
        auto *e = static_cast<const CallExpr *>(node);
        collectFreeVarsImpl(e->getCallee(), params, locals, freeVars, mutatedVars);
        for (auto &arg : e->getArgs())
            collectFreeVarsImpl(arg.get(), params, locals, freeVars, mutatedVars);
        break;
    }
    case NK::GroupExpr: {
        auto *e = static_cast<const GroupExpr *>(node);
        collectFreeVarsImpl(e->getExpr(), params, locals, freeVars, mutatedVars);
        break;
    }
    case NK::CastExpr: {
        auto *e = static_cast<const CastExpr *>(node);
        collectFreeVarsImpl(e->getExpr(), params, locals, freeVars, mutatedVars);
        break;
    }
    case NK::MemberExpr: {
        auto *e = static_cast<const MemberExpr *>(node);
        collectFreeVarsImpl(e->getObject(), params, locals, freeVars, mutatedVars);
        break;
    }
    case NK::IndexExpr: {
        auto *e = static_cast<const IndexExpr *>(node);
        collectFreeVarsImpl(e->getBase(), params, locals, freeVars, mutatedVars);
        collectFreeVarsImpl(e->getIndex(), params, locals, freeVars, mutatedVars);
        break;
    }
    case NK::AssignExpr: {
        auto *e = static_cast<const AssignExpr *>(node);
        // Track mutations for by-ref capture detection
        if (e->getTarget()->getKind() == NK::IdentifierExpr) {
            auto *id = static_cast<const IdentifierExpr *>(e->getTarget());
            mutatedVars.insert(id->getName());
        }
        collectFreeVarsImpl(e->getTarget(), params, locals, freeVars, mutatedVars);
        collectFreeVarsImpl(e->getValue(), params, locals, freeVars, mutatedVars);
        break;
    }
    case NK::UnwrapExpr: {
        auto *e = static_cast<const UnwrapExpr *>(node);
        collectFreeVarsImpl(e->getOperand(), params, locals, freeVars, mutatedVars);
        break;
    }
    case NK::RangeExpr: {
        auto *e = static_cast<const RangeExpr *>(node);
        collectFreeVarsImpl(e->getStart(), params, locals, freeVars, mutatedVars);
        collectFreeVarsImpl(e->getEnd(), params, locals, freeVars, mutatedVars);
        break;
    }
    case NK::MatchExpr: {
        auto *e = static_cast<const MatchExpr *>(node);
        collectFreeVarsImpl(e->getSubject(), params, locals, freeVars, mutatedVars);
        for (auto &arm : e->getArms())
            collectFreeVarsImpl(arm.body.get(), params, locals, freeVars, mutatedVars);
        break;
    }
    case NK::ArrayLiteralExpr: {
        auto *e = static_cast<const ArrayLiteralExpr *>(node);
        for (auto &el : e->getElements())
            collectFreeVarsImpl(el.get(), params, locals, freeVars, mutatedVars);
        break;
    }
    case NK::StructLiteralExpr: {
        auto *e = static_cast<const StructLiteralExpr *>(node);
        for (auto &f : e->getFields())
            collectFreeVarsImpl(f.value.get(), params, locals, freeVars, mutatedVars);
        break;
    }
    case NK::TryExpr: {
        auto *e = static_cast<const TryExpr *>(node);
        collectFreeVarsImpl(e->getOperand(), params, locals, freeVars, mutatedVars);
        break;
    }
    case NK::ClosureExpr: {
        auto *e = static_cast<const ClosureExpr *>(node);
        std::set<std::string> innerParams = params;
        for (auto &p : e->getParams())
            innerParams.insert(p.name);
        std::set<std::string> innerLocals = locals;
        collectFreeVarsImpl(e->getBody(), innerParams, innerLocals, freeVars, mutatedVars);
        break;
    }
    // --- Statements ---
    case NK::BlockStmt: {
        auto *s = static_cast<const BlockStmt *>(node);
        for (auto &stmt : s->getStatements())
            collectFreeVarsImpl(stmt.get(), params, locals, freeVars, mutatedVars);
        break;
    }
    case NK::ReturnStmt: {
        auto *s = static_cast<const ReturnStmt *>(node);
        if (s->hasValue())
            collectFreeVarsImpl(s->getValue(), params, locals, freeVars, mutatedVars);
        break;
    }
    case NK::ExprStmt: {
        auto *s = static_cast<const ExprStmt *>(node);
        collectFreeVarsImpl(s->getExpr(), params, locals, freeVars, mutatedVars);
        break;
    }
    case NK::IfStmt: {
        auto *s = static_cast<const IfStmt *>(node);
        collectFreeVarsImpl(s->getCondition(), params, locals, freeVars, mutatedVars);
        collectFreeVarsImpl(s->getThenBody(), params, locals, freeVars, mutatedVars);
        if (s->hasElse())
            collectFreeVarsImpl(s->getElseBody(), params, locals, freeVars, mutatedVars);
        break;
    }
    case NK::IfLetStmt: {
        auto *s = static_cast<const IfLetStmt *>(node);
        collectFreeVarsImpl(s->getOptionalExpr(), params, locals, freeVars, mutatedVars);
        std::set<std::string> thenLocals = locals;
        thenLocals.insert(s->getBindingName());
        collectFreeVarsImpl(s->getThenBody(), params, thenLocals, freeVars, mutatedVars);
        if (s->hasElse())
            collectFreeVarsImpl(s->getElseBody(), params, locals, freeVars, mutatedVars);
        break;
    }
    case NK::WhileStmt: {
        auto *s = static_cast<const WhileStmt *>(node);
        collectFreeVarsImpl(s->getCondition(), params, locals, freeVars, mutatedVars);
        collectFreeVarsImpl(s->getBody(), params, locals, freeVars, mutatedVars);
        break;
    }
    case NK::ForStmt: {
        auto *s = static_cast<const ForStmt *>(node);
        collectFreeVarsImpl(s->getIterable(), params, locals, freeVars, mutatedVars);
        std::set<std::string> bodyLocals = locals;
        bodyLocals.insert(s->getVarName());
        collectFreeVarsImpl(s->getBody(), params, bodyLocals, freeVars, mutatedVars);
        break;
    }
    // --- Declarations ---
    case NK::VarDecl: {
        auto *d = static_cast<const VarDecl *>(node);
        if (d->hasInit())
            collectFreeVarsImpl(d->getInit(), params, locals, freeVars, mutatedVars);
        locals.insert(d->getName());
        break;
    }
    // Skip: Literals, Break, Continue, FuncDecl, StructDecl, etc.
    default:
        break;
    }
}

static std::vector<CapturedVar> collectFreeVars(
    const ClosureExpr *closure,
    const std::unordered_map<std::string, llvm::AllocaInst *> &outerScope) {
    std::set<std::string> params;
    for (auto &p : closure->getParams())
        params.insert(p.name);
    std::set<std::string> locals;
    std::set<std::string> freeVars;
    std::set<std::string> mutatedVars;
    collectFreeVarsImpl(closure->getBody(), params, locals, freeVars, mutatedVars);
    // Filter: only keep names that exist in outerScope
    std::vector<CapturedVar> result;
    for (auto &name : freeVars) {
        if (outerScope.find(name) != outerScope.end())
            result.push_back({name, mutatedVars.count(name) > 0});
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
    auto savedDynArrayProtocol2 = varDynArrayProtocol_;
    auto savedMapTypes2 = varMapTypes_;
    auto savedSetTypes2 = varSetTypes_;
    auto savedOptionalTypes2 = varOptionalTypes_;
    auto savedFuncTypes2 = varFuncTypes_;
    auto savedProtocolTypes2 = varProtocolTypes_;
    auto savedResultTypes2 = varResultTypes_;
    auto savedFileTypes2 = varFileTypes_;
    auto savedFileOptTypes2 = varFileOptionalTypes_;
    auto savedTupleTypes2 = varTupleTypes_;
    auto savedRefTypes2 = varRefTypes_;
    auto savedMovedVars2 = movedVars_;
    auto *savedFuncRI2 = currentFuncResultInfo_;
    auto savedHeapStringVars2 = heapStringVars_;
    auto savedTempStrings2 = tempStrings_;

    // --- Capture analysis ---
    auto captured = collectFreeVars(node, namedValues_);

    auto *ptrTy = llvm::PointerType::getUnqual(*context_);

    // --- Build environment struct (in outer function context) ---
    llvm::Value *envPtr = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(ptrTy));
    llvm::StructType *envStructTy = nullptr;

    if (!captured.empty()) {
        std::vector<llvm::Type *> envFields;
        for (auto &cap : captured) {
            if (cap.byRef) {
                envFields.push_back(ptrTy);  // pointer to outer alloca
            } else {
                auto *alloca = namedValues_[cap.name];
                envFields.push_back(alloca->getAllocatedType());
            }
        }
        envStructTy = llvm::StructType::create(*context_, envFields,
            "__env_" + std::to_string(closureCounter_));

        auto *outerFunc = builder_->GetInsertBlock()->getParent();
        auto *envAlloca = createEntryBlockAlloca(outerFunc, "env", envStructTy);

        // Copy captured values / pointers into env struct
        for (unsigned i = 0; i < captured.size(); ++i) {
            auto *srcAlloca = namedValues_[captured[i].name];
            auto *gep = builder_->CreateStructGEP(envStructTy, envAlloca, i,
                                                  "env." + captured[i].name);
            if (captured[i].byRef) {
                // Store pointer to outer alloca (not value)
                builder_->CreateStore(srcAlloca, gep);
            } else {
                // Store value copy
                auto *val = builder_->CreateLoad(srcAlloca->getAllocatedType(), srcAlloca,
                                                 captured[i].name + ".cap");
                builder_->CreateStore(val, gep);
            }
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
    varDynArrayProtocol_.clear();
    varMapTypes_.clear();
    varSetTypes_.clear();
    varOptionalTypes_.clear();
    varFuncTypes_.clear();
    varProtocolTypes_.clear();
    varResultTypes_.clear();
    varFileTypes_.clear();
    varFileOptionalTypes_.clear();
    varTupleTypes_.clear();
    varRefTypes_.clear();
    movedVars_.clear();
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
                                                  "env." + captured[i].name);

            if (captured[i].byRef) {
                // By-ref: load ptr from env, create alloca holding ptr, register in varRefTypes_
                auto *outerPtr = builder_->CreateLoad(ptrTy, gep,
                                                      captured[i].name + ".ref");
                auto *ptrAlloca = createEntryBlockAlloca(func, captured[i].name, ptrTy);
                builder_->CreateStore(outerPtr, ptrAlloca);
                namedValues_[captured[i].name] = ptrAlloca;
                // Get outer variable's type for varRefTypes_
                auto *outerAlloca = savedValues[captured[i].name];
                varRefTypes_[captured[i].name] = outerAlloca->getAllocatedType();
            } else {
                // By-value: load value from env, store in local alloca
                auto *val = builder_->CreateLoad(fieldTy, gep, captured[i].name + ".env");
                auto *alloca = createEntryBlockAlloca(func, captured[i].name, fieldTy);
                builder_->CreateStore(val, alloca);
                namedValues_[captured[i].name] = alloca;
            }

            // Restore type tracking for captured variables
            auto stIt = savedStructTypes2.find(captured[i].name);
            if (stIt != savedStructTypes2.end())
                varStructTypes_[captured[i].name] = stIt->second;
            auto enIt = savedEnumTypes2.find(captured[i].name);
            if (enIt != savedEnumTypes2.end())
                varEnumTypes_[captured[i].name] = enIt->second;
            auto optIt = savedOptionalTypes2.find(captured[i].name);
            if (optIt != savedOptionalTypes2.end())
                varOptionalTypes_[captured[i].name] = optIt->second;
            auto fnIt = savedFuncTypes2.find(captured[i].name);
            if (fnIt != savedFuncTypes2.end())
                varFuncTypes_[captured[i].name] = fnIt->second;
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
    varDynArrayProtocol_ = savedDynArrayProtocol2;
    varMapTypes_ = savedMapTypes2;
    varSetTypes_ = savedSetTypes2;
    varOptionalTypes_ = savedOptionalTypes2;
    varFuncTypes_ = savedFuncTypes2;
    varProtocolTypes_ = savedProtocolTypes2;
    varResultTypes_ = savedResultTypes2;
    varFileTypes_ = savedFileTypes2;
    varFileOptionalTypes_ = savedFileOptTypes2;
    varTupleTypes_ = savedTupleTypes2;
    varRefTypes_ = savedRefTypes2;
    movedVars_ = savedMovedVars2;
    currentFuncResultInfo_ = savedFuncRI2;
    heapStringVars_ = savedHeapStringVars2;
    tempStrings_ = savedTempStrings2;

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
    // DynArray index on struct member field: self.grades[i]
    if (node->getBase()->getKind() == ASTNode::NodeKind::MemberExpr) {
        auto *memberBase = static_cast<MemberExpr *>(const_cast<Expr *>(node->getBase()));
        auto daInfo = resolveMemberDynArray(memberBase);
        if (daInfo) {
            auto *indexVal = visit(const_cast<Expr *>(node->getIndex()));
            if (!indexVal) return nullptr;
            auto *structTy = getDynArrayStructTy();
            auto *dataField = builder_->CreateStructGEP(structTy, daInfo->arrGEP, 0);
            auto *dataPtr = builder_->CreateLoad(llvm::PointerType::getUnqual(*context_), dataField);
            if (indexVal->getType()->isIntegerTy(32))
                indexVal = builder_->CreateSExt(indexVal, builder_->getInt64Ty(), "idx.ext");
            auto *lenField = builder_->CreateStructGEP(structTy, daInfo->arrGEP, 1);
            auto *lenVal = builder_->CreateLoad(builder_->getInt64Ty(), lenField, "mda.len");
            emitBoundsCheck(indexVal, lenVal);
            auto *elemPtr = builder_->CreateGEP(daInfo->elementType, dataPtr, indexVal, "mda.elem");
            return builder_->CreateLoad(daInfo->elementType, elemPtr, "mda.val");
        }
    }

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
            auto *sliceResult = builder_->CreateCall(
                llvm::FunctionType::get(ptrTy, {ptrTy, builder_->getInt64Ty(), builder_->getInt64Ty()}, false),
                subFn, {strVal, startVal, sliceLen}, "str.slice");
            trackStringTemp(sliceResult);
            return sliceResult;
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
        auto *charResult = builder_->CreateCall(
            llvm::FunctionType::get(ptrTy, {ptrTy, builder_->getInt64Ty(), builder_->getInt64Ty()}, false),
            subFn, {strVal, indexVal, builder_->getInt64(1)}, "str.char");
        trackStringTemp(charResult);
        return charResult;
    }

    return nullptr;
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

llvm::Value *IRGen::visitAwaitExpr(AwaitExpr *node) {
    auto *childTask = visit(node->getOperand());
    if (!childTask) return nullptr;

    auto *ptrTy = llvm::PointerType::getUnqual(*context_);
    auto *func = builder_->GetInsertBlock()->getParent();

    // Check if child task is already done
    auto *isDoneFn = getOrPanic("liva_task_is_done");
    auto *doneI8 = builder_->CreateCall(isDoneFn, {childTask}, "done.i8");
    auto *isDone = builder_->CreateICmpNE(doneI8, builder_->getInt8(0), "is.done");

    auto *awaitReadyBB = llvm::BasicBlock::Create(*context_, "await.ready", func);
    auto *awaitSuspendBB = llvm::BasicBlock::Create(*context_, "await.suspend", func);
    builder_->CreateCondBr(isDone, awaitReadyBB, awaitSuspendBB);

    // await.suspend: set parent and suspend
    builder_->SetInsertPoint(awaitSuspendBB);
    auto *curTask = builder_->CreateLoad(ptrTy, currentCoroTask_, "cur.task");
    auto *setParentFn = getOrPanic("liva_task_set_parent");
    builder_->CreateCall(setParentFn, {childTask, curTask});

    auto *coroSuspendFn = llvm::Intrinsic::getOrInsertDeclaration(module_.get(),
        llvm::Intrinsic::coro_suspend);
    auto *noneToken = llvm::ConstantTokenNone::get(*context_);
    auto *suspVal = builder_->CreateCall(coroSuspendFn,
        {noneToken, builder_->getFalse()}, "sus");
    auto *sw = builder_->CreateSwitch(suspVal, currentCoroSuspendBB_, 2);
    sw->addCase(builder_->getInt8(0), awaitReadyBB);
    sw->addCase(builder_->getInt8(1), currentCoroCleanupBB_);

    // await.ready: extract result from child's promise
    builder_->SetInsertPoint(awaitReadyBB);

    // Determine the result type from the await operand's resolved type
    llvm::Type *resultType = nullptr;
    auto *operandType = node->getOperand()->getResolvedType();
    if (operandType && operandType->getKind() == TypeRepr::Kind::Generic) {
        auto *genType = static_cast<const GenericTypeRepr *>(operandType);
        if (genType->getBaseName() == "Task" && !genType->getTypeArgs().empty()) {
            resultType = toLLVMType(genType->getTypeArgs()[0].get());
        }
    }
    if (!resultType) {
        resultType = asyncDeclaredRetType_ ? asyncDeclaredRetType_ : builder_->getInt1Ty();
    }

    if (resultType->isVoidTy()) {
        resultType = builder_->getInt1Ty();
    }

    // Get child handle and read promise
    auto *getHandleFn = getOrPanic("liva_task_get_handle");
    auto *childHdl = builder_->CreateCall(getHandleFn, {childTask}, "child.hdl");

    auto *coroPromiseFn = llvm::Intrinsic::getOrInsertDeclaration(module_.get(),
        llvm::Intrinsic::coro_promise);
    auto &dl = module_->getDataLayout();
    unsigned align = dl.getABITypeAlign(resultType).value();
    auto *promisePtr = builder_->CreateCall(coroPromiseFn,
        {childHdl, builder_->getInt32(align), builder_->getFalse()}, "child.promise");
    auto *result = builder_->CreateLoad(resultType, promisePtr, "await.result");

    // Cleanup child
    auto *coroDestroyFn = getOrPanic("liva_coro_destroy");
    builder_->CreateCall(coroDestroyFn, {childHdl});
    auto *taskDestroyFn = getOrPanic("liva_task_destroy");
    builder_->CreateCall(taskDestroyFn, {childTask});

    return result;
}

llvm::Value *IRGen::visitComptimeExpr(ComptimeExpr *node) {
    // Comptime block is validated at sema time; at IRGen we simply
    // evaluate the block's statements (const/var → constValues_) and
    // return the last expression's value. Since all values are compile-time
    // constants, LLVM will fold them automatically.
    auto &stmts = node->getBody()->getStatements();
    llvm::Value *result = nullptr;

    // Save const values that might be shadowed
    std::vector<std::pair<std::string, llvm::Constant *>> savedConsts;

    for (size_t i = 0; i < stmts.size(); ++i) {
        auto *stmt = stmts[i].get();
        if (auto *varDecl = dynamic_cast<VarDecl *>(stmt)) {
            if (varDecl->hasInit()) {
                auto *val = visit(const_cast<Expr *>(varDecl->getInit()));
                if (auto *constVal = llvm::dyn_cast_or_null<llvm::Constant>(val)) {
                    // Save old value if exists
                    auto it = constValues_.find(varDecl->getName());
                    if (it != constValues_.end())
                        savedConsts.push_back({varDecl->getName(), it->second});
                    constValues_[varDecl->getName()] = constVal;
                }
            }
        } else if (auto *exprStmt = dynamic_cast<ExprStmt *>(stmt)) {
            result = visit(exprStmt->getExpr());
        } else {
            visit(stmt);
        }
    }

    // Restore saved const values
    for (auto &[name, val] : savedConsts) {
        constValues_[name] = val;
    }

    return result;
}

} // namespace liva

#endif // LIVA_HAS_LLVM
