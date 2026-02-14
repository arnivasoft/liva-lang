#include "liva/DAP/DAPServer.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace liva {

// ============================================================
// DAPValue
// ============================================================

std::string DAPValue::display() const {
    switch (kind) {
    case Nil: return "nil";
    case Integer: {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(intVal));
        return buf;
    }
    case Float: {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%g", floatVal);
        return buf;
    }
    case Bool: return boolVal ? "true" : "false";
    case String: return "\"" + strVal + "\"";
    case Array: {
        std::string result = "[";
        for (size_t i = 0; i < arrayVal.size(); ++i) {
            if (i > 0) result += ", ";
            result += arrayVal[i].display();
        }
        result += "]";
        return result;
    }
    case Struct: {
        std::string result = structTypeName + " { ";
        bool first = true;
        for (const auto &kv : structVal) {
            if (!first) result += ", ";
            first = false;
            result += kv.first + ": " + kv.second.display();
        }
        result += " }";
        return result;
    }
    }
    return "nil";
}

std::string DAPValue::typeName() const {
    switch (kind) {
    case Nil: return "nil";
    case Integer: return "i64";
    case Float: return "f64";
    case Bool: return "Bool";
    case String: return "String";
    case Array: return "Array";
    case Struct: return structTypeName.empty() ? "Struct" : structTypeName;
    }
    return "unknown";
}

bool DAPValue::isTruthy() const {
    switch (kind) {
    case Nil: return false;
    case Integer: return intVal != 0;
    case Float: return floatVal != 0.0;
    case Bool: return boolVal;
    case String: return !strVal.empty();
    case Array: return !arrayVal.empty();
    case Struct: return true;
    }
    return false;
}

// ============================================================
// DAPInterpreter
// ============================================================

const std::map<std::string, DAPValue> DAPInterpreter::emptyLocals_;

DAPInterpreter::DAPInterpreter() = default;

void DAPInterpreter::load(TranslationUnit *tu) {
    tu_ = tu;
    state_ = ExecState::Terminated;
    callStack_.clear();
    functions_.clear();
    structs_.clear();
    outputBuffer_.clear();
    runtimeError_.clear();
    exitCode_ = 0;
    nextFrameId_ = 1;

    if (!tu_) return;

    // Collect top-level functions and structs
    for (const auto &decl : tu_->getDeclarations()) {
        if (auto *fd = dynamic_cast<const FuncDecl *>(decl.get())) {
            functions_[fd->getName()] = fd;
        } else if (auto *sd = dynamic_cast<const StructDecl *>(decl.get())) {
            structs_[sd->getName()] = sd;
        }
    }
}

bool DAPInterpreter::start() {
    if (!tu_) return false;

    auto it = functions_.find("main");
    if (it == functions_.end()) return false;

    const FuncDecl *mainFunc = it->second;
    if (!mainFunc->hasBody()) return false;

    // Create the initial frame for main()
    CallFrame frame;
    frame.functionName = "main";
    frame.funcDecl = mainFunc;
    frame.frameId = nextFrameId_++;
    frame.location = mainFunc->getBody()->getStartLoc();
    frame.currentBlock = mainFunc->getBody();
    frame.stmtIndex = 0;
    callStack_.push_back(std::move(frame));

    state_ = ExecState::Paused;
    return true;
}

StepResult DAPInterpreter::resume(StepMode mode) {
    stepMode_ = mode;
    stepFrameDepth_ = static_cast<int>(callStack_.size());
    state_ = ExecState::Running;
    stopReason_.clear();

    while (state_ == ExecState::Running && !callStack_.empty()) {
        auto &frame = callStack_.back();
        if (!frame.currentBlock) {
            callStack_.pop_back();
            if (callStack_.empty()) {
                state_ = ExecState::Terminated;
            }
            continue;
        }

        auto result = executeBlock(frame.currentBlock, frame.stmtIndex);

        if (result.state == ExecState::Paused) {
            return result;
        }
        if (result.state == ExecState::Error) {
            state_ = ExecState::Error;
            runtimeError_ = result.error;
            return result;
        }
        if (result.signal == StepResult::Return) {
            // Pop frame and store return value
            DAPValue retVal = std::move(result.returnValue);
            callStack_.pop_back();
            (void)retVal; // Return value is handled at call site
            if (callStack_.empty()) {
                state_ = ExecState::Terminated;
            }
            continue;
        }

        // Block finished normally
        callStack_.pop_back();
        if (callStack_.empty()) {
            state_ = ExecState::Terminated;
        }
    }

    if (callStack_.empty()) {
        state_ = ExecState::Terminated;
    }
    return {state_, runtimeError_};
}

void DAPInterpreter::setBreakpoints(const std::string &path,
                                     const std::vector<int> &lines) {
    auto &bpSet = breakpoints_[path];
    bpSet.clear();
    for (int line : lines) {
        bpSet.insert(line);
    }
}

const std::map<std::string, DAPValue> &
DAPInterpreter::getFrameLocals(int frameId) const {
    for (const auto &frame : callStack_) {
        if (frame.frameId == frameId)
            return frame.locals;
    }
    return emptyLocals_;
}

