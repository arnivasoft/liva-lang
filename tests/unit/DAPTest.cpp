#include "liva/DAP/DAPServer.h"
#include <gtest/gtest.h>

using namespace liva;

// ============================================================
// DAP Test Fixture
// ============================================================

class DAPTest : public ::testing::Test {
protected:
    DAPServer server;

    static std::string escapeForJSON(const std::string &s) {
        std::string result;
        for (char c : s) {
            switch (c) {
            case '"':  result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default:   result += c; break;
            }
        }
        return result;
    }

    std::string initRequest(int seq = 1) {
        return R"({"seq":)" + std::to_string(seq) +
               R"(,"type":"request","command":"initialize","arguments":{}})";
    }

    std::string launchRequest(const std::string &source, int seq = 2) {
        return R"({"seq":)" + std::to_string(seq) +
               R"(,"type":"request","command":"launch","arguments":{)"
               R"("program":"test.liva","source":")" +
               escapeForJSON(source) + R"("}})";
    }

    std::string setBreakpointsRequest(const std::string &path,
                                       const std::vector<int> &lines,
                                       int seq = 3) {
        std::string bps = "[";
        for (size_t i = 0; i < lines.size(); ++i) {
            if (i > 0) bps += ",";
            bps += R"({"line":)" + std::to_string(lines[i]) + "}";
        }
        bps += "]";
        return R"({"seq":)" + std::to_string(seq) +
               R"(,"type":"request","command":"setBreakpoints","arguments":{)"
               R"("source":{"path":")" + escapeForJSON(path) +
               R"("},"breakpoints":)" + bps + "}}";
    }

    std::string configDoneRequest(int seq = 4) {
        return R"({"seq":)" + std::to_string(seq) +
               R"(,"type":"request","command":"configurationDone","arguments":{}})";
    }

    std::string continueRequest(int seq) {
        return R"({"seq":)" + std::to_string(seq) +
               R"(,"type":"request","command":"continue","arguments":{"threadId":1}})";
    }

    std::string nextRequest(int seq) {
        return R"({"seq":)" + std::to_string(seq) +
               R"(,"type":"request","command":"next","arguments":{"threadId":1}})";
    }

    std::string stepInRequest(int seq) {
        return R"({"seq":)" + std::to_string(seq) +
               R"(,"type":"request","command":"stepIn","arguments":{"threadId":1}})";
    }

    std::string stepOutRequest(int seq) {
        return R"({"seq":)" + std::to_string(seq) +
               R"(,"type":"request","command":"stepOut","arguments":{"threadId":1}})";
    }

    std::string threadsRequest(int seq) {
        return R"({"seq":)" + std::to_string(seq) +
               R"(,"type":"request","command":"threads","arguments":{}})";
    }

    std::string stackTraceRequest(int seq) {
        return R"({"seq":)" + std::to_string(seq) +
               R"(,"type":"request","command":"stackTrace","arguments":{"threadId":1}})";
    }

    std::string scopesRequest(int frameId, int seq) {
        return R"({"seq":)" + std::to_string(seq) +
               R"(,"type":"request","command":"scopes","arguments":{"frameId":)" +
               std::to_string(frameId) + "}}";
    }

    std::string variablesRequest(int varRef, int seq) {
        return R"({"seq":)" + std::to_string(seq) +
               R"(,"type":"request","command":"variables","arguments":{"variablesReference":)" +
               std::to_string(varRef) + "}}";
    }

    std::string evaluateRequest(const std::string &expr, int frameId, int seq,
                                   const std::string &context = "hover") {
        return R"({"seq":)" + std::to_string(seq) +
               R"(,"type":"request","command":"evaluate","arguments":{)"
               R"("expression":")" + escapeForJSON(expr) +
               R"(","frameId":)" + std::to_string(frameId) +
               R"(,"context":")" + context + R"("}})";
    }

    std::string disconnectRequest(int seq) {
        return R"({"seq":)" + std::to_string(seq) +
               R"(,"type":"request","command":"disconnect","arguments":{}})";
    }

    std::string setExceptionBreakpointsRequest(int seq) {
        return R"({"seq":)" + std::to_string(seq) +
               R"(,"type":"request","command":"setExceptionBreakpoints","arguments":{"filters":[]}})";
    }

    JSONValue parseResponse(const std::string &resp) {
        auto r = parseJSON(resp);
        return r.success ? std::move(r.value) : JSONValue();
    }

    JSONValue parseEvent(const std::string &ev) {
        auto r = parseJSON(ev);
        return r.success ? std::move(r.value) : JSONValue();
    }

    // Helper: full init + launch + configDone (runs to first breakpoint or end)
    void initAndLaunch(const std::string &source,
                       const std::string &bpPath = "",
                       const std::vector<int> &bpLines = {}) {
        server.handleMessage(initRequest());
        server.takeEvents(); // initialized event
        server.handleMessage(launchRequest(source));
        server.takeEvents();
        if (!bpPath.empty()) {
            server.handleMessage(setBreakpointsRequest(bpPath, bpLines));
            server.takeEvents();
        }
        server.handleMessage(setExceptionBreakpointsRequest(5));
        server.takeEvents();
    }
};

// ============================================================
// Protocol Tests
// ============================================================

TEST_F(DAPTest, InitializeResponse) {
    auto resp = parseResponse(server.handleMessage(initRequest()));
    EXPECT_EQ(resp["type"].getString(), "response");
    EXPECT_EQ(resp["command"].getString(), "initialize");
    EXPECT_TRUE(resp["success"].getBool());
    EXPECT_TRUE(resp["body"]["supportsConfigurationDoneRequest"].getBool());
}

TEST_F(DAPTest, InitializedEvent) {
    server.handleMessage(initRequest());
    auto events = server.takeEvents();
    ASSERT_GE(events.size(), 1u);
    auto ev = parseEvent(events[0]);
    EXPECT_EQ(ev["type"].getString(), "event");
    EXPECT_EQ(ev["event"].getString(), "initialized");
}

TEST_F(DAPTest, LaunchValid) {
    server.handleMessage(initRequest());
    server.takeEvents();
    auto resp = parseResponse(server.handleMessage(
        launchRequest("func main() {\n    let x: i32 = 42\n}\n")));
    EXPECT_TRUE(resp["success"].getBool());
    EXPECT_EQ(resp["command"].getString(), "launch");
}

TEST_F(DAPTest, LaunchInvalid) {
    server.handleMessage(initRequest());
    server.takeEvents();
    auto resp = parseResponse(server.handleMessage(
        launchRequest("func main( {\n")));
    EXPECT_FALSE(resp["success"].getBool());
}

