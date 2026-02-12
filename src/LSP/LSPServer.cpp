#include "liva/LSP/LSPServer.h"
#include "liva/Lexer/Lexer.h"
#include "liva/Parser/Parser.h"
#include "liva/Sema/Sema.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

namespace liva {

// ============================================================
// JSONValue implementation
// ============================================================

static const std::string kEmptyString;
static const std::vector<JSONValue> kEmptyArray;
static const std::map<std::string, JSONValue> kEmptyObject;
static const JSONValue kNullValue;

JSONValue::JSONValue(const JSONValue &other)
    : kind_(other.kind_), boolVal_(other.boolVal_), intVal_(other.intVal_),
      doubleVal_(other.doubleVal_), stringVal_(other.stringVal_),
      arrayVal_(other.arrayVal_), objectVal_(other.objectVal_) {}

JSONValue::JSONValue(JSONValue &&other) noexcept
    : kind_(other.kind_), boolVal_(other.boolVal_), intVal_(other.intVal_),
      doubleVal_(other.doubleVal_), stringVal_(std::move(other.stringVal_)),
      arrayVal_(std::move(other.arrayVal_)),
      objectVal_(std::move(other.objectVal_)) {
    other.kind_ = Null;
}

JSONValue &JSONValue::operator=(const JSONValue &other) {
    if (this != &other) {
        kind_ = other.kind_;
        boolVal_ = other.boolVal_;
        intVal_ = other.intVal_;
        doubleVal_ = other.doubleVal_;
        stringVal_ = other.stringVal_;
        arrayVal_ = other.arrayVal_;
        objectVal_ = other.objectVal_;
    }
    return *this;
}

JSONValue &JSONValue::operator=(JSONValue &&other) noexcept {
    if (this != &other) {
        kind_ = other.kind_;
        boolVal_ = other.boolVal_;
        intVal_ = other.intVal_;
        doubleVal_ = other.doubleVal_;
        stringVal_ = std::move(other.stringVal_);
        arrayVal_ = std::move(other.arrayVal_);
        objectVal_ = std::move(other.objectVal_);
        other.kind_ = Null;
    }
    return *this;
}

const std::string &JSONValue::getString() const {
    return kind_ == String ? stringVal_ : kEmptyString;
}

const std::vector<JSONValue> &JSONValue::getArray() const {
    return kind_ == Array ? arrayVal_ : kEmptyArray;
}

const std::map<std::string, JSONValue> &JSONValue::getObject() const {
    return kind_ == Object ? objectVal_ : kEmptyObject;
}

const JSONValue &JSONValue::operator[](const std::string &key) const {
    if (kind_ != Object) return kNullValue;
    auto it = objectVal_.find(key);
    return it != objectVal_.end() ? it->second : kNullValue;
}

bool JSONValue::hasKey(const std::string &key) const {
    if (kind_ != Object) return false;
    return objectVal_.find(key) != objectVal_.end();
}

void JSONValue::set(const std::string &key, JSONValue val) {
    if (kind_ == Object) {
        objectVal_[key] = std::move(val);
    }
}

void JSONValue::push(JSONValue val) {
    if (kind_ == Array) {
        arrayVal_.push_back(std::move(val));
    }
}

JSONValue JSONValue::object() {
    return JSONValue(std::map<std::string, JSONValue>{});
}

JSONValue JSONValue::array() {
    return JSONValue(std::vector<JSONValue>{});
}

// ============================================================
// JSON Serialization
// ============================================================

static std::string escapeJSONString(const std::string &s) {
    std::string result;
    result.reserve(s.size() + 4);
    for (char c : s) {
        switch (c) {
        case '"':  result += "\\\""; break;
        case '\\': result += "\\\\"; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        case '\b': result += "\\b"; break;
        case '\f': result += "\\f"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x",
                              static_cast<unsigned>(static_cast<unsigned char>(c)));
                result += buf;
            } else {
                result += c;
            }
            break;
        }
    }
    return result;
}

std::string JSONValue::serialize() const {
    switch (kind_) {
    case Null: return "null";
    case Bool: return boolVal_ ? "true" : "false";
    case Integer: {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(intVal_));
        return buf;
    }
    case Double: {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.17g", doubleVal_);
        return buf;
    }
    case String:
        return "\"" + escapeJSONString(stringVal_) + "\"";
    case Array: {
        std::string result = "[";
        for (size_t i = 0; i < arrayVal_.size(); ++i) {
            if (i > 0) result += ",";
            result += arrayVal_[i].serialize();
        }
        result += "]";
        return result;
    }
    case Object: {
        std::string result = "{";
        bool first = true;
        for (const auto &kv : objectVal_) {
            if (!first) result += ",";
            first = false;
            result += "\"" + escapeJSONString(kv.first) + "\":" + kv.second.serialize();
        }
        result += "}";
        return result;
    }
    }
    return "null";
}