bool DAPInterpreter::shouldPause(const SourceLocation &loc) {
    if (!loc.isValid()) return false;

    switch (stepMode_) {
    case StepMode::Continue: {
        // Only pause on breakpoints
        for (const auto &bp : breakpoints_) {
            if (bp.second.count(static_cast<int>(loc.line))) {
                stopReason_ = "breakpoint";
                return true;
            }
        }
        return false;
    }
    case StepMode::StepIn:
        // Pause at every statement
        stopReason_ = "step";
        return true;
    case StepMode::Next: {
        // Pause at same or shallower frame depth
        int currentDepth = static_cast<int>(callStack_.size());
        if (currentDepth <= stepFrameDepth_) {
            stopReason_ = "step";
            return true;
        }
        // Also check breakpoints
        for (const auto &bp : breakpoints_) {
            if (bp.second.count(static_cast<int>(loc.line))) {
                stopReason_ = "breakpoint";
                return true;
            }
        }
        return false;
    }
    case StepMode::StepOut: {
        // Pause when frame depth decreases
        int currentDepth = static_cast<int>(callStack_.size());
        if (currentDepth < stepFrameDepth_) {
            stopReason_ = "step";
            return true;
        }
        // Also check breakpoints
        for (const auto &bp : breakpoints_) {
            if (bp.second.count(static_cast<int>(loc.line))) {
                stopReason_ = "breakpoint";
                return true;
            }
        }
        return false;
    }
    }
    return false;
}

// ============================================================
// Statement execution
// ============================================================

StepResult DAPInterpreter::executeBlock(const BlockStmt *block,
                                         size_t startIdx) {
    const auto &stmts = block->getStatements();
    for (size_t i = startIdx; i < stmts.size(); ++i) {
        auto result = executeStatement(stmts[i].get());
        if (result.state == ExecState::Paused ||
            result.state == ExecState::Error ||
            result.signal == StepResult::Return ||
            result.signal == StepResult::Break ||
            result.signal == StepResult::Continue) {
            return result;
        }
    }
    return {ExecState::Running, ""};
}

StepResult DAPInterpreter::executeStatement(const ASTNode *stmt) {
    if (!stmt) return {ExecState::Running, ""};

    // Check if we should pause before this statement
    auto loc = stmt->getStartLoc();
    if (loc.isValid() && shouldPause(loc)) {
        // Update frame location
        if (!callStack_.empty()) {
            callStack_.back().location = loc;
        }
        state_ = ExecState::Paused;
        return {ExecState::Paused, ""};
    }

    // Update frame location
    if (!callStack_.empty() && loc.isValid()) {
        callStack_.back().location = loc;
    }

    using NK = ASTNode::NodeKind;
    switch (stmt->getKind()) {
    case NK::VarDecl:
        return executeVarDecl(static_cast<const VarDecl *>(stmt));
    case NK::ExprStmt:
        return executeExprStmt(static_cast<const ExprStmt *>(stmt));
    case NK::ReturnStmt:
        return executeReturnStmt(static_cast<const ReturnStmt *>(stmt));
    case NK::IfStmt:
        return executeIfStmt(static_cast<const IfStmt *>(stmt));
    case NK::WhileStmt:
        return executeWhileStmt(static_cast<const WhileStmt *>(stmt));
    case NK::ForStmt:
        return executeForStmt(static_cast<const ForStmt *>(stmt));
    case NK::BlockStmt:
        return executeBlock(static_cast<const BlockStmt *>(stmt));
    case NK::BreakStmt: {
        StepResult r;
        r.state = ExecState::Running;
        r.signal = StepResult::Break;
        return r;
    }
    case NK::ContinueStmt: {
        StepResult r;
        r.state = ExecState::Running;
        r.signal = StepResult::Continue;
        return r;
    }
    default:
        // Try as expression statement
        if (auto *expr = dynamic_cast<const Expr *>(stmt)) {
            evaluateExpr(expr);
            return {ExecState::Running, ""};
        }
        return {ExecState::Running, ""};
    }
}

StepResult DAPInterpreter::executeVarDecl(const VarDecl *decl) {
    DAPValue val;
    if (decl->hasInit()) {
        val = evaluateExpr(decl->getInit());
        if (state_ == ExecState::Paused)
            return {ExecState::Paused, ""};
    }

    if (decl->isDestructured()) {
        // Tuple destructuring
        const auto &names = decl->getDestructuredNames();
        if (val.kind == DAPValue::Array) {
            for (size_t i = 0; i < names.size() && i < val.arrayVal.size(); ++i) {
                setVariable(names[i], val.arrayVal[i]);
            }
        }
    } else {
        setVariable(decl->getName(), std::move(val));
    }
    return {ExecState::Running, ""};
}

StepResult DAPInterpreter::executeExprStmt(const ExprStmt *stmt) {
    evaluateExpr(stmt->getExpr());
    if (state_ == ExecState::Paused)
        return {ExecState::Paused, ""};
    return {ExecState::Running, ""};
}

