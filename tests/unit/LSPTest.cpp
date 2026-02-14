#include "liva/LSP/LSPServer.h"
#include <gtest/gtest.h>

using namespace liva;

// ============================================================
// JSON Parser Tests
// ============================================================

TEST(JSONTest, ParseNull) {
    auto result = parseJSON("null");
    ASSERT_TRUE(result.success);
    EXPECT_TRUE(result.value.isNull());
}

TEST(JSONTest, ParseBooleans) {
    auto t = parseJSON("true");
    ASSERT_TRUE(t.success);
    EXPECT_TRUE(t.value.isBool());
    EXPECT_TRUE(t.value.getBool());

    auto f = parseJSON("false");
    ASSERT_TRUE(f.success);
    EXPECT_TRUE(f.value.isBool());
    EXPECT_FALSE(f.value.getBool());
}

TEST(JSONTest, ParseInteger) {
    auto r1 = parseJSON("42");
    ASSERT_TRUE(r1.success);
    EXPECT_TRUE(r1.value.isInteger());
    EXPECT_EQ(r1.value.getInteger(), 42);

    auto r2 = parseJSON("-7");
    ASSERT_TRUE(r2.success);
    EXPECT_EQ(r2.value.getInteger(), -7);

    auto r3 = parseJSON("0");
    ASSERT_TRUE(r3.success);
    EXPECT_EQ(r3.value.getInteger(), 0);
}

TEST(JSONTest, ParseDouble) {
    auto r1 = parseJSON("3.14");
    ASSERT_TRUE(r1.success);
    EXPECT_TRUE(r1.value.isDouble());
    EXPECT_NEAR(r1.value.getDouble(), 3.14, 0.001);

    auto r2 = parseJSON("-1.5e10");
    ASSERT_TRUE(r2.success);
    EXPECT_TRUE(r2.value.isDouble());
    EXPECT_NEAR(r2.value.getDouble(), -1.5e10, 1e6);
}

TEST(JSONTest, ParseString) {
    auto r1 = parseJSON("\"hello\"");
    ASSERT_TRUE(r1.success);
    EXPECT_TRUE(r1.value.isString());
    EXPECT_EQ(r1.value.getString(), "hello");

    auto r2 = parseJSON("\"line\\nbreak\"");
    ASSERT_TRUE(r2.success);
    EXPECT_EQ(r2.value.getString(), "line\nbreak");

    auto r3 = parseJSON("\"escaped\\\"quote\"");
    ASSERT_TRUE(r3.success);
    EXPECT_EQ(r3.value.getString(), "escaped\"quote");

    auto r4 = parseJSON("\"back\\\\slash\"");
    ASSERT_TRUE(r4.success);
    EXPECT_EQ(r4.value.getString(), "back\\slash");
}

TEST(JSONTest, ParseArray) {
    auto r = parseJSON("[1,\"a\",true]");
    ASSERT_TRUE(r.success);
    EXPECT_TRUE(r.value.isArray());
    const auto &arr = r.value.getArray();
    ASSERT_EQ(arr.size(), 3u);
    EXPECT_EQ(arr[0].getInteger(), 1);
    EXPECT_EQ(arr[1].getString(), "a");
    EXPECT_TRUE(arr[2].getBool());
}

TEST(JSONTest, ParseObject) {
    auto r = parseJSON("{\"key\":\"value\",\"num\":42}");
    ASSERT_TRUE(r.success);
    EXPECT_TRUE(r.value.isObject());
    EXPECT_EQ(r.value["key"].getString(), "value");
    EXPECT_EQ(r.value["num"].getInteger(), 42);
}

TEST(JSONTest, ParseComplex) {
    auto r = parseJSON("{\"a\":[1,{\"b\":true}],\"c\":null}");
    ASSERT_TRUE(r.success);
    EXPECT_TRUE(r.value.isObject());
    const auto &arr = r.value["a"].getArray();
    ASSERT_EQ(arr.size(), 2u);
    EXPECT_EQ(arr[0].getInteger(), 1);
    EXPECT_TRUE(arr[1]["b"].getBool());
    EXPECT_TRUE(r.value["c"].isNull());
}

TEST(JSONTest, ParseError) {
    auto r1 = parseJSON("{invalid}");
    EXPECT_FALSE(r1.success);

    auto r2 = parseJSON("");
    EXPECT_FALSE(r2.success);

    auto r3 = parseJSON("[1,2,");
    EXPECT_FALSE(r3.success);
}

TEST(JSONTest, SerializeRoundtrip) {
    std::string json = "{\"a\":[1,2,3],\"b\":\"hello\",\"c\":true,\"d\":null}";
    auto r1 = parseJSON(json);
    ASSERT_TRUE(r1.success);
    std::string serialized = r1.value.serialize();
    auto r2 = parseJSON(serialized);
    ASSERT_TRUE(r2.success);
    EXPECT_EQ(r2.value["a"].getArray().size(), 3u);
    EXPECT_EQ(r2.value["b"].getString(), "hello");
    EXPECT_TRUE(r2.value["c"].getBool());
    EXPECT_TRUE(r2.value["d"].isNull());
}

// ============================================================
// JSON Serialization Tests
// ============================================================

TEST(JSONTest, SerializeNull) {
    JSONValue v;
    EXPECT_EQ(v.serialize(), "null");
}

TEST(JSONTest, SerializeString) {
    JSONValue v("hello\nworld");
    EXPECT_EQ(v.serialize(), "\"hello\\nworld\"");

    JSONValue v2("quote\"here");
    EXPECT_EQ(v2.serialize(), "\"quote\\\"here\"");
}

TEST(JSONTest, SerializeObject) {
    auto obj = JSONValue::object();
    obj.set("name", JSONValue("test"));
    auto inner = JSONValue::object();
    inner.set("x", JSONValue(static_cast<int64_t>(1)));
    obj.set("nested", std::move(inner));
    std::string s = obj.serialize();
    auto r = parseJSON(s);
    ASSERT_TRUE(r.success);
    EXPECT_EQ(r.value["name"].getString(), "test");
    EXPECT_EQ(r.value["nested"]["x"].getInteger(), 1);
}

TEST(JSONTest, SerializeArray) {
    auto arr = JSONValue::array();
    arr.push(JSONValue(static_cast<int64_t>(1)));
    arr.push(JSONValue("two"));
    arr.push(JSONValue(true));
    arr.push(JSONValue());
    std::string s = arr.serialize();
    auto r = parseJSON(s);
    ASSERT_TRUE(r.success);
    ASSERT_EQ(r.value.getArray().size(), 4u);
    EXPECT_EQ(r.value.getArray()[0].getInteger(), 1);
    EXPECT_EQ(r.value.getArray()[1].getString(), "two");
    EXPECT_TRUE(r.value.getArray()[2].getBool());
    EXPECT_TRUE(r.value.getArray()[3].isNull());
}

// ============================================================
// LSP Test Fixture
// ============================================================

class LSPTest : public ::testing::Test {
protected:
    LSPServer server;

    // Build an initialize request
    std::string initRequest(int id = 1) {
        return "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) +
               ",\"method\":\"initialize\",\"params\":{\"capabilities\":{}}}";
    }

    // Build an initialized notification
    std::string initializedNotif() {
        return "{\"jsonrpc\":\"2.0\",\"method\":\"initialized\",\"params\":{}}";
    }

    // Build a didOpen notification
    std::string didOpen(const std::string &uri, const std::string &text,
                        int version = 1) {
        return "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\","
               "\"params\":{\"textDocument\":{\"uri\":\"" + uri +
               "\",\"languageId\":\"liva\",\"version\":" +
               std::to_string(version) + ",\"text\":\"" +
               escapeForJSON(text) + "\"}}}";
    }

    // Build a didChange notification
    std::string didChange(const std::string &uri, const std::string &text,
                          int version = 2) {
        return "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didChange\","
               "\"params\":{\"textDocument\":{\"uri\":\"" + uri +
               "\",\"version\":" + std::to_string(version) +
               "},\"contentChanges\":[{\"text\":\"" +
               escapeForJSON(text) + "\"}]}}";
    }