TEST_F(DAPTest, Disconnect) {
    server.handleMessage(initRequest());
    server.takeEvents();
    auto resp = parseResponse(server.handleMessage(disconnectRequest(2)));
    EXPECT_TRUE(resp["success"].getBool());
    EXPECT_EQ(resp["command"].getString(), "disconnect");
}

TEST_F(DAPTest, Threads) {
    server.handleMessage(initRequest());
    server.takeEvents();
    auto resp = parseResponse(server.handleMessage(threadsRequest(2)));
    EXPECT_TRUE(resp["success"].getBool());
    const auto &threads = resp["body"]["threads"].getArray();
    ASSERT_EQ(threads.size(), 1u);
    EXPECT_EQ(threads[0]["id"].getInteger(), 1);
    EXPECT_EQ(threads[0]["name"].getString(), "main");
}

// ============================================================
// Breakpoint Tests
// ============================================================

TEST_F(DAPTest, SetBreakpointsBasic) {
    server.handleMessage(initRequest());
    server.takeEvents();
    auto resp = parseResponse(server.handleMessage(
        setBreakpointsRequest("test.liva", {3, 5})));
    EXPECT_TRUE(resp["success"].getBool());
    const auto &bps = resp["body"]["breakpoints"].getArray();
    ASSERT_EQ(bps.size(), 2u);
    EXPECT_TRUE(bps[0]["verified"].getBool());
    EXPECT_EQ(bps[0]["line"].getInteger(), 3);
    EXPECT_TRUE(bps[1]["verified"].getBool());
    EXPECT_EQ(bps[1]["line"].getInteger(), 5);
}

TEST_F(DAPTest, BreakpointHit) {
    std::string src = "func main() {\n"
                      "    let x: i32 = 10\n"
                      "    let y: i32 = 20\n"
                      "    let z: i32 = 30\n"
                      "}\n";
    initAndLaunch(src, "test.liva", {3});
    server.handleMessage(configDoneRequest());
    auto events = server.takeEvents();
    // Should get a "stopped" event with reason "breakpoint"
    bool foundStopped = false;
    for (const auto &e : events) {
        auto ev = parseEvent(e);
        if (ev["event"].getString() == "stopped") {
            EXPECT_EQ(ev["body"]["reason"].getString(), "breakpoint");
            foundStopped = true;
        }
    }
    EXPECT_TRUE(foundStopped);
}

TEST_F(DAPTest, MultipleBreakpoints) {
    std::string src = "func main() {\n"
                      "    let a: i32 = 1\n"
                      "    let b: i32 = 2\n"
                      "    let c: i32 = 3\n"
                      "    let d: i32 = 4\n"
                      "}\n";
    initAndLaunch(src, "test.liva", {3, 5});
    server.handleMessage(configDoneRequest());
    auto events = server.takeEvents();
    // First breakpoint hit
    bool hit1 = false;
    for (const auto &e : events) {
        auto ev = parseEvent(e);
        if (ev["event"].getString() == "stopped") hit1 = true;
    }
    EXPECT_TRUE(hit1);

    // Continue to second breakpoint
    server.handleMessage(continueRequest(10));
    auto events2 = server.takeEvents();
    bool hit2 = false;
    for (const auto &e : events2) {
        auto ev = parseEvent(e);
        if (ev["event"].getString() == "stopped") hit2 = true;
    }
    EXPECT_TRUE(hit2);
}

TEST_F(DAPTest, ClearBreakpoints) {
    server.handleMessage(initRequest());
    server.takeEvents();
    // Set then clear breakpoints
    server.handleMessage(setBreakpointsRequest("test.liva", {3, 5}));
    auto resp = parseResponse(server.handleMessage(
        setBreakpointsRequest("test.liva", {})));
    EXPECT_TRUE(resp["success"].getBool());
    const auto &bps = resp["body"]["breakpoints"].getArray();
    EXPECT_EQ(bps.size(), 0u);
}

// ============================================================
// Execution Tests
// ============================================================

TEST_F(DAPTest, ContinueToEnd) {
    std::string src = "func main() {\n"
                      "    let x: i32 = 42\n"
                      "}\n";
    initAndLaunch(src);
    server.handleMessage(configDoneRequest());
    auto events = server.takeEvents();
    // Should terminate without stopping
    bool terminated = false;
    for (const auto &e : events) {
        auto ev = parseEvent(e);
        if (ev["event"].getString() == "terminated") terminated = true;
    }
    EXPECT_TRUE(terminated);
}

TEST_F(DAPTest, StepNext) {
    std::string src = "func main() {\n"
                      "    let x: i32 = 10\n"
                      "    let y: i32 = 20\n"
                      "    let z: i32 = 30\n"
                      "}\n";
    initAndLaunch(src, "test.liva", {2});
    server.handleMessage(configDoneRequest());
    server.takeEvents(); // stopped at line 2

    // Step next
    server.handleMessage(nextRequest(10));
    auto events = server.takeEvents();
    bool stepped = false;
    for (const auto &e : events) {
        auto ev = parseEvent(e);
        if (ev["event"].getString() == "stopped" &&
            ev["body"]["reason"].getString() == "step") {
            stepped = true;
        }
    }
    EXPECT_TRUE(stepped);
}

TEST_F(DAPTest, StepIn) {
    std::string src = "func add(a: i32, b: i32) -> i32 {\n"
                      "    return a + b\n"
                      "}\n"
                      "func main() {\n"
                      "    let result: i32 = add(1, 2)\n"
                      "}\n";
    initAndLaunch(src, "test.liva", {5});
    server.handleMessage(configDoneRequest());
    server.takeEvents(); // stopped at line 5

    // Step in should enter the function
    server.handleMessage(stepInRequest(10));
    auto events = server.takeEvents();
    bool steppedIn = false;
    for (const auto &e : events) {
        auto ev = parseEvent(e);
        if (ev["event"].getString() == "stopped") {
            steppedIn = true;
        }
    }
    EXPECT_TRUE(steppedIn);
}

TEST_F(DAPTest, StepOut) {
    std::string src = "func helper() -> i32 {\n"
                      "    let x: i32 = 42\n"
                      "    return x\n"
                      "}\n"
                      "func main() {\n"
                      "    let r: i32 = helper()\n"
                      "}\n";
    initAndLaunch(src, "test.liva", {2});
    server.handleMessage(configDoneRequest());
    server.takeEvents(); // stopped inside helper at line 2

    // Step out should return to main
    server.handleMessage(stepOutRequest(10));
    auto events = server.takeEvents();
    bool steppedOut = false;
    for (const auto &e : events) {
        auto ev = parseEvent(e);
        if (ev["event"].getString() == "stopped" ||
            ev["event"].getString() == "terminated") {
            steppedOut = true;
        }
    }
    EXPECT_TRUE(steppedOut);
}