StepResult DAPInterpreter::executeReturnStmt(const ReturnStmt *stmt) {
    StepResult r;
    r.state = ExecState::Running;
    r.signal = StepResult::Return;
    if (stmt->hasValue()) {
        r.returnValue = evaluateExpr(stmt->getValue());
    }
    return r;
}

StepResult DAPInterpreter::executeIfStmt(const IfStmt *stmt) {
    auto cond = evaluateExpr(stmt->getCondition());
    if (state_ == ExecState::Paused) return {ExecState::Paused, ""};
    if (cond.isTruthy()) {
        if (auto *block = dynamic_cast<const BlockStmt *>(stmt->getThenBody())) {
            return executeBlock(block);
        }
        return executeStatement(stmt->getThenBody());
    } else if (stmt->hasElse()) {
        if (auto *block = dynamic_cast<const BlockStmt *>(stmt->getElseBody())) {
            return executeBlock(block);
        }
        return executeStatement(stmt->getElseBody());
    }
    return {ExecState::Running, ""};
}

StepResult DAPInterpreter::executeWhileStmt(const WhileStmt *stmt) {
    int iterLimit = 100000;
    while (iterLimit-- > 0) {
        auto cond = evaluateExpr(stmt->getCondition());
        if (state_ == ExecState::Paused) return {ExecState::Paused, ""};
        if (!cond.isTruthy()) break;

        StepResult result;
        if (auto *block = dynamic_cast<const BlockStmt *>(stmt->getBody())) {
            result = executeBlock(block);
        } else {
            result = executeStatement(stmt->getBody());
        }

        if (result.state == ExecState::Paused ||
            result.state == ExecState::Error) {
            return result;
        }
        if (result.signal == StepResult::Break) break;
        if (result.signal == StepResult::Return) return result;
        // Continue: just loop again
    }
    return {ExecState::Running, ""};
}

StepResult DAPInterpreter::executeForStmt(const ForStmt *stmt) {
    auto iterable = evaluateExpr(stmt->getIterable());
    int iterLimit = 100000;

    // Handle range expressions and arrays
    if (iterable.kind == DAPValue::Array) {
        for (size_t i = 0; i < iterable.arrayVal.size() && iterLimit-- > 0; ++i) {
            setVariable(stmt->getVarName(), iterable.arrayVal[i]);

            StepResult result;
            if (auto *block = dynamic_cast<const BlockStmt *>(stmt->getBody())) {
                result = executeBlock(block);
            } else {
                result = executeStatement(stmt->getBody());
            }

            if (result.state == ExecState::Paused ||
                result.state == ExecState::Error) {
                return result;
            }
            if (result.signal == StepResult::Break) break;
            if (result.signal == StepResult::Return) return result;
        }
    }
    return {ExecState::Running, ""};
}

// ============================================================
// Expression evaluation
// ============================================================

