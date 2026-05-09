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
        auto it = vars_.namedValues.find(ident->getName());
        if (it != vars_.namedValues.end()) optAlloca = it->second;
        auto optIt = vars_.varOptionalTypes.find(ident->getName());
        if (optIt != vars_.varOptionalTypes.end()) innerType = optIt->second;
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

    auto it = vars_.namedValues.find(node->getName());
    if (it != vars_.namedValues.end()) {
        // Reference variable: double indirection (load ptr, then load through ptr)
        auto refIt = vars_.varRefTypes.find(node->getName());
        if (refIt != vars_.varRefTypes.end()) {
            auto *ptr = builder_->CreateLoad(
                llvm::PointerType::getUnqual(*context_), it->second, node->getName() + ".ptr");
            return builder_->CreateLoad(refIt->second, ptr, node->getName());
        }
        return builder_->CreateLoad(it->second->getAllocatedType(), it->second,
                                     node->getName());
    }
    diag_.report(node->getStartLoc(), DiagID::err_irgen_unknown_variable, node->getName());
    return nullptr;
}

llvm::Value *IRGen::visitBinaryExpr(BinaryExpr *node) {
    if (diBuilder_) emitDebugLocation(node->getStartLoc());
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
        case BinaryExpr::Op::Less:
        case BinaryExpr::Op::LessEq:
        case BinaryExpr::Op::Greater:
        case BinaryExpr::Op::GreaterEq: {
            // Lexicographic ordering via runtime strcmp; compare the
            // returned i32 (negative / zero / positive) to 0.
            auto *cmpFn = getOrPanic("liva_str_compare");
            auto *cmp = builder_->CreateCall(cmpFn, {lhs, rhs}, "str.cmp");
            auto *zero = llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context_), 0);
            switch (node->getOp()) {
            case BinaryExpr::Op::Less:
                return builder_->CreateICmpSLT(cmp, zero, "strlt");
            case BinaryExpr::Op::LessEq:
                return builder_->CreateICmpSLE(cmp, zero, "strle");
            case BinaryExpr::Op::Greater:
                return builder_->CreateICmpSGT(cmp, zero, "strgt");
            case BinaryExpr::Op::GreaterEq:
                return builder_->CreateICmpSGE(cmp, zero, "strge");
            default: break;
            }
            return cmp; // unreachable
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
        auto it = vars_.namedValues.find(ident->getName());
        if (it != vars_.namedValues.end()) optAlloca = it->second;
        auto optIt = vars_.varOptionalTypes.find(ident->getName());
        if (optIt != vars_.varOptionalTypes.end()) innerType = optIt->second;
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
    if (node->getObject()->getKind() != ASTNode::NodeKind::IdentifierExpr) {
        diag_.report(node->getStartLoc(), DiagID::err_irgen_optional_chain_failed, node->getMember());
        return nullptr;
    }

    auto *ident = static_cast<IdentifierExpr *>(node->getObject());
    auto optIt = vars_.varOptionalTypes.find(ident->getName());
    if (optIt == vars_.varOptionalTypes.end()) {
        diag_.report(node->getStartLoc(), DiagID::err_irgen_optional_chain_failed, node->getMember());
        return nullptr;
    }
    auto nvIt = vars_.namedValues.find(ident->getName());
    if (nvIt == vars_.namedValues.end()) {
        diag_.report(node->getStartLoc(), DiagID::err_irgen_optional_chain_failed, node->getMember());
        return nullptr;
    }

    auto *optAlloca = nvIt->second;
    auto *innerType = optIt->second; // The struct type inside Optional

    // Find the struct type name for field lookup
    auto stIt = vars_.varStructTypes.find(ident->getName());
    if (stIt == vars_.varStructTypes.end()) {
        diag_.report(node->getStartLoc(), DiagID::err_irgen_optional_chain_failed, node->getMember());
        return nullptr;
    }
    const auto &structTypeName = stIt->second;

    auto stTyIt = structTypes_.find(structTypeName);
    if (stTyIt == structTypes_.end()) {
        diag_.report(node->getStartLoc(), DiagID::err_irgen_optional_chain_failed, node->getMember());
        return nullptr;
    }
    auto *structTy = stTyIt->second;

    int fieldIdx = getStructFieldIndex(structTypeName, node->getMember());
    if (fieldIdx < 0) {
        diag_.report(node->getStartLoc(), DiagID::err_irgen_optional_chain_failed, node->getMember());
        return nullptr;
    }

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

    // Determine signedness from the source AST type. Unsigned source ints
    // (u8/u16/u32/u64) widen with zero-extend; signed (i8/i16/i32/i64) and
    // bool widen with sign-extend. Falls back to sign-extend for unknown
    // sources to preserve the prior default behaviour.
    auto *srcRepr = node->getExpr()->getResolvedType();
    bool srcIsUnsigned = srcRepr && srcRepr->isUnsignedInteger();

    // Integer to integer
    if (val->getType()->isIntegerTy() && targetType->isIntegerTy()) {
        unsigned srcBits = val->getType()->getIntegerBitWidth();
        unsigned dstBits = targetType->getIntegerBitWidth();
        if (srcBits < dstBits) {
            return srcIsUnsigned
                ? builder_->CreateZExt(val, targetType, "zext")
                : builder_->CreateSExt(val, targetType, "sext");
        }
        if (srcBits == dstBits) return val;
        return builder_->CreateTrunc(val, targetType, "trunc");
    }

    // Integer to float
    if (val->getType()->isIntegerTy() && targetType->isFloatingPointTy())
        return srcIsUnsigned
            ? builder_->CreateUIToFP(val, targetType, "uitofp")
            : builder_->CreateSIToFP(val, targetType, "sitofp");

    // Float to integer (signedness of dst from target type)
    auto *dstRepr = node->getTargetType();
    bool dstIsUnsigned = dstRepr && dstRepr->isUnsignedInteger();
    if (val->getType()->isFloatingPointTy() && targetType->isIntegerTy())
        return dstIsUnsigned
            ? builder_->CreateFPToUI(val, targetType, "fptoui")
            : builder_->CreateFPToSI(val, targetType, "fptosi");

    // Float to float
    if (val->getType()->isFloatingPointTy() && targetType->isFloatingPointTy()) {
        if (val->getType() == llvm::Type::getFloatTy(*context_))
            return builder_->CreateFPExt(val, targetType, "fpext");
        return builder_->CreateFPTrunc(val, targetType, "fptrunc");
    }

    // Optional cast (as?) for class types — vtable-based runtime check
    if (node->isOptional() && val->getType()->isPointerTy()) {
        auto *targetTypeRepr = node->getTargetType();
        if (targetTypeRepr->getKind() == TypeRepr::Kind::Named) {
            auto *named = static_cast<const NamedTypeRepr *>(targetTypeRepr);
            const std::string &targetName = named->getName();
            auto ctIt = classTypes_.find(targetName);
            if (ctIt != classTypes_.end() && classVtables_.count(targetName)) {
                // Collect target + descendants' vtables (is-a check)
                std::vector<llvm::GlobalVariable *> matchingVtables;
                for (auto &[cn, vtGlobal] : classVtables_) {
                    std::string cur = cn;
                    while (!cur.empty()) {
                        if (cur == targetName) {
                            matchingVtables.push_back(vtGlobal);
                            break;
                        }
                        auto pit = classParent_.find(cur);
                        if (pit != classParent_.end()) cur = pit->second;
                        else break;
                    }
                }

                auto *ptrTy = llvm::PointerType::getUnqual(*context_);
                auto *vtableGEP = builder_->CreateStructGEP(
                    ctIt->second, val, 0, "cast.vtable.gep");
                auto *objVtable = builder_->CreateLoad(ptrTy, vtableGEP, "cast.vtable");

                llvm::Value *isMatch = builder_->getFalse();
                for (auto *vt : matchingVtables) {
                    auto *cmp = builder_->CreateICmpEQ(objVtable, vt, "cast.cmp");
                    isMatch = builder_->CreateOr(isMatch, cmp, "cast.or");
                }
                return builder_->CreateSelect(
                    isMatch, val,
                    llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(ptrTy)),
                    "as.optional");
            }
        }
    }

    return val;
}