// ============================================================
// Stack Tests
// ============================================================

TEST_F(DAPTest, StackTraceAtBreakpoint) {
    std::string src = "func main() {\n"
                      "    let x: i32 = 42\n"
                      "    let y: i32 = 10\n"
                      "}\n";
    initAndLaunch(src, "test.liva", {2});
    server.handleMessage(configDoneRequest());
    server.takeEvents(); // stopped

    auto resp = parseResponse(server.handleMessage(stackTraceRequest(10)));
    EXPECT_TRUE(resp["success"].getBool());
    const auto &frames = resp["body"]["stackFrames"].getArray();
    ASSERT_GE(frames.size(), 1u);
    EXPECT_EQ(frames[0]["name"].getString(), "main");
}

TEST_F(DAPTest, StackTraceNested) {
    std::string src = "func inner() -> i32 {\n"
                      "    let v: i32 = 99\n"
                      "    return v\n"
                      "}\n"
                      "func main() {\n"
                      "    let r: i32 = inner()\n"
                      "}\n";
    initAndLaunch(src, "test.liva", {2});
    server.handleMessage(configDoneRequest());
    server.takeEvents(); // stopped in inner()

    auto resp = parseResponse(server.handleMessage(stackTraceRequest(10)));
    EXPECT_TRUE(resp["success"].getBool());
    const auto &frames = resp["body"]["stackFrames"].getArray();
    ASSERT_GE(frames.size(), 2u);
    // Top frame should be inner
    EXPECT_EQ(frames[0]["name"].getString(), "inner");
    EXPECT_EQ(frames[1]["name"].getString(), "main");
}

// ============================================================
// Variable Tests
// ============================================================

TEST_F(DAPTest, LocalVariables) {
    std::string src = "func main() {\n"
                      "    let x: i32 = 42\n"
                      "    let y: i32 = 10\n"
                      "}\n";
    initAndLaunch(src, "test.liva", {3});
    server.handleMessage(configDoneRequest());
    server.takeEvents(); // stopped at line 3

    // Get stack trace to find frame id
    auto stResp = parseResponse(server.handleMessage(stackTraceRequest(10)));
    auto frameId = static_cast<int>(
        stResp["body"]["stackFrames"].getArray()[0]["id"].getInteger());

    // Get scopes
    auto scResp = parseResponse(server.handleMessage(scopesRequest(frameId, 11)));
    EXPECT_TRUE(scResp["success"].getBool());
    auto varRef = static_cast<int>(
        scResp["body"]["scopes"].getArray()[0]["variablesReference"].getInteger());

    // Get variables
    auto varResp = parseResponse(server.handleMessage(variablesRequest(varRef, 12)));
    EXPECT_TRUE(varResp["success"].getBool());
    const auto &vars = varResp["body"]["variables"].getArray();
    // Should have x=42 (y not yet declared at line 3 breakpoint)
    bool foundX = false;
    for (const auto &v : vars) {
        if (v["name"].getString() == "x") {
            EXPECT_EQ(v["value"].getString(), "42");
            foundX = true;
        }
    }
    EXPECT_TRUE(foundX);
}

TEST_F(DAPTest, IntegerVariable) {
    std::string src = "func main() {\n"
                      "    let x: i32 = 123\n"
                      "    let y: i32 = 0\n"
                      "}\n";
    initAndLaunch(src, "test.liva", {3});
    server.handleMessage(configDoneRequest());
    server.takeEvents();

    auto stResp = parseResponse(server.handleMessage(stackTraceRequest(10)));
    auto frameId = static_cast<int>(
        stResp["body"]["stackFrames"].getArray()[0]["id"].getInteger());
    auto scResp = parseResponse(server.handleMessage(scopesRequest(frameId, 11)));
    auto varRef = static_cast<int>(
        scResp["body"]["scopes"].getArray()[0]["variablesReference"].getInteger());
    auto varResp = parseResponse(server.handleMessage(variablesRequest(varRef, 12)));

    bool found = false;
    for (const auto &v : varResp["body"]["variables"].getArray()) {
        if (v["name"].getString() == "x") {
            EXPECT_EQ(v["value"].getString(), "123");
            EXPECT_EQ(v["type"].getString(), "i64");
            found = true;
        }
    }
    EXPECT_TRUE(found);
}

TEST_F(DAPTest, StringVariable) {
    std::string src = "func main() {\n"
                      "    let s = \"hello\"\n"
                      "    let x: i32 = 0\n"
                      "}\n";
    initAndLaunch(src, "test.liva", {3});
    server.handleMessage(configDoneRequest());
    server.takeEvents();

    auto stResp = parseResponse(server.handleMessage(stackTraceRequest(10)));
    const auto &frames = stResp["body"]["stackFrames"].getArray();
    ASSERT_GE(frames.size(), 1u);
    auto frameId = static_cast<int>(frames[0]["id"].getInteger());
    auto scResp = parseResponse(server.handleMessage(scopesRequest(frameId, 11)));
    auto varRef = static_cast<int>(
        scResp["body"]["scopes"].getArray()[0]["variablesReference"].getInteger());
    auto varResp = parseResponse(server.handleMessage(variablesRequest(varRef, 12)));

    bool found = false;
    for (const auto &v : varResp["body"]["variables"].getArray()) {
        if (v["name"].getString() == "s") {
            EXPECT_EQ(v["value"].getString(), "\"hello\"");
            EXPECT_EQ(v["type"].getString(), "String");
            found = true;
        }
    }
    EXPECT_TRUE(found);
}

TEST_F(DAPTest, BoolVariable) {
    std::string src = "func main() {\n"
                      "    let b = true\n"
                      "    let x: i32 = 0\n"
                      "}\n";
    initAndLaunch(src, "test.liva", {3});
    server.handleMessage(configDoneRequest());
    server.takeEvents();

    auto stResp = parseResponse(server.handleMessage(stackTraceRequest(10)));
    const auto &frames = stResp["body"]["stackFrames"].getArray();
    ASSERT_GE(frames.size(), 1u);
    auto frameId = static_cast<int>(frames[0]["id"].getInteger());
    auto scResp = parseResponse(server.handleMessage(scopesRequest(frameId, 11)));
    auto varRef = static_cast<int>(
        scResp["body"]["scopes"].getArray()[0]["variablesReference"].getInteger());
    auto varResp = parseResponse(server.handleMessage(variablesRequest(varRef, 12)));

    bool found = false;
    for (const auto &v : varResp["body"]["variables"].getArray()) {
        if (v["name"].getString() == "b") {
            EXPECT_EQ(v["value"].getString(), "true");
            EXPECT_EQ(v["type"].getString(), "Bool");
            found = true;
        }
    }
    EXPECT_TRUE(found);
}