DAPValue DAPInterpreter::evaluateExpr(const Expr *expr) {
    if (!expr) return DAPValue::makeNil();

    using NK = ASTNode::NodeKind;
    switch (expr->getKind()) {
    case NK::IntegerLiteralExpr:
        return DAPValue::makeInt(
            static_cast<const IntegerLiteralExpr *>(expr)->getValue());
    case NK::FloatLiteralExpr:
        return DAPValue::makeFloat(
            static_cast<const FloatLiteralExpr *>(expr)->getValue());
    case NK::BoolLiteralExpr:
        return DAPValue::makeBool(
            static_cast<const BoolLiteralExpr *>(expr)->getValue());
    case NK::StringLiteralExpr:
        return DAPValue::makeString(
            static_cast<const StringLiteralExpr *>(expr)->getValue());
    case NK::NilLiteralExpr:
        return DAPValue::makeNil();
    case NK::IdentifierExpr: {
        auto name = static_cast<const IdentifierExpr *>(expr)->getName();
        return lookupVariable(name);
    }
    case NK::BinaryExpr:
        return evaluateBinaryExpr(static_cast<const BinaryExpr *>(expr));
    case NK::UnaryExpr:
        return evaluateUnaryExpr(static_cast<const UnaryExpr *>(expr));
    case NK::CallExpr:
        return evaluateCallExpr(static_cast<const CallExpr *>(expr));
    case NK::MemberExpr:
        return evaluateMemberExpr(static_cast<const MemberExpr *>(expr));
    case NK::IndexExpr:
        return evaluateIndexExpr(static_cast<const IndexExpr *>(expr));
    case NK::AssignExpr:
        return evaluateAssignExpr(static_cast<const AssignExpr *>(expr));
    case NK::StructLiteralExpr: {
        auto *sl = static_cast<const StructLiteralExpr *>(expr);
        std::map<std::string, DAPValue> fields;
        for (const auto &fi : sl->getFields()) {
            fields[fi.name] = evaluateExpr(fi.value.get());
        }
        return DAPValue::makeStruct(sl->getTypeName(), std::move(fields));
    }
    case NK::ArrayLiteralExpr: {
        auto *al = static_cast<const ArrayLiteralExpr *>(expr);
        std::vector<DAPValue> elements;
        for (const auto &e : al->getElements()) {
            elements.push_back(evaluateExpr(e.get()));
        }
        return DAPValue::makeArray(std::move(elements));
    }
    case NK::TernaryExpr: {
        auto *te = static_cast<const TernaryExpr *>(expr);
        auto cond = evaluateExpr(te->getCondition());
        return cond.isTruthy() ? evaluateExpr(te->getThenExpr())
                               : evaluateExpr(te->getElseExpr());
    }
    case NK::CastExpr: {
        auto *ce = static_cast<const CastExpr *>(expr);
        auto val = evaluateExpr(ce->getExpr());
        // Simple numeric casts
        auto targetKind = ce->getTargetType()->getKind();
        if (targetKind == TypeRepr::Kind::I32 ||
            targetKind == TypeRepr::Kind::I64 ||
            targetKind == TypeRepr::Kind::I8 ||
            targetKind == TypeRepr::Kind::I16 ||
            targetKind == TypeRepr::Kind::U8 ||
            targetKind == TypeRepr::Kind::U16 ||
            targetKind == TypeRepr::Kind::U32 ||
            targetKind == TypeRepr::Kind::U64) {
            if (val.kind == DAPValue::Float)
                return DAPValue::makeInt(static_cast<int64_t>(val.floatVal));
            return val;
        }
        if (targetKind == TypeRepr::Kind::F32 ||
            targetKind == TypeRepr::Kind::F64) {
            if (val.kind == DAPValue::Integer)
                return DAPValue::makeFloat(static_cast<double>(val.intVal));
            return val;
        }
        return val;
    }
    case NK::GroupExpr:
        return evaluateExpr(
            static_cast<const GroupExpr *>(expr)->getExpr());
    case NK::RangeExpr: {
        auto *re = static_cast<const RangeExpr *>(expr);
        auto startVal = evaluateExpr(re->getStart());
        auto endVal = evaluateExpr(re->getEnd());
        // Create an array for the range
        std::vector<DAPValue> elements;
        if (startVal.kind == DAPValue::Integer && endVal.kind == DAPValue::Integer) {
            for (int64_t i = startVal.intVal; i < endVal.intVal; ++i) {
                elements.push_back(DAPValue::makeInt(i));
            }
        }
        return DAPValue::makeArray(std::move(elements));
    }
    default:
        return DAPValue::makeNil();
    }
}