// ============================================================
// JSON Parser (recursive descent)
// ============================================================

namespace {

class JSONParser {
public:
    explicit JSONParser(const std::string &input) : input_(input), pos_(0) {}

    JSONParseResult parse() {
        JSONParseResult result;
        skipWhitespace();
        if (pos_ >= input_.size()) {
            result.error = "empty input";
            return result;
        }
        result.value = parseValue();
        if (!error_.empty()) {
            result.error = error_;
            return result;
        }
        result.success = true;
        return result;
    }

private:
    const std::string &input_;
    size_t pos_;
    std::string error_;

    void skipWhitespace() {
        while (pos_ < input_.size() &&
               (input_[pos_] == ' ' || input_[pos_] == '\t' ||
                input_[pos_] == '\n' || input_[pos_] == '\r')) {
            ++pos_;
        }
    }

    char peek() const {
        return pos_ < input_.size() ? input_[pos_] : '\0';
    }

    char advance() {
        return pos_ < input_.size() ? input_[pos_++] : '\0';
    }

    JSONValue parseValue() {
        skipWhitespace();
        if (pos_ >= input_.size()) {
            error_ = "unexpected end of input";
            return JSONValue();
        }
        char c = peek();
        if (c == '"') return parseString();
        if (c == '{') return parseObject();
        if (c == '[') return parseArray();
        if (c == 't' || c == 'f') return parseBool();
        if (c == 'n') return parseNull();
        if (c == '-' || (c >= '0' && c <= '9')) return parseNumber();
        error_ = std::string("unexpected character '") + c + "'";
        return JSONValue();
    }

    JSONValue parseString() {
        if (advance() != '"') {
            error_ = "expected '\"'";
            return JSONValue();
        }
        std::string result;
        while (pos_ < input_.size()) {
            char c = advance();
            if (c == '"') return JSONValue(result);
            if (c == '\\') {
                if (pos_ >= input_.size()) {
                    error_ = "unterminated escape";
                    return JSONValue();
                }
                char esc = advance();
                switch (esc) {
                case '"':  result += '"'; break;
                case '\\': result += '\\'; break;
                case '/':  result += '/'; break;
                case 'b':  result += '\b'; break;
                case 'f':  result += '\f'; break;
                case 'n':  result += '\n'; break;
                case 'r':  result += '\r'; break;
                case 't':  result += '\t'; break;
                case 'u': {
                    if (pos_ + 4 > input_.size()) {
                        error_ = "incomplete unicode escape";
                        return JSONValue();
                    }
                    std::string hex = input_.substr(pos_, 4);
                    pos_ += 4;
                    unsigned long cp = std::strtoul(hex.c_str(), nullptr, 16);
                    if (cp < 0x80) {
                        result += static_cast<char>(cp);
                    } else if (cp < 0x800) {
                        result += static_cast<char>(0xC0 | (cp >> 6));
                        result += static_cast<char>(0x80 | (cp & 0x3F));
                    } else {
                        result += static_cast<char>(0xE0 | (cp >> 12));
                        result += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                        result += static_cast<char>(0x80 | (cp & 0x3F));
                    }
                    break;
                }
                default:
                    error_ = std::string("unknown escape '\\") + esc + "'";
                    return JSONValue();
                }
            } else {
                result += c;
            }
        }
        error_ = "unterminated string";
        return JSONValue();
    }

    JSONValue parseNumber() {
        size_t start = pos_;
        if (peek() == '-') ++pos_;
        while (pos_ < input_.size() && input_[pos_] >= '0' && input_[pos_] <= '9')
            ++pos_;
        bool isFloat = false;
        if (pos_ < input_.size() && input_[pos_] == '.') {
            isFloat = true;
            ++pos_;
            while (pos_ < input_.size() && input_[pos_] >= '0' && input_[pos_] <= '9')
                ++pos_;
        }
        if (pos_ < input_.size() && (input_[pos_] == 'e' || input_[pos_] == 'E')) {
            isFloat = true;
            ++pos_;
            if (pos_ < input_.size() && (input_[pos_] == '+' || input_[pos_] == '-'))
                ++pos_;
            while (pos_ < input_.size() && input_[pos_] >= '0' && input_[pos_] <= '9')
                ++pos_;
        }
        std::string numStr = input_.substr(start, pos_ - start);
        if (isFloat) {
            char *end = nullptr;
            double d = std::strtod(numStr.c_str(), &end);
            return JSONValue(d);
        } else {
            char *end = nullptr;
            long long ll = std::strtoll(numStr.c_str(), &end, 10);
            return JSONValue(static_cast<int64_t>(ll));
        }
    }

