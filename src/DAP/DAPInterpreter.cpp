#include "liva/DAP/DAPServer.h"
#include "liva/AST/Stmt.h"
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
    implMethods_.clear();
    structs_.clear();
    classes_.clear();
    outputBuffer_.clear();
    runtimeError_.clear();
    exitCode_ = 0;
    nextFrameId_ = 1;

    if (!tu_) return;

    // Collect top-level functions, structs, and impl methods
    for (const auto &decl : tu_->getDeclarations()) {
        if (auto *fd = dynamic_cast<const FuncDecl *>(decl.get())) {
            functions_[fd->getName()] = fd;
        } else if (auto *sd = dynamic_cast<const StructDecl *>(decl.get())) {
            structs_[sd->getName()] = sd;
        } else if (auto *cd = dynamic_cast<const ClassDecl *>(decl.get())) {
            classes_[cd->getName()] = cd;
        } else if (auto *impl = dynamic_cast<const ImplDecl *>(decl.get())) {
            const auto &typeName = impl->getTypeName();
            for (const auto &method : impl->getMethods()) {
                implMethods_[typeName][method->getName()] = method.get();
            }
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
    resumeFromLine_ = lastPausedLine_;

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
    return {state_, runtimeError_, StepResult::None, {}};
}

void DAPInterpreter::setBreakpoints(const std::string &path,
                                     const std::vector<BreakpointInfo> &breakpoints) {
    breakpoints_[path] = breakpoints;
}

// ============================================================
// Mini Expression Evaluator (string-based, for watch/conditional BP)
// ============================================================

namespace {

enum class ExprTokenKind {
    Ident, IntLit, FloatLit, StringLit, BoolLit, NilLit,
    Plus, Minus, Star, Slash, Percent,
    EqEq, NotEq, Less, LessEq, Greater, GreaterEq,
    And, Or, Not,
    Dot, LBrack, RBrack, LParen, RParen, Comma,
    Eof
};

struct ExprToken {
    ExprTokenKind kind = ExprTokenKind::Eof;
    std::string text;
    int64_t intVal = 0;
    double floatVal = 0.0;
};

std::vector<ExprToken> tokenizeExpr(const std::string &src) {
    std::vector<ExprToken> tokens;
    size_t i = 0;
    while (i < src.size()) {
        char c = src[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') { ++i; continue; }
        ExprToken tok;
        if (std::isalpha(c) || c == '_') {
            std::string id;
            while (i < src.size() && (std::isalnum(src[i]) || src[i] == '_'))
                id += src[i++];
            if (id == "true") { tok.kind = ExprTokenKind::BoolLit; tok.intVal = 1; }
            else if (id == "false") { tok.kind = ExprTokenKind::BoolLit; tok.intVal = 0; }
            else if (id == "nil") { tok.kind = ExprTokenKind::NilLit; }
            else { tok.kind = ExprTokenKind::Ident; tok.text = id; }
            tokens.push_back(tok);
            continue;
        }
        if (std::isdigit(c)) {
            std::string num;
            bool isFloat = false;
            while (i < src.size() && (std::isdigit(src[i]) || src[i] == '.')) {
                if (src[i] == '.') {
                    if (isFloat) break;
                    isFloat = true;
                }
                num += src[i++];
            }
            if (isFloat) {
                tok.kind = ExprTokenKind::FloatLit;
                tok.floatVal = std::strtod(num.c_str(), nullptr);
            } else {
                tok.kind = ExprTokenKind::IntLit;
                tok.intVal = std::strtoll(num.c_str(), nullptr, 10);
            }
            tokens.push_back(tok);
            continue;
        }
        if (c == '"') {
            ++i;
            std::string s;
            while (i < src.size() && src[i] != '"') {
                if (src[i] == '\\' && i + 1 < src.size()) { s += src[i + 1]; i += 2; }
                else { s += src[i++]; }
            }
            if (i < src.size()) ++i; // skip closing quote
            tok.kind = ExprTokenKind::StringLit;
            tok.text = s;
            tokens.push_back(tok);
            continue;
        }
        // Two-char operators
        if (i + 1 < src.size()) {
            char c2 = src[i + 1];
            if (c == '=' && c2 == '=') { tok.kind = ExprTokenKind::EqEq; i += 2; tokens.push_back(tok); continue; }
            if (c == '!' && c2 == '=') { tok.kind = ExprTokenKind::NotEq; i += 2; tokens.push_back(tok); continue; }
            if (c == '<' && c2 == '=') { tok.kind = ExprTokenKind::LessEq; i += 2; tokens.push_back(tok); continue; }
            if (c == '>' && c2 == '=') { tok.kind = ExprTokenKind::GreaterEq; i += 2; tokens.push_back(tok); continue; }
            if (c == '&' && c2 == '&') { tok.kind = ExprTokenKind::And; i += 2; tokens.push_back(tok); continue; }
            if (c == '|' && c2 == '|') { tok.kind = ExprTokenKind::Or; i += 2; tokens.push_back(tok); continue; }
        }
        // Single-char operators
        switch (c) {
        case '+': tok.kind = ExprTokenKind::Plus; break;
        case '-': tok.kind = ExprTokenKind::Minus; break;
        case '*': tok.kind = ExprTokenKind::Star; break;
        case '/': tok.kind = ExprTokenKind::Slash; break;
        case '%': tok.kind = ExprTokenKind::Percent; break;
        case '<': tok.kind = ExprTokenKind::Less; break;
        case '>': tok.kind = ExprTokenKind::Greater; break;
        case '!': tok.kind = ExprTokenKind::Not; break;
        case '.': tok.kind = ExprTokenKind::Dot; break;
        case '[': tok.kind = ExprTokenKind::LBrack; break;
        case ']': tok.kind = ExprTokenKind::RBrack; break;
        case '(': tok.kind = ExprTokenKind::LParen; break;
        case ')': tok.kind = ExprTokenKind::RParen; break;
        case ',': tok.kind = ExprTokenKind::Comma; break;
        default: ++i; continue; // skip unknown
        }
        ++i;
        tokens.push_back(tok);
    }
    tokens.push_back({ExprTokenKind::Eof, "", 0, 0.0});
    return tokens;
}

class ExprParser {
public:
    ExprParser(const std::vector<ExprToken> &tokens, DAPInterpreter &interp, int frameId)
        : tokens_(tokens), interp_(interp), frameId_(frameId) {}

    DAPValue parse() {
        auto result = parseOr();
        return result;
    }

private:
    const std::vector<ExprToken> &tokens_;
    DAPInterpreter &interp_;
    int frameId_;
    size_t pos_ = 0;

    const ExprToken &peek() const {
        return pos_ < tokens_.size() ? tokens_[pos_] : tokens_.back();
    }
    const ExprToken &advance() {
        auto &tok = tokens_[pos_];
        if (pos_ < tokens_.size() - 1) ++pos_;
        return tok;
    }
    bool match(ExprTokenKind k) {
        if (peek().kind == k) { advance(); return true; }
        return false;
    }

    DAPValue parseOr() {
        auto left = parseAnd();
        while (peek().kind == ExprTokenKind::Or) {
            advance();
            auto right = parseAnd();
            left = DAPValue::makeBool(left.isTruthy() || right.isTruthy());
        }
        return left;
    }

    DAPValue parseAnd() {
        auto left = parseComparison();
        while (peek().kind == ExprTokenKind::And) {
            advance();
            auto right = parseComparison();
            left = DAPValue::makeBool(left.isTruthy() && right.isTruthy());
        }
        return left;
    }

    DAPValue parseComparison() {
        auto left = parseAddSub();
        while (true) {
            auto k = peek().kind;
            if (k != ExprTokenKind::EqEq && k != ExprTokenKind::NotEq &&
                k != ExprTokenKind::Less && k != ExprTokenKind::LessEq &&
                k != ExprTokenKind::Greater && k != ExprTokenKind::GreaterEq)
                break;
            advance();
            auto right = parseAddSub();
            double l = (left.kind == DAPValue::Float) ? left.floatVal : static_cast<double>(left.intVal);
            double r = (right.kind == DAPValue::Float) ? right.floatVal : static_cast<double>(right.intVal);
            bool useNum = (left.kind == DAPValue::Integer || left.kind == DAPValue::Float) &&
                          (right.kind == DAPValue::Integer || right.kind == DAPValue::Float);
            switch (k) {
            case ExprTokenKind::EqEq:
                if (left.kind == DAPValue::String && right.kind == DAPValue::String)
                    left = DAPValue::makeBool(left.strVal == right.strVal);
                else if (left.kind == DAPValue::Bool && right.kind == DAPValue::Bool)
                    left = DAPValue::makeBool(left.boolVal == right.boolVal);
                else if (useNum)
                    left = DAPValue::makeBool(l == r);
                else left = DAPValue::makeBool(false);
                break;
            case ExprTokenKind::NotEq:
                if (left.kind == DAPValue::String && right.kind == DAPValue::String)
                    left = DAPValue::makeBool(left.strVal != right.strVal);
                else if (left.kind == DAPValue::Bool && right.kind == DAPValue::Bool)
                    left = DAPValue::makeBool(left.boolVal != right.boolVal);
                else if (useNum)
                    left = DAPValue::makeBool(l != r);
                else left = DAPValue::makeBool(true);
                break;
            case ExprTokenKind::Less: left = DAPValue::makeBool(useNum && l < r); break;
            case ExprTokenKind::LessEq: left = DAPValue::makeBool(useNum && l <= r); break;
            case ExprTokenKind::Greater: left = DAPValue::makeBool(useNum && l > r); break;
            case ExprTokenKind::GreaterEq: left = DAPValue::makeBool(useNum && l >= r); break;
            default: break;
            }
        }
        return left;
    }

    DAPValue parseAddSub() {
        auto left = parseMulDiv();
        while (peek().kind == ExprTokenKind::Plus || peek().kind == ExprTokenKind::Minus) {
            auto op = advance().kind;
            auto right = parseMulDiv();
            if (op == ExprTokenKind::Plus) {
                if (left.kind == DAPValue::Integer && right.kind == DAPValue::Integer)
                    left = DAPValue::makeInt(left.intVal + right.intVal);
                else if (left.kind == DAPValue::String && right.kind == DAPValue::String)
                    left = DAPValue::makeString(left.strVal + right.strVal);
                else {
                    double l = left.kind == DAPValue::Float ? left.floatVal : static_cast<double>(left.intVal);
                    double r = right.kind == DAPValue::Float ? right.floatVal : static_cast<double>(right.intVal);
                    left = DAPValue::makeFloat(l + r);
                }
            } else {
                if (left.kind == DAPValue::Integer && right.kind == DAPValue::Integer)
                    left = DAPValue::makeInt(left.intVal - right.intVal);
                else {
                    double l = left.kind == DAPValue::Float ? left.floatVal : static_cast<double>(left.intVal);
                    double r = right.kind == DAPValue::Float ? right.floatVal : static_cast<double>(right.intVal);
                    left = DAPValue::makeFloat(l - r);
                }
            }
        }
        return left;
    }

    DAPValue parseMulDiv() {
        auto left = parseUnary();
        while (peek().kind == ExprTokenKind::Star || peek().kind == ExprTokenKind::Slash ||
               peek().kind == ExprTokenKind::Percent) {
            auto op = advance().kind;
            auto right = parseUnary();
            if (op == ExprTokenKind::Star) {
                if (left.kind == DAPValue::Integer && right.kind == DAPValue::Integer)
                    left = DAPValue::makeInt(left.intVal * right.intVal);
                else {
                    double l = left.kind == DAPValue::Float ? left.floatVal : static_cast<double>(left.intVal);
                    double r = right.kind == DAPValue::Float ? right.floatVal : static_cast<double>(right.intVal);
                    left = DAPValue::makeFloat(l * r);
                }
            } else if (op == ExprTokenKind::Slash) {
                if (left.kind == DAPValue::Integer && right.kind == DAPValue::Integer) {
                    left = right.intVal != 0 ? DAPValue::makeInt(left.intVal / right.intVal) : DAPValue::makeInt(0);
                } else {
                    double l = left.kind == DAPValue::Float ? left.floatVal : static_cast<double>(left.intVal);
                    double r = right.kind == DAPValue::Float ? right.floatVal : static_cast<double>(right.intVal);
                    left = r != 0.0 ? DAPValue::makeFloat(l / r) : DAPValue::makeFloat(0.0);
                }
            } else {
                if (left.kind == DAPValue::Integer && right.kind == DAPValue::Integer)
                    left = right.intVal != 0 ? DAPValue::makeInt(left.intVal % right.intVal) : DAPValue::makeInt(0);
                else left = DAPValue::makeNil();
            }
        }
        return left;
    }

    DAPValue parseUnary() {
        if (peek().kind == ExprTokenKind::Not) {
            advance();
            auto val = parseUnary();
            return DAPValue::makeBool(!val.isTruthy());
        }
        if (peek().kind == ExprTokenKind::Minus) {
            advance();
            auto val = parseUnary();
            if (val.kind == DAPValue::Integer) return DAPValue::makeInt(-val.intVal);
            if (val.kind == DAPValue::Float) return DAPValue::makeFloat(-val.floatVal);
            return DAPValue::makeNil();
        }
        return parsePrimary();
    }

    DAPValue parsePrimary() {
        auto &tok = peek();
        if (tok.kind == ExprTokenKind::IntLit) {
            advance();
            return DAPValue::makeInt(tok.intVal);
        }
        if (tok.kind == ExprTokenKind::FloatLit) {
            advance();
            return DAPValue::makeFloat(tok.floatVal);
        }
        if (tok.kind == ExprTokenKind::StringLit) {
            advance();
            return DAPValue::makeString(tok.text);
        }
        if (tok.kind == ExprTokenKind::BoolLit) {
            advance();
            return DAPValue::makeBool(tok.intVal != 0);
        }
        if (tok.kind == ExprTokenKind::NilLit) {
            advance();
            return DAPValue::makeNil();
        }
        if (tok.kind == ExprTokenKind::LParen) {
            advance();
            auto val = parseOr();
            match(ExprTokenKind::RParen);
            return val;
        }
        if (tok.kind == ExprTokenKind::Ident) {
            std::string name = tok.text;
            advance();
            // Look up variable
            DAPValue val = interp_.evaluateExprString(name, frameId_);
            // Postfix: .field, [index]
            while (true) {
                if (peek().kind == ExprTokenKind::Dot) {
                    advance();
                    if (peek().kind == ExprTokenKind::Ident) {
                        std::string field = peek().text;
                        advance();
                        if (val.kind == DAPValue::Struct) {
                            auto it = val.structVal.find(field);
                            val = it != val.structVal.end() ? it->second : DAPValue::makeNil();
                        } else if (val.kind == DAPValue::Array) {
                            if (field == "length" || field == "count")
                                val = DAPValue::makeInt(static_cast<int64_t>(val.arrayVal.size()));
                            else if (field == "isEmpty")
                                val = DAPValue::makeBool(val.arrayVal.empty());
                            else val = DAPValue::makeNil();
                        } else if (val.kind == DAPValue::String) {
                            if (field == "length")
                                val = DAPValue::makeInt(static_cast<int64_t>(val.strVal.size()));
                            else val = DAPValue::makeNil();
                        } else val = DAPValue::makeNil();
                    }
                } else if (peek().kind == ExprTokenKind::LBrack) {
                    advance();
                    auto idx = parseOr();
                    match(ExprTokenKind::RBrack);
                    if (val.kind == DAPValue::Array && idx.kind == DAPValue::Integer) {
                        auto i = idx.intVal;
                        if (i >= 0 && i < static_cast<int64_t>(val.arrayVal.size()))
                            val = val.arrayVal[static_cast<size_t>(i)];
                        else val = DAPValue::makeNil();
                    } else val = DAPValue::makeNil();
                } else break;
            }
            return val;
        }
        return DAPValue::makeNil();
    }
};

} // anonymous namespace

DAPValue DAPInterpreter::evaluateExprString(const std::string &expr, int frameId) {
    if (expr.empty()) return DAPValue::makeNil();

    // Simple variable lookup (no operators, just a plain identifier)
    bool isSimpleIdent = true;
    for (char c : expr) {
        if (!std::isalnum(c) && c != '_') { isSimpleIdent = false; break; }
    }
    if (isSimpleIdent) {
        // Look up in specified frame or current
        if (frameId >= 0) {
            const auto &locals = getFrameLocals(frameId);
            auto it = locals.find(expr);
            if (it != locals.end()) return it->second;
        }
        // Fall back to interpreter variable lookup
        for (auto it = callStack_.rbegin(); it != callStack_.rend(); ++it) {
            auto found = it->locals.find(expr);
            if (found != it->locals.end())
                return found->second;
        }
        return DAPValue::makeNil();
    }

    auto tokens = tokenizeExpr(expr);
    ExprParser parser(tokens, *this, frameId);
    return parser.parse();
}

const std::map<std::string, DAPValue> &
DAPInterpreter::getFrameLocals(int frameId) const {
    for (const auto &frame : callStack_) {
        if (frame.frameId == frameId)
            return frame.locals;
    }
    return emptyLocals_;
}

bool DAPInterpreter::checkHitCondition(const std::string &cond, int hitCount) {
    if (cond.empty()) return true;
    // Parse patterns: ">N", ">=N", "==N", "<N", "<=N", "%N", or just "N" (== N)
    size_t i = 0;
    std::string op;
    while (i < cond.size() && !std::isdigit(cond[i]) && cond[i] != '-') {
        op += cond[i++];
    }
    long n = std::strtol(cond.c_str() + i, nullptr, 10);
    if (op.empty() || op == "==") return hitCount == static_cast<int>(n);
    if (op == ">") return hitCount > static_cast<int>(n);
    if (op == ">=") return hitCount >= static_cast<int>(n);
    if (op == "<") return hitCount < static_cast<int>(n);
    if (op == "<=") return hitCount <= static_cast<int>(n);
    if (op == "%") return n > 0 && (hitCount % static_cast<int>(n)) == 0;
    return true;
}

std::string DAPInterpreter::interpolateLogMessage(const std::string &msg) {
    std::string result;
    size_t i = 0;
    while (i < msg.size()) {
        if (msg[i] == '{') {
            ++i;
            std::string expr;
            int braceDepth = 1;
            while (i < msg.size() && braceDepth > 0) {
                if (msg[i] == '{') ++braceDepth;
                else if (msg[i] == '}') { if (--braceDepth == 0) { ++i; break; } }
                expr += msg[i++];
            }
            auto val = evaluateExprString(expr);
            if (val.kind == DAPValue::String)
                result += val.strVal;
            else
                result += val.display();
        } else {
            result += msg[i++];
        }
    }
    return result;
}

bool DAPInterpreter::checkBreakpointAtLine(int line) {
    for (auto &bpEntry : breakpoints_) {
        for (auto &bp : bpEntry.second) {
            if (bp.line != line) continue;
            bp.hitCount++;

            // Check hit condition
            if (!bp.hitCondition.empty() && !checkHitCondition(bp.hitCondition, bp.hitCount))
                continue;

            // Check condition expression
            if (!bp.condition.empty()) {
                auto val = evaluateExprString(bp.condition);
                if (!val.isTruthy()) continue;
            }

            // Logpoint: output message but don't pause
            if (!bp.logMessage.empty()) {
                outputBuffer_ += interpolateLogMessage(bp.logMessage) + "\n";
                return false; // Don't pause for logpoints
            }

            stopReason_ = "breakpoint";
            lastPausedLine_ = line;
            return true;
        }
    }
    return false;
}

bool DAPInterpreter::shouldPause(const SourceLocation &loc) {
    if (!loc.isValid()) return false;

    int line = static_cast<int>(loc.line);

    // Resume navigation: skip statements until we pass the previously paused line.
    // This prevents re-pausing at the same statement after a step/continue.
    if (resumeFromLine_ > 0) {
        if (line == resumeFromLine_) {
            resumeFromLine_ = 0; // Reached the pause point, clear
        }
        return false; // Don't pause during navigation
    }

    switch (stepMode_) {
    case StepMode::Continue: {
        return checkBreakpointAtLine(line);
    }
    case StepMode::StepIn:
        // Pause at every statement
        stopReason_ = "step";
        lastPausedLine_ = line;
        return true;
    case StepMode::Next: {
        // Pause at same or shallower frame depth
        int currentDepth = static_cast<int>(callStack_.size());
        if (currentDepth <= stepFrameDepth_) {
            stopReason_ = "step";
            lastPausedLine_ = line;
            return true;
        }
        // Also check breakpoints
        return checkBreakpointAtLine(line);
    }
    case StepMode::StepOut: {
        // Pause when frame depth decreases
        int currentDepth = static_cast<int>(callStack_.size());
        if (currentDepth < stepFrameDepth_) {
            stopReason_ = "step";
            lastPausedLine_ = line;
            return true;
        }
        // Also check breakpoints
        return checkBreakpointAtLine(line);
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
        if (result.state == ExecState::Paused) {
            // Save position in the frame's main block so resume() starts here
            if (!callStack_.empty() &&
                callStack_.back().currentBlock == block) {
                callStack_.back().stmtIndex = i;
            }
            return result;
        }
        if (result.state == ExecState::Error ||
            result.signal == StepResult::Return ||
            result.signal == StepResult::Break ||
            result.signal == StepResult::Continue) {
            return result;
        }
    }
    return {ExecState::Running, "", StepResult::None, {}};
}

StepResult DAPInterpreter::executeStatement(const ASTNode *stmt) {
    if (!stmt) return {ExecState::Running, "", StepResult::None, {}};

    // Check if we should pause before this statement
    auto loc = stmt->getStartLoc();
    if (loc.isValid() && shouldPause(loc)) {
        // Update frame location
        if (!callStack_.empty()) {
            callStack_.back().location = loc;
        }
        state_ = ExecState::Paused;
        return {ExecState::Paused, "", StepResult::None, {}};
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
    case NK::IfLetStmt:
        return executeIfLetStmt(static_cast<const IfLetStmt *>(stmt));
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
            return {ExecState::Running, "", StepResult::None, {}};
        }
        return {ExecState::Running, "", StepResult::None, {}};
    }
}

StepResult DAPInterpreter::executeVarDecl(const VarDecl *decl) {
    DAPValue val;
    if (decl->hasInit()) {
        val = evaluateExpr(decl->getInit());
        if (state_ == ExecState::Paused)
            return {ExecState::Paused, "", StepResult::None, {}};
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
    return {ExecState::Running, "", StepResult::None, {}};
}

StepResult DAPInterpreter::executeExprStmt(const ExprStmt *stmt) {
    evaluateExpr(stmt->getExpr());
    if (state_ == ExecState::Paused)
        return {ExecState::Paused, "", StepResult::None, {}};
    return {ExecState::Running, "", StepResult::None, {}};
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
    if (state_ == ExecState::Paused) return {ExecState::Paused, "", StepResult::None, {}};
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
    return {ExecState::Running, "", StepResult::None, {}};
}

StepResult DAPInterpreter::executeWhileStmt(const WhileStmt *stmt) {
    int iterLimit = 100000;
    while (iterLimit-- > 0) {
        auto cond = evaluateExpr(stmt->getCondition());
        if (state_ == ExecState::Paused) return {ExecState::Paused, "", StepResult::None, {}};
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
    return {ExecState::Running, "", StepResult::None, {}};
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
    return {ExecState::Running, "", StepResult::None, {}};
}

StepResult DAPInterpreter::executeIfLetStmt(const IfLetStmt *stmt) {
    auto val = evaluateExpr(stmt->getOptionalExpr());
    if (state_ == ExecState::Paused) return {ExecState::Paused, "", StepResult::None, {}};
    if (val.kind != DAPValue::Nil) {
        setVariable(stmt->getBindingName(), std::move(val));
        return executeBlock(stmt->getThenBody());
    } else if (stmt->hasElse()) {
        if (auto *block = dynamic_cast<const BlockStmt *>(stmt->getElseBody())) {
            return executeBlock(block);
        }
        return executeStatement(stmt->getElseBody());
    }
    return {ExecState::Running, "", StepResult::None, {}};
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
    case NK::MatchExpr: {
        auto *me = static_cast<const MatchExpr *>(expr);
        auto subject = evaluateExpr(me->getSubject());
        for (const auto &arm : me->getArms()) {
            // Bind pattern variable if present
            if (!arm.bindings.empty()) {
                setVariable(arm.bindings[0], subject);
            }
            // Check guard condition
            if (arm.guard) {
                auto guardVal = evaluateExpr(arm.guard.get());
                if (!guardVal.isTruthy()) continue;
            }
            // Wildcard "_" or matching pattern
            bool isWildcard = arm.patternNode &&
                               arm.patternNode->getKind() == Pattern::Kind::Wildcard;
            if (isWildcard || !arm.bindings.empty()) {
                auto result = evaluateExpr(arm.body.get());
                return result;
            }
        }
        return DAPValue::makeNil();
    }
    case NK::RangeExpr: {
        auto *re = static_cast<const RangeExpr *>(expr);
        auto startVal = evaluateExpr(re->getStart());
        auto endVal = evaluateExpr(re->getEnd());
        // Create an array for the range
        std::vector<DAPValue> elements;
        if (startVal.kind == DAPValue::Integer && endVal.kind == DAPValue::Integer) {
            int64_t end = re->isInclusive() ? endVal.intVal + 1 : endVal.intVal;
            for (int64_t i = startVal.intVal; i < end; ++i) {
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
    // Check if this is a method call (member.method(args))
    if (auto *member = dynamic_cast<const MemberExpr *>(expr->getCallee())) {
        return evaluateMethodCall(member, expr);
    }

    // Get function name
    std::string funcName;
    if (auto *id = dynamic_cast<const IdentifierExpr *>(expr->getCallee())) {
        funcName = id->getName();
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
    if (it != functions_.end()) {
        const FuncDecl *func = it->second;
        if (!func->hasBody()) return DAPValue::makeNil();

        CallFrame frame;
        frame.functionName = funcName;
        frame.funcDecl = func;
        frame.frameId = nextFrameId_++;
        frame.location = func->getBody()->getStartLoc();
        frame.currentBlock = func->getBody();
        frame.stmtIndex = 0;

        const auto &params = func->getParams();
        for (size_t i = 0; i < params.size() && i < argVals.size(); ++i) {
            frame.locals[params[i].name] = std::move(argVals[i]);
        }

        callStack_.push_back(std::move(frame));
        auto result = executeBlock(func->getBody());

        DAPValue retVal;
        if (result.signal == StepResult::Return) {
            retVal = std::move(result.returnValue);
        }
        if (result.state == ExecState::Paused) {
            return DAPValue::makeNil();
        }
        callStack_.pop_back();
        return retVal;
    }

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
    // Try class constructor
    auto cit = classes_.find(funcName);
    if (cit != classes_.end()) {
        std::map<std::string, DAPValue> fields;
        const auto classFields = cit->second->getFields();
        for (size_t i = 0; i < classFields.size() && i < argVals.size(); ++i) {
            fields[classFields[i]->getName()] = std::move(argVals[i]);
        }
        return DAPValue::makeStruct(funcName, std::move(fields));
    }
    return DAPValue::makeNil();
}

DAPValue DAPInterpreter::evaluateMethodCall(const MemberExpr *member,
                                             const CallExpr *call) {
    const std::string &methodName = member->getMember();

    // Evaluate arguments
    std::vector<DAPValue> argVals;
    for (const auto &arg : call->getArgs()) {
        argVals.push_back(evaluateExpr(arg.get()));
    }

    // Check if callee object is a type name (static method): Student.new(...)
    if (auto *objId = dynamic_cast<const IdentifierExpr *>(member->getObject())) {
        const std::string &objName = objId->getName();

        // Check if objName is a struct/class type → static method call
        auto implIt = implMethods_.find(objName);
        if (implIt != implMethods_.end()) {
            auto methIt = implIt->second.find(methodName);
            if (methIt != implIt->second.end()) {
                const FuncDecl *func = methIt->second;
                if (!func->isMethod() && func->hasBody()) {
                    // Static method: no self parameter
                    CallFrame frame;
                    frame.functionName = objName + "." + methodName;
                    frame.funcDecl = func;
                    frame.frameId = nextFrameId_++;
                    frame.location = func->getBody()->getStartLoc();
                    frame.currentBlock = func->getBody();
                    frame.stmtIndex = 0;

                    const auto &params = func->getParams();
                    for (size_t i = 0; i < params.size() && i < argVals.size(); ++i) {
                        frame.locals[params[i].name] = std::move(argVals[i]);
                    }

                    callStack_.push_back(std::move(frame));
                    auto result = executeBlock(func->getBody());

                    DAPValue retVal;
                    if (result.signal == StepResult::Return) {
                        retVal = std::move(result.returnValue);
                    }
                    if (result.state == ExecState::Paused) {
                        return DAPValue::makeNil();
                    }
                    callStack_.pop_back();
                    return retVal;
                }
            }
        }

        // If objName is a variable (not a type), fall through to instance method
        // Check if it's actually a variable
        bool isVariable = false;
        for (auto it = callStack_.rbegin(); it != callStack_.rend(); ++it) {
            if (it->locals.count(objName)) { isVariable = true; break; }
        }

        if (isVariable) {
            auto &obj = lookupVariable(objName);

            // Array built-in methods: push, pop, etc.
            if (obj.kind == DAPValue::Array) {
                if (methodName == "push" && !argVals.empty()) {
                    obj.arrayVal.push_back(std::move(argVals[0]));
                    return DAPValue::makeNil();
                }
                if (methodName == "pop" && !obj.arrayVal.empty()) {
                    auto val = std::move(obj.arrayVal.back());
                    obj.arrayVal.pop_back();
                    return val;
                }
                return DAPValue::makeNil();
            }

            // Instance method on struct: student.addGrade(92.5)
            if (obj.kind == DAPValue::Struct) {
                auto implIt2 = implMethods_.find(obj.structTypeName);
                if (implIt2 != implMethods_.end()) {
                    auto methIt2 = implIt2->second.find(methodName);
                    if (methIt2 != implIt2->second.end()) {
                        const FuncDecl *func = methIt2->second;
                        if (func->isMethod() && func->hasBody()) {
                            CallFrame frame;
                            frame.functionName = obj.structTypeName + "." + methodName;
                            frame.funcDecl = func;
                            frame.frameId = nextFrameId_++;
                            frame.location = func->getBody()->getStartLoc();
                            frame.currentBlock = func->getBody();
                            frame.stmtIndex = 0;

                            // Bind self as a copy; we'll write back after
                            frame.locals["self"] = obj;

                            // Bind remaining params (skip self param)
                            const auto &params = func->getParams();
                            size_t argIdx = 0;
                            for (size_t i = 0; i < params.size(); ++i) {
                                if (params[i].isSelf) continue;
                                if (argIdx < argVals.size()) {
                                    frame.locals[params[i].name] = std::move(argVals[argIdx++]);
                                }
                            }

                            bool isMutSelf = !params.empty() && params[0].isSelf && params[0].isMutRef;
                            callStack_.push_back(std::move(frame));
                            auto result = executeBlock(func->getBody());

                            DAPValue retVal;
                            if (result.signal == StepResult::Return) {
                                retVal = std::move(result.returnValue);
                            }
                            if (result.state == ExecState::Paused) {
                                return DAPValue::makeNil();
                            }

                            // Write back self for ref mut self methods
                            if (isMutSelf) {
                                auto &selfVal = callStack_.back().locals["self"];
                                obj = std::move(selfVal);
                            }

                            callStack_.pop_back();
                            return retVal;
                        }
                    }
                }
            }
        }
    }

    // Chained member method: student.grades.push(val)
    // Evaluate the object and try array methods
    auto obj = evaluateExpr(member->getObject());
    if (obj.kind == DAPValue::Array) {
        if (methodName == "push" && !argVals.empty()) {
            // For chained access we need to modify through the original variable
            // Try to handle common case: var.field.push(val)
            if (auto *innerMember = dynamic_cast<const MemberExpr *>(member->getObject())) {
                if (auto *baseId = dynamic_cast<const IdentifierExpr *>(innerMember->getObject())) {
                    auto &baseObj = lookupVariable(baseId->getName());
                    if (baseObj.kind == DAPValue::Struct) {
                        auto fIt = baseObj.structVal.find(innerMember->getMember());
                        if (fIt != baseObj.structVal.end() && fIt->second.kind == DAPValue::Array) {
                            fIt->second.arrayVal.push_back(std::move(argVals[0]));
                            return DAPValue::makeNil();
                        }
                    }
                }
            }
            return DAPValue::makeNil();
        }
    }

    // Built-in function fallback
    if (isBuiltin(methodName)) {
        return callBuiltin(methodName, argVals);
    }

    return DAPValue::makeNil();
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
        if (member == "isEmpty")
            return DAPValue::makeBool(obj.arrayVal.empty());
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
        "len", "push", "pop", "abs", "min", "max", "round"
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
        if (!args.empty()) {
            // For strings, return the value directly (no quotes)
            if (args[0].kind == DAPValue::String)
                return args[0];
            return DAPValue::makeString(args[0].display());
        }
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
    if (name == "round") {
        if (!args.empty()) {
            double val = args[0].kind == DAPValue::Float ? args[0].floatVal
                                                          : static_cast<double>(args[0].intVal);
            int decimals = 0;
            if (args.size() >= 2 && args[1].kind == DAPValue::Integer)
                decimals = static_cast<int>(args[1].intVal);
            double factor = std::pow(10.0, decimals);
            return DAPValue::makeFloat(std::round(val * factor) / factor);
        }
        return DAPValue::makeFloat(0.0);
    }
    return DAPValue::makeNil();
}

} // namespace liva
