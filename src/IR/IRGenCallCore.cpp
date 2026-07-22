#include "liva/IR/IRGen.h"

#ifdef LIVA_HAS_LLVM

#include <llvm/IR/Intrinsics.h>

namespace liva {

std::optional<llvm::Value *>
IRGen::tryEmitCoreBuiltin(CallExpr *node, const std::string &funcName) {
    // Handle len() built-in
    if (funcName == "len") {
        if (!node->getArgs().empty()) {
            auto *arg = visit(node->getArgs()[0].get());
            if (!arg) return nullptr;
            auto *lenFn = getOrPanic("liva_str_length");
            return builder_->CreateCall(lenFn, {arg});
        }
        return nullptr;
    }

    // Handle toString() built-in
    if (funcName == "toString") {
        if (!node->getArgs().empty()) {
            auto *arg = visit(node->getArgs()[0].get());
            if (!arg) return nullptr;
            if (arg->getType()->isIntegerTy(32)) {
                auto *r = builder_->CreateCall(getOrPanic("liva_i32_to_str"), {arg});
                trackStringTemp(r);
                return r;
            } else if (arg->getType()->isIntegerTy(64)) {
                auto *r = builder_->CreateCall(getOrPanic("liva_i64_to_str"), {arg});
                trackStringTemp(r);
                return r;
            } else if (arg->getType()->isDoubleTy()) {
                auto *r = builder_->CreateCall(getOrPanic("liva_f64_to_str"), {arg});
                trackStringTemp(r);
                return r;
            } else if (arg->getType()->isIntegerTy(1)) {
                auto *ext = builder_->CreateZExt(arg, llvm::Type::getInt8Ty(*context_));
                auto *r = builder_->CreateCall(getOrPanic("liva_bool_to_str"), {ext});
                trackStringTemp(r);
                return r;
            } else if (arg->getType()->isPointerTy()) {
                return arg; // already a string
            }
            return arg;
        }
        return nullptr;
    }

    // Handle charToString(i32) -> string
    if (funcName == "charToString" && !node->getArgs().empty()) {
        auto *arg = visit(node->getArgs()[0].get());
        if (!arg) return nullptr;
        auto *r = builder_->CreateCall(getOrPanic("liva_char_to_str"), {arg});
        trackStringTemp(r);
        return r;
    }

    // Handle parseInt/parseInt64/parseFloat built-ins → Optional<T>
    if (funcName == "parseInt" && !node->getArgs().empty()) {
        auto *strArg = visit(node->getArgs()[0].get());
        if (!strArg) return nullptr;
        auto *curFunc = builder_->GetInsertBlock()->getParent();
        auto *resultAlloca = createEntryBlockAlloca(curFunc, "parse.tmp", builder_->getInt32Ty());
        auto *fn = getOrPanic("liva_str_parse_i32");
        auto *ok = builder_->CreateCall(fn, {strArg, resultAlloca}, "parse.ok");
        auto *hasVal = builder_->CreateTrunc(ok, builder_->getInt1Ty(), "parse.hasval");
        auto *val = builder_->CreateLoad(builder_->getInt32Ty(), resultAlloca, "parse.val");
        auto *optTy = getOptionalType(builder_->getInt32Ty());
        auto *optAlloca = createEntryBlockAlloca(curFunc, "parse.opt", optTy);
        auto *hasValPtr = builder_->CreateStructGEP(optTy, optAlloca, 0);
        builder_->CreateStore(hasVal, hasValPtr);
        auto *valPtr = builder_->CreateStructGEP(optTy, optAlloca, 1);
        builder_->CreateStore(val, valPtr);
        return builder_->CreateLoad(optTy, optAlloca, "parse.result");
    }

    if (funcName == "parseInt64" && !node->getArgs().empty()) {
        auto *strArg = visit(node->getArgs()[0].get());
        if (!strArg) return nullptr;
        auto *curFunc = builder_->GetInsertBlock()->getParent();
        auto *resultAlloca = createEntryBlockAlloca(curFunc, "parse.tmp", builder_->getInt64Ty());
        auto *fn = getOrPanic("liva_str_parse_i64");
        auto *ok = builder_->CreateCall(fn, {strArg, resultAlloca}, "parse.ok");
        auto *hasVal = builder_->CreateTrunc(ok, builder_->getInt1Ty(), "parse.hasval");
        auto *val = builder_->CreateLoad(builder_->getInt64Ty(), resultAlloca, "parse.val");
        auto *optTy = getOptionalType(builder_->getInt64Ty());
        auto *optAlloca = createEntryBlockAlloca(curFunc, "parse.opt", optTy);
        auto *hasValPtr = builder_->CreateStructGEP(optTy, optAlloca, 0);
        builder_->CreateStore(hasVal, hasValPtr);
        auto *valPtr = builder_->CreateStructGEP(optTy, optAlloca, 1);
        builder_->CreateStore(val, valPtr);
        return builder_->CreateLoad(optTy, optAlloca, "parse.result");
    }

    if (funcName == "parseFloat" && !node->getArgs().empty()) {
        auto *strArg = visit(node->getArgs()[0].get());
        if (!strArg) return nullptr;
        auto *curFunc = builder_->GetInsertBlock()->getParent();
        auto *resultAlloca = createEntryBlockAlloca(curFunc, "parse.tmp", builder_->getDoubleTy());
        auto *fn = getOrPanic("liva_str_parse_f64");
        auto *ok = builder_->CreateCall(fn, {strArg, resultAlloca}, "parse.ok");
        auto *hasVal = builder_->CreateTrunc(ok, builder_->getInt1Ty(), "parse.hasval");
        auto *val = builder_->CreateLoad(builder_->getDoubleTy(), resultAlloca, "parse.val");
        auto *optTy = getOptionalType(builder_->getDoubleTy());
        auto *optAlloca = createEntryBlockAlloca(curFunc, "parse.opt", optTy);
        auto *hasValPtr = builder_->CreateStructGEP(optTy, optAlloca, 0);
        builder_->CreateStore(hasVal, hasValPtr);
        auto *valPtr = builder_->CreateStructGEP(optTy, optAlloca, 1);
        builder_->CreateStore(val, valPtr);
        return builder_->CreateLoad(optTy, optAlloca, "parse.result");
    }

    // Handle readLine() built-in
    if (funcName == "readLine") {
        auto *fn = getOrPanic("liva_read_line");
        auto *r = builder_->CreateCall(fn, {}, "readline");
        trackStringTemp(r);
        return r;
    }

    // Handle format() built-in
    if (funcName == "format" && !node->getArgs().empty()) {
        // First arg is format string literal
        auto *fmtArg = visit(node->getArgs()[0].get());
        if (!fmtArg) return nullptr;

        // If only format string, return it directly
        if (node->getArgs().size() == 1) return fmtArg;

        // Parse format string from AST for {} placeholders
        auto *fmtLit = node->getArgs()[0].get();
        std::string fmtStr;
        if (fmtLit->getKind() == ASTNode::NodeKind::StringLiteralExpr) {
            fmtStr = static_cast<StringLiteralExpr *>(fmtLit)->getValue();
        }

        // Split format string by {} and interleave with toString'd args
        std::vector<llvm::Value *> parts;
        size_t argIdx = 1;
        size_t pos = 0;
        while (pos < fmtStr.size()) {
            auto bracePos = fmtStr.find("{}", pos);
            if (bracePos == std::string::npos) {
                // Remaining literal text
                auto *lit = builder_->CreateGlobalString(
                    llvm::StringRef(fmtStr.c_str() + pos, fmtStr.size() - pos));
                parts.push_back(lit);
                break;
            }
            // Text before {}
            if (bracePos > pos) {
                auto *lit = builder_->CreateGlobalString(
                    llvm::StringRef(fmtStr.c_str() + pos, bracePos - pos));
                parts.push_back(lit);
            }
            // Convert arg to string
            if (argIdx < node->getArgs().size()) {
                auto *argVal = visit(node->getArgs()[argIdx].get());
                if (argVal) {
                    // emitToString inline
                    if (argVal->getType()->isIntegerTy(32)) {
                        argVal = builder_->CreateCall(
                            getOrPanic("liva_i32_to_str"), {argVal}, "fmt.i32");
                        trackStringTemp(argVal);
                    } else if (argVal->getType()->isIntegerTy(64)) {
                        argVal = builder_->CreateCall(
                            getOrPanic("liva_i64_to_str"), {argVal}, "fmt.i64");
                        trackStringTemp(argVal);
                    } else if (argVal->getType()->isDoubleTy()) {
                        argVal = builder_->CreateCall(
                            getOrPanic("liva_f64_to_str"), {argVal}, "fmt.f64");
                        trackStringTemp(argVal);
                    } else if (argVal->getType()->isIntegerTy(1)) {
                        auto *ext = builder_->CreateZExt(argVal,
                            llvm::Type::getInt8Ty(*context_));
                        argVal = builder_->CreateCall(
                            getOrPanic("liva_bool_to_str"), {ext}, "fmt.bool");
                        trackStringTemp(argVal);
                    }
                    // ptr (string) → use directly
                    parts.push_back(argVal);
                }
                ++argIdx;
            }
            pos = bracePos + 2;
        }

        // Concatenate all parts with liva_str_concat
        if (parts.empty()) return fmtArg;
        llvm::Value *result = parts[0];
        auto *concatFn = getOrPanic("liva_str_concat");
        for (size_t i = 1; i < parts.size(); ++i) {
            result = builder_->CreateCall(concatFn, {result, parts[i]}, "fmt.concat");
            trackStringTemp(result);
        }
        return result;
    }

    // Handle math built-ins: abs, min, max, sqrt, pow, floor, ceil
    if (funcName == "abs" && !node->getArgs().empty()) {
        auto *x = visit(node->getArgs()[0].get());
        if (!x) return nullptr;
        if (x->getType()->isDoubleTy()) {
            auto *fn = llvm::Intrinsic::getOrInsertDeclaration(
                module_.get(), llvm::Intrinsic::fabs, {llvm::Type::getDoubleTy(*context_)});
            return builder_->CreateCall(fn, {x}, "fabstmp");
        }
        // Integer abs: select (x < 0), -x, x
        auto *zero = llvm::ConstantInt::get(x->getType(), 0);
        auto *neg = builder_->CreateNeg(x, "negtmp");
        auto *cmp = builder_->CreateICmpSLT(x, zero, "abstmp");
        return builder_->CreateSelect(cmp, neg, x, "abs");
    }

    if (funcName == "min" && node->getArgs().size() >= 2) {
        auto *a = visit(node->getArgs()[0].get());
        auto *b = visit(node->getArgs()[1].get());
        if (!a || !b) return nullptr;
        if (a->getType()->isDoubleTy()) {
            auto *fn = llvm::Intrinsic::getOrInsertDeclaration(
                module_.get(), llvm::Intrinsic::minnum, {llvm::Type::getDoubleTy(*context_)});
            return builder_->CreateCall(fn, {a, b}, "mintmp");
        }
        auto *cmp = builder_->CreateICmpSLT(a, b, "mincmp");
        return builder_->CreateSelect(cmp, a, b, "min");
    }

    if (funcName == "max" && node->getArgs().size() >= 2) {
        auto *a = visit(node->getArgs()[0].get());
        auto *b = visit(node->getArgs()[1].get());
        if (!a || !b) return nullptr;
        if (a->getType()->isDoubleTy()) {
            auto *fn = llvm::Intrinsic::getOrInsertDeclaration(
                module_.get(), llvm::Intrinsic::maxnum, {llvm::Type::getDoubleTy(*context_)});
            return builder_->CreateCall(fn, {a, b}, "maxtmp");
        }
        auto *cmp = builder_->CreateICmpSGT(a, b, "maxcmp");
        return builder_->CreateSelect(cmp, a, b, "max");
    }

    if (funcName == "sqrt" && !node->getArgs().empty()) {
        auto *x = visit(node->getArgs()[0].get());
        if (!x) return nullptr;
        if (x->getType()->isIntegerTy())
            x = builder_->CreateSIToFP(x, llvm::Type::getDoubleTy(*context_), "tofp");
        auto *fn = llvm::Intrinsic::getOrInsertDeclaration(
            module_.get(), llvm::Intrinsic::sqrt, {llvm::Type::getDoubleTy(*context_)});
        return builder_->CreateCall(fn, {x}, "sqrttmp");
    }

    if (funcName == "pow" && node->getArgs().size() >= 2) {
        auto *x = visit(node->getArgs()[0].get());
        auto *y = visit(node->getArgs()[1].get());
        if (!x || !y) return nullptr;
        if (x->getType()->isIntegerTy())
            x = builder_->CreateSIToFP(x, llvm::Type::getDoubleTy(*context_), "tofp");
        if (y->getType()->isIntegerTy())
            y = builder_->CreateSIToFP(y, llvm::Type::getDoubleTy(*context_), "tofp");
        auto *fn = llvm::Intrinsic::getOrInsertDeclaration(
            module_.get(), llvm::Intrinsic::pow, {llvm::Type::getDoubleTy(*context_)});
        return builder_->CreateCall(fn, {x, y}, "powtmp");
    }

    if (funcName == "floor" && !node->getArgs().empty()) {
        auto *x = visit(node->getArgs()[0].get());
        if (!x) return nullptr;
        if (x->getType()->isIntegerTy())
            x = builder_->CreateSIToFP(x, llvm::Type::getDoubleTy(*context_), "tofp");
        auto *fn = llvm::Intrinsic::getOrInsertDeclaration(
            module_.get(), llvm::Intrinsic::floor, {llvm::Type::getDoubleTy(*context_)});
        return builder_->CreateCall(fn, {x}, "floortmp");
    }

    if (funcName == "ceil" && !node->getArgs().empty()) {
        auto *x = visit(node->getArgs()[0].get());
        if (!x) return nullptr;
        if (x->getType()->isIntegerTy())
            x = builder_->CreateSIToFP(x, llvm::Type::getDoubleTy(*context_), "tofp");
        auto *fn = llvm::Intrinsic::getOrInsertDeclaration(
            module_.get(), llvm::Intrinsic::ceil, {llvm::Type::getDoubleTy(*context_)});
        return builder_->CreateCall(fn, {x}, "ceiltmp");
    }

    if (funcName == "log" && !node->getArgs().empty()) {
        auto *x = visit(node->getArgs()[0].get());
        if (!x) return nullptr;
        if (x->getType()->isIntegerTy())
            x = builder_->CreateSIToFP(x, llvm::Type::getDoubleTy(*context_), "tofp");
        auto *fn = llvm::Intrinsic::getOrInsertDeclaration(
            module_.get(), llvm::Intrinsic::log, {llvm::Type::getDoubleTy(*context_)});
        return builder_->CreateCall(fn, {x}, "logtmp");
    }

    if (funcName == "log10" && !node->getArgs().empty()) {
        auto *x = visit(node->getArgs()[0].get());
        if (!x) return nullptr;
        if (x->getType()->isIntegerTy())
            x = builder_->CreateSIToFP(x, llvm::Type::getDoubleTy(*context_), "tofp");
        auto *fn = llvm::Intrinsic::getOrInsertDeclaration(
            module_.get(), llvm::Intrinsic::log10, {llvm::Type::getDoubleTy(*context_)});
        return builder_->CreateCall(fn, {x}, "log10tmp");
    }

    if (funcName == "sin" && !node->getArgs().empty()) {
        auto *x = visit(node->getArgs()[0].get());
        if (!x) return nullptr;
        if (x->getType()->isIntegerTy())
            x = builder_->CreateSIToFP(x, llvm::Type::getDoubleTy(*context_), "tofp");
        auto *fn = llvm::Intrinsic::getOrInsertDeclaration(
            module_.get(), llvm::Intrinsic::sin, {llvm::Type::getDoubleTy(*context_)});
        return builder_->CreateCall(fn, {x}, "sintmp");
    }

    if (funcName == "cos" && !node->getArgs().empty()) {
        auto *x = visit(node->getArgs()[0].get());
        if (!x) return nullptr;
        if (x->getType()->isIntegerTy())
            x = builder_->CreateSIToFP(x, llvm::Type::getDoubleTy(*context_), "tofp");
        auto *fn = llvm::Intrinsic::getOrInsertDeclaration(
            module_.get(), llvm::Intrinsic::cos, {llvm::Type::getDoubleTy(*context_)});
        return builder_->CreateCall(fn, {x}, "costmp");
    }

    if (funcName == "tan" && !node->getArgs().empty()) {
        auto *x = visit(node->getArgs()[0].get());
        if (!x) return nullptr;
        if (x->getType()->isIntegerTy())
            x = builder_->CreateSIToFP(x, llvm::Type::getDoubleTy(*context_), "tofp");
        auto *fn = llvm::Intrinsic::getOrInsertDeclaration(
            module_.get(), llvm::Intrinsic::tan, {llvm::Type::getDoubleTy(*context_)});
        return builder_->CreateCall(fn, {x}, "tantmp");
    }

    if (funcName == "round" && !node->getArgs().empty()) {
        auto *x = visit(node->getArgs()[0].get());
        if (!x) return nullptr;
        if (x->getType()->isIntegerTy())
            x = builder_->CreateSIToFP(x, llvm::Type::getDoubleTy(*context_), "tofp");
        auto *f64Ty = llvm::Type::getDoubleTy(*context_);
        if (node->getArgs().size() >= 2) {
            // round(x, digits): round(x * 10^d) / 10^d
            auto *d = visit(node->getArgs()[1].get());
            if (!d) return nullptr;
            if (d->getType()->isIntegerTy())
                d = builder_->CreateSIToFP(d, f64Ty, "tofp");
            auto *ten = llvm::ConstantFP::get(f64Ty, 10.0);
            auto *powFn = llvm::Intrinsic::getOrInsertDeclaration(
                module_.get(), llvm::Intrinsic::pow, {f64Ty});
            auto *factor = builder_->CreateCall(powFn, {ten, d}, "factor");
            auto *scaled = builder_->CreateFMul(x, factor, "scaled");
            auto *roundFn = llvm::Intrinsic::getOrInsertDeclaration(
                module_.get(), llvm::Intrinsic::round, {f64Ty});
            auto *rounded = builder_->CreateCall(roundFn, {scaled}, "rounded");
            return builder_->CreateFDiv(rounded, factor, "roundtmp");
        }
        // round(x): round to nearest integer
        auto *fn = llvm::Intrinsic::getOrInsertDeclaration(
            module_.get(), llvm::Intrinsic::round, {f64Ty});
        return builder_->CreateCall(fn, {x}, "roundtmp");
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

        // Multi-arg: println(a, b, c) → print each with space separator, then newline
        llvm::Value *lastCall = nullptr;
        for (size_t i = 0; i < node->getArgs().size(); ++i) {
            auto *arg = visit(node->getArgs()[i].get());
            if (!arg) continue;

            // bool → "true"/"false" via runtime helper, printed as %s. Without
            // this, printf with %d on an i1 vararg reads garbage from the
            // calling-convention slot (varargs require at least i32).
            if (arg->getType()->isIntegerTy(1)) {
                auto *ext = builder_->CreateZExt(arg, llvm::Type::getInt8Ty(*context_));
                arg = builder_->CreateCall(getOrPanic("liva_bool_to_str"), {ext},
                                            "bool.str");
                trackStringTemp(arg);
            }

            std::string fmt;
            if (arg->getType()->isIntegerTy(32))
                fmt = "%d";
            else if (arg->getType()->isIntegerTy(64))
                fmt = "%lld";
            else if (arg->getType()->isFloatingPointTy())
                fmt = "%f";
            else if (arg->getType()->isPointerTy())
                fmt = "%s";
            else
                fmt = "%d";

            // Add space between args, newline at end for println
            if (i + 1 < node->getArgs().size())
                fmt += " ";
            else if (funcName == "println")
                fmt += "\n";

            auto *fmtStr = builder_->CreateGlobalString(fmt);
            lastCall = builder_->CreateCall(printfFunc, {fmtStr, arg});
        }
        return lastCall;
    }

    return std::nullopt;
}

} // namespace liva

#endif // LIVA_HAS_LLVM