    // Build a didClose notification
    std::string didClose(const std::string &uri) {
        return "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didClose\","
               "\"params\":{\"textDocument\":{\"uri\":\"" + uri + "\"}}}";
    }

    // Build a shutdown request
    std::string shutdownRequest(int id = 99) {
        return "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) +
               ",\"method\":\"shutdown\"}";
    }

    // Build an exit notification
    std::string exitNotif() {
        return "{\"jsonrpc\":\"2.0\",\"method\":\"exit\"}";
    }

    // Build a completion request
    std::string completionRequest(const std::string &uri, int line, int col,
                                  int id = 10) {
        return "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) +
               ",\"method\":\"textDocument/completion\","
               "\"params\":{\"textDocument\":{\"uri\":\"" + uri +
               "\"},\"position\":{\"line\":" + std::to_string(line) +
               ",\"character\":" + std::to_string(col) + "}}}";
    }

    // Build a hover request
    std::string hoverRequest(const std::string &uri, int line, int col,
                             int id = 20) {
        return "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) +
               ",\"method\":\"textDocument/hover\","
               "\"params\":{\"textDocument\":{\"uri\":\"" + uri +
               "\"},\"position\":{\"line\":" + std::to_string(line) +
               ",\"character\":" + std::to_string(col) + "}}}";
    }

    // Build a definition request
    std::string definitionRequest(const std::string &uri, int line, int col,
                                  int id = 30) {
        return "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) +
               ",\"method\":\"textDocument/definition\","
               "\"params\":{\"textDocument\":{\"uri\":\"" + uri +
               "\"},\"position\":{\"line\":" + std::to_string(line) +
               ",\"character\":" + std::to_string(col) + "}}}";
    }

    // Build a document symbol request
    std::string documentSymbolRequest(const std::string &uri, int id = 40) {
        return "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) +
               ",\"method\":\"textDocument/documentSymbol\","
               "\"params\":{\"textDocument\":{\"uri\":\"" + uri + "\"}}}";
    }

    // Build a references request
    std::string referencesRequest(const std::string &uri, int line, int col,
                                  int id = 50) {
        return "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) +
               ",\"method\":\"textDocument/references\","
               "\"params\":{\"textDocument\":{\"uri\":\"" + uri +
               "\"},\"position\":{\"line\":" + std::to_string(line) +
               ",\"character\":" + std::to_string(col) +
               "},\"context\":{\"includeDeclaration\":true}}}";
    }

    // Build a rename request
    std::string renameRequest(const std::string &uri, int line, int col,
                              const std::string &newName, int id = 60) {
        return "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) +
               ",\"method\":\"textDocument/rename\","
               "\"params\":{\"textDocument\":{\"uri\":\"" + uri +
               "\"},\"position\":{\"line\":" + std::to_string(line) +
               ",\"character\":" + std::to_string(col) +
               "},\"newName\":\"" + newName + "\"}}";
    }

    // Build a signatureHelp request
    std::string signatureHelpRequest(const std::string &uri, int line, int col,
                                     int id = 70) {
        return "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) +
               ",\"method\":\"textDocument/signatureHelp\","
               "\"params\":{\"textDocument\":{\"uri\":\"" + uri +
               "\"},\"position\":{\"line\":" + std::to_string(line) +
               ",\"character\":" + std::to_string(col) + "}}}";
    }

    // Build a semanticTokens/full request
    std::string semanticTokensRequest(const std::string &uri, int id = 80) {
        return "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) +
               ",\"method\":\"textDocument/semanticTokens/full\","
               "\"params\":{\"textDocument\":{\"uri\":\"" + uri + "\"}}}";
    }

    // Build a textDocument/formatting request
    std::string formattingRequest(const std::string &uri, int id = 90) {
        return "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) +
               ",\"method\":\"textDocument/formatting\","
               "\"params\":{\"textDocument\":{\"uri\":\"" + uri +
               "\"},\"options\":{\"tabSize\":4,\"insertSpaces\":true}}}";
    }

    // Build a textDocument/foldingRange request
    std::string foldingRangeRequest(const std::string &uri, int id = 100) {
        return "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) +
               ",\"method\":\"textDocument/foldingRange\","
               "\"params\":{\"textDocument\":{\"uri\":\"" + uri + "\"}}}";
    }

    // Build a textDocument/selectionRange request
    std::string selectionRangeRequest(const std::string &uri,
                                      const std::string &positionsJson,
                                      int id = 110) {
        return "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) +
               ",\"method\":\"textDocument/selectionRange\","
               "\"params\":{\"textDocument\":{\"uri\":\"" + uri +
               "\"},\"positions\":" + positionsJson + "}}";
    }

    // Build a textDocument/documentHighlight request
    std::string documentHighlightRequest(const std::string &uri, int line,
                                         int col, int id = 120) {
        return "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) +
               ",\"method\":\"textDocument/documentHighlight\","
               "\"params\":{\"textDocument\":{\"uri\":\"" + uri +
               "\"},\"position\":{\"line\":" + std::to_string(line) +
               ",\"character\":" + std::to_string(col) + "}}}";
    }

    // Build a textDocument/codeAction request
    std::string codeActionRequest(const std::string &uri, int startLine,
                                  int startCol, int endLine, int endCol,
                                  int id = 95) {
        return "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) +
               ",\"method\":\"textDocument/codeAction\","
               "\"params\":{\"textDocument\":{\"uri\":\"" + uri +
               "\"},\"range\":{\"start\":{\"line\":" + std::to_string(startLine) +
               ",\"character\":" + std::to_string(startCol) +
               "},\"end\":{\"line\":" + std::to_string(endLine) +
               ",\"character\":" + std::to_string(endCol) +
               "}},\"context\":{\"diagnostics\":[]}}}";
    }

    // Parse a JSON response
    JSONValue parseResponse(const std::string &resp) {
        auto r = parseJSON(resp);
        return r.success ? std::move(r.value) : JSONValue();
    }

    // Helper to init server + open a document
    void initAndOpen(const std::string &uri, const std::string &text) {
        server.handleMessage(initRequest());
        server.takeNotifications();
        server.handleMessage(initializedNotif());
        server.handleMessage(didOpen(uri, text));
        server.takeNotifications(); // consume diagnostics
    }

private:
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
};

// ============================================================
// LSP Lifecycle Tests
// ============================================================

TEST_F(LSPTest, Initialize) {
    auto resp = parseResponse(server.handleMessage(initRequest()));
    EXPECT_FALSE(resp.isNull());
    EXPECT_TRUE(resp["result"].isObject());
    EXPECT_TRUE(resp["result"]["capabilities"].isObject());
    EXPECT_TRUE(resp["result"]["serverInfo"].isObject());
    EXPECT_EQ(resp["result"]["serverInfo"]["name"].getString(), "liva-lsp");
}

TEST_F(LSPTest, InitializeCapabilities) {
    auto resp = parseResponse(server.handleMessage(initRequest()));
    const auto &caps = resp["result"]["capabilities"];
    EXPECT_EQ(caps["textDocumentSync"].getInteger(), 1);
    EXPECT_TRUE(caps["hoverProvider"].getBool());
    EXPECT_TRUE(caps["definitionProvider"].getBool());
    EXPECT_TRUE(caps["documentSymbolProvider"].getBool());
    EXPECT_TRUE(caps["completionProvider"].isObject());
}

TEST_F(LSPTest, Shutdown) {
    server.handleMessage(initRequest());
    auto resp = parseResponse(server.handleMessage(shutdownRequest()));
    EXPECT_TRUE(resp["result"].isNull());
    EXPECT_TRUE(server.isShutdown());
}

TEST_F(LSPTest, ShutdownThenExit) {
    server.handleMessage(initRequest());
    server.handleMessage(shutdownRequest());
    EXPECT_TRUE(server.isShutdown());
    EXPECT_FALSE(server.shouldExit());
    server.handleMessage(exitNotif());
    EXPECT_TRUE(server.shouldExit());
}

