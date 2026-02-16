#pragma once

#include "liva/AST/Decl.h"
#include "liva/Common/Diagnostics.h"
#include "liva/Common/JSON.h"
#include "liva/Common/SourceLocation.h"
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace liva {

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
    JSONValue handleReferences(const JSONValue &id, const JSONValue &params);
    JSONValue handleRename(const JSONValue &id, const JSONValue &params);
    JSONValue handleSignatureHelp(const JSONValue &id, const JSONValue &params);
    JSONValue handleSemanticTokens(const JSONValue &id, const JSONValue &params);
    JSONValue handleFormatting(const JSONValue &id, const JSONValue &params);
    JSONValue handleCodeAction(const JSONValue &id, const JSONValue &params);
    JSONValue handleFoldingRange(const JSONValue &id, const JSONValue &params);
    JSONValue handleSelectionRange(const JSONValue &id, const JSONValue &params);
    JSONValue handleDocumentHighlight(const JSONValue &id, const JSONValue &params);
    JSONValue handleInlayHint(const JSONValue &id, const JSONValue &params);
    JSONValue handleWorkspaceSymbol(const JSONValue &id, const JSONValue &params);

    // Code Lens
    JSONValue handleCodeLens(const JSONValue &id, const JSONValue &params);

    // Call Hierarchy
    JSONValue handleCallHierarchyPrepare(const JSONValue &id, const JSONValue &params);
    JSONValue handleCallHierarchyIncoming(const JSONValue &id, const JSONValue &params);
    JSONValue handleCallHierarchyOutgoing(const JSONValue &id, const JSONValue &params);

    // Inlay hint helpers
    void collectVarDeclHints(const ASTNode *node, uint32_t startLine,
                             uint32_t endLine, JSONValue &hints);

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

    // Find all occurrences of a name in document text (line/col are 0-indexed)
    struct TextLocation { uint32_t line; uint32_t col; uint32_t endCol; };
    std::vector<TextLocation> findNameOccurrences(const std::string &content,
                                                   const std::string &name) const;

    // Get declaration name at cursor position
    std::string getDeclNameAtPosition(const TranslationUnit *tu,
                                      uint32_t line, uint32_t col) const;

    // Collect all call expressions in a function body
    struct CallInfo { std::string callee; SourceRange range; };
    void collectCalls(const ASTNode *node, std::vector<CallInfo> &calls) const;
    void collectCallsInExpr(const Expr *expr, std::vector<CallInfo> &calls) const;

    // Count references to a name across all documents (excluding declarations)
    int countReferences(const std::string &name) const;

    // Completion helpers
    struct CompletionContext {
        bool isMemberAccess = false;
        std::vector<std::string> chain; // e.g. ["student", "grades"]
        uint32_t line = 0, col = 0;
    };

    CompletionContext extractCompletionContext(const std::string &content,
                                               uint32_t line, uint32_t col);
    void addMemberCompletions(JSONValue &items, const TranslationUnit *tu,
                              const CompletionContext &ctx);
    void addLocalCompletions(JSONValue &items, const TranslationUnit *tu,
                             uint32_t line, uint32_t col);
    std::string resolveTypeName(const TranslationUnit *tu,
                                const std::string &varName,
                                uint32_t line, uint32_t col);
    void collectStructMembers(JSONValue &items, const TranslationUnit *tu,
                              const std::string &typeName);
    void collectBuiltinMembers(JSONValue &items, const std::string &typeName);

    // State
    std::map<std::string, DocumentState> documents_;
    std::vector<std::string> pendingNotifications_;
    bool initialized_ = false;
    bool shutdownRequested_ = false;
    bool exitRequested_ = false;
    bool useStdio_ = true;
};

} // namespace liva