llvm::Value *IRGen::visitIsExpr(IsExpr *node) {
    auto *val = visit(const_cast<Expr *>(node->getExpr()));
    if (!val || !val->getType()->isPointerTy())
        return builder_->getFalse();

    auto *targetTypeRepr = node->getTargetType();
    if (targetTypeRepr->getKind() != TypeRepr::Kind::Named)
        return builder_->getFalse();

    auto *named = static_cast<const NamedTypeRepr *>(targetTypeRepr);
    const std::string &targetName = named->getName();

    auto vtIt = classVtables_.find(targetName);
    auto ctIt = classTypes_.find(targetName);
    if (vtIt == classVtables_.end() || ctIt == classTypes_.end())
        return builder_->getFalse();

    // Collect all vtables for target and its descendants
    // obj is Animal → true for Animal, Dog (: Animal), Cat (: Animal), etc.
    std::vector<llvm::GlobalVariable *> matchingVtables;
    for (auto &[cn, vtGlobal] : classVtables_) {
        // Is cn == target or does cn's parent chain contain target?
        std::string cur = cn;
        while (!cur.empty()) {
            if (cur == targetName) {
                matchingVtables.push_back(vtGlobal);
                break;
            }
            auto pit = classParent_.find(cur);
            if (pit != classParent_.end()) cur = pit->second;
            else break;
        }
    }

    auto *ptrTy = llvm::PointerType::getUnqual(*context_);
    // Load obj's vtable ptr (index 0 in any class struct — layout invariant)
    auto *vtableGEP = builder_->CreateStructGEP(
        ctIt->second, val, 0, "is.vtable.gep");
    auto *objVtable = builder_->CreateLoad(ptrTy, vtableGEP, "is.vtable");

    // OR all comparisons: objVtable == vt1 || objVtable == vt2 || ...
    llvm::Value *result = builder_->getFalse();
    for (auto *vt : matchingVtables) {
        auto *cmp = builder_->CreateICmpEQ(objVtable, vt, "is.cmp");
        result = builder_->CreateOr(result, cmp, "is.or");
    }
    return result;
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
    // Save outer scope state: vars_ snapshotted via RAII; non-VarState bits explicit.
    auto savedBlock = builder_->GetInsertBlock();
    auto savedDebugLoc = builder_->getCurrentDebugLocation();
    auto guard = pushVarState();

    // --- Capture analysis ---
    auto captured = collectFreeVars(node, vars_.namedValues);

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
                auto *alloca = vars_.namedValues[cap.name];
                envFields.push_back(alloca->getAllocatedType());
            }
        }
        envStructTy = llvm::StructType::create(*context_, envFields,
            "__env_" + std::to_string(closureCounter_));

        auto *outerFunc = builder_->GetInsertBlock()->getParent();
        auto *envAlloca = createEntryBlockAlloca(outerFunc, "env", envStructTy);

        // Copy captured values / pointers into env struct
        for (unsigned i = 0; i < captured.size(); ++i) {
            auto *srcAlloca = vars_.namedValues[captured[i].name];
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

    // Attach debug info subprogram to closure function
    if (diBuilder_) {
        auto *funcDbgType = createFunctionDebugType();
        unsigned lineNo = node->getStartLoc().isValid() ? node->getStartLoc().line : 0;
        auto *sp = diBuilder_->createFunction(
            diFile_, name, name, diFile_, lineNo,
            funcDbgType, lineNo,
            llvm::DINode::FlagPrototyped,
            llvm::DISubprogram::SPFlagDefinition);
        func->setSubprogram(sp);
    }

    // --- Generate closure body ---
    auto *entry = llvm::BasicBlock::Create(*context_, "entry", func);
    builder_->SetInsertPoint(entry);
    // Reset debug location to avoid outer scope leaking into closure
    builder_->SetCurrentDebugLocation(llvm::DebugLoc());
    // Closure body runs in a fresh scope; outer state is held in `guard.saved()`
    // for partial restore of captured-variable type metadata below.
    vars_ = VarState{};

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
                // By-ref: load ptr from env, create alloca holding ptr, register in vars_.varRefTypes
                auto *outerPtr = builder_->CreateLoad(ptrTy, gep,
                                                      captured[i].name + ".ref");
                auto *ptrAlloca = createEntryBlockAlloca(func, captured[i].name, ptrTy);
                builder_->CreateStore(outerPtr, ptrAlloca);
                vars_.namedValues[captured[i].name] = ptrAlloca;
                // Get outer variable's type for vars_.varRefTypes
                auto *outerAlloca = guard.saved().namedValues.at(captured[i].name);
                vars_.varRefTypes[captured[i].name] = outerAlloca->getAllocatedType();
            } else {
                // By-value: load value from env, store in local alloca
                auto *val = builder_->CreateLoad(fieldTy, gep, captured[i].name + ".env");
                auto *alloca = createEntryBlockAlloca(func, captured[i].name, fieldTy);
                builder_->CreateStore(val, alloca);
                vars_.namedValues[captured[i].name] = alloca;
            }

            // Restore type tracking for captured variables from outer snapshot
            const auto &outer = guard.saved();
            auto stIt = outer.varStructTypes.find(captured[i].name);
            if (stIt != outer.varStructTypes.end())
                vars_.varStructTypes[captured[i].name] = stIt->second;
            auto enIt = outer.varEnumTypes.find(captured[i].name);
            if (enIt != outer.varEnumTypes.end())
                vars_.varEnumTypes[captured[i].name] = enIt->second;
            auto optIt = outer.varOptionalTypes.find(captured[i].name);
            if (optIt != outer.varOptionalTypes.end())
                vars_.varOptionalTypes[captured[i].name] = optIt->second;
            auto fnIt = outer.varFuncTypes.find(captured[i].name);
            if (fnIt != outer.varFuncTypes.end())
                vars_.varFuncTypes[captured[i].name] = fnIt->second;
        }
    }

    // Bind user parameters
    unsigned pi = 0;
    for (; argIt != func->arg_end(); ++argIt, ++pi) {
        auto &param = node->getParams()[pi];
        argIt->setName(param.name);
        auto *alloca = createEntryBlockAlloca(func, param.name, argIt->getType());
        builder_->CreateStore(&*argIt, alloca);
        vars_.namedValues[param.name] = alloca;
    }

    // Generate body
    visit(node->getBody());

    if (!builder_->GetInsertBlock()->getTerminator()) {
        if (retTy->isVoidTy()) builder_->CreateRetVoid();
    }

    // --- Restore outer scope and build closure object ---
    builder_->SetInsertPoint(savedBlock);
    builder_->SetCurrentDebugLocation(savedDebugLoc);
    // vars_ restored automatically by `guard` dtor when this function returns.

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
    if (!tupleTypeRepr) {
        diag_.report(node->getStartLoc(), DiagID::err_irgen_tuple_type_failed);
        return nullptr;
    }
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
    // Class subscript: obj[i] → ClassName_subscript(obj, i)
    if (node->getBase()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
        auto *baseIdent = static_cast<const IdentifierExpr *>(node->getBase());
        auto cvIt = vars_.varClassTypes.find(baseIdent->getName());
        if (cvIt != vars_.varClassTypes.end()) {
            std::string subName = cvIt->second + "_subscript";
            auto *subFn = module_->getFunction(subName);
            if (subFn) {
                auto *ptrTy = llvm::PointerType::getUnqual(*context_);
                auto nvIt = vars_.namedValues.find(baseIdent->getName());
                if (nvIt != vars_.namedValues.end()) {
                    auto *selfVal = nvIt->second;
                    llvm::Value *selfLoaded = selfVal;
                    if (selfVal->getAllocatedType()->isPointerTy()) {
                        selfLoaded = builder_->CreateLoad(ptrTy, selfVal, "sub.self");
                    }
                    auto *idxVal = visit(const_cast<Expr *>(node->getIndex()));
                    if (idxVal) {
                        return builder_->CreateCall(subFn, {selfLoaded, idxVal}, "sub.call");
                    }
                }
            }
        }
    }

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

    if (node->getBase()->getKind() != ASTNode::NodeKind::IdentifierExpr) {
        diag_.report(node->getStartLoc(), DiagID::err_irgen_subscript_failed, "non-identifier");
        return nullptr;
    }
    auto *ident = static_cast<const IdentifierExpr *>(node->getBase());

    // === Range slicing: arr[1..3] or s[1..3] or arr[1..=3] ===
    if (node->getIndex()->getKind() == ASTNode::NodeKind::RangeExpr) {
        auto *rangeExpr = static_cast<RangeExpr *>(const_cast<Expr *>(node->getIndex()));
        auto *startVal = visit(const_cast<Expr *>(rangeExpr->getStart()));
        auto *endVal = visit(const_cast<Expr *>(rangeExpr->getEnd()));
        if (!startVal || !endVal) return nullptr;
        if (startVal->getType()->isIntegerTy(32))
            startVal = builder_->CreateSExt(startVal, builder_->getInt64Ty(), "slice.start");
        if (endVal->getType()->isIntegerTy(32))
            endVal = builder_->CreateSExt(endVal, builder_->getInt64Ty(), "slice.end");
        // Inclusive range: end is one past the last index → bump end by 1
        if (rangeExpr->isInclusive())
            endVal = builder_->CreateAdd(endVal, builder_->getInt64(1), "slice.end.incl");
        auto *sliceLen = builder_->CreateSub(endVal, startVal, "slice.len");

        // String slicing: s[1..3] -> liva_str_substring(s, start, end-start)
        auto daIt = vars_.varDynArrayTypes.find(ident->getName());
        if (daIt == vars_.varDynArrayTypes.end() && vars_.varArrayTypes.find(ident->getName()) == vars_.varArrayTypes.end()) {
            auto allocaIt = vars_.namedValues.find(ident->getName());
            if (allocaIt == vars_.namedValues.end()) {
                diag_.report(node->getStartLoc(), DiagID::err_irgen_subscript_failed, ident->getName());
                return nullptr;
            }
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
        if (daIt != vars_.varDynArrayTypes.end()) {
            auto allocaIt = vars_.namedValues.find(ident->getName());
            if (allocaIt == vars_.namedValues.end()) {
                diag_.report(node->getStartLoc(), DiagID::err_irgen_subscript_failed, ident->getName());
                return nullptr;
            }
            auto *arrAlloca = allocaIt->second;
            auto *structTy = getDynArrayStructTy();
            auto *elemType = daIt->second.elementType;
            auto elemSize = builder_->getInt64(daIt->second.elemSize);
            auto *ptrTy = llvm::PointerType::getUnqual(*context_);
            // Read array length for bounds check
            auto *lenField = builder_->CreateStructGEP(structTy, arrAlloca, 1);
            auto *arrLen = builder_->CreateLoad(builder_->getInt64Ty(), lenField, "arr.len");
            emitSliceBoundsCheck(startVal, endVal, arrLen);
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
    auto daIt = vars_.varDynArrayTypes.find(ident->getName());
    if (daIt != vars_.varDynArrayTypes.end()) {
        auto allocaIt = vars_.namedValues.find(ident->getName());
        if (allocaIt == vars_.namedValues.end()) {
            diag_.report(node->getStartLoc(), DiagID::err_irgen_subscript_failed, ident->getName());
            return nullptr;
        }
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

    auto arrIt = vars_.varArrayTypes.find(ident->getName());
    if (arrIt != vars_.varArrayTypes.end()) {
        auto allocaIt = vars_.namedValues.find(ident->getName());
        if (allocaIt == vars_.namedValues.end()) return nullptr;
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
    auto allocaIt = vars_.namedValues.find(ident->getName());
    if (allocaIt != vars_.namedValues.end()) {
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
    if (!vars_.currentFuncResultInfo) {
        // If not in a Result-returning function, just evaluate the operand
        return visit(node->getOperand());
    }

    // Evaluate operand (should return a Result value)
    auto *resultVal = visit(node->getOperand());
    if (!resultVal) return nullptr;

    // Store to a temp alloca to extract tag
    auto *resTy = getResultType(vars_.currentFuncResultInfo->okType, vars_.currentFuncResultInfo->errType);
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
    auto *errVal = builder_->CreateLoad(vars_.currentFuncResultInfo->errType, errPayloadPtr, "try.err.val");
    auto *errResult = emitResultErr(vars_.currentFuncResultInfo->okType,
                                     vars_.currentFuncResultInfo->errType, errVal);
    builder_->CreateRet(errResult);

    // Ok path: extract the Ok value and continue
    builder_->SetInsertPoint(okBB);
    auto *okPayloadPtr = builder_->CreateStructGEP(resTy, tmpAlloca, 1, "try.ok.payload");
    return builder_->CreateLoad(vars_.currentFuncResultInfo->okType, okPayloadPtr, "try.ok.val");
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
        auto it = vars_.namedValues.find(ident->getName());
        if (it != vars_.namedValues.end()) {
            // If x is itself a ref, load the pointer (pass-through)
            auto refIt = vars_.varRefTypes.find(ident->getName());
            if (refIt != vars_.varRefTypes.end()) {
                return builder_->CreateLoad(
                    llvm::PointerType::getUnqual(*context_), it->second,
                    ident->getName() + ".ref");
            }
            // Normal variable: return alloca address
            return it->second;
        }
        diag_.report(node->getStartLoc(), DiagID::err_irgen_ref_target_not_found, ident->getName());
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

llvm::Value *IRGen::visitYieldExpr(YieldExpr *node) {
    auto *value = visit(node->getValue());
    if (!value) return nullptr;

    // Store yielded value in coroutine promise (if in async/generator context)
    if (currentCoroPromise_) {
        builder_->CreateStore(value, currentCoroPromise_);

        // Suspend the coroutine (non-final suspend)
        auto *coroSuspendFn = llvm::Intrinsic::getOrInsertDeclaration(
            module_.get(), llvm::Intrinsic::coro_suspend);
        auto *noneToken = llvm::ConstantTokenNone::get(*context_);
        auto *suspVal = builder_->CreateCall(
            coroSuspendFn, {noneToken, builder_->getFalse()}, "yield.sus");

        auto *func = builder_->GetInsertBlock()->getParent();
        auto *resumeBB = llvm::BasicBlock::Create(*context_, "yield.resume", func);

        auto *sw = builder_->CreateSwitch(suspVal, currentCoroSuspendBB_, 2);
        sw->addCase(builder_->getInt8(0), resumeBB);        // resumed
        sw->addCase(builder_->getInt8(1), currentCoroCleanupBB_); // destroyed

        builder_->SetInsertPoint(resumeBB);
    }

    return value;
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