TEST_F(DAPTest, StructVariable) {
    std::string src = "struct Point { var x: i32; var y: i32 }\n"
                      "func main() {\n"
                      "    let p: Point = Point { x: 10, y: 20 }\n"
                      "    let z: i32 = 0\n"
                      "}\n";
    initAndLaunch(src, "test.liva", {4});
    server.handleMessage(configDoneRequest());
    server.takeEvents();

    auto stResp = parseResponse(server.handleMessage(stackTraceRequest(10)));
    auto frameId = static_cast<int>(
        stResp["body"]["stackFrames"].getArray()[0]["id"].getInteger());
    auto scResp = parseResponse(server.handleMessage(scopesRequest(frameId, 11)));
    auto varRef = static_cast<int>(
        scResp["body"]["scopes"].getArray()[0]["variablesReference"].getInteger());
    auto varResp = parseResponse(server.handleMessage(variablesRequest(varRef, 12)));

    bool found = false;
    for (const auto &v : varResp["body"]["variables"].getArray()) {
        if (v["name"].getString() == "p") {
            EXPECT_EQ(v["type"].getString(), "Point");
            found = true;
        }
    }
    EXPECT_TRUE(found);
}

TEST_F(DAPTest, ArrayVariable) {
    std::string src = "func main() {\n"
                      "    let arr: [i32] = [1, 2, 3]\n"
                      "    let x: i32 = 0\n"
                      "}\n";
    initAndLaunch(src, "test.liva", {3});
    server.handleMessage(configDoneRequest());
    server.takeEvents();

    auto stResp = parseResponse(server.handleMessage(stackTraceRequest(10)));
    auto frameId = static_cast<int>(
        stResp["body"]["stackFrames"].getArray()[0]["id"].getInteger());
    auto scResp = parseResponse(server.handleMessage(scopesRequest(frameId, 11)));
    auto varRef = static_cast<int>(
        scResp["body"]["scopes"].getArray()[0]["variablesReference"].getInteger());
    auto varResp = parseResponse(server.handleMessage(variablesRequest(varRef, 12)));

    bool found = false;
    for (const auto &v : varResp["body"]["variables"].getArray()) {
        if (v["name"].getString() == "arr") {
            EXPECT_EQ(v["type"].getString(), "Array");
            found = true;
        }
    }
    EXPECT_TRUE(found);
}

TEST_F(DAPTest, VariableAfterAssignment) {
    std::string src = "func main() {\n"
                      "    var x: i32 = 10\n"
                      "    x = 20\n"
                      "    let y: i32 = 0\n"
                      "}\n";
    initAndLaunch(src, "test.liva", {4});
    server.handleMessage(configDoneRequest());
    server.takeEvents();

    auto stResp = parseResponse(server.handleMessage(stackTraceRequest(10)));
    auto frameId = static_cast<int>(
        stResp["body"]["stackFrames"].getArray()[0]["id"].getInteger());
    auto scResp = parseResponse(server.handleMessage(scopesRequest(frameId, 11)));
    auto varRef = static_cast<int>(
        scResp["body"]["scopes"].getArray()[0]["variablesReference"].getInteger());
    auto varResp = parseResponse(server.handleMessage(variablesRequest(varRef, 12)));

    bool found = false;
    for (const auto &v : varResp["body"]["variables"].getArray()) {
        if (v["name"].getString() == "x") {
            EXPECT_EQ(v["value"].getString(), "20");
            found = true;
        }
    }
    EXPECT_TRUE(found);
}

// ============================================================
// Interpreter Tests
// ============================================================

TEST_F(DAPTest, WhileLoop) {
    std::string src = "func main() {\n"
                      "    var i: i32 = 0\n"
                      "    while i < 3 {\n"
                      "        i = i + 1\n"
                      "    }\n"
                      "    let done: i32 = i\n"
                      "}\n";
    initAndLaunch(src, "test.liva", {6});
    server.handleMessage(configDoneRequest());
    server.takeEvents();

    auto stResp = parseResponse(server.handleMessage(stackTraceRequest(10)));
    auto frameId = static_cast<int>(
        stResp["body"]["stackFrames"].getArray()[0]["id"].getInteger());
    auto scResp = parseResponse(server.handleMessage(scopesRequest(frameId, 11)));
    auto varRef = static_cast<int>(
        scResp["body"]["scopes"].getArray()[0]["variablesReference"].getInteger());
    auto varResp = parseResponse(server.handleMessage(variablesRequest(varRef, 12)));

    bool found = false;
    for (const auto &v : varResp["body"]["variables"].getArray()) {
        if (v["name"].getString() == "i") {
            EXPECT_EQ(v["value"].getString(), "3");
            found = true;
        }
    }
    EXPECT_TRUE(found);
}

TEST_F(DAPTest, IfElse) {
    std::string src = "func main() {\n"
                      "    let x: i32 = 10\n"
                      "    var result: i32 = 0\n"
                      "    if x > 5 {\n"
                      "        result = 1\n"
                      "    } else {\n"
                      "        result = 2\n"
                      "    }\n"
                      "    let done: i32 = result\n"
                      "}\n";
    initAndLaunch(src, "test.liva", {9});
    server.handleMessage(configDoneRequest());
    server.takeEvents();

    auto stResp = parseResponse(server.handleMessage(stackTraceRequest(10)));
    auto frameId = static_cast<int>(
        stResp["body"]["stackFrames"].getArray()[0]["id"].getInteger());
    auto scResp = parseResponse(server.handleMessage(scopesRequest(frameId, 11)));
    auto varRef = static_cast<int>(
        scResp["body"]["scopes"].getArray()[0]["variablesReference"].getInteger());
    auto varResp = parseResponse(server.handleMessage(variablesRequest(varRef, 12)));

    bool found = false;
    for (const auto &v : varResp["body"]["variables"].getArray()) {
        if (v["name"].getString() == "result") {
            EXPECT_EQ(v["value"].getString(), "1");
            found = true;
        }
    }
    EXPECT_TRUE(found);
}

TEST_F(DAPTest, FunctionCall) {
    std::string src = "func double(n: i32) -> i32 {\n"
                      "    return n * 2\n"
                      "}\n"
                      "func main() {\n"
                      "    let r: i32 = double(21)\n"
                      "    let done: i32 = r\n"
                      "}\n";
    initAndLaunch(src, "test.liva", {6});
    server.handleMessage(configDoneRequest());
    server.takeEvents();

    auto stResp = parseResponse(server.handleMessage(stackTraceRequest(10)));
    auto frameId = static_cast<int>(
        stResp["body"]["stackFrames"].getArray()[0]["id"].getInteger());
    auto scResp = parseResponse(server.handleMessage(scopesRequest(frameId, 11)));
    auto varRef = static_cast<int>(
        scResp["body"]["scopes"].getArray()[0]["variablesReference"].getInteger());
    auto varResp = parseResponse(server.handleMessage(variablesRequest(varRef, 12)));

    bool found = false;
    for (const auto &v : varResp["body"]["variables"].getArray()) {
        if (v["name"].getString() == "r") {
            EXPECT_EQ(v["value"].getString(), "42");
            found = true;
        }
    }
    EXPECT_TRUE(found);
}