DAPValue DAPInterpreter::evaluateBinaryExpr(const BinaryExpr *expr) {
    auto lhs = evaluateExpr(expr->getLHS());
    auto rhs = evaluateExpr(expr->getRHS());

    using Op = BinaryExpr::Op;
    switch (expr->getOp()) {
    case Op::Add:
        if (lhs.kind == DAPValue::Integer && rhs.kind == DAPValue::Integer)
            return DAPValue::makeInt(lhs.intVal + rhs.intVal);
        if (lhs.kind == DAPValue::Float || rhs.kind == DAPValue::Float) {
            double l = lhs.kind == DAPValue::Float ? lhs.floatVal
                                                    : static_cast<double>(lhs.intVal);
            double r = rhs.kind == DAPValue::Float ? rhs.floatVal
                                                    : static_cast<double>(rhs.intVal);
            return DAPValue::makeFloat(l + r);
        }
        if (lhs.kind == DAPValue::String && rhs.kind == DAPValue::String)
            return DAPValue::makeString(lhs.strVal + rhs.strVal);
        break;
    case Op::Sub:
        if (lhs.kind == DAPValue::Integer && rhs.kind == DAPValue::Integer)
            return DAPValue::makeInt(lhs.intVal - rhs.intVal);
        if (lhs.kind == DAPValue::Float || rhs.kind == DAPValue::Float) {
            double l = lhs.kind == DAPValue::Float ? lhs.floatVal
                                                    : static_cast<double>(lhs.intVal);
            double r = rhs.kind == DAPValue::Float ? rhs.floatVal
                                                    : static_cast<double>(rhs.intVal);
            return DAPValue::makeFloat(l - r);
        }
        break;
    case Op::Mul:
        if (lhs.kind == DAPValue::Integer && rhs.kind == DAPValue::Integer)
            return DAPValue::makeInt(lhs.intVal * rhs.intVal);
        if (lhs.kind == DAPValue::Float || rhs.kind == DAPValue::Float) {
            double l = lhs.kind == DAPValue::Float ? lhs.floatVal
                                                    : static_cast<double>(lhs.intVal);
            double r = rhs.kind == DAPValue::Float ? rhs.floatVal
                                                    : static_cast<double>(rhs.intVal);
            return DAPValue::makeFloat(l * r);
        }
        break;
    case Op::Div:
        if (lhs.kind == DAPValue::Integer && rhs.kind == DAPValue::Integer) {
            if (rhs.intVal == 0) return DAPValue::makeInt(0);
            return DAPValue::makeInt(lhs.intVal / rhs.intVal);
        }
        if (lhs.kind == DAPValue::Float || rhs.kind == DAPValue::Float) {
            double l = lhs.kind == DAPValue::Float ? lhs.floatVal
                                                    : static_cast<double>(lhs.intVal);
            double r = rhs.kind == DAPValue::Float ? rhs.floatVal
                                                    : static_cast<double>(rhs.intVal);
            if (r == 0.0) return DAPValue::makeFloat(0.0);
            return DAPValue::makeFloat(l / r);
        }
        break;
    case Op::Mod:
        if (lhs.kind == DAPValue::Integer && rhs.kind == DAPValue::Integer) {
            if (rhs.intVal == 0) return DAPValue::makeInt(0);
            return DAPValue::makeInt(lhs.intVal % rhs.intVal);
        }
        break;
    case Op::Eq:
        if (lhs.kind == DAPValue::Integer && rhs.kind == DAPValue::Integer)
            return DAPValue::makeBool(lhs.intVal == rhs.intVal);
        if (lhs.kind == DAPValue::Float || rhs.kind == DAPValue::Float) {
            double l = lhs.kind == DAPValue::Float ? lhs.floatVal
                                                    : static_cast<double>(lhs.intVal);
            double r = rhs.kind == DAPValue::Float ? rhs.floatVal
                                                    : static_cast<double>(rhs.intVal);
            return DAPValue::makeBool(l == r);
        }
        if (lhs.kind == DAPValue::Bool && rhs.kind == DAPValue::Bool)
            return DAPValue::makeBool(lhs.boolVal == rhs.boolVal);
        if (lhs.kind == DAPValue::String && rhs.kind == DAPValue::String)
            return DAPValue::makeBool(lhs.strVal == rhs.strVal);
        return DAPValue::makeBool(false);
    case Op::NotEq:
        if (lhs.kind == DAPValue::Integer && rhs.kind == DAPValue::Integer)
            return DAPValue::makeBool(lhs.intVal != rhs.intVal);
        if (lhs.kind == DAPValue::Float || rhs.kind == DAPValue::Float) {
            double l = lhs.kind == DAPValue::Float ? lhs.floatVal
                                                    : static_cast<double>(lhs.intVal);
            double r = rhs.kind == DAPValue::Float ? rhs.floatVal
                                                    : static_cast<double>(rhs.intVal);
            return DAPValue::makeBool(l != r);
        }
        if (lhs.kind == DAPValue::Bool && rhs.kind == DAPValue::Bool)
            return DAPValue::makeBool(lhs.boolVal != rhs.boolVal);
        if (lhs.kind == DAPValue::String && rhs.kind == DAPValue::String)
            return DAPValue::makeBool(lhs.strVal != rhs.strVal);
        return DAPValue::makeBool(true);
    case Op::Less:
        if (lhs.kind == DAPValue::Integer && rhs.kind == DAPValue::Integer)
            return DAPValue::makeBool(lhs.intVal < rhs.intVal);
        if (lhs.kind == DAPValue::Float || rhs.kind == DAPValue::Float) {
            double l = lhs.kind == DAPValue::Float ? lhs.floatVal
                                                    : static_cast<double>(lhs.intVal);
            double r = rhs.kind == DAPValue::Float ? rhs.floatVal
                                                    : static_cast<double>(rhs.intVal);
            return DAPValue::makeBool(l < r);
        }
        break;
    case Op::LessEq:
        if (lhs.kind == DAPValue::Integer && rhs.kind == DAPValue::Integer)
            return DAPValue::makeBool(lhs.intVal <= rhs.intVal);
        if (lhs.kind == DAPValue::Float || rhs.kind == DAPValue::Float) {
            double l = lhs.kind == DAPValue::Float ? lhs.floatVal
                                                    : static_cast<double>(lhs.intVal);
            double r = rhs.kind == DAPValue::Float ? rhs.floatVal
                                                    : static_cast<double>(rhs.intVal);
            return DAPValue::makeBool(l <= r);
        }
        break;
    case Op::Greater:
        if (lhs.kind == DAPValue::Integer && rhs.kind == DAPValue::Integer)
            return DAPValue::makeBool(lhs.intVal > rhs.intVal);
        if (lhs.kind == DAPValue::Float || rhs.kind == DAPValue::Float) {
            double l = lhs.kind == DAPValue::Float ? lhs.floatVal
                                                    : static_cast<double>(lhs.intVal);
            double r = rhs.kind == DAPValue::Float ? rhs.floatVal
                                                    : static_cast<double>(rhs.intVal);
            return DAPValue::makeBool(l > r);
        }
        break;
    case Op::GreaterEq:
        if (lhs.kind == DAPValue::Integer && rhs.kind == DAPValue::Integer)
            return DAPValue::makeBool(lhs.intVal >= rhs.intVal);
        if (lhs.kind == DAPValue::Float || rhs.kind == DAPValue::Float) {
            double l = lhs.kind == DAPValue::Float ? lhs.floatVal
                                                    : static_cast<double>(lhs.intVal);
            double r = rhs.kind == DAPValue::Float ? rhs.floatVal
                                                    : static_cast<double>(rhs.intVal);
            return DAPValue::makeBool(l >= r);
        }
        break;
    case Op::And:
        return DAPValue::makeBool(lhs.isTruthy() && rhs.isTruthy());
    case Op::Or:
        return DAPValue::makeBool(lhs.isTruthy() || rhs.isTruthy());
    case Op::BitAnd:
        if (lhs.kind == DAPValue::Integer && rhs.kind == DAPValue::Integer)
            return DAPValue::makeInt(lhs.intVal & rhs.intVal);
        break;
    case Op::BitOr:
        if (lhs.kind == DAPValue::Integer && rhs.kind == DAPValue::Integer)
            return DAPValue::makeInt(lhs.intVal | rhs.intVal);
        break;
    case Op::BitXor:
        if (lhs.kind == DAPValue::Integer && rhs.kind == DAPValue::Integer)
            return DAPValue::makeInt(lhs.intVal ^ rhs.intVal);
        break;
    case Op::Shl:
        if (lhs.kind == DAPValue::Integer && rhs.kind == DAPValue::Integer)
            return DAPValue::makeInt(lhs.intVal << rhs.intVal);
        break;
    case Op::Shr:
        if (lhs.kind == DAPValue::Integer && rhs.kind == DAPValue::Integer)
            return DAPValue::makeInt(lhs.intVal >> rhs.intVal);
        break;
    case Op::NilCoalesce:
        return lhs.kind != DAPValue::Nil ? lhs : rhs;
    }
    return DAPValue::makeNil();
}