    JSONValue parseArray() {
        advance(); // '['
        skipWhitespace();
        auto arr = JSONValue::array();
        if (peek() == ']') {
            advance();
            return arr;
        }
        while (true) {
            JSONValue val = parseValue();
            if (!error_.empty()) return JSONValue();
            arr.push(std::move(val));
            skipWhitespace();
            if (peek() == ',') {
                advance();
                skipWhitespace();
            } else if (peek() == ']') {
                advance();
                return arr;
            } else {
                error_ = "expected ',' or ']' in array";
                return JSONValue();
            }
        }
    }

    JSONValue parseObject() {
        advance(); // '{'
        skipWhitespace();
        auto obj = JSONValue::object();
        if (peek() == '}') {
            advance();
            return obj;
        }
        while (true) {
            skipWhitespace();
            if (peek() != '"') {
                error_ = "expected string key in object";
                return JSONValue();
            }
            JSONValue keyVal = parseString();
            if (!error_.empty()) return JSONValue();
            skipWhitespace();
            if (advance() != ':') {
                error_ = "expected ':' after object key";
                return JSONValue();
            }
            JSONValue val = parseValue();
            if (!error_.empty()) return JSONValue();
            obj.set(keyVal.getString(), std::move(val));
            skipWhitespace();
            if (peek() == ',') {
                advance();
                skipWhitespace();
            } else if (peek() == '}') {
                advance();
                return obj;
            } else {
                error_ = "expected ',' or '}' in object";
                return JSONValue();
            }
        }
    }

    JSONValue parseBool() {
        if (input_.compare(pos_, 4, "true") == 0) {
            pos_ += 4;
            return JSONValue(true);
        }
        if (input_.compare(pos_, 5, "false") == 0) {
            pos_ += 5;
            return JSONValue(false);
        }
        error_ = "invalid literal";
        return JSONValue();
    }

    JSONValue parseNull() {
        if (input_.compare(pos_, 4, "null") == 0) {
            pos_ += 4;
            return JSONValue();
        }
        error_ = "invalid literal";
        return JSONValue();
    }
};

} // anonymous namespace

JSONParseResult parseJSON(const std::string &input) {
    JSONParser parser(input);
    return parser.parse();
}

// ============================================================
// LSPServer
// ============================================================

LSPServer::LSPServer() = default;

// ============================================================
// Transport
// ============================================================

bool LSPServer::readMessage(std::string &out) {
    // Read Content-Length header
    int contentLength = -1;
    while (true) {
        std::string line;
        int c;
        while ((c = std::fgetc(stdin)) != EOF) {
            if (c == '\n') break;
            if (c != '\r') line += static_cast<char>(c);
        }
        if (c == EOF) return false;
        if (line.empty()) break; // blank line after headers
        if (line.compare(0, 16, "Content-Length: ") == 0) {
            contentLength = static_cast<int>(
                std::strtol(line.c_str() + 16, nullptr, 10));
        }
    }
    if (contentLength <= 0) return false;

    out.resize(static_cast<size_t>(contentLength));
    size_t bytesRead = std::fread(&out[0], 1, static_cast<size_t>(contentLength), stdin);
    return bytesRead == static_cast<size_t>(contentLength);
}

void LSPServer::writeMessage(const std::string &json) {
    std::fprintf(stdout, "Content-Length: %u\r\n\r\n",
                 static_cast<unsigned>(json.size()));
    std::fwrite(json.c_str(), 1, json.size(), stdout);
    std::fflush(stdout);
}

// ============================================================
// Main loop
// ============================================================

int LSPServer::run() {
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    useStdio_ = true;
    while (!exitRequested_) {
        std::string msg;
        if (!readMessage(msg)) break;
        auto parsed = parseJSON(msg);
        if (!parsed.success) continue;
        auto response = dispatch(parsed.value);
        if (!response.isNull()) {
            writeMessage(response.serialize());
        }
    }
    return shutdownRequested_ ? 0 : 1;
}