TEST_F(DAPTest, BuiltinPrintln) {
    std::string src = "func main() {\n"
                      "    println(\"hello world\")\n"
                      "}\n";
    initAndLaunch(src);
    server.handleMessage(configDoneRequest());
    server.takeEvents();
    // Program runs to end; check no crash
    // The output buffer would contain "hello world\n"
    // We can't directly access it from server, but the program should terminate cleanly
}

TEST_F(DAPTest, ReturnValue) {
    std::string src = "func add(a: i32, b: i32) -> i32 {\n"
                      "    return a + b\n"
                      "}\n"
                      "func main() {\n"
                      "    let sum: i32 = add(3, 4)\n"
                      "    let done: i32 = sum\n"
                      "}\n";
    initAndLaunch(src, "test.liva", {6});
    server.handleMessage(configDoneRequest());
    server.takeEvents();

    auto stResp = parseResponse(server.handleMessage(stackTraceRequest(10)));
    auto frameId = static_cast<int>(
        stResp["body"]["stackFrames"].getArray()[0]["id"].getInteger());
    auto scResp = parseResponse(server.handleMessage(scopesRequest(frameId, 11)));
    auto varRef = static_cast<int>(
        scResp["body"]["scopes"].getArray()[0]["variablesReference"].getInteger());
    auto varResp = parseResponse(server.handleMessage(variablesRequest(varRef, 12)));

    bool found = false;
    for (const auto &v : varResp["body"]["variables"].getArray()) {
        if (v["name"].getString() == "sum") {
            EXPECT_EQ(v["value"].getString(), "7");
            found = true;
        }
    }
    EXPECT_TRUE(found);
}

// ============================================================
// Event Tests
// ============================================================

TEST_F(DAPTest, StoppedEvent) {
    std::string src = "func main() {\n"
                      "    let x: i32 = 42\n"
                      "}\n";
    initAndLaunch(src, "test.liva", {2});
    server.handleMessage(configDoneRequest());
    auto events = server.takeEvents();

    bool foundStopped = false;
    for (const auto &e : events) {
        auto ev = parseEvent(e);
        if (ev["event"].getString() == "stopped") {
            EXPECT_EQ(ev["body"]["threadId"].getInteger(), 1);
            EXPECT_FALSE(ev["body"]["reason"].getString().empty());
            foundStopped = true;
        }
    }
    EXPECT_TRUE(foundStopped);
}

TEST_F(DAPTest, TerminatedEvent) {
    std::string src = "func main() {\n"
                      "    let x: i32 = 42\n"
                      "}\n";
    initAndLaunch(src);
    server.handleMessage(configDoneRequest());
    auto events = server.takeEvents();

    bool foundTerminated = false;
    for (const auto &e : events) {
        auto ev = parseEvent(e);
        if (ev["event"].getString() == "terminated") {
            foundTerminated = true;
        }
    }
    EXPECT_TRUE(foundTerminated);
}

TEST_F(DAPTest, ExitedEvent) {
    std::string src = "func main() {\n"
                      "    let x: i32 = 42\n"
                      "}\n";
    initAndLaunch(src);
    server.handleMessage(configDoneRequest());
    auto events = server.takeEvents();

    bool foundExited = false;
    for (const auto &e : events) {
        auto ev = parseEvent(e);
        if (ev["event"].getString() == "exited") {
            EXPECT_EQ(ev["body"]["exitCode"].getInteger(), 0);
            foundExited = true;
        }
    }
    EXPECT_TRUE(foundExited);
}

// ============================================================
// Impl Method Tests
// ============================================================

TEST_F(DAPTest, ImplStaticMethod) {
    std::string src = "struct Point {\n"
                      "    var x: i32\n"
                      "    var y: i32\n"
                      "}\n"
                      "impl Point {\n"
                      "    func new(x: i32, y: i32) -> Point {\n"
                      "        return Point { x: x, y: y }\n"
                      "    }\n"
                      "}\n"
                      "func main() {\n"
                      "    var p = Point.new(3, 4)\n"
                      "    let done: i32 = p.x\n"
                      "}\n";
    initAndLaunch(src, "test.liva", {12});
    server.handleMessage(configDoneRequest());
    server.takeEvents();

    auto stResp = parseResponse(server.handleMessage(stackTraceRequest(10)));
    auto frameId = static_cast<int>(
        stResp["body"]["stackFrames"].getArray()[0]["id"].getInteger());
    auto scResp = parseResponse(server.handleMessage(scopesRequest(frameId, 11)));
    auto varRef = static_cast<int>(
        scResp["body"]["scopes"].getArray()[0]["variablesReference"].getInteger());
    auto varResp = parseResponse(server.handleMessage(variablesRequest(varRef, 12)));

    bool foundP = false;
    for (const auto &v : varResp["body"]["variables"].getArray()) {
        if (v["name"].getString() == "p") {
            // p should be a struct with x=3 y=4
            auto val = v["value"].getString();
            EXPECT_TRUE(val.find("3") != std::string::npos);
            EXPECT_TRUE(val.find("4") != std::string::npos);
            foundP = true;
        }
    }
    EXPECT_TRUE(foundP);
}

TEST_F(DAPTest, ImplInstanceMethod) {
    std::string src = "struct Counter {\n"
                      "    var count: i32\n"
                      "}\n"
                      "impl Counter {\n"
                      "    func new() -> Counter {\n"
                      "        return Counter { count: 0 }\n"
                      "    }\n"
                      "    func increment(ref mut self) {\n"
                      "        self.count = self.count + 1\n"
                      "    }\n"
                      "    func getCount(ref self) -> i32 {\n"
                      "        return self.count\n"
                      "    }\n"
                      "}\n"
                      "func main() {\n"
                      "    var c = Counter.new()\n"
                      "    c.increment()\n"
                      "    c.increment()\n"
                      "    let val: i32 = c.getCount()\n"
                      "    let done: i32 = val\n"
                      "}\n";
    initAndLaunch(src, "test.liva", {20});
    server.handleMessage(configDoneRequest());
    server.takeEvents();

    auto stResp = parseResponse(server.handleMessage(stackTraceRequest(10)));
    auto frameId = static_cast<int>(
        stResp["body"]["stackFrames"].getArray()[0]["id"].getInteger());
    auto scResp = parseResponse(server.handleMessage(scopesRequest(frameId, 11)));
    auto varRef = static_cast<int>(
        scResp["body"]["scopes"].getArray()[0]["variablesReference"].getInteger());
    auto varResp = parseResponse(server.handleMessage(variablesRequest(varRef, 12)));

    bool foundVal = false;
    for (const auto &v : varResp["body"]["variables"].getArray()) {
        if (v["name"].getString() == "val") {
            EXPECT_EQ(v["value"].getString(), "2");
            foundVal = true;
        }
    }
    EXPECT_TRUE(foundVal);
}