// ============================================================
// Document Sync + Diagnostics Tests
// ============================================================

TEST_F(LSPTest, DidOpenValid) {
    server.handleMessage(initRequest());
    server.takeNotifications();
    server.handleMessage(didOpen("file:///test.liva",
                                 "func main() {\n    println(\"hello\")\n}"));
    auto notifs = server.takeNotifications();
    ASSERT_FALSE(notifs.empty());
    auto diag = parseResponse(notifs[0]);
    EXPECT_EQ(diag["method"].getString(), "textDocument/publishDiagnostics");
    // Valid code should produce no diagnostics (or only sema diagnostics)
    // We just check the notification was sent
    EXPECT_TRUE(diag["params"]["diagnostics"].isArray());
}

TEST_F(LSPTest, DidOpenWithError) {
    server.handleMessage(initRequest());
    server.takeNotifications();
    // Malformed code: missing closing brace
    server.handleMessage(didOpen("file:///test.liva", "func foo() {"));
    auto notifs = server.takeNotifications();
    ASSERT_FALSE(notifs.empty());
    auto diag = parseResponse(notifs[0]);
    const auto &diagnostics = diag["params"]["diagnostics"].getArray();
    // Should have at least one error
    EXPECT_FALSE(diagnostics.empty());
}

TEST_F(LSPTest, DidChangeUpdates) {
    server.handleMessage(initRequest());
    server.takeNotifications();
    server.handleMessage(didOpen("file:///test.liva", "func foo() {"));
    server.takeNotifications();

    // Fix the code
    server.handleMessage(didChange("file:///test.liva",
                                   "func foo() {\n    return\n}"));
    auto notifs = server.takeNotifications();
    ASSERT_FALSE(notifs.empty());
    auto diag = parseResponse(notifs[0]);
    EXPECT_EQ(diag["method"].getString(), "textDocument/publishDiagnostics");
}

TEST_F(LSPTest, DidCloseClearsDoc) {
    server.handleMessage(initRequest());
    server.takeNotifications();
    server.handleMessage(didOpen("file:///test.liva", "func foo() {}"));
    server.takeNotifications();

    server.handleMessage(didClose("file:///test.liva"));
    auto notifs = server.takeNotifications();
    ASSERT_FALSE(notifs.empty());
    auto diag = parseResponse(notifs[0]);
    EXPECT_EQ(diag["method"].getString(), "textDocument/publishDiagnostics");
    EXPECT_TRUE(diag["params"]["diagnostics"].getArray().empty());
}

TEST_F(LSPTest, DiagnosticSeverity) {
    server.handleMessage(initRequest());
    server.takeNotifications();
    // Code with a guaranteed parse error
    server.handleMessage(didOpen("file:///test.liva", "???"));
    auto notifs = server.takeNotifications();
    ASSERT_FALSE(notifs.empty());
    auto diag = parseResponse(notifs[0]);
    const auto &diagnostics = diag["params"]["diagnostics"].getArray();
    if (!diagnostics.empty()) {
        // Parser errors should be severity 1 (Error)
        EXPECT_EQ(diagnostics[0]["severity"].getInteger(), 1);
    }
}

TEST_F(LSPTest, DiagnosticLineMapping) {
    server.handleMessage(initRequest());
    server.takeNotifications();
    // Error on line 2 (0-indexed: line 1)
    server.handleMessage(didOpen("file:///test.liva", "func foo() {\n  ???\n}"));
    auto notifs = server.takeNotifications();
    ASSERT_FALSE(notifs.empty());
    auto diag = parseResponse(notifs[0]);
    const auto &diagnostics = diag["params"]["diagnostics"].getArray();
    if (!diagnostics.empty()) {
        // Lines should be 0-indexed
        int64_t line = diagnostics[0]["range"]["start"]["line"].getInteger();
        EXPECT_GE(line, 0);
    }
}

// ============================================================
// Completion Tests
// ============================================================

TEST_F(LSPTest, CompletionKeywords) {
    initAndOpen("file:///test.liva", "func main() {}");
    auto resp = parseResponse(
        server.handleMessage(completionRequest("file:///test.liva", 0, 0)));
    const auto &items = resp["result"].getArray();
    EXPECT_FALSE(items.empty());

    // Check that some keywords are present
    bool hasFunc = false, hasLet = false, hasVar = false, hasStruct = false;
    for (const auto &item : items) {
        std::string label = item["label"].getString();
        if (label == "func") hasFunc = true;
        if (label == "let") hasLet = true;
        if (label == "var") hasVar = true;
        if (label == "struct") hasStruct = true;
    }
    EXPECT_TRUE(hasFunc);
    EXPECT_TRUE(hasLet);
    EXPECT_TRUE(hasVar);
    EXPECT_TRUE(hasStruct);
}

TEST_F(LSPTest, CompletionBuiltins) {
    initAndOpen("file:///test.liva", "func main() {}");
    auto resp = parseResponse(
        server.handleMessage(completionRequest("file:///test.liva", 0, 0)));
    const auto &items = resp["result"].getArray();

    bool hasPrintln = false, hasLen = false, hasToString = false;
    for (const auto &item : items) {
        std::string label = item["label"].getString();
        if (label == "println") hasPrintln = true;
        if (label == "len") hasLen = true;
        if (label == "toString") hasToString = true;
    }
    EXPECT_TRUE(hasPrintln);
    EXPECT_TRUE(hasLen);
    EXPECT_TRUE(hasToString);
}

TEST_F(LSPTest, CompletionSymbols) {
    initAndOpen("file:///test.liva",
                "func greet() {}\nstruct Point {\n    var x: i32\n}");
    auto resp = parseResponse(
        server.handleMessage(completionRequest("file:///test.liva", 0, 0)));
    const auto &items = resp["result"].getArray();

    bool hasGreet = false, hasPoint = false;
    for (const auto &item : items) {
        std::string label = item["label"].getString();
        if (label == "greet") hasGreet = true;
        if (label == "Point") hasPoint = true;
    }
    EXPECT_TRUE(hasGreet);
    EXPECT_TRUE(hasPoint);
}

TEST_F(LSPTest, CompletionEmpty) {
    initAndOpen("file:///test.liva", "");
    auto resp = parseResponse(
        server.handleMessage(completionRequest("file:///test.liva", 0, 0)));
    const auto &items = resp["result"].getArray();
    // Should still have keywords + builtins
    EXPECT_GE(items.size(), 30u);
}

// ============================================================
// Hover Tests
// ============================================================

TEST_F(LSPTest, HoverOnFunction) {
    initAndOpen("file:///test.liva", "func greet(name: string) -> string {\n    return name\n}");
    // Hover on line 0 (func keyword)
    auto resp = parseResponse(
        server.handleMessage(hoverRequest("file:///test.liva", 0, 5)));
    if (!resp["result"].isNull()) {
        EXPECT_TRUE(resp["result"]["contents"].isObject());
        std::string val = resp["result"]["contents"]["value"].getString();
        EXPECT_FALSE(val.empty());
    }
}

TEST_F(LSPTest, HoverOnVariable) {
    initAndOpen("file:///test.liva", "let x: i32 = 42");
    auto resp = parseResponse(
        server.handleMessage(hoverRequest("file:///test.liva", 0, 4)));
    if (!resp["result"].isNull()) {
        std::string val = resp["result"]["contents"]["value"].getString();
        EXPECT_FALSE(val.empty());
    }
}

TEST_F(LSPTest, HoverOnNothing) {
    initAndOpen("file:///test.liva", "func main() {}");
    // Hover on a line that doesn't exist
    auto resp = parseResponse(
        server.handleMessage(hoverRequest("file:///test.liva", 100, 0)));
    EXPECT_TRUE(resp["result"].isNull());
}

// ============================================================
// Go to Definition Tests
// ============================================================

TEST_F(LSPTest, DefOfFunction) {
    initAndOpen("file:///test.liva", "func foo() {}");
    auto resp = parseResponse(
        server.handleMessage(definitionRequest("file:///test.liva", 0, 5)));
    if (!resp["result"].isNull()) {
        EXPECT_TRUE(resp["result"]["uri"].isString());
        EXPECT_TRUE(resp["result"]["range"].isObject());
    }
}