std::string LSPServer::handleMessage(const std::string &json) {
    useStdio_ = false;
    auto parsed = parseJSON(json);
    if (!parsed.success) return "";
    auto response = dispatch(parsed.value);
    if (response.isNull()) return "";
    return response.serialize();
}

std::vector<std::string> LSPServer::takeNotifications() {
    std::vector<std::string> result;
    result.swap(pendingNotifications_);
    return result;
}

// ============================================================
// Dispatch
// ============================================================

JSONValue LSPServer::dispatch(const JSONValue &msg) {
    const std::string &method = msg["method"].getString();
    const JSONValue &id = msg["id"];
    const JSONValue &params = msg["params"];

    // Requests (have id)
    if (!id.isNull()) {
        if (method == "initialize")
            return handleInitialize(id, params);
        if (method == "shutdown")
            return handleShutdown(id);
        if (method == "textDocument/completion")
            return handleCompletion(id, params);
        if (method == "textDocument/hover")
            return handleHover(id, params);
        if (method == "textDocument/definition")
            return handleDefinition(id, params);
        if (method == "textDocument/documentSymbol")
            return handleDocumentSymbol(id, params);
        // Unknown request → method not found
        return makeError(id, -32601, "method not found: " + method);
    }

    // Notifications (no id)
    if (method == "initialized")
        handleInitialized(params);
    else if (method == "exit")
        handleExit();
    else if (method == "textDocument/didOpen")
        handleDidOpen(params);
    else if (method == "textDocument/didChange")
        handleDidChange(params);
    else if (method == "textDocument/didClose")
        handleDidClose(params);

    return JSONValue(); // null → no response for notifications
}

JSONValue LSPServer::makeResponse(const JSONValue &id, JSONValue result) {
    auto resp = JSONValue::object();
    resp.set("jsonrpc", JSONValue("2.0"));
    resp.set("id", id);
    resp.set("result", std::move(result));
    return resp;
}

JSONValue LSPServer::makeError(const JSONValue &id, int code,
                               const std::string &msg) {
    auto err = JSONValue::object();
    err.set("code", JSONValue(static_cast<int64_t>(code)));
    err.set("message", JSONValue(msg));
    auto resp = JSONValue::object();
    resp.set("jsonrpc", JSONValue("2.0"));
    resp.set("id", id);
    resp.set("error", std::move(err));
    return resp;
}

void LSPServer::sendNotification(const std::string &method, JSONValue params) {
    auto notif = JSONValue::object();
    notif.set("jsonrpc", JSONValue("2.0"));
    notif.set("method", JSONValue(method));
    notif.set("params", std::move(params));
    std::string json = notif.serialize();
    if (useStdio_) {
        writeMessage(json);
    } else {
        pendingNotifications_.push_back(std::move(json));
    }
}

// ============================================================
// Lifecycle handlers
// ============================================================

JSONValue LSPServer::handleInitialize(const JSONValue &id,
                                      const JSONValue & /*params*/) {
    initialized_ = true;

    auto capabilities = JSONValue::object();

    // Full document sync
    capabilities.set("textDocumentSync", JSONValue(static_cast<int64_t>(1)));

    // Completion
    auto completionOpts = JSONValue::object();
    auto triggers = JSONValue::array();
    triggers.push(JSONValue("."));
    triggers.push(JSONValue(":"));
    completionOpts.set("triggerCharacters", std::move(triggers));
    capabilities.set("completionProvider", std::move(completionOpts));

    // Hover
    capabilities.set("hoverProvider", JSONValue(true));

    // Go to definition
    capabilities.set("definitionProvider", JSONValue(true));

    // Document symbols
    capabilities.set("documentSymbolProvider", JSONValue(true));

    auto serverInfo = JSONValue::object();
    serverInfo.set("name", JSONValue("liva-lsp"));
    serverInfo.set("version", JSONValue("0.1.0"));

    auto result = JSONValue::object();
    result.set("capabilities", std::move(capabilities));
    result.set("serverInfo", std::move(serverInfo));

    return makeResponse(id, std::move(result));
}

void LSPServer::handleInitialized(const JSONValue & /*params*/) {
    // No action needed
}

JSONValue LSPServer::handleShutdown(const JSONValue &id) {
    shutdownRequested_ = true;
    return makeResponse(id, JSONValue());
}

void LSPServer::handleExit() {
    exitRequested_ = true;
}

// ============================================================
// Document sync
// ============================================================