TEST_F(DAPTest, ImplArrayPush) {
    std::string src = "struct Bag {\n"
                      "    var items: [i32]\n"
                      "}\n"
                      "impl Bag {\n"
                      "    func new() -> Bag {\n"
                      "        return Bag { items: [] }\n"
                      "    }\n"
                      "    func add(ref mut self, item: i32) {\n"
                      "        self.items.push(item)\n"
                      "    }\n"
                      "}\n"
                      "func main() {\n"
                      "    var b = Bag.new()\n"
                      "    b.add(10)\n"
                      "    b.add(20)\n"
                      "    let n: i32 = b.items.length\n"
                      "    let done: i32 = n\n"
                      "}\n";
    initAndLaunch(src, "test.liva", {17});
    server.handleMessage(configDoneRequest());
    server.takeEvents();

    auto stResp = parseResponse(server.handleMessage(stackTraceRequest(10)));
    auto frameId = static_cast<int>(
        stResp["body"]["stackFrames"].getArray()[0]["id"].getInteger());
    auto scResp = parseResponse(server.handleMessage(scopesRequest(frameId, 11)));
    auto varRef = static_cast<int>(
        scResp["body"]["scopes"].getArray()[0]["variablesReference"].getInteger());
    auto varResp = parseResponse(server.handleMessage(variablesRequest(varRef, 12)));

    bool foundN = false;
    for (const auto &v : varResp["body"]["variables"].getArray()) {
        if (v["name"].getString() == "n") {
            EXPECT_EQ(v["value"].getString(), "2");
            foundN = true;
        }
    }
    EXPECT_TRUE(foundN);
}

TEST_F(DAPTest, IfLetOptional) {
    std::string src = "func maybeVal() -> i32? {\n"
                      "    return 42\n"
                      "}\n"
                      "func main() {\n"
                      "    var result: i32 = 0\n"
                      "    if let v = maybeVal() {\n"
                      "        result = v\n"
                      "    }\n"
                      "    let done: i32 = result\n"
                      "}\n";
    initAndLaunch(src, "test.liva", {9});
    server.handleMessage(configDoneRequest());
    server.takeEvents();

    auto stResp = parseResponse(server.handleMessage(stackTraceRequest(10)));
    auto frameId = static_cast<int>(
        stResp["body"]["stackFrames"].getArray()[0]["id"].getInteger());
    auto scResp = parseResponse(server.handleMessage(scopesRequest(frameId, 11)));
    auto varRef = static_cast<int>(
        scResp["body"]["scopes"].getArray()[0]["variablesReference"].getInteger());
    auto varResp = parseResponse(server.handleMessage(variablesRequest(varRef, 12)));

    bool foundResult = false;
    for (const auto &v : varResp["body"]["variables"].getArray()) {
        if (v["name"].getString() == "result") {
            EXPECT_EQ(v["value"].getString(), "42");
            foundResult = true;
        }
    }
    EXPECT_TRUE(foundResult);
}

TEST_F(DAPTest, StructExpand) {
    std::string src = "struct Point { var x: i32; var y: i32 }\n"
                      "func main() {\n"
                      "    let p: Point = Point { x: 10, y: 20 }\n"
                      "    let z: i32 = 0\n"
                      "}\n";
    initAndLaunch(src, "test.liva", {4});
    server.handleMessage(configDoneRequest());
    server.takeEvents();

    auto stResp = parseResponse(server.handleMessage(stackTraceRequest(10)));
    auto frameId = static_cast<int>(
        stResp["body"]["stackFrames"].getArray()[0]["id"].getInteger());
    auto scResp = parseResponse(server.handleMessage(scopesRequest(frameId, 11)));
    auto varRef = static_cast<int>(
        scResp["body"]["scopes"].getArray()[0]["variablesReference"].getInteger());
    auto varResp = parseResponse(server.handleMessage(variablesRequest(varRef, 12)));

    // p should have variablesReference > 0
    int structRef = 0;
    for (const auto &v : varResp["body"]["variables"].getArray()) {
        if (v["name"].getString() == "p") {
            structRef = static_cast<int>(v["variablesReference"].getInteger());
            EXPECT_GT(structRef, 0);
        }
    }
    ASSERT_GT(structRef, 0);

    // Expand struct: second variables request with structRef
    auto subResp = parseResponse(server.handleMessage(variablesRequest(structRef, 13)));
    EXPECT_TRUE(subResp["success"].getBool());
    const auto &fields = subResp["body"]["variables"].getArray();
    bool foundX = false, foundY = false;
    for (const auto &f : fields) {
        if (f["name"].getString() == "x") {
            EXPECT_EQ(f["value"].getString(), "10");
            foundX = true;
        }
        if (f["name"].getString() == "y") {
            EXPECT_EQ(f["value"].getString(), "20");
            foundY = true;
        }
    }
    EXPECT_TRUE(foundX);
    EXPECT_TRUE(foundY);
}

TEST_F(DAPTest, ArrayExpand) {
    std::string src = "func main() {\n"
                      "    let arr: [i32] = [1, 2, 3]\n"
                      "    let x: i32 = 0\n"
                      "}\n";
    initAndLaunch(src, "test.liva", {3});
    server.handleMessage(configDoneRequest());
    server.takeEvents();

    auto stResp = parseResponse(server.handleMessage(stackTraceRequest(10)));
    auto frameId = static_cast<int>(
        stResp["body"]["stackFrames"].getArray()[0]["id"].getInteger());
    auto scResp = parseResponse(server.handleMessage(scopesRequest(frameId, 11)));
    auto varRef = static_cast<int>(
        scResp["body"]["scopes"].getArray()[0]["variablesReference"].getInteger());
    auto varResp = parseResponse(server.handleMessage(variablesRequest(varRef, 12)));

    // arr should have variablesReference > 0
    int arrayRef = 0;
    for (const auto &v : varResp["body"]["variables"].getArray()) {
        if (v["name"].getString() == "arr") {
            arrayRef = static_cast<int>(v["variablesReference"].getInteger());
            EXPECT_GT(arrayRef, 0);
        }
    }
    ASSERT_GT(arrayRef, 0);

    // Expand array
    auto subResp = parseResponse(server.handleMessage(variablesRequest(arrayRef, 13)));
    EXPECT_TRUE(subResp["success"].getBool());
    const auto &elems = subResp["body"]["variables"].getArray();
    ASSERT_EQ(elems.size(), 3u);
    EXPECT_EQ(elems[0]["name"].getString(), "[0]");
    EXPECT_EQ(elems[0]["value"].getString(), "1");
    EXPECT_EQ(elems[1]["name"].getString(), "[1]");
    EXPECT_EQ(elems[1]["value"].getString(), "2");
    EXPECT_EQ(elems[2]["name"].getString(), "[2]");
    EXPECT_EQ(elems[2]["value"].getString(), "3");
}