DAPValue DAPInterpreter::evaluateUnaryExpr(const UnaryExpr *expr) {
    auto operand = evaluateExpr(expr->getOperand());
    using Op = UnaryExpr::Op;
    switch (expr->getOp()) {
    case Op::Negate:
        if (operand.kind == DAPValue::Integer)
            return DAPValue::makeInt(-operand.intVal);
        if (operand.kind == DAPValue::Float)
            return DAPValue::makeFloat(-operand.floatVal);
        break;
    case Op::Not:
        return DAPValue::makeBool(!operand.isTruthy());
    case Op::BitNot:
        if (operand.kind == DAPValue::Integer)
            return DAPValue::makeInt(~operand.intVal);
        break;
    }
    return DAPValue::makeNil();
}

DAPValue DAPInterpreter::evaluateCallExpr(const CallExpr *expr) {
    // Get function name
    std::string funcName;
    if (auto *id = dynamic_cast<const IdentifierExpr *>(expr->getCallee())) {
        funcName = id->getName();
    } else if (auto *member = dynamic_cast<const MemberExpr *>(expr->getCallee())) {
        funcName = member->getMember();
    }

    // Evaluate arguments
    std::vector<DAPValue> argVals;
    for (const auto &arg : expr->getArgs()) {
        argVals.push_back(evaluateExpr(arg.get()));
    }

    // Check built-in functions
    if (isBuiltin(funcName)) {
        return callBuiltin(funcName, argVals);
    }

    // Look up user function
    auto it = functions_.find(funcName);
    if (it == functions_.end()) {
        // Try struct constructor
        auto sit = structs_.find(funcName);
        if (sit != structs_.end()) {
            std::map<std::string, DAPValue> fields;
            const auto &fieldDecls = sit->second->getFields();
            for (size_t i = 0; i < fieldDecls.size() && i < argVals.size(); ++i) {
                fields[fieldDecls[i]->getName()] = std::move(argVals[i]);
            }
            return DAPValue::makeStruct(funcName, std::move(fields));
        }
        return DAPValue::makeNil();
    }

    const FuncDecl *func = it->second;
    if (!func->hasBody()) return DAPValue::makeNil();

    // Create call frame
    CallFrame frame;
    frame.functionName = funcName;
    frame.funcDecl = func;
    frame.frameId = nextFrameId_++;
    frame.location = func->getBody()->getStartLoc();
    frame.currentBlock = func->getBody();
    frame.stmtIndex = 0;

    // Bind parameters
    const auto &params = func->getParams();
    for (size_t i = 0; i < params.size() && i < argVals.size(); ++i) {
        frame.locals[params[i].name] = std::move(argVals[i]);
    }

    callStack_.push_back(std::move(frame));

    // Execute the function body
    auto result = executeBlock(func->getBody());

    DAPValue retVal;
    if (result.signal == StepResult::Return) {
        retVal = std::move(result.returnValue);
    }

    // If paused inside the function, don't pop frame yet
    if (result.state == ExecState::Paused) {
        return DAPValue::makeNil(); // Will be resumed later
    }

    callStack_.pop_back();
    return retVal;
}

DAPValue DAPInterpreter::evaluateMemberExpr(const MemberExpr *expr) {
    auto obj = evaluateExpr(expr->getObject());
    auto member = expr->getMember();

    if (obj.kind == DAPValue::Struct) {
        auto it = obj.structVal.find(member);
        if (it != obj.structVal.end())
            return it->second;
    }
    if (obj.kind == DAPValue::Array) {
        if (member == "length" || member == "count")
            return DAPValue::makeInt(static_cast<int64_t>(obj.arrayVal.size()));
    }
    if (obj.kind == DAPValue::String) {
        if (member == "length")
            return DAPValue::makeInt(static_cast<int64_t>(obj.strVal.size()));
    }
    return DAPValue::makeNil();
}