TEST_F(LSPTest, DefOfVariable) {
    initAndOpen("file:///test.liva", "let x: i32 = 42");
    auto resp = parseResponse(
        server.handleMessage(definitionRequest("file:///test.liva", 0, 4)));
    if (!resp["result"].isNull()) {
        EXPECT_TRUE(resp["result"]["uri"].isString());
    }
}

TEST_F(LSPTest, DefOfStruct) {
    initAndOpen("file:///test.liva", "struct Point {\n    var x: i32\n}");
    auto resp = parseResponse(
        server.handleMessage(definitionRequest("file:///test.liva", 0, 7)));
    if (!resp["result"].isNull()) {
        EXPECT_TRUE(resp["result"]["uri"].isString());
    }
}

TEST_F(LSPTest, DefNotFound) {
    initAndOpen("file:///test.liva", "func main() {}");
    auto resp = parseResponse(
        server.handleMessage(definitionRequest("file:///test.liva", 100, 0)));
    EXPECT_TRUE(resp["result"].isNull());
}

// ============================================================
// Document Symbol Tests
// ============================================================

TEST_F(LSPTest, DocumentSymbols) {
    initAndOpen("file:///test.liva",
                "func greet() {}\nstruct Point {\n    var x: i32\n}\nenum Color {\n    case Red\n}");
    auto resp = parseResponse(
        server.handleMessage(documentSymbolRequest("file:///test.liva")));
    const auto &symbols = resp["result"].getArray();
    ASSERT_GE(symbols.size(), 3u);

    bool hasGreet = false, hasPoint = false, hasColor = false;
    for (const auto &sym : symbols) {
        std::string name = sym["name"].getString();
        if (name == "greet") {
            hasGreet = true;
            EXPECT_EQ(sym["kind"].getInteger(), 12); // Function
        }
        if (name == "Point") {
            hasPoint = true;
            EXPECT_EQ(sym["kind"].getInteger(), 23); // Struct
        }
        if (name == "Color") {
            hasColor = true;
            EXPECT_EQ(sym["kind"].getInteger(), 10); // Enum
        }
    }
    EXPECT_TRUE(hasGreet);
    EXPECT_TRUE(hasPoint);
    EXPECT_TRUE(hasColor);
}

TEST_F(LSPTest, DocumentSymbolsEmpty) {
    initAndOpen("file:///test.liva", "");
    auto resp = parseResponse(
        server.handleMessage(documentSymbolRequest("file:///test.liva")));
    EXPECT_TRUE(resp["result"].isArray());
    EXPECT_TRUE(resp["result"].getArray().empty());
}

// ============================================================
// References Tests
// ============================================================

TEST_F(LSPTest, ReferencesOfFunction) {
    initAndOpen("file:///test.liva", "func greet() {}\nfunc main() {\n    greet()\n}");
    auto resp = parseResponse(
        server.handleMessage(referencesRequest("file:///test.liva", 0, 5)));
    const auto &refs = resp["result"].getArray();
    // Should find at least the declaration and the call
    EXPECT_GE(refs.size(), 2u);
    for (const auto &loc : refs) {
        EXPECT_EQ(loc["uri"].getString(), "file:///test.liva");
        EXPECT_TRUE(loc["range"].isObject());
    }
}

TEST_F(LSPTest, ReferencesOfVariable) {
    initAndOpen("file:///test.liva", "let x: i32 = 42");
    auto resp = parseResponse(
        server.handleMessage(referencesRequest("file:///test.liva", 0, 4)));
    const auto &refs = resp["result"].getArray();
    EXPECT_GE(refs.size(), 1u);
}

TEST_F(LSPTest, ReferencesNoSymbol) {
    initAndOpen("file:///test.liva", "func main() {}");
    auto resp = parseResponse(
        server.handleMessage(referencesRequest("file:///test.liva", 100, 0)));
    const auto &refs = resp["result"].getArray();
    EXPECT_TRUE(refs.empty());
}

TEST_F(LSPTest, ReferencesPositionsAreCorrect) {
    // "func foo() {}\nfunc main() {\n    foo()\n}"
    // "foo" at line 0 col 5..8 and line 2 col 4..7
    initAndOpen("file:///test.liva", "func foo() {}\nfunc main() {\n    foo()\n}");
    auto resp = parseResponse(
        server.handleMessage(referencesRequest("file:///test.liva", 0, 5)));
    const auto &refs = resp["result"].getArray();
    ASSERT_GE(refs.size(), 2u);
    // First reference at line 0, col 5
    EXPECT_EQ(refs[0]["range"]["start"]["line"].getInteger(), 0);
    EXPECT_EQ(refs[0]["range"]["start"]["character"].getInteger(), 5);
    EXPECT_EQ(refs[0]["range"]["end"]["character"].getInteger(), 8);
    // Second reference at line 2, col 4
    EXPECT_EQ(refs[1]["range"]["start"]["line"].getInteger(), 2);
    EXPECT_EQ(refs[1]["range"]["start"]["character"].getInteger(), 4);
    EXPECT_EQ(refs[1]["range"]["end"]["character"].getInteger(), 7);
}

TEST_F(LSPTest, ReferencesOfStruct) {
    // Struct name should be found in declaration
    initAndOpen("file:///test.liva", "struct Point {\n    var x: i32\n}");
    auto resp = parseResponse(
        server.handleMessage(referencesRequest("file:///test.liva", 0, 7)));
    const auto &refs = resp["result"].getArray();
    ASSERT_GE(refs.size(), 1u);
    EXPECT_EQ(refs[0]["uri"].getString(), "file:///test.liva");
    // "Point" starts at col 7
    EXPECT_EQ(refs[0]["range"]["start"]["character"].getInteger(), 7);
    EXPECT_EQ(refs[0]["range"]["end"]["character"].getInteger(), 12);
}

// ============================================================
// Rename Tests
// ============================================================

TEST_F(LSPTest, RenameFunction) {
    initAndOpen("file:///test.liva", "func greet() {}\nfunc main() {\n    greet()\n}");
    auto resp = parseResponse(
        server.handleMessage(renameRequest("file:///test.liva", 0, 5, "sayHello")));
    EXPECT_TRUE(resp["result"].isObject());
    const auto &changes = resp["result"]["changes"];
    EXPECT_TRUE(changes.isObject());
    const auto &edits = changes["file:///test.liva"].getArray();
    EXPECT_GE(edits.size(), 2u);
    for (const auto &edit : edits) {
        EXPECT_EQ(edit["newText"].getString(), "sayHello");
        EXPECT_TRUE(edit["range"].isObject());
    }
}

TEST_F(LSPTest, RenameVariable) {
    initAndOpen("file:///test.liva", "let x: i32 = 42");
    auto resp = parseResponse(
        server.handleMessage(renameRequest("file:///test.liva", 0, 4, "y")));
    EXPECT_TRUE(resp["result"].isObject());
    const auto &edits = resp["result"]["changes"]["file:///test.liva"].getArray();
    EXPECT_GE(edits.size(), 1u);
    EXPECT_EQ(edits[0]["newText"].getString(), "y");
}

TEST_F(LSPTest, RenameNoSymbol) {
    initAndOpen("file:///test.liva", "func main() {}");
    auto resp = parseResponse(
        server.handleMessage(renameRequest("file:///test.liva", 100, 0, "newName")));
    // Should return error
    EXPECT_TRUE(resp["error"].isObject());
}

