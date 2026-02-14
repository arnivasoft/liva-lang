#pragma once

#include "liva/AST/Decl.h"
#include "liva/Common/SourceLocation.h"
#include "liva/LSP/LSPServer.h" // JSONValue, parseJSON
#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace liva {

// ============================================================
// DAPValue — Interpreter value type
// ============================================================

struct DAPValue {
    enum Kind { Nil, Integer, Float, Bool, String, Array, Struct };
    Kind kind = Nil;
    int64_t intVal = 0;
    double floatVal = 0.0;
    bool boolVal = false;
    std::string strVal;
    std::vector<DAPValue> arrayVal;
    std::map<std::string, DAPValue> structVal;
    std::string structTypeName; // for Struct kind

    std::string display() const;
    std::string typeName() const;
    bool isTruthy() const;

    static DAPValue makeNil() { return DAPValue(); }
    static DAPValue makeInt(int64_t v) {
        DAPValue r;
        r.kind = Integer;
        r.intVal = v;
        return r;
    }
    static DAPValue makeFloat(double v) {
        DAPValue r;
        r.kind = Float;
        r.floatVal = v;
        return r;
    }
    static DAPValue makeBool(bool v) {
        DAPValue r;
        r.kind = Bool;
        r.boolVal = v;
        return r;
    }
    static DAPValue makeString(const std::string &v) {
        DAPValue r;
        r.kind = String;
        r.strVal = v;
        return r;
    }
    static DAPValue makeArray(std::vector<DAPValue> v) {
        DAPValue r;
        r.kind = Array;
        r.arrayVal = std::move(v);
        return r;
    }
    static DAPValue makeStruct(const std::string &name, std::map<std::string, DAPValue> fields) {
        DAPValue r;
        r.kind = Struct;
        r.structTypeName = name;
        r.structVal = std::move(fields);
        return r;
    }
};

// ============================================================
// CallFrame — call stack frame
// ============================================================

struct CallFrame {
    std::string functionName;
    SourceLocation location;
    const FuncDecl *funcDecl = nullptr;
    std::map<std::string, DAPValue> locals;
    int frameId = 0;
    // For resumable execution
    const BlockStmt *currentBlock = nullptr;
    size_t stmtIndex = 0;
};

// ============================================================
// ExecState / StepMode / StepResult
// ============================================================

enum class ExecState { Running, Paused, Terminated, Error };
enum class StepMode { Continue, Next, StepIn, StepOut };

struct StepResult {
    ExecState state = ExecState::Running;
    std::string error;
    enum Signal { None, Break, Continue, Return } signal = None;
    DAPValue returnValue;
};

// ============================================================
// DAPInterpreter — tree-walking interpreter for debugging
// ============================================================

class DAPInterpreter {
public:
    DAPInterpreter();

    void load(TranslationUnit *tu);
    bool start();
    StepResult resume(StepMode mode);
    void setBreakpoints(const std::string &path, const std::vector<int> &lines);
    const std::vector<CallFrame> &getCallStack() const { return callStack_; }
    const std::map<std::string, DAPValue> &getFrameLocals(int frameId) const;
    ExecState getState() const { return state_; }
    int getExitCode() const { return exitCode_; }
    const std::string &getOutput() const { return outputBuffer_; }

    // Stop reason for DAP event
    const std::string &getStopReason() const { return stopReason_; }

private:
    // Statement execution
    StepResult executeBlock(const BlockStmt *block, size_t startIdx = 0);
    StepResult executeStatement(const ASTNode *stmt);
    StepResult executeVarDecl(const VarDecl *decl);
    StepResult executeExprStmt(const ExprStmt *stmt);
    StepResult executeReturnStmt(const ReturnStmt *stmt);
    StepResult executeIfStmt(const IfStmt *stmt);
    StepResult executeWhileStmt(const WhileStmt *stmt);
    StepResult executeForStmt(const ForStmt *stmt);

