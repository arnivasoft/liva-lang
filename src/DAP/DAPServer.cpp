#include "liva/DAP/DAPServer.h"
#include "liva/Lexer/Lexer.h"
#include "liva/Parser/Parser.h"
#include "liva/Sema/Sema.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

namespace liva {

// ============================================================
// DAPServer
// ============================================================

DAPServer::DAPServer() = default;

int DAPServer::run() {
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif

    while (true) {
        std::string msg;
        if (!readMessage(msg))
            break;

        auto parsed = parseJSON(msg);
        if (!parsed.success)
            continue;

        auto response = dispatch(parsed.value);
        if (!response.isNull()) {
            writeMessage(response.serialize());
        }

        // Send pending events
        for (const auto &ev : pendingEvents_) {
            if (useStdio_)
                writeMessage(ev);
        }
        pendingEvents_.clear();
    }
    return 0;
}

std::string DAPServer::handleMessage(const std::string &json) {
    useStdio_ = false;
    auto parsed = parseJSON(json);
    if (!parsed.success)
        return "";

    auto response = dispatch(parsed.value);
    if (response.isNull())
        return "";
    return response.serialize();
}

std::vector<std::string> DAPServer::takeEvents() {
    std::vector<std::string> result;
    result.swap(pendingEvents_);
    return result;
}

// ============================================================
// Transport (Content-Length header, same as LSP)
// ============================================================

bool DAPServer::readMessage(std::string &out) {
    // Read "Content-Length: <n>\r\n\r\n"
    std::string header;
    int contentLength = -1;
    while (true) {
        int c = std::fgetc(stdin);
        if (c == EOF)
            return false;
        header += static_cast<char>(c);
        if (header.size() >= 4 &&
            header.substr(header.size() - 4) == "\r\n\r\n") {
            const char *cl = "Content-Length: ";
            auto pos = header.find(cl);
            if (pos != std::string::npos) {
                contentLength =
                    static_cast<int>(std::strtol(header.c_str() + pos + std::strlen(cl),
                                                 nullptr, 10));
            }
            break;
        }
    }
    if (contentLength <= 0)
        return false;
    out.resize(contentLength);
    size_t bytesRead = std::fread(&out[0], 1, contentLength, stdin);
    return bytesRead == static_cast<size_t>(contentLength);
}

void DAPServer::writeMessage(const std::string &json) {
    std::fprintf(stdout, "Content-Length: %zu\r\n\r\n%s",
                 json.size(), json.c_str());
    std::fflush(stdout);
}

// ============================================================
// Dispatch
// ============================================================

JSONValue DAPServer::dispatch(const JSONValue &msg) {
    auto type = msg["type"].getString();
    if (type != "request")
        return JSONValue();

    auto command = msg["command"].getString();
    auto seq = msg["seq"].getInteger();
    const auto &args = msg["arguments"];

    if (command == "initialize")
        return handleInitialize(seq, args);
    if (command == "launch")
        return handleLaunch(seq, args);
    if (command == "disconnect")
        return handleDisconnect(seq, args);
    if (command == "setBreakpoints")
        return handleSetBreakpoints(seq, args);
    if (command == "setExceptionBreakpoints")
        return handleSetExceptionBreakpoints(seq, args);
    if (command == "configurationDone")
        return handleConfigurationDone(seq, args);
    if (command == "continue")
        return handleContinue(seq, args);
    if (command == "next")
        return handleNext(seq, args);
    if (command == "stepIn")
        return handleStepIn(seq, args);
    if (command == "stepOut")
        return handleStepOut(seq, args);
    if (command == "threads")
        return handleThreads(seq, args);
    if (command == "stackTrace")
        return handleStackTrace(seq, args);
    if (command == "scopes")
        return handleScopes(seq, args);
    if (command == "variables")
        return handleVariables(seq, args);
    if (command == "evaluate")
        return handleEvaluate(seq, args);

    return makeErrorResponse(seq, command, "unknown command: " + command);
}

// ============================================================
// Response/Event builders
// ============================================================

JSONValue DAPServer::makeResponse(int64_t reqSeq, const std::string &cmd,
                                  bool success, JSONValue body) {
    auto resp = JSONValue::object();
    resp.set("seq", JSONValue(nextSeq_++));
    resp.set("type", JSONValue("response"));
    resp.set("request_seq", JSONValue(reqSeq));
    resp.set("command", JSONValue(cmd));
    resp.set("success", JSONValue(success));
    if (!body.isNull())
        resp.set("body", std::move(body));
    return resp;
}

JSONValue DAPServer::makeErrorResponse(int64_t reqSeq, const std::string &cmd,
                                       const std::string &msg) {
    auto resp = makeResponse(reqSeq, cmd, false);
    resp.set("message", JSONValue(msg));
    return resp;
}

void DAPServer::sendEvent(const std::string &event, JSONValue body) {
    auto ev = JSONValue::object();
    ev.set("seq", JSONValue(nextSeq_++));
    ev.set("type", JSONValue("event"));
    ev.set("event", JSONValue(event));
    if (!body.isNull())
        ev.set("body", std::move(body));
    pendingEvents_.push_back(ev.serialize());
}

// ============================================================
// Compile source helper
// ============================================================

bool DAPServer::compileSource(const std::string &source,
                              const std::string &filename) {
    sm_ = std::make_unique<SourceManager>(filename, source);
    DiagnosticsEngine diag;

    Lexer lexer(*sm_, diag);
    if (diag.hasErrors())
        return false;

    Parser parser(lexer, diag);
    auto tu = parser.parseTranslationUnit();
    if (!tu || diag.hasErrors())
        return false;

    TypeChecker tc(diag);
    tc.check(*tu);
    if (diag.hasErrors())
        return false;

    tu_ = std::move(tu);
    return true;
}

// ============================================================
// Handlers
// ============================================================

JSONValue DAPServer::handleInitialize(int64_t seq, const JSONValue & /*args*/) {
    auto body = JSONValue::object();
    body.set("supportsConfigurationDoneRequest", JSONValue(true));
    body.set("supportsFunctionBreakpoints", JSONValue(false));
    body.set("supportsConditionalBreakpoints", JSONValue(false));
    body.set("supportsEvaluateForHovers", JSONValue(true));

    // Send initialized event
    sendEvent("initialized");

    return makeResponse(seq, "initialize", true, std::move(body));
}

JSONValue DAPServer::handleLaunch(int64_t seq, const JSONValue &args) {
    auto program = args["program"].getString();
    if (program.empty())
        return makeErrorResponse(seq, "launch", "no program specified");

    // For testing: if source is provided directly, use it
    auto source = args["source"].getString();
    if (source.empty()) {
        // Read from file
        std::FILE *f = std::fopen(program.c_str(), "r");
        if (!f)
            return makeErrorResponse(seq, "launch",
                                     "cannot open file: " + program);
        std::string content;
        char buf[4096];
        while (size_t n = std::fread(buf, 1, sizeof(buf), f))
            content.append(buf, n);
        std::fclose(f);
        source = content;
    }

    launchedFile_ = program;
    sourceContent_ = source;

    if (!compileSource(source, program))
        return makeErrorResponse(seq, "launch", "compilation failed");

    interpreter_.load(tu_.get());
    return makeResponse(seq, "launch", true);
}

JSONValue DAPServer::handleDisconnect(int64_t seq, const JSONValue & /*args*/) {
    return makeResponse(seq, "disconnect", true);
}

JSONValue DAPServer::handleSetBreakpoints(int64_t seq, const JSONValue &args) {
    auto sourcePath = args["source"]["path"].getString();
    std::vector<int> lines;

    auto body = JSONValue::object();
    auto bpArray = JSONValue::array();

    if (args["breakpoints"].isArray()) {
        for (const auto &bp : args["breakpoints"].getArray()) {
            int line = static_cast<int>(bp["line"].getInteger());
            lines.push_back(line);

            auto bpObj = JSONValue::object();
            bpObj.set("verified", JSONValue(true));
            bpObj.set("line", JSONValue(static_cast<int64_t>(line)));
            bpArray.push(std::move(bpObj));
        }
    }

    interpreter_.setBreakpoints(sourcePath, lines);
    body.set("breakpoints", std::move(bpArray));
    return makeResponse(seq, "setBreakpoints", true, std::move(body));
}

JSONValue DAPServer::handleSetExceptionBreakpoints(int64_t seq,
                                                    const JSONValue & /*args*/) {
    return makeResponse(seq, "setExceptionBreakpoints", true);
}

JSONValue DAPServer::handleConfigurationDone(int64_t seq,
                                              const JSONValue & /*args*/) {
    auto resp = makeResponse(seq, "configurationDone", true);

    // Start the interpreter and run to first breakpoint
    if (interpreter_.start()) {
        runInterpreter(StepMode::Continue);
    } else {
        // No main function or error
        auto body = JSONValue::object();
        body.set("exitCode", JSONValue(static_cast<int64_t>(1)));
        sendEvent("exited", std::move(body));
        sendEvent("terminated");
    }

    return resp;
}

void DAPServer::runInterpreter(StepMode mode) {
    auto result = interpreter_.resume(mode);

    // Flush interpreter output (println etc.) as DAP output events
    auto output = interpreter_.takeOutput();
    if (!output.empty()) {
        auto outBody = JSONValue::object();
        outBody.set("category", JSONValue("stdout"));
        outBody.set("output", JSONValue(output));
        sendEvent("output", std::move(outBody));
    }

    if (result.state == ExecState::Paused) {
        auto body = JSONValue::object();
        body.set("reason", JSONValue(interpreter_.getStopReason()));
        body.set("threadId", JSONValue(static_cast<int64_t>(1)));
        sendEvent("stopped", std::move(body));
    } else if (result.state == ExecState::Terminated) {
        auto exitBody = JSONValue::object();
        exitBody.set("exitCode",
                     JSONValue(static_cast<int64_t>(interpreter_.getExitCode())));
        sendEvent("exited", std::move(exitBody));
        sendEvent("terminated");
    } else if (result.state == ExecState::Error) {
        auto outBody = JSONValue::object();
        outBody.set("category", JSONValue("stderr"));
        outBody.set("output", JSONValue(result.error + "\n"));
        sendEvent("output", std::move(outBody));

        auto exitBody = JSONValue::object();
        exitBody.set("exitCode", JSONValue(static_cast<int64_t>(1)));
        sendEvent("exited", std::move(exitBody));
        sendEvent("terminated");
    }
}

JSONValue DAPServer::handleContinue(int64_t seq, const JSONValue & /*args*/) {
    auto resp = makeResponse(seq, "continue", true);
    runInterpreter(StepMode::Continue);
    return resp;
}

JSONValue DAPServer::handleNext(int64_t seq, const JSONValue & /*args*/) {
    auto resp = makeResponse(seq, "next", true);
    runInterpreter(StepMode::Next);
    return resp;
}

JSONValue DAPServer::handleStepIn(int64_t seq, const JSONValue & /*args*/) {
    auto resp = makeResponse(seq, "stepIn", true);
    runInterpreter(StepMode::StepIn);
    return resp;
}

JSONValue DAPServer::handleStepOut(int64_t seq, const JSONValue & /*args*/) {
    auto resp = makeResponse(seq, "stepOut", true);
    runInterpreter(StepMode::StepOut);
    return resp;
}

JSONValue DAPServer::handleThreads(int64_t seq, const JSONValue & /*args*/) {
    auto body = JSONValue::object();
    auto threads = JSONValue::array();
    auto t = JSONValue::object();
    t.set("id", JSONValue(static_cast<int64_t>(1)));
    t.set("name", JSONValue("main"));
    threads.push(std::move(t));
    body.set("threads", std::move(threads));
    return makeResponse(seq, "threads", true, std::move(body));
}

JSONValue DAPServer::handleStackTrace(int64_t seq, const JSONValue & /*args*/) {
    auto body = JSONValue::object();
    auto frames = JSONValue::array();

    const auto &callStack = interpreter_.getCallStack();
    for (int i = static_cast<int>(callStack.size()) - 1; i >= 0; --i) {
        const auto &cf = callStack[i];
        auto frame = JSONValue::object();
        frame.set("id", JSONValue(static_cast<int64_t>(cf.frameId)));
        frame.set("name", JSONValue(cf.functionName));
        frame.set("line", JSONValue(static_cast<int64_t>(cf.location.line)));
        frame.set("column", JSONValue(static_cast<int64_t>(cf.location.column)));

        auto source = JSONValue::object();
        source.set("path", JSONValue(launchedFile_));
        frame.set("source", std::move(source));

        frames.push(std::move(frame));
    }

    body.set("stackFrames", std::move(frames));
    body.set("totalFrames",
             JSONValue(static_cast<int64_t>(callStack.size())));
    return makeResponse(seq, "stackTrace", true, std::move(body));
}

JSONValue DAPServer::handleScopes(int64_t seq, const JSONValue &args) {
    int frameId = static_cast<int>(args["frameId"].getInteger());

    // Map variable reference to frame
    int varRef = nextVarRef_++;
    variableRefs_[varRef] = frameId;

    auto body = JSONValue::object();
    auto scopes = JSONValue::array();

    auto scope = JSONValue::object();
    scope.set("name", JSONValue("Locals"));
    scope.set("variablesReference", JSONValue(static_cast<int64_t>(varRef)));
    scope.set("expensive", JSONValue(false));
    scopes.push(std::move(scope));

    body.set("scopes", std::move(scopes));
    return makeResponse(seq, "scopes", true, std::move(body));
}

JSONValue DAPServer::handleVariables(int64_t seq, const JSONValue &args) {
    int varRef = static_cast<int>(args["variablesReference"].getInteger());
    auto body = JSONValue::object();
    auto vars = JSONValue::array();

    // Helper: assign varRef for struct/array values, 0 for primitives
    auto assignRef = [&](const DAPValue &val) -> int64_t {
        if (val.kind == DAPValue::Struct || val.kind == DAPValue::Array) {
            int ref = nextVarRef_++;
            structVarRefs_[ref] = val;
            return static_cast<int64_t>(ref);
        }
        return 0;
    };

    // Check if this is a struct/array sub-reference
    auto sit = structVarRefs_.find(varRef);
    if (sit != structVarRefs_.end()) {
        const auto &parent = sit->second;
        if (parent.kind == DAPValue::Struct) {
            for (const auto &kv : parent.structVal) {
                auto v = JSONValue::object();
                v.set("name", JSONValue(kv.first));
                v.set("value", JSONValue(kv.second.display()));
                v.set("type", JSONValue(kv.second.typeName()));
                v.set("variablesReference", JSONValue(assignRef(kv.second)));
                vars.push(std::move(v));
            }
        } else if (parent.kind == DAPValue::Array) {
            for (size_t i = 0; i < parent.arrayVal.size(); ++i) {
                auto v = JSONValue::object();
                v.set("name", JSONValue("[" + std::to_string(i) + "]"));
                v.set("value", JSONValue(parent.arrayVal[i].display()));
                v.set("type", JSONValue(parent.arrayVal[i].typeName()));
                v.set("variablesReference", JSONValue(assignRef(parent.arrayVal[i])));
                vars.push(std::move(v));
            }
        }
    } else {
        // Frame-level locals
        auto it = variableRefs_.find(varRef);
        if (it != variableRefs_.end()) {
            int frameId = it->second;
            const auto &locals = interpreter_.getFrameLocals(frameId);

            for (const auto &kv : locals) {
                auto v = JSONValue::object();
                v.set("name", JSONValue(kv.first));
                v.set("value", JSONValue(kv.second.display()));
                v.set("type", JSONValue(kv.second.typeName()));
                v.set("variablesReference", JSONValue(assignRef(kv.second)));
                vars.push(std::move(v));
            }
        }
    }

    body.set("variables", std::move(vars));
    return makeResponse(seq, "variables", true, std::move(body));
}

JSONValue DAPServer::handleEvaluate(int64_t seq, const JSONValue &args) {
    auto expression = args["expression"].getString();
    auto frameId = static_cast<int>(args["frameId"].getInteger());

    if (expression.empty())
        return makeErrorResponse(seq, "evaluate", "empty expression");

    // Split expression by '.' for member access
    std::vector<std::string> segments;
    std::string current;
    for (size_t i = 0; i < expression.size(); ++i) {
        char c = expression[i];
        if (c == '.') {
            if (!current.empty()) {
                segments.push_back(current);
                current.clear();
            }
        } else if (c == '[') {
            // Push current segment if any
            if (!current.empty()) {
                segments.push_back(current);
                current.clear();
            }
            // Read index: [N]
            ++i;
            std::string idx;
            while (i < expression.size() && expression[i] != ']') {
                idx += expression[i++];
            }
            // Store as "[N]" to distinguish from field names
            segments.push_back("[" + idx + "]");
        } else {
            current += c;
        }
    }
    if (!current.empty())
        segments.push_back(current);

    if (segments.empty())
        return makeErrorResponse(seq, "evaluate", "invalid expression");

    // Look up first segment in frame locals
    const auto &locals = interpreter_.getFrameLocals(frameId);
    auto it = locals.find(segments[0]);
    if (it == locals.end())
        return makeErrorResponse(seq, "evaluate",
                                 "variable not found: " + segments[0]);

    const DAPValue *val = &it->second;

    // Navigate remaining segments
    for (size_t i = 1; i < segments.size(); ++i) {
        const auto &seg = segments[i];
        if (seg.size() >= 2 && seg[0] == '[' && seg.back() == ']') {
            // Array index
            if (val->kind != DAPValue::Array)
                return makeErrorResponse(seq, "evaluate",
                                         "not an array for index access");
            auto idxStr = seg.substr(1, seg.size() - 2);
            long idx = std::strtol(idxStr.c_str(), nullptr, 10);
            if (idx < 0 || static_cast<size_t>(idx) >= val->arrayVal.size())
                return makeErrorResponse(seq, "evaluate", "index out of bounds");
            val = &val->arrayVal[static_cast<size_t>(idx)];
        } else {
            // Struct field
            if (val->kind != DAPValue::Struct)
                return makeErrorResponse(seq, "evaluate",
                                         "not a struct for member access");
            auto fit = val->structVal.find(seg);
            if (fit == val->structVal.end())
                return makeErrorResponse(seq, "evaluate",
                                         "field not found: " + seg);
            val = &fit->second;
        }
    }

    auto body = JSONValue::object();
    body.set("result", JSONValue(val->display()));
    body.set("type", JSONValue(val->typeName()));

    int64_t ref = 0;
    if (val->kind == DAPValue::Struct || val->kind == DAPValue::Array) {
        int r = nextVarRef_++;
        structVarRefs_[r] = *val;
        ref = static_cast<int64_t>(r);
    }
    body.set("variablesReference", JSONValue(ref));

    return makeResponse(seq, "evaluate", true, std::move(body));
}

} // namespace liva