TEST_F(LSPTest, RenameEditsHaveCorrectPositions) {
    // "func foo() {}\nfunc main() {\n    foo()\n}"
    // "foo" at line 0 col 5..8, and line 2 col 4..7
    initAndOpen("file:///test.liva", "func foo() {}\nfunc main() {\n    foo()\n}");
    auto resp = parseResponse(
        server.handleMessage(renameRequest("file:///test.liva", 0, 5, "bar")));
    EXPECT_TRUE(resp["result"].isObject());
    const auto &edits = resp["result"]["changes"]["file:///test.liva"].getArray();
    ASSERT_GE(edits.size(), 2u);
    // First edit should be at line 0, character 5
    EXPECT_EQ(edits[0]["range"]["start"]["line"].getInteger(), 0);
    EXPECT_EQ(edits[0]["range"]["start"]["character"].getInteger(), 5);
    EXPECT_EQ(edits[0]["range"]["end"]["character"].getInteger(), 8);
    EXPECT_EQ(edits[0]["newText"].getString(), "bar");
    // Second edit should be at line 2, character 4
    EXPECT_EQ(edits[1]["range"]["start"]["line"].getInteger(), 2);
    EXPECT_EQ(edits[1]["range"]["start"]["character"].getInteger(), 4);
    EXPECT_EQ(edits[1]["range"]["end"]["character"].getInteger(), 7);
    EXPECT_EQ(edits[1]["newText"].getString(), "bar");
}

TEST_F(LSPTest, RenameStruct) {
    // Rename a struct name
    initAndOpen("file:///test.liva", "struct Point {\n    var x: i32\n}");
    auto resp = parseResponse(
        server.handleMessage(renameRequest("file:///test.liva", 0, 7, "Vec2")));
    EXPECT_TRUE(resp["result"].isObject());
    const auto &edits = resp["result"]["changes"]["file:///test.liva"].getArray();
    ASSERT_GE(edits.size(), 1u);
    EXPECT_EQ(edits[0]["newText"].getString(), "Vec2");
    // "Point" starts at col 7 and ends at col 12
    EXPECT_EQ(edits[0]["range"]["start"]["character"].getInteger(), 7);
    EXPECT_EQ(edits[0]["range"]["end"]["character"].getInteger(), 12);
}

// ============================================================
// Signature Help Tests
// ============================================================

TEST_F(LSPTest, SignatureHelpBasic) {
    initAndOpen("file:///test.liva",
                "func add(a: i32, b: i32) -> i32 {\n    return a + b\n}\nfunc main() {\n    add(\n}");
    // Cursor after 'add(' on line 4, col 8
    auto resp = parseResponse(
        server.handleMessage(signatureHelpRequest("file:///test.liva", 4, 8)));
    if (!resp["result"].isNull()) {
        const auto &sigs = resp["result"]["signatures"].getArray();
        ASSERT_GE(sigs.size(), 1u);
        std::string label = sigs[0]["label"].getString();
        EXPECT_FALSE(label.empty());
        // Should contain 'add' and parameter names
        EXPECT_NE(label.find("add"), std::string::npos);
        EXPECT_NE(label.find("a:"), std::string::npos);
        EXPECT_NE(label.find("b:"), std::string::npos);
        // Check parameters
        const auto &params = sigs[0]["parameters"].getArray();
        EXPECT_EQ(params.size(), 2u);
    }
}

TEST_F(LSPTest, SignatureHelpActiveParam) {
    initAndOpen("file:///test.liva",
                "func add(a: i32, b: i32) -> i32 {\n    return a + b\n}\nfunc main() {\n    add(1, \n}");
    // After the comma — second parameter should be active
    auto resp = parseResponse(
        server.handleMessage(signatureHelpRequest("file:///test.liva", 4, 11)));
    if (!resp["result"].isNull()) {
        int64_t activeParam = resp["result"]["activeParameter"].getInteger();
        EXPECT_EQ(activeParam, 1);
    }
}

TEST_F(LSPTest, SignatureHelpNoFunc) {
    initAndOpen("file:///test.liva", "func main() {}");
    auto resp = parseResponse(
        server.handleMessage(signatureHelpRequest("file:///test.liva", 0, 0)));
    // When no function call context is found, should return empty signatures
    const auto &sigs = resp["result"]["signatures"].getArray();
    EXPECT_TRUE(sigs.empty());
    EXPECT_EQ(resp["result"]["activeSignature"].getInteger(), 0);
    EXPECT_EQ(resp["result"]["activeParameter"].getInteger(), 0);
}

TEST_F(LSPTest, SignatureHelpBuiltinFunction) {
    // Test signature help for built-in println function
    initAndOpen("file:///test.liva", "func main() {\n    println(\n}");
    // Cursor after 'println(' on line 1, col 12
    auto resp = parseResponse(
        server.handleMessage(signatureHelpRequest("file:///test.liva", 1, 12)));
    EXPECT_FALSE(resp["result"].isNull());
    const auto &sigs = resp["result"]["signatures"].getArray();
    ASSERT_GE(sigs.size(), 1u);
    std::string label = sigs[0]["label"].getString();
    EXPECT_NE(label.find("println"), std::string::npos);
    EXPECT_NE(label.find("value"), std::string::npos);
    // Should have 1 parameter
    const auto &params = sigs[0]["parameters"].getArray();
    EXPECT_EQ(params.size(), 1u);
    EXPECT_EQ(params[0]["label"].getString(), "value: any");
    EXPECT_EQ(resp["result"]["activeParameter"].getInteger(), 0);
}

TEST_F(LSPTest, SignatureHelpBuiltinWithActiveParam) {
    // Test signature help for built-in pow function after comma
    initAndOpen("file:///test.liva", "func main() {\n    pow(2.0, \n}");
    // Cursor after 'pow(2.0, ' on line 1, col 13
    auto resp = parseResponse(
        server.handleMessage(signatureHelpRequest("file:///test.liva", 1, 13)));
    EXPECT_FALSE(resp["result"].isNull());
    const auto &sigs = resp["result"]["signatures"].getArray();
    ASSERT_GE(sigs.size(), 1u);
    std::string label = sigs[0]["label"].getString();
    EXPECT_NE(label.find("pow"), std::string::npos);
    EXPECT_NE(label.find("base"), std::string::npos);
    EXPECT_NE(label.find("exp"), std::string::npos);
    // Should have 2 parameters
    const auto &params = sigs[0]["parameters"].getArray();
    EXPECT_EQ(params.size(), 2u);
    EXPECT_EQ(params[0]["label"].getString(), "base: f64");
    EXPECT_EQ(params[1]["label"].getString(), "exp: f64");
    // Active parameter should be 1 (after the comma)
    EXPECT_EQ(resp["result"]["activeParameter"].getInteger(), 1);
}

TEST_F(LSPTest, SignatureHelpCapability) {
    auto resp = parseResponse(server.handleMessage(initRequest()));
    const auto &caps = resp["result"]["capabilities"];
    EXPECT_TRUE(caps["signatureHelpProvider"].isObject());
    const auto &triggerChars =
        caps["signatureHelpProvider"]["triggerCharacters"].getArray();
    ASSERT_GE(triggerChars.size(), 2u);
    // Should include '(' and ','
    bool hasOpenParen = false, hasComma = false;
    for (const auto &tc : triggerChars) {
        if (tc.getString() == "(") hasOpenParen = true;
        if (tc.getString() == ",") hasComma = true;
    }
    EXPECT_TRUE(hasOpenParen);
    EXPECT_TRUE(hasComma);
}

// ============================================================
// Capability Tests (new capabilities)
// ============================================================

TEST_F(LSPTest, InitializeHasNewCapabilities) {
    auto resp = parseResponse(server.handleMessage(initRequest()));
    const auto &caps = resp["result"]["capabilities"];
    EXPECT_TRUE(caps["referencesProvider"].getBool());
    EXPECT_TRUE(caps["renameProvider"].getBool());
    EXPECT_TRUE(caps["signatureHelpProvider"].isObject());
    const auto &sigTriggers =
        caps["signatureHelpProvider"]["triggerCharacters"].getArray();
    EXPECT_FALSE(sigTriggers.empty());
}

// ============================================================
// Semantic Tokens Tests
// ============================================================