void LSPServer::handleDidOpen(const JSONValue &params) {
    const JSONValue &td = params["textDocument"];
    std::string uri = td["uri"].getString();
    std::string text = td["text"].getString();
    int version = static_cast<int>(td["version"].getInteger());

    DocumentState doc;
    doc.uri = uri;
    doc.content = std::move(text);
    doc.version = version;

    analyzeDocument(doc);
    publishDiagnostics(doc);
    documents_[uri] = std::move(doc);
}

void LSPServer::handleDidChange(const JSONValue &params) {
    const JSONValue &td = params["textDocument"];
    std::string uri = td["uri"].getString();
    int version = static_cast<int>(td["version"].getInteger());

    auto it = documents_.find(uri);
    if (it == documents_.end()) return;

    const auto &changes = params["contentChanges"].getArray();
    if (!changes.empty()) {
        // Full sync: take the last change's text
        it->second.content = changes.back()["text"].getString();
    }
    it->second.version = version;

    analyzeDocument(it->second);
    publishDiagnostics(it->second);
}

void LSPServer::handleDidClose(const JSONValue &params) {
    const JSONValue &td = params["textDocument"];
    std::string uri = td["uri"].getString();

    // Publish empty diagnostics to clear
    auto diagParams = JSONValue::object();
    diagParams.set("uri", JSONValue(uri));
    diagParams.set("diagnostics", JSONValue::array());
    sendNotification("textDocument/publishDiagnostics", std::move(diagParams));

    documents_.erase(uri);
}

// ============================================================
// Analysis
// ============================================================

void LSPServer::analyzeDocument(DocumentState &doc) {
    std::string path = uriToPath(doc.uri);
    if (path.empty()) path = doc.uri;

    doc.sm = std::make_unique<SourceManager>(path, doc.content);
    doc.diag = DiagnosticsEngine(doc.sm.get());
    doc.tu.reset();
    doc.analysisValid = false;

    Lexer lexer(*doc.sm, doc.diag);
    Parser parser(lexer, doc.diag);
    doc.tu = parser.parseTranslationUnit();

    if (!doc.diag.hasErrors() && doc.tu) {
        Sema sema(doc.diag, nullptr); // no ModuleLoader for single-file
        sema.analyze(*doc.tu);
    }
    doc.analysisValid = (doc.tu != nullptr);
}

void LSPServer::publishDiagnostics(const DocumentState &doc) {
    auto diags = JSONValue::array();
    for (const auto &d : doc.diag.getDiagnostics()) {
        auto diagObj = JSONValue::object();

        // Range (start == end for point diagnostic)
        auto start = JSONValue::object();
        int line = (d.location.line > 0) ? static_cast<int>(d.location.line) - 1 : 0;
        int col  = (d.location.column > 0) ? static_cast<int>(d.location.column) - 1 : 0;
        start.set("line", JSONValue(static_cast<int64_t>(line)));
        start.set("character", JSONValue(static_cast<int64_t>(col)));

        auto end = JSONValue::object();
        end.set("line", JSONValue(static_cast<int64_t>(line)));
        end.set("character", JSONValue(static_cast<int64_t>(col)));

        auto range = JSONValue::object();
        range.set("start", std::move(start));
        range.set("end", std::move(end));
        diagObj.set("range", std::move(range));

        // Severity: 1=Error, 2=Warning, 3=Info, 4=Hint
        int severity = 1;
        switch (d.level) {
        case DiagLevel::Error:   severity = 1; break;
        case DiagLevel::Warning: severity = 2; break;
        case DiagLevel::Note:    severity = 3; break;
        }
        diagObj.set("severity", JSONValue(static_cast<int64_t>(severity)));

        diagObj.set("source", JSONValue("liva"));
        diagObj.set("message", JSONValue(d.message));

        diags.push(std::move(diagObj));
    }

    auto params = JSONValue::object();
    params.set("uri", JSONValue(doc.uri));
    params.set("diagnostics", std::move(diags));
    sendNotification("textDocument/publishDiagnostics", std::move(params));
}

// ============================================================
// Completion
// ============================================================