    // Expression evaluation
    DAPValue evaluateExpr(const Expr *expr);
    DAPValue evaluateBinaryExpr(const BinaryExpr *expr);
    DAPValue evaluateUnaryExpr(const UnaryExpr *expr);
    DAPValue evaluateCallExpr(const CallExpr *expr);
    DAPValue evaluateMemberExpr(const MemberExpr *expr);
    DAPValue evaluateIndexExpr(const IndexExpr *expr);
    DAPValue evaluateAssignExpr(const AssignExpr *expr);

    // Variable lookup/store
    DAPValue &lookupVariable(const std::string &name);
    void setVariable(const std::string &name, DAPValue val);

    // Breakpoint/step control
    bool shouldPause(const SourceLocation &loc);

    // Built-in functions
    DAPValue callBuiltin(const std::string &name, const std::vector<DAPValue> &args);
    bool isBuiltin(const std::string &name) const;

    // State
    TranslationUnit *tu_ = nullptr;
    std::vector<CallFrame> callStack_;
    ExecState state_ = ExecState::Terminated;
    StepMode stepMode_ = StepMode::Continue;
    int stepFrameDepth_ = 0;
    std::map<std::string, std::set<int>> breakpoints_;
    std::map<std::string, const FuncDecl *> functions_;
    std::map<std::string, const StructDecl *> structs_;
    std::string outputBuffer_;
    std::string stopReason_;
    std::string runtimeError_;
    int exitCode_ = 0;
    int nextFrameId_ = 1;
    static const std::map<std::string, DAPValue> emptyLocals_;
};

// ============================================================
// DAPServer — Debug Adapter Protocol handler
// ============================================================

class DAPServer {
public:
    DAPServer();

    /// Run the stdio event loop (blocks until exit)
    int run();

    /// Process a single DAP message and return the response (for testing)
    std::string handleMessage(const std::string &json);

    /// Retrieve and clear buffered events (for testing)
    std::vector<std::string> takeEvents();

private:
    // Transport
    bool readMessage(std::string &out);
    void writeMessage(const std::string &json);
    JSONValue dispatch(const JSONValue &msg);

    // Response/Event builders
    JSONValue makeResponse(int64_t reqSeq, const std::string &cmd,
                           bool success, JSONValue body = JSONValue());
    JSONValue makeErrorResponse(int64_t reqSeq, const std::string &cmd,
                                const std::string &msg);
    void sendEvent(const std::string &event, JSONValue body = JSONValue());

    // Run interpreter and send appropriate events
    void runInterpreter(StepMode mode);

    // Handlers
    JSONValue handleInitialize(int64_t seq, const JSONValue &args);
    JSONValue handleLaunch(int64_t seq, const JSONValue &args);
    JSONValue handleDisconnect(int64_t seq, const JSONValue &args);
    JSONValue handleSetBreakpoints(int64_t seq, const JSONValue &args);
    JSONValue handleSetExceptionBreakpoints(int64_t seq, const JSONValue &args);
    JSONValue handleConfigurationDone(int64_t seq, const JSONValue &args);
    JSONValue handleContinue(int64_t seq, const JSONValue &args);
    JSONValue handleNext(int64_t seq, const JSONValue &args);
    JSONValue handleStepIn(int64_t seq, const JSONValue &args);
    JSONValue handleStepOut(int64_t seq, const JSONValue &args);
    JSONValue handleThreads(int64_t seq, const JSONValue &args);
    JSONValue handleStackTrace(int64_t seq, const JSONValue &args);
    JSONValue handleScopes(int64_t seq, const JSONValue &args);
    JSONValue handleVariables(int64_t seq, const JSONValue &args);

    // Compile source to AST
    bool compileSource(const std::string &source, const std::string &filename);

    // State
    DAPInterpreter interpreter_;
    std::unique_ptr<TranslationUnit> tu_;
    std::unique_ptr<SourceManager> sm_;
    int64_t nextSeq_ = 1;
    bool useStdio_ = true;
    std::vector<std::string> pendingEvents_;
    std::map<int, int> variableRefs_; // varRef -> frameId
    int nextVarRef_ = 1;
    std::string launchedFile_;
    std::string sourceContent_;
};

} // namespace liva