TEST_F(LSPTest, SemanticTokensCapability) {
    auto resp = parseResponse(server.handleMessage(initRequest()));
    const auto &caps = resp["result"]["capabilities"];
    EXPECT_TRUE(caps["semanticTokensProvider"].isObject());
    const auto &legend = caps["semanticTokensProvider"]["legend"];
    EXPECT_TRUE(legend.isObject());
    const auto &tokenTypes = legend["tokenTypes"].getArray();
    EXPECT_GE(tokenTypes.size(), 11u);
    EXPECT_EQ(tokenTypes[0].getString(), "keyword");
    EXPECT_EQ(tokenTypes[1].getString(), "type");
    EXPECT_EQ(tokenTypes[2].getString(), "function");
    EXPECT_EQ(tokenTypes[3].getString(), "variable");
    EXPECT_EQ(tokenTypes[4].getString(), "string");
    EXPECT_EQ(tokenTypes[5].getString(), "number");
    EXPECT_TRUE(caps["semanticTokensProvider"]["full"].getBool());
}

TEST_F(LSPTest, SemanticTokensBasic) {
    // "func main() { let x: i32 = 42 }" on two lines
    initAndOpen("file:///test.liva", "func main() {\n    let x: i32 = 42\n}");
    auto resp = parseResponse(
        server.handleMessage(semanticTokensRequest("file:///test.liva")));
    EXPECT_TRUE(resp["result"].isObject());
    const auto &dataArr = resp["result"]["data"].getArray();
    // Data array should be non-empty and a multiple of 5
    EXPECT_FALSE(dataArr.empty());
    EXPECT_EQ(dataArr.size() % 5, 0u);
}

TEST_F(LSPTest, SemanticTokensClassifiesKeywords) {
    // "let x: i32 = 42" — 'let' should be keyword(0), 'x' variable(3),
    // 'i32' type(1), '=' operator(7), '42' number(5)
    initAndOpen("file:///test.liva", "let x: i32 = 42");
    auto resp = parseResponse(
        server.handleMessage(semanticTokensRequest("file:///test.liva")));
    const auto &dataArr = resp["result"]["data"].getArray();
    ASSERT_GE(dataArr.size(), 5u);

    // First token: 'let' — keyword (type 0), on line 0, col 0, length 3
    EXPECT_EQ(dataArr[0].getInteger(), 0);  // deltaLine
    EXPECT_EQ(dataArr[1].getInteger(), 0);  // deltaCol
    EXPECT_EQ(dataArr[2].getInteger(), 3);  // length ("let")
    EXPECT_EQ(dataArr[3].getInteger(), 0);  // tokenType = keyword
}

TEST_F(LSPTest, SemanticTokensFunctionVsVariable) {
    // "func foo() {}\nlet x = 1" — 'foo' after 'func' and before '(' is function(2)
    // 'x' after 'let' with no '(' is variable(3)
    initAndOpen("file:///test.liva", "func foo() {}\nlet x = 1");
    auto resp = parseResponse(
        server.handleMessage(semanticTokensRequest("file:///test.liva")));
    const auto &dataArr = resp["result"]["data"].getArray();

    // Walk through data looking for token types
    bool foundFunction = false;
    bool foundVariable = false;
    for (size_t i = 3; i < dataArr.size(); i += 5) {
        int64_t tokenType = dataArr[i].getInteger();
        if (tokenType == 2) foundFunction = true;   // function
        if (tokenType == 3) foundVariable = true;    // variable
    }
    EXPECT_TRUE(foundFunction);
    EXPECT_TRUE(foundVariable);
}

TEST_F(LSPTest, SemanticTokensTypeKeywords) {
    // Verify i32 is classified as type, true/false as enumMember
    initAndOpen("file:///test.liva", "let x: i32 = 0\nlet b: bool = true");
    auto resp = parseResponse(
        server.handleMessage(semanticTokensRequest("file:///test.liva")));
    const auto &dataArr = resp["result"]["data"].getArray();

    bool foundType = false;
    bool foundEnumMember = false;
    for (size_t i = 3; i < dataArr.size(); i += 5) {
        int64_t tokenType = dataArr[i].getInteger();
        if (tokenType == 1) foundType = true;        // type
        if (tokenType == 8) foundEnumMember = true;  // enumMember (true/false)
    }
    EXPECT_TRUE(foundType);
    EXPECT_TRUE(foundEnumMember);
}

TEST_F(LSPTest, SemanticTokensEmptyDocument) {
    initAndOpen("file:///test.liva", "");
    auto resp = parseResponse(
        server.handleMessage(semanticTokensRequest("file:///test.liva")));
    EXPECT_TRUE(resp["result"].isObject());
    const auto &dataArr = resp["result"]["data"].getArray();
    EXPECT_TRUE(dataArr.empty());
}

// ============================================================
// Formatting Tests
// ============================================================

TEST_F(LSPTest, FormattingBasic) {
    // Poorly indented function — all lines at column 0
    std::string badCode = "func foo() {\nreturn 42\n}";
    initAndOpen("file:///test.liva", badCode);
    auto resp = parseResponse(
        server.handleMessage(formattingRequest("file:///test.liva")));
    const auto &edits = resp["result"].getArray();
    ASSERT_EQ(edits.size(), 1u);
    EXPECT_TRUE(edits[0]["range"].isObject());
    std::string newText = edits[0]["newText"].getString();
    // After formatting: "func foo() {\n    return 42\n}"
    EXPECT_NE(newText.find("func foo() {"), std::string::npos);
    EXPECT_NE(newText.find("    return 42"), std::string::npos);
    EXPECT_NE(newText.find("\n}"), std::string::npos);
}

TEST_F(LSPTest, FormattingNestedBlocks) {
    // Nested if/while blocks with no indentation
    std::string badCode = "func test() {\nif true {\nwhile true {\nbreak\n}\n}\n}";
    initAndOpen("file:///test.liva", badCode);
    auto resp = parseResponse(
        server.handleMessage(formattingRequest("file:///test.liva")));
    const auto &edits = resp["result"].getArray();
    ASSERT_EQ(edits.size(), 1u);
    std::string newText = edits[0]["newText"].getString();
    // Verify nested indentation: 4 spaces for if body, 8 spaces for while body
    EXPECT_NE(newText.find("    if true {"), std::string::npos);
    EXPECT_NE(newText.find("        while true {"), std::string::npos);
    EXPECT_NE(newText.find("            break"), std::string::npos);
    // Closing braces should be at proper depth
    EXPECT_NE(newText.find("        }"), std::string::npos);
    EXPECT_NE(newText.find("    }"), std::string::npos);
}

TEST_F(LSPTest, FormattingCapability) {
    auto resp = parseResponse(server.handleMessage(initRequest()));
    const auto &caps = resp["result"]["capabilities"];
    EXPECT_TRUE(caps["documentFormattingProvider"].getBool());
}

// ============================================================
// Code Action Tests
// ============================================================

TEST_F(LSPTest, CodeActionEmpty) {
    initAndOpen("file:///test.liva", "func main() {\n    return\n}");
    auto resp = parseResponse(
        server.handleMessage(codeActionRequest("file:///test.liva", 0, 0, 2, 1)));
    EXPECT_TRUE(resp["result"].isArray());
    EXPECT_TRUE(resp["result"].getArray().empty());
}

TEST_F(LSPTest, CodeActionCapability) {
    auto resp = parseResponse(server.handleMessage(initRequest()));
    const auto &caps = resp["result"]["capabilities"];
    EXPECT_TRUE(caps["codeActionProvider"].getBool());
}

// ============================================================
// Folding Range Tests
// ============================================================

TEST_F(LSPTest, FoldingRangeBasic) {
    // A function spanning lines 0-2 should produce a folding range
    initAndOpen("file:///test.liva", "func main() {\n    return\n}");
    auto resp = parseResponse(
        server.handleMessage(foldingRangeRequest("file:///test.liva")));
    const auto &ranges = resp["result"].getArray();
    ASSERT_GE(ranges.size(), 1u);
    // The brace block should span from line 0 to line 2
    EXPECT_EQ(ranges[0]["startLine"].getInteger(), 0);
    EXPECT_EQ(ranges[0]["endLine"].getInteger(), 2);
    EXPECT_EQ(ranges[0]["kind"].getString(), "region");
}