JSONValue LSPServer::handleCompletion(const JSONValue &id,
                                      const JSONValue &params) {
    auto items = JSONValue::array();

    // 1. Keywords (CompletionItemKind::Keyword = 14)
    static const char *keywords[] = {
        "func", "struct", "let", "var", "if", "else", "while", "for",
        "return", "match", "enum", "impl", "protocol", "ref", "mut", "as",
        "import", "pub", "self", "break", "continue", "nil", "where",
        "async", "await", "const", "try", "type", "true", "false", "in", "case"
    };
    for (const char *kw : keywords) {
        auto item = JSONValue::object();
        item.set("label", JSONValue(kw));
        item.set("kind", JSONValue(static_cast<int64_t>(14)));
        items.push(std::move(item));
    }

    // 2. Built-in functions (CompletionItemKind::Function = 3)
    static const char *builtins[] = {
        "println", "print", "len", "toString", "abs", "sqrt", "pow",
        "min", "max", "readLine", "format", "parseInt", "parseFloat",
        "randInt", "randFloat"
    };
    for (const char *bi : builtins) {
        auto item = JSONValue::object();
        item.set("label", JSONValue(bi));
        item.set("kind", JSONValue(static_cast<int64_t>(3)));
        items.push(std::move(item));
    }

    // 3. Symbols from the document
    std::string uri = params["textDocument"]["uri"].getString();
    auto it = documents_.find(uri);
    if (it != documents_.end() && it->second.tu) {
        for (const auto &node : it->second.tu->getDeclarations()) {
            auto *decl = node.get();
            if (auto *fd = dynamic_cast<FuncDecl *>(decl)) {
                auto item = JSONValue::object();
                item.set("label", JSONValue(fd->getName()));
                item.set("kind", JSONValue(static_cast<int64_t>(3))); // Function
                items.push(std::move(item));
            } else if (auto *sd = dynamic_cast<StructDecl *>(decl)) {
                auto item = JSONValue::object();
                item.set("label", JSONValue(sd->getName()));
                item.set("kind", JSONValue(static_cast<int64_t>(22))); // Struct
                items.push(std::move(item));
            } else if (auto *ed = dynamic_cast<EnumDecl *>(decl)) {
                auto item = JSONValue::object();
                item.set("label", JSONValue(ed->getName()));
                item.set("kind", JSONValue(static_cast<int64_t>(13))); // Enum
                items.push(std::move(item));
            } else if (auto *vd = dynamic_cast<VarDecl *>(decl)) {
                auto item = JSONValue::object();
                item.set("label", JSONValue(vd->getName()));
                item.set("kind", JSONValue(static_cast<int64_t>(6))); // Variable
                items.push(std::move(item));
            } else if (auto *pd = dynamic_cast<ProtocolDecl *>(decl)) {
                auto item = JSONValue::object();
                item.set("label", JSONValue(pd->getName()));
                item.set("kind", JSONValue(static_cast<int64_t>(8))); // Interface
                items.push(std::move(item));
            }
        }
    }

    return makeResponse(id, std::move(items));
}

// ============================================================
// Hover
// ============================================================

JSONValue LSPServer::handleHover(const JSONValue &id,
                                 const JSONValue &params) {
    std::string uri = params["textDocument"]["uri"].getString();
    uint32_t line = static_cast<uint32_t>(params["position"]["line"].getInteger()) + 1;
    uint32_t col = static_cast<uint32_t>(params["position"]["character"].getInteger()) + 1;

    auto it = documents_.find(uri);
    if (it == documents_.end() || !it->second.tu) {
        return makeResponse(id, JSONValue());
    }

    const ASTNode *node = findNodeAtPosition(it->second.tu.get(), line, col);
    if (!node) {
        return makeResponse(id, JSONValue());
    }

    std::string hoverText;

    if (auto *fd = dynamic_cast<const FuncDecl *>(node)) {
        hoverText = "func " + fd->getName() + "(";
        bool first = true;
        for (const auto &p : fd->getParams()) {
            if (p.isSelf) continue;
            if (!first) hoverText += ", ";
            first = false;
            hoverText += p.name;
            if (p.type) {
                hoverText += ": " + p.type->toString();
            }
        }
        hoverText += ")";
        if (fd->getReturnType() && !fd->getReturnType()->isVoid()) {
            hoverText += " -> " + fd->getReturnType()->toString();
        }
    } else if (auto *vd = dynamic_cast<const VarDecl *>(node)) {
        hoverText = vd->isMutable() ? "var " : "let ";
        hoverText += vd->getName();
        if (vd->hasTypeAnnotation()) {
            hoverText += ": " + vd->getType()->toString();
        }
    } else if (auto *sd = dynamic_cast<const StructDecl *>(node)) {
        hoverText = "struct " + sd->getName();
    } else if (auto *ed = dynamic_cast<const EnumDecl *>(node)) {
        hoverText = "enum " + ed->getName();
    } else if (auto *pd = dynamic_cast<const ProtocolDecl *>(node)) {
        hoverText = "protocol " + pd->getName();
    } else if (auto *td = dynamic_cast<const TypeAliasDecl *>(node)) {
        hoverText = "type " + td->getName();
        if (td->getTargetType()) {
            hoverText += " = " + td->getTargetType()->toString();
        }
    }

    if (hoverText.empty()) {
        return makeResponse(id, JSONValue());
    }

    auto contents = JSONValue::object();
    contents.set("kind", JSONValue("markdown"));
    contents.set("value", JSONValue("```liva\n" + hoverText + "\n```"));

    auto result = JSONValue::object();
    result.set("contents", std::move(contents));
    return makeResponse(id, std::move(result));
}