TEST_F(DAPTest, EvaluateHoverSimple) {
    std::string src = "func main() {\n"
                      "    let x: i32 = 42\n"
                      "    let y: i32 = 0\n"
                      "}\n";
    initAndLaunch(src, "test.liva", {3});
    server.handleMessage(configDoneRequest());
    server.takeEvents();

    auto stResp = parseResponse(server.handleMessage(stackTraceRequest(10)));
    auto frameId = static_cast<int>(
        stResp["body"]["stackFrames"].getArray()[0]["id"].getInteger());

    auto resp = parseResponse(server.handleMessage(evaluateRequest("x", frameId, 11)));
    EXPECT_TRUE(resp["success"].getBool());
    EXPECT_EQ(resp["body"]["result"].getString(), "42");
    EXPECT_EQ(resp["body"]["type"].getString(), "i64");
    EXPECT_EQ(resp["body"]["variablesReference"].getInteger(), 0);
}

TEST_F(DAPTest, EvaluateHoverMember) {
    std::string src = "struct Point { var x: i32; var y: i32 }\n"
                      "func main() {\n"
                      "    let p: Point = Point { x: 10, y: 20 }\n"
                      "    let z: i32 = 0\n"
                      "}\n";
    initAndLaunch(src, "test.liva", {4});
    server.handleMessage(configDoneRequest());
    server.takeEvents();

    auto stResp = parseResponse(server.handleMessage(stackTraceRequest(10)));
    auto frameId = static_cast<int>(
        stResp["body"]["stackFrames"].getArray()[0]["id"].getInteger());

    auto resp = parseResponse(server.handleMessage(evaluateRequest("p.x", frameId, 11)));
    EXPECT_TRUE(resp["success"].getBool());
    EXPECT_EQ(resp["body"]["result"].getString(), "10");

    auto resp2 = parseResponse(server.handleMessage(evaluateRequest("p.y", frameId, 12)));
    EXPECT_TRUE(resp2["success"].getBool());
    EXPECT_EQ(resp2["body"]["result"].getString(), "20");
}

TEST_F(DAPTest, StringInterpolationWithMemberAccess) {
    std::string src = "struct Person {\n"
                      "    var name: string\n"
                      "}\n"
                      "func main() {\n"
                      "    var p = Person { name: \"Alice\" }\n"
                      "    println(\"Hello \\(p.name)\")\n"
                      "}\n";
    initAndLaunch(src);
    server.handleMessage(configDoneRequest());
    server.takeEvents();
    // Program should run to end without crash
    // Output should contain "Hello Alice"
}

// ============================================================
// Helper for conditional breakpoints
// ============================================================

std::string setBreakpointsWithCondition(const std::string &path,
                                         const std::vector<std::tuple<int, std::string, std::string, std::string>> &bps,
                                         int seq = 3) {
    // Each tuple: (line, condition, hitCondition, logMessage)
    std::string bpArr = "[";
    for (size_t i = 0; i < bps.size(); ++i) {
        if (i > 0) bpArr += ",";
        bpArr += R"({"line":)" + std::to_string(std::get<0>(bps[i]));
        if (!std::get<1>(bps[i]).empty())
            bpArr += R"(,"condition":")" + std::get<1>(bps[i]) + "\"";
        if (!std::get<2>(bps[i]).empty())
            bpArr += R"(,"hitCondition":")" + std::get<2>(bps[i]) + "\"";
        if (!std::get<3>(bps[i]).empty())
            bpArr += R"(,"logMessage":")" + std::get<3>(bps[i]) + "\"";
        bpArr += "}";
    }
    bpArr += "]";
    return R"({"seq":)" + std::to_string(seq) +
           R"(,"type":"request","command":"setBreakpoints","arguments":{)"
           R"("source":{"path":")" + path +
           R"("},"breakpoints":)" + bpArr + "}}";
}

// ============================================================
// Conditional Breakpoint Tests
// ============================================================

TEST_F(DAPTest, ConditionalBreakpoint) {
    std::string src = "func main() {\n"
                      "    var i: i32 = 0\n"
                      "    while i < 10 {\n"
                      "        i = i + 1\n"
                      "    }\n"
                      "}\n";
    server.handleMessage(initRequest());
    server.takeEvents();
    server.handleMessage(launchRequest(src));
    server.takeEvents();
    // Set conditional breakpoint on line 4 (i = i + 1) with condition "i == 5"
    server.handleMessage(setBreakpointsWithCondition("test.liva",
        {{4, "i == 5", "", ""}}));
    server.takeEvents();
    server.handleMessage(setExceptionBreakpointsRequest(5));
    server.takeEvents();

    server.handleMessage(configDoneRequest());
    auto events = server.takeEvents();
    // Should stop at breakpoint
    bool stopped = false;
    for (const auto &ev : events) {
        auto parsed = parseEvent(ev);
        if (parsed["event"].getString() == "stopped") {
            stopped = true;
        }
    }
    EXPECT_TRUE(stopped);

    // Check that i == 5
    auto stResp = parseResponse(server.handleMessage(stackTraceRequest(10)));
    auto frameId = static_cast<int>(
        stResp["body"]["stackFrames"].getArray()[0]["id"].getInteger());
    auto evalResp = parseResponse(server.handleMessage(evaluateRequest("i", frameId, 11)));
    EXPECT_EQ(evalResp["body"]["result"].getString(), "5");
}

