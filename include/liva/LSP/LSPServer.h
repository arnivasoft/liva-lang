#pragma once

#include "liva/AST/Decl.h"
#include "liva/Common/Diagnostics.h"
#include "liva/Common/SourceLocation.h"
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace liva {

// ============================================================
// JSONValue — minimal JSON value type (no exceptions)
// ============================================================

class JSONValue {
public:
    enum Kind { Null, Bool, Integer, Double, String, Array, Object };

    JSONValue() : kind_(Null), boolVal_(false), intVal_(0), doubleVal_(0.0) {}
    explicit JSONValue(bool v) : kind_(Bool), boolVal_(v), intVal_(0), doubleVal_(0.0) {}
    explicit JSONValue(int64_t v) : kind_(Integer), boolVal_(false), intVal_(v), doubleVal_(0.0) {}
    explicit JSONValue(int v) : kind_(Integer), boolVal_(false), intVal_(v), doubleVal_(0.0) {}
    explicit JSONValue(double v) : kind_(Double), boolVal_(false), intVal_(0), doubleVal_(v) {}
    explicit JSONValue(const std::string &v)
        : kind_(String), boolVal_(false), intVal_(0), doubleVal_(0.0), stringVal_(v) {}
    explicit JSONValue(const char *v)
        : kind_(String), boolVal_(false), intVal_(0), doubleVal_(0.0), stringVal_(v ? v : "") {}
    explicit JSONValue(std::vector<JSONValue> v)
        : kind_(Array), boolVal_(false), intVal_(0), doubleVal_(0.0),
          arrayVal_(std::move(v)) {}
    explicit JSONValue(std::map<std::string, JSONValue> v)
        : kind_(Object), boolVal_(false), intVal_(0), doubleVal_(0.0),
          objectVal_(std::move(v)) {}

    JSONValue(const JSONValue &other);
    JSONValue(JSONValue &&other) noexcept;
    JSONValue &operator=(const JSONValue &other);
    JSONValue &operator=(JSONValue &&other) noexcept;

    Kind getKind() const { return kind_; }
    bool isNull() const { return kind_ == Null; }
    bool isBool() const { return kind_ == Bool; }
    bool isInteger() const { return kind_ == Integer; }
    bool isDouble() const { return kind_ == Double; }
    bool isString() const { return kind_ == String; }
    bool isArray() const { return kind_ == Array; }
    bool isObject() const { return kind_ == Object; }

    bool getBool(bool def = false) const { return kind_ == Bool ? boolVal_ : def; }
    int64_t getInteger(int64_t def = 0) const { return kind_ == Integer ? intVal_ : def; }
    double getDouble(double def = 0.0) const { return kind_ == Double ? doubleVal_ : def; }
    const std::string &getString() const;
    const std::vector<JSONValue> &getArray() const;
    const std::map<std::string, JSONValue> &getObject() const;

    const JSONValue &operator[](const std::string &key) const;
    bool hasKey(const std::string &key) const;

    std::string serialize() const;

    void set(const std::string &key, JSONValue val);
    void push(JSONValue val);

    static JSONValue object();
    static JSONValue array();

private:
    Kind kind_;
    bool boolVal_;
    int64_t intVal_;
    double doubleVal_;
    std::string stringVal_;
    std::vector<JSONValue> arrayVal_;
    std::map<std::string, JSONValue> objectVal_;
};

struct JSONParseResult {
    JSONValue value;
    bool success = false;
    std::string error;
};

JSONParseResult parseJSON(const std::string &input);

// ============================================================
// DocumentState — per-file analysis state
// ============================================================

struct DocumentState {
    std::string uri;
    std::string content;
    int version = 0;
    std::unique_ptr<TranslationUnit> tu;
    std::unique_ptr<SourceManager> sm;
    DiagnosticsEngine diag;
    bool analysisValid = false;
};

// ============================================================
// LSPServer
// ============================================================

class LSPServer {
public:
    LSPServer();

    /// Run the stdio event loop (blocks until exit)
    int run();

    /// Process a single JSON-RPC message and return the response (for testing)
    std::string handleMessage(const std::string &json);

    /// Retrieve and clear buffered notifications (for testing)
    std::vector<std::string> takeNotifications();

    bool isShutdown() const { return shutdownRequested_; }
    bool shouldExit() const { return exitRequested_; }

private:
    // Transport
    bool readMessage(std::string &out);
    void writeMessage(const std::string &json);

    // Dispatch
    JSONValue dispatch(const JSONValue &msg);
    JSONValue makeResponse(const JSONValue &id, JSONValue result);
    JSONValue makeError(const JSONValue &id, int code, const std::string &msg);
    void sendNotification(const std::string &method, JSONValue params);

    // Lifecycle
    JSONValue handleInitialize(const JSONValue &id, const JSONValue &params);
    void handleInitialized(const JSONValue &params);
    JSONValue handleShutdown(const JSONValue &id);
    void handleExit();

    // Document sync
    void handleDidOpen(const JSONValue &params);
    void handleDidChange(const JSONValue &params);
    void handleDidClose(const JSONValue &params);

    // Language features
    JSONValue handleCompletion(const JSONValue &id, const JSONValue &params);
    JSONValue handleHover(const JSONValue &id, const JSONValue &params);
    JSONValue handleDefinition(const JSONValue &id, const JSONValue &params);
    JSONValue handleDocumentSymbol(const JSONValue &id, const JSONValue &params);

    // Analysis helpers
    void analyzeDocument(DocumentState &doc);
    void publishDiagnostics(const DocumentState &doc);
    std::string uriToPath(const std::string &uri) const;
    std::string pathToUri(const std::string &path) const;

    // AST helpers
    const ASTNode *findNodeAtPosition(const TranslationUnit *tu,
                                      uint32_t line, uint32_t col) const;
    const Decl *findDeclByName(const TranslationUnit *tu,
                               const std::string &name) const;

    // State
    std::map<std::string, DocumentState> documents_;
    std::vector<std::string> pendingNotifications_;
    bool initialized_ = false;
    bool shutdownRequested_ = false;
    bool exitRequested_ = false;
    bool useStdio_ = true;
};

} // namespace liva