DAPValue DAPInterpreter::evaluateIndexExpr(const IndexExpr *expr) {
    auto base = evaluateExpr(expr->getBase());
    auto index = evaluateExpr(expr->getIndex());

    if (base.kind == DAPValue::Array && index.kind == DAPValue::Integer) {
        auto idx = index.intVal;
        if (idx >= 0 && idx < static_cast<int64_t>(base.arrayVal.size()))
            return base.arrayVal[static_cast<size_t>(idx)];
    }
    return DAPValue::makeNil();
}

DAPValue DAPInterpreter::evaluateAssignExpr(const AssignExpr *expr) {
    auto newVal = evaluateExpr(expr->getValue());

    if (auto *id = dynamic_cast<const IdentifierExpr *>(expr->getTarget())) {
        auto name = id->getName();
        using Op = AssignExpr::Op;
        switch (expr->getOp()) {
        case Op::Assign:
            setVariable(name, newVal);
            break;
        case Op::AddAssign: {
            auto &cur = lookupVariable(name);
            if (cur.kind == DAPValue::Integer && newVal.kind == DAPValue::Integer)
                cur.intVal += newVal.intVal;
            else if (cur.kind == DAPValue::Float)
                cur.floatVal += (newVal.kind == DAPValue::Float ? newVal.floatVal
                                                                 : static_cast<double>(newVal.intVal));
            else if (cur.kind == DAPValue::String && newVal.kind == DAPValue::String)
                cur.strVal += newVal.strVal;
            break;
        }
        case Op::SubAssign: {
            auto &cur = lookupVariable(name);
            if (cur.kind == DAPValue::Integer && newVal.kind == DAPValue::Integer)
                cur.intVal -= newVal.intVal;
            else if (cur.kind == DAPValue::Float)
                cur.floatVal -= (newVal.kind == DAPValue::Float ? newVal.floatVal
                                                                 : static_cast<double>(newVal.intVal));
            break;
        }
        case Op::MulAssign: {
            auto &cur = lookupVariable(name);
            if (cur.kind == DAPValue::Integer && newVal.kind == DAPValue::Integer)
                cur.intVal *= newVal.intVal;
            else if (cur.kind == DAPValue::Float)
                cur.floatVal *= (newVal.kind == DAPValue::Float ? newVal.floatVal
                                                                 : static_cast<double>(newVal.intVal));
            break;
        }
        case Op::DivAssign: {
            auto &cur = lookupVariable(name);
            if (cur.kind == DAPValue::Integer && newVal.kind == DAPValue::Integer && newVal.intVal != 0)
                cur.intVal /= newVal.intVal;
            else if (cur.kind == DAPValue::Float && newVal.floatVal != 0.0)
                cur.floatVal /= (newVal.kind == DAPValue::Float ? newVal.floatVal
                                                                 : static_cast<double>(newVal.intVal));
            break;
        }
        case Op::ModAssign: {
            auto &cur = lookupVariable(name);
            if (cur.kind == DAPValue::Integer && newVal.kind == DAPValue::Integer && newVal.intVal != 0)
                cur.intVal %= newVal.intVal;
            break;
        }
        }
    } else if (auto *member = dynamic_cast<const MemberExpr *>(expr->getTarget())) {
        // Struct member assignment: obj.field = val
        if (auto *objId = dynamic_cast<const IdentifierExpr *>(member->getObject())) {
            auto &obj = lookupVariable(objId->getName());
            if (obj.kind == DAPValue::Struct) {
                obj.structVal[member->getMember()] = std::move(newVal);
            }
        }
    } else if (auto *idx = dynamic_cast<const IndexExpr *>(expr->getTarget())) {
        // Array index assignment: arr[i] = val
        if (auto *baseId = dynamic_cast<const IdentifierExpr *>(idx->getBase())) {
            auto &arr = lookupVariable(baseId->getName());
            auto indexVal = evaluateExpr(idx->getIndex());
            if (arr.kind == DAPValue::Array && indexVal.kind == DAPValue::Integer) {
                auto i = indexVal.intVal;
                if (i >= 0 && i < static_cast<int64_t>(arr.arrayVal.size()))
                    arr.arrayVal[static_cast<size_t>(i)] = std::move(newVal);
            }
        }
    }
    return newVal;
}

// ============================================================
// Variable lookup/store
// ============================================================

DAPValue &DAPInterpreter::lookupVariable(const std::string &name) {
    // Search from top of call stack
    for (auto it = callStack_.rbegin(); it != callStack_.rend(); ++it) {
        auto found = it->locals.find(name);
        if (found != it->locals.end())
            return found->second;
    }
    // Create in current frame
    if (!callStack_.empty()) {
        return callStack_.back().locals[name];
    }
    static DAPValue nil;
    nil = DAPValue::makeNil();
    return nil;
}