TEST_F(DAPTest, HitCountBreakpoint) {
    std::string src = "func main() {\n"
                      "    var i: i32 = 0\n"
                      "    while i < 10 {\n"
                      "        i = i + 1\n"
                      "    }\n"
                      "}\n";
    server.handleMessage(initRequest());
    server.takeEvents();
    server.handleMessage(launchRequest(src));
    server.takeEvents();
    // Set hit count breakpoint on line 4: stop on 3rd hit
    server.handleMessage(setBreakpointsWithCondition("test.liva",
        {{4, "", "==3", ""}}));
    server.takeEvents();
    server.handleMessage(setExceptionBreakpointsRequest(5));
    server.takeEvents();

    server.handleMessage(configDoneRequest());
    auto events = server.takeEvents();
    bool stopped = false;
    for (const auto &ev : events) {
        auto parsed = parseEvent(ev);
        if (parsed["event"].getString() == "stopped") {
            stopped = true;
        }
    }
    EXPECT_TRUE(stopped);

    // After 3 hits, i should be 2 (about to become 3)
    auto stResp = parseResponse(server.handleMessage(stackTraceRequest(10)));
    auto frameId = static_cast<int>(
        stResp["body"]["stackFrames"].getArray()[0]["id"].getInteger());
    auto evalResp = parseResponse(server.handleMessage(evaluateRequest("i", frameId, 11)));
    EXPECT_EQ(evalResp["body"]["result"].getString(), "2");
}

TEST_F(DAPTest, Logpoint) {
    std::string src = "func main() {\n"
                      "    var i: i32 = 42\n"
                      "    var x: i32 = 99\n"
                      "}\n";
    server.handleMessage(initRequest());
    server.takeEvents();
    server.handleMessage(launchRequest(src));
    server.takeEvents();
    // Set logpoint on line 3 with message "value is {i}"
    server.handleMessage(setBreakpointsWithCondition("test.liva",
        {{3, "", "", "value is {i}"}}));
    server.takeEvents();
    server.handleMessage(setExceptionBreakpointsRequest(5));
    server.takeEvents();

    server.handleMessage(configDoneRequest());
    auto events = server.takeEvents();
    // Should NOT stop (logpoints don't pause), should get terminated
    bool terminated = false;
    bool hasStopped = false;
    std::string outputStr;
    for (const auto &ev : events) {
        auto parsed = parseEvent(ev);
        if (parsed["event"].getString() == "terminated") terminated = true;
        if (parsed["event"].getString() == "stopped") hasStopped = true;
        if (parsed["event"].getString() == "output") {
            outputStr += parsed["body"]["output"].getString();
        }
    }
    EXPECT_TRUE(terminated);
    EXPECT_FALSE(hasStopped);
    EXPECT_NE(outputStr.find("value is 42"), std::string::npos);
}

TEST_F(DAPTest, EvaluateArithmetic) {
    std::string src = "func main() {\n"
                      "    var x: i32 = 10\n"
                      "    var y: i32 = 20\n"
                      "    var z: i32 = 0\n"
                      "}\n";
    initAndLaunch(src, "test.liva", {4});
    server.handleMessage(configDoneRequest());
    server.takeEvents();

    auto stResp = parseResponse(server.handleMessage(stackTraceRequest(10)));
    auto frameId = static_cast<int>(
        stResp["body"]["stackFrames"].getArray()[0]["id"].getInteger());

    // x + y
    auto r1 = parseResponse(server.handleMessage(evaluateRequest("x + y", frameId, 11)));
    EXPECT_TRUE(r1["success"].getBool());
    EXPECT_EQ(r1["body"]["result"].getString(), "30");

    // x * 2 + 5
    auto r2 = parseResponse(server.handleMessage(evaluateRequest("x * 2 + 5", frameId, 12)));
    EXPECT_TRUE(r2["success"].getBool());
    EXPECT_EQ(r2["body"]["result"].getString(), "25");

    // x < y
    auto r3 = parseResponse(server.handleMessage(evaluateRequest("x < y", frameId, 13)));
    EXPECT_TRUE(r3["success"].getBool());
    EXPECT_EQ(r3["body"]["result"].getString(), "true");
}

TEST_F(DAPTest, InitializeSupportsConditionalBreakpoints) {
    auto resp = parseResponse(server.handleMessage(initRequest()));
    EXPECT_TRUE(resp["success"].getBool());
    EXPECT_TRUE(resp["body"]["supportsConditionalBreakpoints"].getBool());
    EXPECT_TRUE(resp["body"]["supportsHitConditionalBreakpoints"].getBool());
    EXPECT_TRUE(resp["body"]["supportsLogPoints"].getBool());
}

// ============================================================
// Exception Breakpoint Tests
// ============================================================

TEST_F(DAPTest, ExceptionBreakpointFilters) {
    auto resp = parseResponse(server.handleMessage(initRequest()));
    EXPECT_TRUE(resp["success"].getBool());
    // Should have exceptionBreakpointFilters
    const auto &filters = resp["body"]["exceptionBreakpointFilters"].getArray();
    ASSERT_GE(filters.size(), 2u);
    // Check "all" filter
    EXPECT_EQ(filters[0]["filter"].getString(), "all");
    EXPECT_EQ(filters[0]["label"].getString(), "All Exceptions");
    EXPECT_FALSE(filters[0]["default"].getBool());
    // Check "uncaught" filter
    EXPECT_EQ(filters[1]["filter"].getString(), "uncaught");
    EXPECT_EQ(filters[1]["label"].getString(), "Uncaught Exceptions");
    EXPECT_TRUE(filters[1]["default"].getBool());
}

TEST_F(DAPTest, SetExceptionBreakpointsAll) {
    server.handleMessage(initRequest());
    server.takeEvents();
    // Set "all" filter
    std::string req = R"({"seq":10,"type":"request","command":"setExceptionBreakpoints",)"
                      R"("arguments":{"filters":["all"]}})";
    auto resp = parseResponse(server.handleMessage(req));
    EXPECT_TRUE(resp["success"].getBool());
}

TEST_F(DAPTest, SetExceptionBreakpointsUncaught) {
    server.handleMessage(initRequest());
    server.takeEvents();
    // Set "uncaught" filter
    std::string req = R"({"seq":10,"type":"request","command":"setExceptionBreakpoints",)"
                      R"("arguments":{"filters":["uncaught"]}})";
    auto resp = parseResponse(server.handleMessage(req));
    EXPECT_TRUE(resp["success"].getBool());
}

TEST_F(DAPTest, ExceptionFilterOptionCapability) {
    auto resp = parseResponse(server.handleMessage(initRequest()));
    EXPECT_TRUE(resp["body"]["supportsExceptionFilterOptions"].getBool());
}

TEST_F(DAPTest, SteppingGranularityCapability) {
    auto resp = parseResponse(server.handleMessage(initRequest()));
    EXPECT_TRUE(resp["body"]["supportsSteppingGranularity"].getBool());
}

TEST_F(DAPTest, ValueFormattingCapability) {
    auto resp = parseResponse(server.handleMessage(initRequest()));
    EXPECT_TRUE(resp["body"]["supportsValueFormattingOptions"].getBool());
}