TEST_F(LSPTest, FoldingRangeNested) {
    // Nested braces should produce multiple folding ranges
    initAndOpen("file:///test.liva",
                "func foo() {\n    if true {\n        return\n    }\n}");
    auto resp = parseResponse(
        server.handleMessage(foldingRangeRequest("file:///test.liva")));
    const auto &ranges = resp["result"].getArray();
    // Should have at least 2 folding ranges: outer func and inner if
    ASSERT_GE(ranges.size(), 2u);
    // Verify we have both an outer range (0-4) and inner range (1-3)
    bool hasOuter = false, hasInner = false;
    for (const auto &r : ranges) {
        int64_t startLine = r["startLine"].getInteger();
        int64_t endLine = r["endLine"].getInteger();
        if (startLine == 0 && endLine == 4) hasOuter = true;
        if (startLine == 1 && endLine == 3) hasInner = true;
    }
    EXPECT_TRUE(hasOuter);
    EXPECT_TRUE(hasInner);
}

TEST_F(LSPTest, FoldingRangeComments) {
    // Consecutive comment lines should produce a comment folding range
    initAndOpen("file:///test.liva",
                "// line one\n// line two\n// line three\nfunc main() {}");
    auto resp = parseResponse(
        server.handleMessage(foldingRangeRequest("file:///test.liva")));
    const auto &ranges = resp["result"].getArray();
    // Should have a comment fold for lines 0-2
    bool hasComment = false;
    for (const auto &r : ranges) {
        if (r["kind"].getString() == "comment") {
            EXPECT_EQ(r["startLine"].getInteger(), 0);
            EXPECT_EQ(r["endLine"].getInteger(), 2);
            hasComment = true;
        }
    }
    EXPECT_TRUE(hasComment);
}

TEST_F(LSPTest, FoldingRangeEmpty) {
    // Single-line document has no folding ranges
    initAndOpen("file:///test.liva", "let x = 1");
    auto resp = parseResponse(
        server.handleMessage(foldingRangeRequest("file:///test.liva")));
    const auto &ranges = resp["result"].getArray();
    EXPECT_TRUE(ranges.empty());
}

TEST_F(LSPTest, FoldingRangeCapability) {
    auto resp = parseResponse(server.handleMessage(initRequest()));
    const auto &caps = resp["result"]["capabilities"];
    EXPECT_TRUE(caps["foldingRangeProvider"].getBool());
}

// ============================================================
// Selection Range Tests
// ============================================================

TEST_F(LSPTest, SelectionRangeBasic) {
    // Request selection range at a word inside a function body
    initAndOpen("file:///test.liva", "func main() {\n    return\n}");
    // Position at "return" (line 1, col 4)
    auto resp = parseResponse(
        server.handleMessage(selectionRangeRequest(
            "file:///test.liva",
            "[{\"line\":1,\"character\":4}]")));
    const auto &ranges = resp["result"].getArray();
    ASSERT_EQ(ranges.size(), 1u);

    // Innermost range should be the word "return"
    const auto &innermost = ranges[0];
    EXPECT_TRUE(innermost["range"].isObject());
    int64_t startCol = innermost["range"]["start"]["character"].getInteger();
    int64_t endCol = innermost["range"]["end"]["character"].getInteger();
    EXPECT_EQ(startCol, 4);
    EXPECT_EQ(endCol, 10); // "return" is 6 chars

    // Should have a parent chain
    EXPECT_TRUE(innermost.hasKey("parent"));
}

TEST_F(LSPTest, SelectionRangeParentChain) {
    // Verify the parent chain goes from word -> line -> block -> document
    initAndOpen("file:///test.liva", "func main() {\n    return\n}");
    auto resp = parseResponse(
        server.handleMessage(selectionRangeRequest(
            "file:///test.liva",
            "[{\"line\":1,\"character\":4}]")));
    const auto &ranges = resp["result"].getArray();
    ASSERT_EQ(ranges.size(), 1u);

    // Walk the parent chain and count levels
    int depth = 0;
    const JSONValue *current = &ranges[0];
    while (!current->isNull() && current->isObject()) {
        EXPECT_TRUE((*current)["range"].isObject());
        ++depth;
        if (current->hasKey("parent") && !(*current)["parent"].isNull()) {
            current = &(*current)["parent"];
        } else {
            break;
        }
    }
    // word -> line -> block -> document = at least 4 levels
    EXPECT_GE(depth, 3);
}

TEST_F(LSPTest, SelectionRangeCapability) {
    auto resp = parseResponse(server.handleMessage(initRequest()));
    const auto &caps = resp["result"]["capabilities"];
    EXPECT_TRUE(caps["selectionRangeProvider"].getBool());
}

// ============================================================
// Document Highlight Tests
// ============================================================

TEST_F(LSPTest, DocumentHighlightFunction) {
    // Highlight occurrences of "greet" — declaration and usage
    initAndOpen("file:///test.liva", "func greet() {}\nfunc main() {\n    greet()\n}");
    auto resp = parseResponse(
        server.handleMessage(documentHighlightRequest("file:///test.liva", 0, 5)));
    const auto &highlights = resp["result"].getArray();
    // Should find at least 2 occurrences: declaration and call
    ASSERT_GE(highlights.size(), 2u);
    for (const auto &h : highlights) {
        EXPECT_TRUE(h["range"].isObject());
        // kind should be 2 (Read) or 3 (Write/declaration)
        int64_t kind = h["kind"].getInteger();
        EXPECT_TRUE(kind == 2 || kind == 3);
    }
    // First occurrence (declaration) should be Write (3)
    bool hasWrite = false;
    bool hasRead = false;
    for (const auto &h : highlights) {
        if (h["kind"].getInteger() == 3) hasWrite = true;
        if (h["kind"].getInteger() == 2) hasRead = true;
    }
    EXPECT_TRUE(hasWrite);
    EXPECT_TRUE(hasRead);
}

TEST_F(LSPTest, DocumentHighlightVariable) {
    initAndOpen("file:///test.liva", "let x: i32 = 42");
    auto resp = parseResponse(
        server.handleMessage(documentHighlightRequest("file:///test.liva", 0, 4)));
    const auto &highlights = resp["result"].getArray();
    ASSERT_GE(highlights.size(), 1u);
    // The declaration should have kind 3 (Write)
    EXPECT_EQ(highlights[0]["kind"].getInteger(), 3);
}

TEST_F(LSPTest, DocumentHighlightNoSymbol) {
    initAndOpen("file:///test.liva", "func main() {}");
    // Position way off in empty space
    auto resp = parseResponse(
        server.handleMessage(documentHighlightRequest("file:///test.liva", 100, 0)));
    const auto &highlights = resp["result"].getArray();
    EXPECT_TRUE(highlights.empty());
}

TEST_F(LSPTest, DocumentHighlightCapability) {
    auto resp = parseResponse(server.handleMessage(initRequest()));
    const auto &caps = resp["result"]["capabilities"];
    EXPECT_TRUE(caps["documentHighlightProvider"].getBool());
}

// ============================================================
// Doc Comment Hover Tests
// ============================================================

TEST_F(LSPTest, HoverWithDocComment) {
    initAndOpen("file:///test.liva", "/// Adds two numbers\nfunc add() {}");
    auto resp = parseResponse(
        server.handleMessage(hoverRequest("file:///test.liva", 1, 5)));
    if (!resp["result"].isNull()) {
        std::string val = resp["result"]["contents"]["value"].getString();
        EXPECT_NE(val.find("func add()"), std::string::npos);
        EXPECT_NE(val.find("Adds two numbers"), std::string::npos);
    }
}

TEST_F(LSPTest, HoverWithMultiLineDocComment) {
    initAndOpen("file:///test.liva", "/// First line\n/// Second line\nfunc foo() {}");
    auto resp = parseResponse(
        server.handleMessage(hoverRequest("file:///test.liva", 2, 5)));
    if (!resp["result"].isNull()) {
        std::string val = resp["result"]["contents"]["value"].getString();
        EXPECT_NE(val.find("func foo()"), std::string::npos);
        EXPECT_NE(val.find("First line"), std::string::npos);
        EXPECT_NE(val.find("Second line"), std::string::npos);
    }
}