// ============================================================
// Go to Definition
// ============================================================

JSONValue LSPServer::handleDefinition(const JSONValue &id,
                                      const JSONValue &params) {
    std::string uri = params["textDocument"]["uri"].getString();
    uint32_t line = static_cast<uint32_t>(params["position"]["line"].getInteger()) + 1;
    uint32_t col = static_cast<uint32_t>(params["position"]["character"].getInteger()) + 1;

    auto it = documents_.find(uri);
    if (it == documents_.end() || !it->second.tu) {
        return makeResponse(id, JSONValue());
    }

    // Find the node at the cursor position
    const ASTNode *node = findNodeAtPosition(it->second.tu.get(), line, col);
    if (!node) {
        return makeResponse(id, JSONValue());
    }

    // If it's already a declaration, jump to its start
    const Decl *targetDecl = dynamic_cast<const Decl *>(node);
    if (!targetDecl) {
        return makeResponse(id, JSONValue());
    }

    SourceLocation loc = targetDecl->getRange().start;
    if (!loc.isValid()) {
        return makeResponse(id, JSONValue());
    }

    auto start = JSONValue::object();
    start.set("line", JSONValue(static_cast<int64_t>(loc.line > 0 ? loc.line - 1 : 0)));
    start.set("character", JSONValue(static_cast<int64_t>(loc.column > 0 ? loc.column - 1 : 0)));

    auto end = JSONValue::object();
    end.set("line", JSONValue(static_cast<int64_t>(loc.line > 0 ? loc.line - 1 : 0)));
    end.set("character", JSONValue(static_cast<int64_t>(loc.column > 0 ? loc.column - 1 : 0)));

    auto range = JSONValue::object();
    range.set("start", std::move(start));
    range.set("end", std::move(end));

    auto location = JSONValue::object();
    location.set("uri", JSONValue(uri));
    location.set("range", std::move(range));

    return makeResponse(id, std::move(location));
}

// ============================================================
// Document Symbols
// ============================================================

JSONValue LSPServer::handleDocumentSymbol(const JSONValue &id,
                                          const JSONValue &params) {
    std::string uri = params["textDocument"]["uri"].getString();
    auto it = documents_.find(uri);
    if (it == documents_.end() || !it->second.tu) {
        return makeResponse(id, JSONValue::array());
    }

    auto symbols = JSONValue::array();
    for (const auto &node : it->second.tu->getDeclarations()) {
        auto *decl = node.get();
        std::string name;
        int kind = 0;

        if (auto *fd = dynamic_cast<FuncDecl *>(decl)) {
            name = fd->getName();
            kind = 12; // Function
        } else if (auto *sd = dynamic_cast<StructDecl *>(decl)) {
            name = sd->getName();
            kind = 23; // Struct
        } else if (auto *ed = dynamic_cast<EnumDecl *>(decl)) {
            name = ed->getName();
            kind = 10; // Enum
        } else if (auto *pd = dynamic_cast<ProtocolDecl *>(decl)) {
            name = pd->getName();
            kind = 11; // Interface
        } else if (auto *vd = dynamic_cast<VarDecl *>(decl)) {
            name = vd->getName();
            kind = 13; // Variable
        } else if (auto *td = dynamic_cast<TypeAliasDecl *>(decl)) {
            name = td->getName();
            kind = 26; // TypeParameter
        }

        if (name.empty() || kind == 0) continue;

        SourceRange sr = decl->getRange();
        int startLine = sr.start.isValid() ? static_cast<int>(sr.start.line) - 1 : 0;
        int startCol  = sr.start.isValid() ? static_cast<int>(sr.start.column) - 1 : 0;
        int endLine   = sr.end.isValid() ? static_cast<int>(sr.end.line) - 1 : startLine;
        int endCol    = sr.end.isValid() ? static_cast<int>(sr.end.column) - 1 : startCol;

        auto rangeStart = JSONValue::object();
        rangeStart.set("line", JSONValue(static_cast<int64_t>(startLine)));
        rangeStart.set("character", JSONValue(static_cast<int64_t>(startCol)));
        auto rangeEnd = JSONValue::object();
        rangeEnd.set("line", JSONValue(static_cast<int64_t>(endLine)));
        rangeEnd.set("character", JSONValue(static_cast<int64_t>(endCol)));
        auto range = JSONValue::object();
        range.set("start", std::move(rangeStart));
        range.set("end", std::move(rangeEnd));

        // selectionRange = same as range for simplicity
        auto selStart = JSONValue::object();
        selStart.set("line", JSONValue(static_cast<int64_t>(startLine)));
        selStart.set("character", JSONValue(static_cast<int64_t>(startCol)));
        auto selEnd = JSONValue::object();
        selEnd.set("line", JSONValue(static_cast<int64_t>(startLine)));
        selEnd.set("character", JSONValue(static_cast<int64_t>(startCol)));
        auto selRange = JSONValue::object();
        selRange.set("start", std::move(selStart));
        selRange.set("end", std::move(selEnd));

        auto sym = JSONValue::object();
        sym.set("name", JSONValue(name));
        sym.set("kind", JSONValue(static_cast<int64_t>(kind)));
        sym.set("range", std::move(range));
        sym.set("selectionRange", std::move(selRange));

        symbols.push(std::move(sym));
    }

    return makeResponse(id, std::move(symbols));
}