void DAPInterpreter::setVariable(const std::string &name, DAPValue val) {
    // Set in current (top) frame
    if (!callStack_.empty()) {
        callStack_.back().locals[name] = std::move(val);
    }
}

// ============================================================
// Built-in functions
// ============================================================

bool DAPInterpreter::isBuiltin(const std::string &name) const {
    static const std::set<std::string> builtins = {
        "println", "print", "toString", "toInt", "toFloat",
        "len", "push", "pop", "abs", "min", "max"
    };
    return builtins.count(name) > 0;
}

DAPValue DAPInterpreter::callBuiltin(const std::string &name,
                                      const std::vector<DAPValue> &args) {
    if (name == "println") {
        std::string output;
        for (size_t i = 0; i < args.size(); ++i) {
            if (i > 0) output += " ";
            if (args[i].kind == DAPValue::String)
                output += args[i].strVal;
            else
                output += args[i].display();
        }
        output += "\n";
        outputBuffer_ += output;
        return DAPValue::makeNil();
    }
    if (name == "print") {
        std::string output;
        for (size_t i = 0; i < args.size(); ++i) {
            if (i > 0) output += " ";
            if (args[i].kind == DAPValue::String)
                output += args[i].strVal;
            else
                output += args[i].display();
        }
        outputBuffer_ += output;
        return DAPValue::makeNil();
    }
    if (name == "toString") {
        if (!args.empty())
            return DAPValue::makeString(args[0].display());
        return DAPValue::makeString("");
    }
    if (name == "toInt") {
        if (!args.empty()) {
            if (args[0].kind == DAPValue::Float)
                return DAPValue::makeInt(static_cast<int64_t>(args[0].floatVal));
            if (args[0].kind == DAPValue::String) {
                long v = std::strtol(args[0].strVal.c_str(), nullptr, 10);
                return DAPValue::makeInt(v);
            }
            return DAPValue::makeInt(args[0].intVal);
        }
        return DAPValue::makeInt(0);
    }
    if (name == "toFloat") {
        if (!args.empty()) {
            if (args[0].kind == DAPValue::Integer)
                return DAPValue::makeFloat(static_cast<double>(args[0].intVal));
            if (args[0].kind == DAPValue::String) {
                double v = std::strtod(args[0].strVal.c_str(), nullptr);
                return DAPValue::makeFloat(v);
            }
            return DAPValue::makeFloat(args[0].floatVal);
        }
        return DAPValue::makeFloat(0.0);
    }
    if (name == "len") {
        if (!args.empty()) {
            if (args[0].kind == DAPValue::Array)
                return DAPValue::makeInt(
                    static_cast<int64_t>(args[0].arrayVal.size()));
            if (args[0].kind == DAPValue::String)
                return DAPValue::makeInt(
                    static_cast<int64_t>(args[0].strVal.size()));
        }
        return DAPValue::makeInt(0);
    }
    if (name == "push") {
        // push modifies the first argument (array) by adding the second
        // Since we pass by value, this is a no-op on the actual variable
        // In a real interpreter we'd need reference semantics
        return DAPValue::makeNil();
    }
    if (name == "pop") {
        return DAPValue::makeNil();
    }
    if (name == "abs") {
        if (!args.empty()) {
            if (args[0].kind == DAPValue::Integer)
                return DAPValue::makeInt(
                    args[0].intVal < 0 ? -args[0].intVal : args[0].intVal);
            if (args[0].kind == DAPValue::Float)
                return DAPValue::makeFloat(std::fabs(args[0].floatVal));
        }
        return DAPValue::makeInt(0);
    }
    if (name == "min") {
        if (args.size() >= 2) {
            if (args[0].kind == DAPValue::Integer && args[1].kind == DAPValue::Integer)
                return DAPValue::makeInt(
                    args[0].intVal < args[1].intVal ? args[0].intVal : args[1].intVal);
            if (args[0].kind == DAPValue::Float || args[1].kind == DAPValue::Float) {
                double a = args[0].kind == DAPValue::Float ? args[0].floatVal
                                                            : static_cast<double>(args[0].intVal);
                double b = args[1].kind == DAPValue::Float ? args[1].floatVal
                                                            : static_cast<double>(args[1].intVal);
                return DAPValue::makeFloat(a < b ? a : b);
            }
        }
        return DAPValue::makeNil();
    }
    if (name == "max") {
        if (args.size() >= 2) {
            if (args[0].kind == DAPValue::Integer && args[1].kind == DAPValue::Integer)
                return DAPValue::makeInt(
                    args[0].intVal > args[1].intVal ? args[0].intVal : args[1].intVal);
            if (args[0].kind == DAPValue::Float || args[1].kind == DAPValue::Float) {
                double a = args[0].kind == DAPValue::Float ? args[0].floatVal
                                                            : static_cast<double>(args[0].intVal);
                double b = args[1].kind == DAPValue::Float ? args[1].floatVal
                                                            : static_cast<double>(args[1].intVal);
                return DAPValue::makeFloat(a > b ? a : b);
            }
        }
        return DAPValue::makeNil();
    }
    return DAPValue::makeNil();
}

} // namespace liva