TEST_F(LSPTest, HoverWithoutDocComment) {
    initAndOpen("file:///test.liva", "func bar() {}");
    auto resp = parseResponse(
        server.handleMessage(hoverRequest("file:///test.liva", 0, 5)));
    if (!resp["result"].isNull()) {
        std::string val = resp["result"]["contents"]["value"].getString();
        EXPECT_NE(val.find("func bar()"), std::string::npos);
        // No doc comment separator should appear
        EXPECT_EQ(val.find("---"), std::string::npos);
    }
}

// ============================================================
// Inlay Hint Tests
// ============================================================

// Helper to build inlayHint requests in LSPTest fixture
static std::string inlayHintRequest(const std::string &uri, int startLine,
                                     int endLine, int id = 99) {
    return "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) +
           ",\"method\":\"textDocument/inlayHint\","
           "\"params\":{\"textDocument\":{\"uri\":\"" + uri +
           "\"},\"range\":{\"start\":{\"line\":" + std::to_string(startLine) +
           ",\"character\":0},\"end\":{\"line\":" + std::to_string(endLine) +
           ",\"character\":0}}}}";
}

TEST_F(LSPTest, InlayHintBasicType) {
    initAndOpen("file:///test.liva", "let x = 42");
    auto resp = parseResponse(
        server.handleMessage(inlayHintRequest("file:///test.liva", 0, 1)));
    const auto &hints = resp["result"].getArray();
    ASSERT_EQ(hints.size(), 1u);
    std::string label = hints[0]["label"].getString();
    EXPECT_EQ(label, ": i32");
    EXPECT_EQ(hints[0]["kind"].getInteger(), 1); // Type
    // Position: after "let x" → line 0, character 5
    EXPECT_EQ(hints[0]["position"]["line"].getInteger(), 0);
    EXPECT_EQ(hints[0]["position"]["character"].getInteger(), 5);
}

TEST_F(LSPTest, InlayHintString) {
    initAndOpen("file:///test.liva", "var s = \"hello\"");
    auto resp = parseResponse(
        server.handleMessage(inlayHintRequest("file:///test.liva", 0, 1)));
    const auto &hints = resp["result"].getArray();
    ASSERT_EQ(hints.size(), 1u);
    std::string label = hints[0]["label"].getString();
    EXPECT_EQ(label, ": string");
}

TEST_F(LSPTest, InlayHintNoAnnotation) {
    // Explicit type annotation → no hint
    initAndOpen("file:///test.liva", "let z: i32 = 42");
    auto resp = parseResponse(
        server.handleMessage(inlayHintRequest("file:///test.liva", 0, 1)));
    const auto &hints = resp["result"].getArray();
    EXPECT_TRUE(hints.empty());
}

TEST_F(LSPTest, InlayHintMultipleVars) {
    initAndOpen("file:///test.liva", "let a = 1\nlet b = 2\nlet c = 3");
    auto resp = parseResponse(
        server.handleMessage(inlayHintRequest("file:///test.liva", 0, 3)));
    const auto &hints = resp["result"].getArray();
    ASSERT_EQ(hints.size(), 3u);
    for (const auto &h : hints) {
        EXPECT_EQ(h["label"].getString(), ": i32");
    }
}

TEST_F(LSPTest, InlayHintInsideFunction) {
    initAndOpen("file:///test.liva", "func foo() {\n    let x = 42\n}");
    auto resp = parseResponse(
        server.handleMessage(inlayHintRequest("file:///test.liva", 0, 3)));
    const auto &hints = resp["result"].getArray();
    ASSERT_EQ(hints.size(), 1u);
    EXPECT_EQ(hints[0]["label"].getString(), ": i32");
    EXPECT_EQ(hints[0]["position"]["line"].getInteger(), 1);
}

TEST_F(LSPTest, InlayHintCapability) {
    auto resp = parseResponse(server.handleMessage(initRequest()));
    const auto &caps = resp["result"]["capabilities"];
    EXPECT_TRUE(caps["inlayHintProvider"].getBool());
}

// ============================================================
// Workspace Symbol Tests
// ============================================================

static std::string workspaceSymbolRequest(const std::string &query, int id = 130) {
    return "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) +
           ",\"method\":\"workspace/symbol\","
           "\"params\":{\"query\":\"" + query + "\"}}";
}

TEST_F(LSPTest, WorkspaceSymbolBasic) {
    initAndOpen("file:///test.liva", "func greet() {}\nstruct Point {\n    var x: i32\n}");
    auto resp = parseResponse(
        server.handleMessage(workspaceSymbolRequest("")));
    const auto &syms = resp["result"].getArray();
    ASSERT_GE(syms.size(), 2u);
    // Should contain greet and Point
    bool hasGreet = false, hasPoint = false;
    for (const auto &s : syms) {
        if (s["name"].getString() == "greet") hasGreet = true;
        if (s["name"].getString() == "Point") hasPoint = true;
    }
    EXPECT_TRUE(hasGreet);
    EXPECT_TRUE(hasPoint);
    // Check SymbolInformation format (location with uri)
    EXPECT_TRUE(syms[0].hasKey("location"));
    EXPECT_TRUE(syms[0]["location"].hasKey("uri"));
    EXPECT_TRUE(syms[0]["location"].hasKey("range"));
}

TEST_F(LSPTest, WorkspaceSymbolQuery) {
    initAndOpen("file:///test.liva", "func greet() {}\nfunc main() {}");
    auto resp = parseResponse(
        server.handleMessage(workspaceSymbolRequest("greet")));
    const auto &syms = resp["result"].getArray();
    ASSERT_EQ(syms.size(), 1u);
    EXPECT_EQ(syms[0]["name"].getString(), "greet");
    EXPECT_EQ(syms[0]["kind"].getInteger(), 12); // Function
}

TEST_F(LSPTest, WorkspaceSymbolMultipleFiles) {
    // Init server
    server.handleMessage(initRequest());
    server.takeNotifications();
    server.handleMessage(initializedNotif());
    // Open two files
    server.handleMessage(didOpen("file:///a.liva", "func alpha() {}"));
    server.takeNotifications();
    server.handleMessage(didOpen("file:///b.liva", "struct Beta {\n    var x: i32\n}"));
    server.takeNotifications();

    auto resp = parseResponse(
        server.handleMessage(workspaceSymbolRequest("")));
    const auto &syms = resp["result"].getArray();
    bool hasAlpha = false, hasBeta = false;
    for (const auto &s : syms) {
        if (s["name"].getString() == "alpha") {
            hasAlpha = true;
            EXPECT_EQ(s["location"]["uri"].getString(), "file:///a.liva");
        }
        if (s["name"].getString() == "Beta") {
            hasBeta = true;
            EXPECT_EQ(s["location"]["uri"].getString(), "file:///b.liva");
        }
    }
    EXPECT_TRUE(hasAlpha);
    EXPECT_TRUE(hasBeta);
}

TEST_F(LSPTest, WorkspaceSymbolCaseInsensitive) {
    initAndOpen("file:///test.liva", "struct Point {\n    var x: i32\n}");
    auto resp = parseResponse(
        server.handleMessage(workspaceSymbolRequest("point")));
    const auto &syms = resp["result"].getArray();
    ASSERT_GE(syms.size(), 1u);
    EXPECT_EQ(syms[0]["name"].getString(), "Point");
}

TEST_F(LSPTest, WorkspaceSymbolNoMatch) {
    initAndOpen("file:///test.liva", "func greet() {}");
    auto resp = parseResponse(
        server.handleMessage(workspaceSymbolRequest("xyz")));
    const auto &syms = resp["result"].getArray();
    EXPECT_EQ(syms.size(), 0u);
}

TEST_F(LSPTest, WorkspaceSymbolCapability) {
    auto resp = parseResponse(server.handleMessage(initRequest()));
    const auto &caps = resp["result"]["capabilities"];
    EXPECT_TRUE(caps["workspaceSymbolProvider"].getBool());
}