// ============================================================
// AST helpers
// ============================================================

const ASTNode *LSPServer::findNodeAtPosition(const TranslationUnit *tu,
                                             uint32_t line, uint32_t col) const {
    if (!tu) return nullptr;

    // Simple linear scan of top-level declarations
    for (const auto &node : tu->getDeclarations()) {
        SourceRange sr = node->getRange();
        if (!sr.isValid()) continue;

        // Check if the position falls within this declaration's range
        if (line >= sr.start.line && line <= sr.end.line) {
            if (line == sr.start.line && col < sr.start.column) continue;
            if (line == sr.end.line && col > sr.end.column) continue;
            return node.get();
        }
    }
    return nullptr;
}

const Decl *LSPServer::findDeclByName(const TranslationUnit *tu,
                                      const std::string &name) const {
    if (!tu) return nullptr;
    for (const auto &node : tu->getDeclarations()) {
        if (auto *fd = dynamic_cast<FuncDecl *>(node.get())) {
            if (fd->getName() == name) return fd;
        } else if (auto *sd = dynamic_cast<StructDecl *>(node.get())) {
            if (sd->getName() == name) return sd;
        } else if (auto *ed = dynamic_cast<EnumDecl *>(node.get())) {
            if (ed->getName() == name) return ed;
        } else if (auto *pd = dynamic_cast<ProtocolDecl *>(node.get())) {
            if (pd->getName() == name) return pd;
        } else if (auto *vd = dynamic_cast<VarDecl *>(node.get())) {
            if (vd->getName() == name) return vd;
        } else if (auto *td = dynamic_cast<TypeAliasDecl *>(node.get())) {
            if (td->getName() == name) return td;
        }
    }
    return nullptr;
}

// ============================================================
// URI helpers
// ============================================================

std::string LSPServer::uriToPath(const std::string &uri) const {
    // Handle file:///... URIs
    std::string prefix = "file:///";
    if (uri.compare(0, prefix.size(), prefix) != 0) {
        return uri;
    }

    std::string path = uri.substr(prefix.size());

    // Percent-decode
    std::string decoded;
    decoded.reserve(path.size());
    for (size_t i = 0; i < path.size(); ++i) {
        if (path[i] == '%' && i + 2 < path.size()) {
            char hex[3] = {path[i + 1], path[i + 2], '\0'};
            unsigned long val = std::strtoul(hex, nullptr, 16);
            decoded += static_cast<char>(val);
            i += 2;
        } else if (path[i] == '/') {
#ifdef _WIN32
            decoded += '\\';
#else
            decoded += '/';
#endif
        } else {
            decoded += path[i];
        }
    }

    return decoded;
}

std::string LSPServer::pathToUri(const std::string &path) const {
    std::string uri = "file:///";
    for (char c : path) {
        if (c == '\\') {
            uri += '/';
        } else {
            uri += c;
        }
    }
    return uri;
}

} // namespace liva
