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
        if (method == "textDocument/references")
            return handleReferences(id, params);
        if (method == "textDocument/rename")
            return handleRename(id, params);
        if (method == "textDocument/signatureHelp")
            return handleSignatureHelp(id, params);
        if (method == "textDocument/semanticTokens/full")
            return handleSemanticTokens(id, params);
        if (method == "textDocument/formatting")
            return handleFormatting(id, params);
        if (method == "textDocument/codeAction")
            return handleCodeAction(id, params);
        if (method == "textDocument/foldingRange")
            return handleFoldingRange(id, params);
        if (method == "textDocument/selectionRange")
            return handleSelectionRange(id, params);
        if (method == "textDocument/documentHighlight")
            return handleDocumentHighlight(id, params);
        if (method == "textDocument/inlayHint")
            return handleInlayHint(id, params);
        if (method == "workspace/symbol")
            return handleWorkspaceSymbol(id, params);
        if (method == "textDocument/codeLens")
            return handleCodeLens(id, params);
        if (method == "textDocument/prepareCallHierarchy")
            return handleCallHierarchyPrepare(id, params);
        if (method == "callHierarchy/incomingCalls")
            return handleCallHierarchyIncoming(id, params);
        if (method == "callHierarchy/outgoingCalls")
            return handleCallHierarchyOutgoing(id, params);
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

    // Workspace symbols
    capabilities.set("workspaceSymbolProvider", JSONValue(true));

    // References
    capabilities.set("referencesProvider", JSONValue(true));

    // Rename
    capabilities.set("renameProvider", JSONValue(true));

    // Signature help
    auto signatureHelpOpts = JSONValue::object();
    auto sigTriggers = JSONValue::array();
    sigTriggers.push(JSONValue("("));
    sigTriggers.push(JSONValue(","));
    signatureHelpOpts.set("triggerCharacters", std::move(sigTriggers));
    capabilities.set("signatureHelpProvider", std::move(signatureHelpOpts));

    // Semantic tokens
    auto tokenTypes = JSONValue::array();
    tokenTypes.push(JSONValue("keyword"));       // 0
    tokenTypes.push(JSONValue("type"));           // 1
    tokenTypes.push(JSONValue("function"));       // 2
    tokenTypes.push(JSONValue("variable"));       // 3
    tokenTypes.push(JSONValue("string"));         // 4
    tokenTypes.push(JSONValue("number"));         // 5
    tokenTypes.push(JSONValue("comment"));        // 6
    tokenTypes.push(JSONValue("operator"));       // 7
    tokenTypes.push(JSONValue("enumMember"));     // 8
    tokenTypes.push(JSONValue("struct"));         // 9
    tokenTypes.push(JSONValue("parameter"));      // 10

    auto tokenModifiers = JSONValue::array();
    tokenModifiers.push(JSONValue("declaration"));  // 0
    tokenModifiers.push(JSONValue("definition"));   // 1
    tokenModifiers.push(JSONValue("readonly"));     // 2

    auto legend = JSONValue::object();
    legend.set("tokenTypes", std::move(tokenTypes));
    legend.set("tokenModifiers", std::move(tokenModifiers));

    auto semanticTokensOpts = JSONValue::object();
    semanticTokensOpts.set("legend", std::move(legend));
    semanticTokensOpts.set("full", JSONValue(true));
    capabilities.set("semanticTokensProvider", std::move(semanticTokensOpts));

    // Document formatting
    capabilities.set("documentFormattingProvider", JSONValue(true));

    // Code actions — with resolve support and kinds
    {
        auto codeActionOpts = JSONValue::object();
        auto kinds = JSONValue::array();
        kinds.push(JSONValue("quickfix"));
        codeActionOpts.set("codeActionKinds", std::move(kinds));
        capabilities.set("codeActionProvider", std::move(codeActionOpts));
    }

    // Folding range
    capabilities.set("foldingRangeProvider", JSONValue(true));

    // Selection range
    capabilities.set("selectionRangeProvider", JSONValue(true));

    // Document highlight
    capabilities.set("documentHighlightProvider", JSONValue(true));

    // Inlay hints
    capabilities.set("inlayHintProvider", JSONValue(true));

    // Code Lens
    capabilities.set("codeLensProvider", JSONValue::object());

    // Call Hierarchy
    capabilities.set("callHierarchyProvider", JSONValue(true));

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
// Completion helpers
// ============================================================

LSPServer::CompletionContext
LSPServer::extractCompletionContext(const std::string &content,
                                    uint32_t line, uint32_t col) {
    CompletionContext ctx;
    ctx.line = line;
    ctx.col = col;

    // Extract the line at the given position
    uint32_t curLine = 0;
    size_t lineStart = 0;
    for (size_t i = 0; i < content.size(); ++i) {
        if (curLine == line) {
            lineStart = i;
            break;
        }
        if (content[i] == '\n')
            ++curLine;
    }
    if (curLine != line)
        return ctx;

    // Get text up to cursor column
    size_t end = lineStart + col;
    if (end > content.size())
        end = content.size();
    std::string before = content.substr(lineStart, end - lineStart);

    // Walk backwards from cursor to build chain
    // e.g. "    student.grades." → chain=["student","grades"], isMemberAccess=true
    int pos = static_cast<int>(before.size()) - 1;

    // Skip trailing dot
    if (pos >= 0 && before[pos] == '.') {
        ctx.isMemberAccess = true;
        --pos;
    } else {
        return ctx; // no dot → not member access
    }

    // Parse identifier chain backwards
    while (pos >= 0) {
        // Collect identifier
        int idEnd = pos + 1;
        while (pos >= 0 && (std::isalnum(static_cast<unsigned char>(before[pos])) ||
                            before[pos] == '_'))
            --pos;
        int idStart = pos + 1;
        if (idStart < idEnd) {
            ctx.chain.insert(ctx.chain.begin(),
                             before.substr(idStart, idEnd - idStart));
        }
        // Check for another dot to continue chain
        if (pos >= 0 && before[pos] == '.') {
            --pos;
        } else {
            break;
        }
    }

    return ctx;
}

std::string LSPServer::resolveTypeName(const TranslationUnit *tu,
                                        const std::string &varName,
                                        uint32_t line, uint32_t col) {
    if (!tu)
        return "";

    // 1-indexed line for SourceRange comparison
    uint32_t srcLine = line + 1;

    // Helper: resolve type from a TypeRepr
    auto typeReprToName = [](const TypeRepr *tr) -> std::string {
        if (!tr)
            return "";
        if (auto *named = dynamic_cast<const NamedTypeRepr *>(tr))
            return named->getName();
        if (tr->getKind() == TypeRepr::Kind::String)
            return "String";
        if (tr->getKind() == TypeRepr::Kind::Array)
            return "Array";
        if (tr->getKind() == TypeRepr::Kind::Bool)
            return "Bool";
        if (tr->getKind() == TypeRepr::Kind::I32 || tr->getKind() == TypeRepr::Kind::I64 ||
            tr->getKind() == TypeRepr::Kind::I8 || tr->getKind() == TypeRepr::Kind::I16)
            return "Int";
        if (tr->getKind() == TypeRepr::Kind::F32 || tr->getKind() == TypeRepr::Kind::F64)
            return "Float";
        return tr->toString();
    };

    // Helper: resolve type from an initializer expression
    auto typeFromInit = [](const Expr *init) -> std::string {
        if (!init)
            return "";
        if (auto *sl = dynamic_cast<const StructLiteralExpr *>(init))
            return sl->getTypeName();
        // CallExpr where callee is MemberExpr like Type.new(...)
        if (auto *call = dynamic_cast<const CallExpr *>(init)) {
            if (auto *member = dynamic_cast<const MemberExpr *>(call->getCallee())) {
                // e.g. Student.new() → "Student"
                if (auto *ident = dynamic_cast<const IdentifierExpr *>(member->getObject()))
                    return ident->getName();
            }
            // Direct call: Student(...) constructor
            if (auto *ident = dynamic_cast<const IdentifierExpr *>(call->getCallee()))
                return ident->getName();
        }
        // String literal
        if (dynamic_cast<const StringLiteralExpr *>(init))
            return "String";
        // Array literal
        if (dynamic_cast<const ArrayLiteralExpr *>(init))
            return "Array";
        return "";
    };

    // Helper: walk a block recursively to find VarDecl before cursor
    std::function<std::string(const BlockStmt *)> searchBlock;
    searchBlock = [&](const BlockStmt *block) -> std::string {
        if (!block)
            return "";
        for (const auto &stmt : block->getStatements()) {
            // Only look at declarations before cursor line
            if (stmt->getRange().isValid() && stmt->getRange().start.line > srcLine)
                break;
            if (auto *vd = dynamic_cast<const VarDecl *>(stmt.get())) {
                if (vd->getName() == varName) {
                    if (vd->getType())
                        return typeReprToName(vd->getType());
                    if (vd->hasInit())
                        return typeFromInit(vd->getInit());
                }
            }
            // Recurse into nested blocks (if/while/for bodies)
            if (auto *bs = dynamic_cast<const BlockStmt *>(stmt.get())) {
                auto r = searchBlock(bs);
                if (!r.empty())
                    return r;
            }
        }
        return "";
    };

    // Search: find function containing cursor, then search params + body
    for (const auto &node : tu->getDeclarations()) {
        auto *decl = node.get();

        // Check FuncDecl
        if (auto *fd = dynamic_cast<const FuncDecl *>(decl)) {
            auto range = fd->getRange();
            if (range.isValid() && srcLine >= range.start.line &&
                srcLine <= range.end.line) {
                // Search parameters
                for (const auto &param : fd->getParams()) {
                    if (param.name == varName && param.type)
                        return typeReprToName(param.type.get());
                }
                // Search body
                if (fd->getBody()) {
                    auto r = searchBlock(fd->getBody());
                    if (!r.empty())
                        return r;
                }
            }
        }

        // Check ImplDecl methods
        if (auto *impl = dynamic_cast<const ImplDecl *>(decl)) {
            for (const auto &method : impl->getMethods()) {
                auto range = method->getRange();
                if (range.isValid() && srcLine >= range.start.line &&
                    srcLine <= range.end.line) {
                    // "self" → impl type name
                    if (varName == "self")
                        return impl->getTypeName();
                    // Search parameters
                    for (const auto &param : method->getParams()) {
                        if (param.name == varName && param.type)
                            return typeReprToName(param.type.get());
                    }
                    // Search body
                    if (method->getBody()) {
                        auto r = searchBlock(method->getBody());
                        if (!r.empty())
                            return r;
                    }
                }
            }
        }

        // Check ClassDecl methods
        if (auto *cd = dynamic_cast<const ClassDecl *>(decl)) {
            for (const auto *method : cd->getMethods()) {
                auto range = method->getRange();
                if (range.isValid() && srcLine >= range.start.line &&
                    srcLine <= range.end.line) {
                    if (varName == "self")
                        return cd->getName();
                    for (const auto &param : method->getParams()) {
                        if (param.name == varName && param.type)
                            return typeReprToName(param.type.get());
                    }
                    if (method->getBody()) {
                        auto r = searchBlock(method->getBody());
                        if (!r.empty())
                            return r;
                    }
                }
            }
            // Check class init
            if (auto *init = cd->getInit()) {
                auto range = init->getRange();
                if (range.isValid() && srcLine >= range.start.line &&
                    srcLine <= range.end.line) {
                    if (varName == "self")
                        return cd->getName();
                    for (const auto &param : init->getParams()) {
                        if (param.name == varName && param.type)
                            return typeReprToName(param.type.get());
                    }
                    if (init->getBody()) {
                        auto r = searchBlock(init->getBody());
                        if (!r.empty())
                            return r;
                    }
                }
            }
        }

        // Top-level VarDecl
        if (auto *vd = dynamic_cast<const VarDecl *>(decl)) {
            if (vd->getName() == varName) {
                if (vd->getType())
                    return typeReprToName(vd->getType());
                if (vd->hasInit())
                    return typeFromInit(vd->getInit());
            }
        }
    }

    return "";
}

void LSPServer::collectStructMembers(JSONValue &items,
                                      const TranslationUnit *tu,
                                      const std::string &typeName) {
    if (!tu || typeName.empty())
        return;

    for (const auto &node : tu->getDeclarations()) {
        auto *decl = node.get();

        // StructDecl fields
        if (auto *sd = dynamic_cast<const StructDecl *>(decl)) {
            if (sd->getName() == typeName) {
                for (const auto &field : sd->getFields()) {
                    auto item = JSONValue::object();
                    item.set("label", JSONValue(field->getName()));
                    item.set("kind", JSONValue(static_cast<int64_t>(5))); // Field
                    if (field->getType())
                        item.set("detail", JSONValue(field->getType()->toString()));
                    items.push(std::move(item));
                }
            }
        }

        // ClassDecl fields + methods
        if (auto *cd = dynamic_cast<const ClassDecl *>(decl)) {
            if (cd->getName() == typeName) {
                for (const auto *field : cd->getFields()) {
                    auto item = JSONValue::object();
                    item.set("label", JSONValue(field->getName()));
                    item.set("kind", JSONValue(static_cast<int64_t>(5))); // Field
                    if (field->getType())
                        item.set("detail", JSONValue(field->getType()->toString()));
                    items.push(std::move(item));
                }
                for (const auto *method : cd->getMethods()) {
                    auto item = JSONValue::object();
                    item.set("label", JSONValue(method->getName()));
                    item.set("kind", JSONValue(static_cast<int64_t>(2))); // Method
                    // Build signature (exclude self param)
                    std::string sig = "(";
                    bool first = true;
                    for (const auto &p : method->getParams()) {
                        if (p.isSelf)
                            continue;
                        if (!first)
                            sig += ", ";
                        sig += p.name + ": " + (p.type ? p.type->toString() : "?");
                        first = false;
                    }
                    sig += ")";
                    if (method->getReturnType() && !method->getReturnType()->isVoid())
                        sig += " -> " + method->getReturnType()->toString();
                    item.set("detail", JSONValue(sig));
                    items.push(std::move(item));
                }
            }
        }

        // ImplDecl methods for this type
        if (auto *impl = dynamic_cast<const ImplDecl *>(decl)) {
            if (impl->getTypeName() == typeName) {
                for (const auto &method : impl->getMethods()) {
                    auto item = JSONValue::object();
                    item.set("label", JSONValue(method->getName()));
                    item.set("kind", JSONValue(static_cast<int64_t>(2))); // Method
                    std::string sig = "(";
                    bool first = true;
                    for (const auto &p : method->getParams()) {
                        if (p.isSelf)
                            continue;
                        if (!first)
                            sig += ", ";
                        sig += p.name + ": " + (p.type ? p.type->toString() : "?");
                        first = false;
                    }
                    sig += ")";
                    if (method->getReturnType() && !method->getReturnType()->isVoid())
                        sig += " -> " + method->getReturnType()->toString();
                    item.set("detail", JSONValue(sig));
                    items.push(std::move(item));
                }
            }
        }
    }
}

void LSPServer::collectBuiltinMembers(JSONValue &items,
                                       const std::string &typeName) {
    struct MemberInfo {
        const char *label;
        int kind; // 5=Field, 2=Method
        const char *detail;
    };

    std::vector<MemberInfo> members;

    if (typeName == "String") {
        members = {
            {"length", 5, "i64"},
            {"isEmpty", 2, "() -> Bool"},
            {"contains", 2, "(s: String) -> Bool"},
            {"startsWith", 2, "(s: String) -> Bool"},
            {"endsWith", 2, "(s: String) -> Bool"},
            {"toUpper", 2, "() -> String"},
            {"toLower", 2, "() -> String"},
            {"trim", 2, "() -> String"},
            {"split", 2, "(sep: String) -> [String]"},
            {"substring", 2, "(start: i64, end: i64) -> String"},
            {"indexOf", 2, "(s: String) -> i64"},
            {"replace", 2, "(old: String, new: String) -> String"},
        };
    } else if (typeName == "Array") {
        members = {
            {"length", 5, "i64"},
            {"isEmpty", 2, "() -> Bool"},
            {"push", 2, "(element)"},
            {"pop", 2, "() -> T"},
            {"contains", 2, "(element) -> Bool"},
            {"indexOf", 2, "(element) -> i64"},
            {"reverse", 2, "()"},
            {"map", 2, "(f: (T) -> U) -> [U]"},
            {"filter", 2, "(f: (T) -> Bool) -> [T]"},
            {"forEach", 2, "(f: (T) -> Void)"},
        };
    } else if (typeName == "Map") {
        members = {
            {"size", 5, "i64"},
            {"isEmpty", 2, "() -> Bool"},
            {"insert", 2, "(key: K, value: V)"},
            {"remove", 2, "(key: K)"},
            {"contains", 2, "(key: K) -> Bool"},
            {"get", 2, "(key: K) -> V?"},
            {"keys", 2, "() -> [K]"},
            {"values", 2, "() -> [V]"},
        };
    } else if (typeName == "Set") {
        members = {
            {"size", 5, "i64"},
            {"isEmpty", 2, "() -> Bool"},
            {"insert", 2, "(element: T)"},
            {"remove", 2, "(element: T)"},
            {"contains", 2, "(element: T) -> Bool"},
        };
    }

    for (const auto &m : members) {
        auto item = JSONValue::object();
        item.set("label", JSONValue(m.label));
        item.set("kind", JSONValue(static_cast<int64_t>(m.kind)));
        item.set("detail", JSONValue(m.detail));
        items.push(std::move(item));
    }
}

void LSPServer::addMemberCompletions(JSONValue &items,
                                      const TranslationUnit *tu,
                                      const CompletionContext &ctx) {
    if (!tu || ctx.chain.empty())
        return;

    // Resolve the type of the first identifier in chain
    std::string typeName = resolveTypeName(tu, ctx.chain[0], ctx.line, ctx.col);
    if (typeName.empty())
        return;

    // Walk subsequent chain elements to resolve nested types
    for (size_t i = 1; i < ctx.chain.size(); ++i) {
        const std::string &member = ctx.chain[i];
        std::string nextType;

        // Search struct/class fields for this member
        for (const auto &node : tu->getDeclarations()) {
            if (auto *sd = dynamic_cast<const StructDecl *>(node.get())) {
                if (sd->getName() == typeName) {
                    for (const auto &field : sd->getFields()) {
                        if (field->getName() == member && field->getType()) {
                            auto *tr = field->getType();
                            if (auto *named = dynamic_cast<const NamedTypeRepr *>(tr))
                                nextType = named->getName();
                            else if (tr->getKind() == TypeRepr::Kind::Array)
                                nextType = "Array";
                            else if (tr->getKind() == TypeRepr::Kind::String)
                                nextType = "String";
                            else
                                nextType = tr->toString();
                        }
                    }
                }
            }
            if (auto *cd = dynamic_cast<const ClassDecl *>(node.get())) {
                if (cd->getName() == typeName) {
                    for (const auto *field : cd->getFields()) {
                        if (field->getName() == member && field->getType()) {
                            auto *tr = field->getType();
                            if (auto *named = dynamic_cast<const NamedTypeRepr *>(tr))
                                nextType = named->getName();
                            else if (tr->getKind() == TypeRepr::Kind::Array)
                                nextType = "Array";
                            else if (tr->getKind() == TypeRepr::Kind::String)
                                nextType = "String";
                            else
                                nextType = tr->toString();
                        }
                    }
                }
            }
        }
        if (nextType.empty())
            return; // can't resolve further
        typeName = nextType;
    }

    // Collect members for the resolved type
    collectBuiltinMembers(items, typeName);
    collectStructMembers(items, tu, typeName);
}

void LSPServer::addLocalCompletions(JSONValue &items,
                                     const TranslationUnit *tu,
                                     uint32_t line, uint32_t col) {
    if (!tu)
        return;

    uint32_t srcLine = line + 1; // 1-indexed

    // Helper: walk block to collect VarDecls before cursor
    std::function<void(const BlockStmt *)> collectFromBlock;
    collectFromBlock = [&](const BlockStmt *block) {
        if (!block)
            return;
        for (const auto &stmt : block->getStatements()) {
            if (stmt->getRange().isValid() && stmt->getRange().start.line > srcLine)
                break;
            if (auto *vd = dynamic_cast<const VarDecl *>(stmt.get())) {
                auto item = JSONValue::object();
                item.set("label", JSONValue(vd->getName()));
                item.set("kind", JSONValue(static_cast<int64_t>(6))); // Variable
                if (vd->getType())
                    item.set("detail", JSONValue(vd->getType()->toString()));
                items.push(std::move(item));
            }
        }
    };

    for (const auto &node : tu->getDeclarations()) {
        auto *decl = node.get();

        // FuncDecl containing cursor
        if (auto *fd = dynamic_cast<const FuncDecl *>(decl)) {
            auto range = fd->getRange();
            if (range.isValid() && srcLine >= range.start.line &&
                srcLine <= range.end.line) {
                // Add parameters
                for (const auto &param : fd->getParams()) {
                    if (param.isSelf)
                        continue;
                    auto item = JSONValue::object();
                    item.set("label", JSONValue(param.name));
                    item.set("kind", JSONValue(static_cast<int64_t>(6))); // Variable
                    if (param.type)
                        item.set("detail", JSONValue(param.type->toString()));
                    items.push(std::move(item));
                }
                collectFromBlock(fd->getBody());
            }
        }

        // ImplDecl methods
        if (auto *impl = dynamic_cast<const ImplDecl *>(decl)) {
            for (const auto &method : impl->getMethods()) {
                auto range = method->getRange();
                if (range.isValid() && srcLine >= range.start.line &&
                    srcLine <= range.end.line) {
                    // Add self
                    auto selfItem = JSONValue::object();
                    selfItem.set("label", JSONValue("self"));
                    selfItem.set("kind", JSONValue(static_cast<int64_t>(6)));
                    selfItem.set("detail", JSONValue(impl->getTypeName()));
                    items.push(std::move(selfItem));
                    // Add parameters
                    for (const auto &param : method->getParams()) {
                        if (param.isSelf)
                            continue;
                        auto item = JSONValue::object();
                        item.set("label", JSONValue(param.name));
                        item.set("kind", JSONValue(static_cast<int64_t>(6)));
                        if (param.type)
                            item.set("detail", JSONValue(param.type->toString()));
                        items.push(std::move(item));
                    }
                    collectFromBlock(method->getBody());
                }
            }
        }

        // ClassDecl methods
        if (auto *cd = dynamic_cast<const ClassDecl *>(decl)) {
            for (const auto *method : cd->getMethods()) {
                auto range = method->getRange();
                if (range.isValid() && srcLine >= range.start.line &&
                    srcLine <= range.end.line) {
                    auto selfItem = JSONValue::object();
                    selfItem.set("label", JSONValue("self"));
                    selfItem.set("kind", JSONValue(static_cast<int64_t>(6)));
                    selfItem.set("detail", JSONValue(cd->getName()));
                    items.push(std::move(selfItem));
                    for (const auto &param : method->getParams()) {
                        if (param.isSelf)
                            continue;
                        auto item = JSONValue::object();
                        item.set("label", JSONValue(param.name));
                        item.set("kind", JSONValue(static_cast<int64_t>(6)));
                        if (param.type)
                            item.set("detail", JSONValue(param.type->toString()));
                        items.push(std::move(item));
                    }
                    collectFromBlock(method->getBody());
                }
            }
        }
    }
}

// ============================================================
// Completion
// ============================================================

JSONValue LSPServer::handleCompletion(const JSONValue &id,
                                      const JSONValue &params) {
    auto items = JSONValue::array();

    std::string uri = params["textDocument"]["uri"].getString();
    uint32_t line = static_cast<uint32_t>(params["position"]["line"].getInteger());
    uint32_t col = static_cast<uint32_t>(params["position"]["character"].getInteger());

    auto docIt = documents_.find(uri);
    const TranslationUnit *tu = nullptr;
    std::string content;
    if (docIt != documents_.end()) {
        tu = docIt->second.tu.get();
        content = docIt->second.content;
    }

    // Detect if this is a member access (dot-triggered)
    auto ctx = extractCompletionContext(content, line, col);

    if (ctx.isMemberAccess) {
        // Member access: only show fields/methods
        addMemberCompletions(items, tu, ctx);
    } else {
        // Normal completion: keywords + builtins + symbols + locals

        // 1. Keywords (CompletionItemKind::Keyword = 14)
        static const char *keywords[] = {
            "func", "struct", "let", "var", "if", "else", "while", "for",
            "return", "match", "enum", "impl", "protocol", "ref", "mut", "as",
            "import", "pub", "self", "break", "continue", "nil", "where",
            "async", "await", "const", "try", "type", "true", "false", "in",
            "case", "dyn", "extern"
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
            "randInt", "randFloat",
            "sorted", "reversed", "enumerate", "zip", "flatten",
            "any", "all", "count", "forEach",
            "strRepeat", "strPadLeft", "strPadRight", "strJoin",
            "strTrim", "strTrimLeft", "strTrimRight",
            "strReverse", "strChars", "strLines",
            "strContains", "strStartsWith", "strEndsWith",
            "strReplace", "strSplit", "strToUpper", "strToLower"
        };
        for (const char *bi : builtins) {
            auto item = JSONValue::object();
            item.set("label", JSONValue(bi));
            item.set("kind", JSONValue(static_cast<int64_t>(3)));
            items.push(std::move(item));
        }

        // 3. Top-level symbols from the document
        if (tu) {
            for (const auto &node : tu->getDeclarations()) {
                auto *decl = node.get();
                if (auto *fd = dynamic_cast<FuncDecl *>(decl)) {
                    auto item = JSONValue::object();
                    item.set("label", JSONValue(fd->getName()));
                    item.set("kind", JSONValue(static_cast<int64_t>(3)));
                    items.push(std::move(item));
                } else if (auto *sd = dynamic_cast<StructDecl *>(decl)) {
                    auto item = JSONValue::object();
                    item.set("label", JSONValue(sd->getName()));
                    item.set("kind", JSONValue(static_cast<int64_t>(22)));
                    items.push(std::move(item));
                } else if (auto *cd = dynamic_cast<ClassDecl *>(decl)) {
                    auto item = JSONValue::object();
                    item.set("label", JSONValue(cd->getName()));
                    item.set("kind", JSONValue(static_cast<int64_t>(7)));
                    items.push(std::move(item));
                } else if (auto *ed = dynamic_cast<EnumDecl *>(decl)) {
                    auto item = JSONValue::object();
                    item.set("label", JSONValue(ed->getName()));
                    item.set("kind", JSONValue(static_cast<int64_t>(13)));
                    items.push(std::move(item));
                } else if (auto *vd = dynamic_cast<VarDecl *>(decl)) {
                    auto item = JSONValue::object();
                    item.set("label", JSONValue(vd->getName()));
                    item.set("kind", JSONValue(static_cast<int64_t>(6)));
                    items.push(std::move(item));
                } else if (auto *pd = dynamic_cast<ProtocolDecl *>(decl)) {
                    auto item = JSONValue::object();
                    item.set("label", JSONValue(pd->getName()));
                    item.set("kind", JSONValue(static_cast<int64_t>(8)));
                    items.push(std::move(item));
                }
            }
        }

        // 4. Local variables (params + locals in current function)
        addLocalCompletions(items, tu, line, col);
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
    } else if (auto *cd = dynamic_cast<const ClassDecl *>(node)) {
        hoverText = "class " + cd->getName();
        if (cd->hasParentClass()) {
            hoverText += " : " + cd->getParentClass();
        }
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

    // Append doc comment if available
    std::string docComment;
    if (auto *decl = dynamic_cast<const Decl *>(node)) {
        if (decl->hasDocComment())
            docComment = decl->getDocComment();
    }

    auto contents = JSONValue::object();
    contents.set("kind", JSONValue("markdown"));
    std::string markdown = "```liva\n" + hoverText + "\n```";
    if (!docComment.empty())
        markdown += "\n\n---\n\n" + docComment;
    contents.set("value", JSONValue(markdown));

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
        } else if (auto *cd = dynamic_cast<ClassDecl *>(decl)) {
            name = cd->getName();
            kind = 5; // Class
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
// References
// ============================================================

JSONValue LSPServer::handleReferences(const JSONValue &id,
                                      const JSONValue &params) {
    std::string uri = params["textDocument"]["uri"].getString();
    uint32_t line = static_cast<uint32_t>(params["position"]["line"].getInteger()) + 1;
    uint32_t col = static_cast<uint32_t>(params["position"]["character"].getInteger()) + 1;

    auto it = documents_.find(uri);
    if (it == documents_.end() || !it->second.tu) {
        return makeResponse(id, JSONValue::array());
    }

    std::string name = getDeclNameAtPosition(it->second.tu.get(), line, col);
    if (name.empty()) {
        return makeResponse(id, JSONValue::array());
    }

    auto locations = findNameOccurrences(it->second.content, name);
    auto result = JSONValue::array();
    for (const auto &loc : locations) {
        auto start = JSONValue::object();
        start.set("line", JSONValue(static_cast<int64_t>(loc.line)));
        start.set("character", JSONValue(static_cast<int64_t>(loc.col)));
        auto end = JSONValue::object();
        end.set("line", JSONValue(static_cast<int64_t>(loc.line)));
        end.set("character", JSONValue(static_cast<int64_t>(loc.endCol)));
        auto range = JSONValue::object();
        range.set("start", std::move(start));
        range.set("end", std::move(end));
        auto location = JSONValue::object();
        location.set("uri", JSONValue(uri));
        location.set("range", std::move(range));
        result.push(std::move(location));
    }

    return makeResponse(id, std::move(result));
}

// ============================================================
// Rename
// ============================================================

JSONValue LSPServer::handleRename(const JSONValue &id,
                                  const JSONValue &params) {
    std::string uri = params["textDocument"]["uri"].getString();
    uint32_t line = static_cast<uint32_t>(params["position"]["line"].getInteger()) + 1;
    uint32_t col = static_cast<uint32_t>(params["position"]["character"].getInteger()) + 1;
    std::string newName = params["newName"].getString();

    auto it = documents_.find(uri);
    if (it == documents_.end() || !it->second.tu) {
        return makeError(id, -32600, "document not found");
    }

    std::string oldName = getDeclNameAtPosition(it->second.tu.get(), line, col);
    if (oldName.empty()) {
        return makeError(id, -32600, "no symbol at position");
    }

    if (newName.empty()) {
        return makeError(id, -32600, "new name is empty");
    }

    auto locations = findNameOccurrences(it->second.content, oldName);

    auto edits = JSONValue::array();
    for (const auto &loc : locations) {
        auto start = JSONValue::object();
        start.set("line", JSONValue(static_cast<int64_t>(loc.line)));
        start.set("character", JSONValue(static_cast<int64_t>(loc.col)));
        auto end = JSONValue::object();
        end.set("line", JSONValue(static_cast<int64_t>(loc.line)));
        end.set("character", JSONValue(static_cast<int64_t>(loc.endCol)));
        auto range = JSONValue::object();
        range.set("start", std::move(start));
        range.set("end", std::move(end));
        auto edit = JSONValue::object();
        edit.set("range", std::move(range));
        edit.set("newText", JSONValue(newName));
        edits.push(std::move(edit));
    }

    auto changes = JSONValue::object();
    changes.set(uri, std::move(edits));

    auto result = JSONValue::object();
    result.set("changes", std::move(changes));
    return makeResponse(id, std::move(result));
}

// ============================================================
// Signature Help
// ============================================================

JSONValue LSPServer::handleSignatureHelp(const JSONValue &id,
                                         const JSONValue &params) {
    std::string uri = params["textDocument"]["uri"].getString();
    uint32_t line = static_cast<uint32_t>(params["position"]["line"].getInteger()) + 1;
    uint32_t col = static_cast<uint32_t>(params["position"]["character"].getInteger()) + 1;

    auto it = documents_.find(uri);
    if (it == documents_.end() || !it->second.tu) {
        return makeResponse(id, JSONValue());
    }

    // Find what function is being called at the cursor by scanning the line
    // Look backward from cursor for a function name followed by '('
    const std::string &content = it->second.content;
    uint32_t targetLine = line - 1; // 0-indexed
    uint32_t targetCol = col - 1;

    // Find the line in content
    uint32_t currentLine = 0;
    size_t lineStart = 0;
    for (size_t i = 0; i < content.size(); ++i) {
        if (currentLine == targetLine) {
            lineStart = i;
            break;
        }
        if (content[i] == '\n') ++currentLine;
    }

    // Scan backward from cursor to find the function name
    size_t cursorPos = lineStart + targetCol;
    if (cursorPos > content.size()) cursorPos = content.size();

    // Count commas to determine active parameter
    int activeParam = 0;
    int parenDepth = 0;
    std::string funcName;

    for (size_t i = cursorPos; i > 0; --i) {
        char c = content[i - 1];
        if (c == ')') ++parenDepth;
        else if (c == '(') {
            if (parenDepth == 0) {
                // Found the opening paren — extract function name
                size_t nameEnd = i - 1;
                // Skip whitespace
                while (nameEnd > 0 && (content[nameEnd - 1] == ' ' || content[nameEnd - 1] == '\t'))
                    --nameEnd;
                size_t nameStart = nameEnd;
                while (nameStart > 0 && (std::isalnum(static_cast<unsigned char>(content[nameStart - 1])) ||
                                          content[nameStart - 1] == '_'))
                    --nameStart;
                funcName = content.substr(nameStart, nameEnd - nameStart);
                break;
            }
            --parenDepth;
        }
        else if (c == ',' && parenDepth == 0) {
            ++activeParam;
        }
    }

    if (funcName.empty()) {
        auto emptyResult = JSONValue::object();
        emptyResult.set("signatures", JSONValue::array());
        emptyResult.set("activeSignature", JSONValue(static_cast<int64_t>(0)));
        emptyResult.set("activeParameter", JSONValue(static_cast<int64_t>(0)));
        return makeResponse(id, std::move(emptyResult));
    }

    // Find the function declaration from user code
    const Decl *decl = findDeclByName(it->second.tu.get(), funcName);
    const FuncDecl *fd = decl ? dynamic_cast<const FuncDecl *>(decl) : nullptr;

    // Also search in impl blocks
    if (!fd) {
        for (const auto &node : it->second.tu->getDeclarations()) {
            auto *impl = dynamic_cast<ImplDecl *>(node.get());
            if (!impl) continue;
            for (const auto &method : impl->getMethods()) {
                if (method->getName() == funcName) {
                    fd = method.get();
                    break;
                }
            }
            if (fd) break;
        }
    }

    std::string sigLabel;
    auto sigParams = JSONValue::array();

    if (fd) {
        // Build signature from AST declaration
        sigLabel = "func " + fd->getName() + "(";
        bool first = true;
        for (const auto &p : fd->getParams()) {
            if (p.isSelf) continue;
            if (!first) sigLabel += ", ";
            first = false;

            std::string paramStr = p.name;
            if (p.type) {
                paramStr += ": " + p.type->toString();
            }

            auto paramInfo = JSONValue::object();
            paramInfo.set("label", JSONValue(paramStr));
            sigParams.push(std::move(paramInfo));

            sigLabel += paramStr;
        }
        sigLabel += ")";
        if (fd->getReturnType() && !fd->getReturnType()->isVoid()) {
            sigLabel += " -> " + fd->getReturnType()->toString();
        }
    } else {
        // Check built-in functions
        struct BuiltinSig {
            const char *name;
            const char *label;
            std::vector<const char *> params;
        };
        static const BuiltinSig builtins[] = {
            {"println", "func println(value: any)", {"value: any"}},
            {"print", "func print(value: any)", {"value: any"}},
            {"len", "func len(collection: any) -> i64", {"collection: any"}},
            {"toString", "func toString(value: any) -> string", {"value: any"}},
            {"parseInt", "func parseInt(s: string) -> i32?", {"s: string"}},
            {"abs", "func abs(x: numeric) -> numeric", {"x: numeric"}},
            {"min", "func min(a: numeric, b: numeric) -> numeric", {"a: numeric", "b: numeric"}},
            {"max", "func max(a: numeric, b: numeric) -> numeric", {"a: numeric", "b: numeric"}},
            {"sqrt", "func sqrt(x: f64) -> f64", {"x: f64"}},
            {"pow", "func pow(base: f64, exp: f64) -> f64", {"base: f64", "exp: f64"}},
            {"format", "func format(fmt: string, args: any...) -> string", {"fmt: string", "args: any..."}},
            {"readLine", "func readLine() -> string", {}},
            {"parseFloat", "func parseFloat(s: string) -> f64?", {"s: string"}},
            {"randInt", "func randInt(min: i64, max: i64) -> i64", {"min: i64", "max: i64"}},
            {"randFloat", "func randFloat() -> f64", {}},
        };

        bool found = false;
        for (const auto &bi : builtins) {
            if (funcName == bi.name) {
                sigLabel = bi.label;
                for (const char *p : bi.params) {
                    auto paramInfo = JSONValue::object();
                    paramInfo.set("label", JSONValue(p));
                    sigParams.push(std::move(paramInfo));
                }
                found = true;
                break;
            }
        }

        if (!found) {
            // Also scan document text for func declarations that the parser
            // may not have captured (e.g., in incomplete/errored code)
            // Look for pattern: func <funcName>(...) [-> RetType]
            std::string pattern = "func " + funcName + "(";
            size_t pos = content.find(pattern);
            if (pos != std::string::npos) {
                // Extract from "func name(" to the matching ")"
                size_t parenStart = pos + pattern.size();
                int depth = 1;
                size_t parenEnd = parenStart;
                while (parenEnd < content.size() && depth > 0) {
                    if (content[parenEnd] == '(') ++depth;
                    else if (content[parenEnd] == ')') --depth;
                    if (depth > 0) ++parenEnd;
                }
                // Extract params string
                std::string paramsStr = content.substr(parenStart, parenEnd - parenStart);

                // Check for return type
                std::string retType;
                size_t afterParen = parenEnd + 1;
                // Skip whitespace
                while (afterParen < content.size() && content[afterParen] == ' ')
                    ++afterParen;
                if (afterParen + 1 < content.size() &&
                    content[afterParen] == '-' && content[afterParen + 1] == '>') {
                    afterParen += 2;
                    while (afterParen < content.size() && content[afterParen] == ' ')
                        ++afterParen;
                    size_t retStart = afterParen;
                    while (afterParen < content.size() && content[afterParen] != '\n' &&
                           content[afterParen] != '{' && content[afterParen] != ' ')
                        ++afterParen;
                    retType = content.substr(retStart, afterParen - retStart);
                }

                sigLabel = "func " + funcName + "(" + paramsStr + ")";
                if (!retType.empty()) {
                    sigLabel += " -> " + retType;
                }

                // Parse individual parameters
                if (!paramsStr.empty()) {
                    size_t pStart = 0;
                    int pDepth = 0;
                    for (size_t i = 0; i <= paramsStr.size(); ++i) {
                        if (i == paramsStr.size() ||
                            (paramsStr[i] == ',' && pDepth == 0)) {
                            std::string param = paramsStr.substr(pStart, i - pStart);
                            // Trim whitespace
                            size_t tStart = 0;
                            while (tStart < param.size() && param[tStart] == ' ') ++tStart;
                            size_t tEnd = param.size();
                            while (tEnd > tStart && param[tEnd - 1] == ' ') --tEnd;
                            param = param.substr(tStart, tEnd - tStart);
                            if (!param.empty()) {
                                auto paramInfo = JSONValue::object();
                                paramInfo.set("label", JSONValue(param));
                                sigParams.push(std::move(paramInfo));
                            }
                            pStart = i + 1;
                        }
                        if (i < paramsStr.size()) {
                            if (paramsStr[i] == '(') ++pDepth;
                            else if (paramsStr[i] == ')') --pDepth;
                        }
                    }
                }
                found = true;
            }

            if (!found) {
                auto emptyResult = JSONValue::object();
                emptyResult.set("signatures", JSONValue::array());
                emptyResult.set("activeSignature", JSONValue(static_cast<int64_t>(0)));
                emptyResult.set("activeParameter", JSONValue(static_cast<int64_t>(0)));
                return makeResponse(id, std::move(emptyResult));
            }
        }
    }

    auto sigInfo = JSONValue::object();
    sigInfo.set("label", JSONValue(sigLabel));
    sigInfo.set("parameters", std::move(sigParams));

    auto signatures = JSONValue::array();
    signatures.push(std::move(sigInfo));

    auto result = JSONValue::object();
    result.set("signatures", std::move(signatures));
    result.set("activeSignature", JSONValue(static_cast<int64_t>(0)));
    result.set("activeParameter", JSONValue(static_cast<int64_t>(activeParam)));

    return makeResponse(id, std::move(result));
}

// ============================================================
// Semantic Tokens
// ============================================================

JSONValue LSPServer::handleSemanticTokens(const JSONValue &id,
                                           const JSONValue &params) {
    std::string uri = params["textDocument"]["uri"].getString();
    auto it = documents_.find(uri);
    if (it == documents_.end()) {
        auto result = JSONValue::object();
        result.set("data", JSONValue::array());
        return makeResponse(id, std::move(result));
    }

    const std::string &content = it->second.content;
    std::string path = uriToPath(uri);
    if (path.empty()) path = uri;

    // Create a temporary SourceManager and DiagnosticsEngine for lexing
    SourceManager sm(path, content);
    DiagnosticsEngine diag(&sm);
    Lexer lexer(sm, diag);

    auto tokens = lexer.lexAll();

    auto data = JSONValue::array();
    uint32_t prevLine = 0;
    uint32_t prevCol = 0;

    for (size_t i = 0; i < tokens.size(); ++i) {
        const Token &tok = tokens[i];
        TokenKind kind = tok.getKind();

        // Skip EOF and newline tokens
        if (kind == TokenKind::eof || kind == TokenKind::newline)
            continue;

        // Classify the token into a semantic type
        int tokenType = -1;
        int tokenModifiers = 0;

        switch (kind) {
        // Language keywords
        case TokenKind::kw_func:
        case TokenKind::kw_let:
        case TokenKind::kw_var:
        case TokenKind::kw_if:
        case TokenKind::kw_else:
        case TokenKind::kw_while:
        case TokenKind::kw_for:
        case TokenKind::kw_in:
        case TokenKind::kw_match:
        case TokenKind::kw_case:
        case TokenKind::kw_enum:
        case TokenKind::kw_struct:
        case TokenKind::kw_impl:
        case TokenKind::kw_protocol:
        case TokenKind::kw_import:
        case TokenKind::kw_pub:
        case TokenKind::kw_async:
        case TokenKind::kw_await:
        case TokenKind::kw_return:
        case TokenKind::kw_break:
        case TokenKind::kw_continue:
        case TokenKind::kw_const:
        case TokenKind::kw_try:
        case TokenKind::kw_type:
        case TokenKind::kw_where:
        case TokenKind::kw_as:
        case TokenKind::kw_ref:
        case TokenKind::kw_mut:
        case TokenKind::kw_dyn:
        case TokenKind::kw_extern:
            tokenType = 0; // keyword
            break;

        // Type keywords
        case TokenKind::kw_i8:
        case TokenKind::kw_i16:
        case TokenKind::kw_i32:
        case TokenKind::kw_i64:
        case TokenKind::kw_u8:
        case TokenKind::kw_u16:
        case TokenKind::kw_u32:
        case TokenKind::kw_u64:
        case TokenKind::kw_f32:
        case TokenKind::kw_f64:
        case TokenKind::kw_bool:
        case TokenKind::kw_string:
        case TokenKind::kw_void:
            tokenType = 1; // type
            break;

        // Boolean and nil literals
        case TokenKind::kw_true:
        case TokenKind::kw_false:
        case TokenKind::bool_literal:
        case TokenKind::kw_nil:
            tokenType = 8; // enumMember
            break;

        // self keyword
        case TokenKind::kw_self:
            tokenType = 10; // parameter
            break;

        // Identifiers — check if followed by '(' to classify as function
        case TokenKind::identifier: {
            bool isFunc = false;
            // Look ahead to see if the next non-newline token is '('
            for (size_t j = i + 1; j < tokens.size(); ++j) {
                if (tokens[j].getKind() == TokenKind::newline)
                    continue;
                if (tokens[j].getKind() == TokenKind::l_paren)
                    isFunc = true;
                break;
            }
            tokenType = isFunc ? 2 : 3; // function or variable
            break;
        }

        // String literals
        case TokenKind::string_literal:
        case TokenKind::string_interp_begin:
        case TokenKind::string_interp_mid:
        case TokenKind::string_interp_end:
        case TokenKind::char_literal:
            tokenType = 4; // string
            break;

        // Numeric literals
        case TokenKind::integer_literal:
        case TokenKind::float_literal:
            tokenType = 5; // number
            break;

        // Operators and punctuators
        case TokenKind::plus:
        case TokenKind::minus:
        case TokenKind::star:
        case TokenKind::slash:
        case TokenKind::percent:
        case TokenKind::equal_equal:
        case TokenKind::bang_equal:
        case TokenKind::less:
        case TokenKind::less_equal:
        case TokenKind::greater:
        case TokenKind::greater_equal:
        case TokenKind::amp_amp:
        case TokenKind::pipe_pipe:
        case TokenKind::bang:
        case TokenKind::amp:
        case TokenKind::pipe:
        case TokenKind::caret:
        case TokenKind::tilde:
        case TokenKind::less_less:
        case TokenKind::greater_greater:
        case TokenKind::equal:
        case TokenKind::plus_equal:
        case TokenKind::minus_equal:
        case TokenKind::star_equal:
        case TokenKind::slash_equal:
        case TokenKind::percent_equal:
        case TokenKind::arrow:
        case TokenKind::fat_arrow:
            tokenType = 7; // operator
            break;

        default:
            // Skip punctuation like parens, braces, commas, etc.
            break;
        }

        if (tokenType < 0)
            continue;

        // Get line/column (1-indexed from SourceLocation, convert to 0-indexed)
        SourceLocation loc = tok.getLocation();
        uint32_t line = loc.line > 0 ? loc.line - 1 : 0;
        uint32_t col = loc.column > 0 ? loc.column - 1 : 0;
        uint32_t length = static_cast<uint32_t>(tok.getText().size());

        // Delta encoding
        uint32_t deltaLine = line - prevLine;
        uint32_t deltaCol = (deltaLine == 0) ? (col - prevCol) : col;

        data.push(JSONValue(static_cast<int64_t>(deltaLine)));
        data.push(JSONValue(static_cast<int64_t>(deltaCol)));
        data.push(JSONValue(static_cast<int64_t>(length)));
        data.push(JSONValue(static_cast<int64_t>(tokenType)));
        data.push(JSONValue(static_cast<int64_t>(tokenModifiers)));

        prevLine = line;
        prevCol = col;
    }

    auto result = JSONValue::object();
    result.set("data", std::move(data));
    return makeResponse(id, std::move(result));
}

// ============================================================
// Formatting
// ============================================================

JSONValue LSPServer::handleFormatting(const JSONValue &id,
                                      const JSONValue &params) {
    std::string uri = params["textDocument"]["uri"].getString();
    auto it = documents_.find(uri);
    if (it == documents_.end()) {
        return makeResponse(id, JSONValue::array());
    }

    const std::string &content = it->second.content;

    // Split content into lines
    std::vector<std::string> lines;
    std::string currentLine;
    for (size_t i = 0; i < content.size(); ++i) {
        if (content[i] == '\n') {
            lines.push_back(currentLine);
            currentLine.clear();
        } else if (content[i] == '\r') {
            // skip \r (handle \r\n)
            continue;
        } else {
            currentLine += content[i];
        }
    }
    // Add the last line if non-empty or if content ends without newline
    if (!currentLine.empty() || (!content.empty() && content.back() == '\n')) {
        lines.push_back(currentLine);
    }

    // Format each line with brace-depth indentation
    int depth = 0;
    std::string formatted;
    for (size_t i = 0; i < lines.size(); ++i) {
        // Trim leading and trailing whitespace
        size_t start = 0;
        while (start < lines[i].size() &&
               (lines[i][start] == ' ' || lines[i][start] == '\t'))
            ++start;
        size_t end = lines[i].size();
        while (end > start &&
               (lines[i][end - 1] == ' ' || lines[i][end - 1] == '\t'))
            --end;
        std::string trimmed = lines[i].substr(start, end - start);

        // Count braces in this line
        int opens = 0;
        int closes = 0;
        for (char c : trimmed) {
            if (c == '{') ++opens;
            else if (c == '}') ++closes;
        }

        // If line starts with '}', this line should be at reduced depth
        bool leadingClose = (!trimmed.empty() && trimmed[0] == '}');
        int lineDepth = depth;
        if (leadingClose) {
            lineDepth = depth - 1;
            if (lineDepth < 0) lineDepth = 0;
        }

        // Build indented line
        std::string indent(static_cast<size_t>(lineDepth) * 4, ' ');
        if (!trimmed.empty()) {
            formatted += indent + trimmed;
        }
        if (i + 1 < lines.size()) {
            formatted += "\n";
        }

        // Update depth: add opens, subtract closes
        depth += opens - closes;
        if (depth < 0) depth = 0;
    }

    // Build the TextEdit that replaces the entire document
    int lastLine = lines.empty() ? 0 : static_cast<int>(lines.size()) - 1;
    int lastCol = lines.empty() ? 0 : static_cast<int>(lines.back().size());

    auto rangeStart = JSONValue::object();
    rangeStart.set("line", JSONValue(static_cast<int64_t>(0)));
    rangeStart.set("character", JSONValue(static_cast<int64_t>(0)));
    auto rangeEnd = JSONValue::object();
    rangeEnd.set("line", JSONValue(static_cast<int64_t>(lastLine)));
    rangeEnd.set("character", JSONValue(static_cast<int64_t>(lastCol)));
    auto range = JSONValue::object();
    range.set("start", std::move(rangeStart));
    range.set("end", std::move(rangeEnd));

    auto edit = JSONValue::object();
    edit.set("range", std::move(range));
    edit.set("newText", JSONValue(formatted));

    auto result = JSONValue::array();
    result.push(std::move(edit));
    return makeResponse(id, std::move(result));
}

// ============================================================
// Code Action
// ============================================================

JSONValue LSPServer::handleCodeAction(const JSONValue &id,
                                      const JSONValue &params) {
    std::string uri = params["textDocument"]["uri"].getString();
    auto it = documents_.find(uri);
    if (it == documents_.end())
        return makeResponse(id, JSONValue::array());

    auto actions = JSONValue::array();
    const auto &doc = it->second;

    int rangeStartLine = static_cast<int>(params["range"]["start"]["line"].getInteger());
    int rangeEndLine = static_cast<int>(params["range"]["end"]["line"].getInteger());

    for (const auto &d : doc.diag.getDiagnostics()) {
        int diagLine = (d.location.line > 0) ? static_cast<int>(d.location.line) - 1 : 0;
        if (diagLine < rangeStartLine || diagLine > rangeEndLine)
            continue;

        if (d.id == DiagID::warn_unused_variable) {
            // Extract variable name from message "unused variable 'NAME'"
            std::string varName;
            auto q1 = d.message.find('\'');
            auto q2 = d.message.rfind('\'');
            if (q1 != std::string::npos && q2 > q1)
                varName = d.message.substr(q1 + 1, q2 - q1 - 1);

            if (!varName.empty()) {
                // Action 1: Prefix with _
                {
                    auto action = JSONValue::object();
                    action.set("title", JSONValue("Prefix '" + varName + "' with _"));
                    action.set("kind", JSONValue("quickfix"));

                    // Build workspace edit: rename varName → _varName on that line
                    auto edit = JSONValue::object();
                    auto changes = JSONValue::object();
                    auto edits = JSONValue::array();
                    auto textEdit = JSONValue::object();
                    // Find variable name on the diagnostic line
                    int col = (d.location.column > 0) ? static_cast<int>(d.location.column) - 1 : 0;
                    auto startPos = JSONValue::object();
                    startPos.set("line", JSONValue(static_cast<int64_t>(diagLine)));
                    startPos.set("character", JSONValue(static_cast<int64_t>(col)));
                    auto endPos = JSONValue::object();
                    endPos.set("line", JSONValue(static_cast<int64_t>(diagLine)));
                    endPos.set("character", JSONValue(static_cast<int64_t>(col + static_cast<int>(varName.size()))));
                    auto range = JSONValue::object();
                    range.set("start", std::move(startPos));
                    range.set("end", std::move(endPos));
                    textEdit.set("range", std::move(range));
                    textEdit.set("newText", JSONValue("_" + varName));
                    edits.push(std::move(textEdit));
                    changes.set(uri, std::move(edits));
                    edit.set("changes", std::move(changes));
                    action.set("edit", std::move(edit));
                    actions.push(std::move(action));
                }
                // Action 2: Remove unused variable line
                {
                    auto action = JSONValue::object();
                    action.set("title", JSONValue("Remove unused variable '" + varName + "'"));
                    action.set("kind", JSONValue("quickfix"));

                    auto edit = JSONValue::object();
                    auto changes = JSONValue::object();
                    auto edits = JSONValue::array();
                    auto textEdit = JSONValue::object();
                    // Delete the entire line
                    auto startPos = JSONValue::object();
                    startPos.set("line", JSONValue(static_cast<int64_t>(diagLine)));
                    startPos.set("character", JSONValue(static_cast<int64_t>(0)));
                    auto endPos = JSONValue::object();
                    endPos.set("line", JSONValue(static_cast<int64_t>(diagLine + 1)));
                    endPos.set("character", JSONValue(static_cast<int64_t>(0)));
                    auto range = JSONValue::object();
                    range.set("start", std::move(startPos));
                    range.set("end", std::move(endPos));
                    textEdit.set("range", std::move(range));
                    textEdit.set("newText", JSONValue(""));
                    edits.push(std::move(textEdit));
                    changes.set(uri, std::move(edits));
                    edit.set("changes", std::move(changes));
                    action.set("edit", std::move(edit));
                    actions.push(std::move(action));
                }
            }
        }
    }
    return makeResponse(id, std::move(actions));
}

// ============================================================
// Folding Range
// ============================================================

JSONValue LSPServer::handleFoldingRange(const JSONValue &id,
                                        const JSONValue &params) {
    std::string uri = params["textDocument"]["uri"].getString();
    auto it = documents_.find(uri);
    if (it == documents_.end()) {
        return makeResponse(id, JSONValue::array());
    }

    const std::string &content = it->second.content;
    auto result = JSONValue::array();

    // Track brace-delimited regions for folding
    // Use a stack of opening brace positions
    struct BracePos { uint32_t line; uint32_t col; };
    std::vector<BracePos> stack;

    uint32_t lineNum = 0;
    uint32_t colNum = 0;
    for (size_t i = 0; i < content.size(); ++i) {
        char c = content[i];
        if (c == '\n') {
            ++lineNum;
            colNum = 0;
            continue;
        }
        if (c == '{') {
            stack.push_back({lineNum, colNum});
        } else if (c == '}') {
            if (!stack.empty()) {
                BracePos open = stack.back();
                stack.pop_back();
                // Only create a folding range if it spans multiple lines
                if (lineNum > open.line) {
                    auto range = JSONValue::object();
                    range.set("startLine", JSONValue(static_cast<int64_t>(open.line)));
                    range.set("startCharacter", JSONValue(static_cast<int64_t>(open.col)));
                    range.set("endLine", JSONValue(static_cast<int64_t>(lineNum)));
                    range.set("endCharacter", JSONValue(static_cast<int64_t>(colNum)));
                    range.set("kind", JSONValue("region"));
                    result.push(std::move(range));
                }
            }
        }
        ++colNum;
    }

    // Also detect consecutive single-line comment blocks for folding
    // Split into lines
    std::vector<std::string> lines;
    std::string currentLine;
    for (size_t i = 0; i < content.size(); ++i) {
        if (content[i] == '\n') {
            lines.push_back(currentLine);
            currentLine.clear();
        } else if (content[i] != '\r') {
            currentLine += content[i];
        }
    }
    if (!currentLine.empty()) {
        lines.push_back(currentLine);
    }

    // Find consecutive comment lines (starting with //)
    size_t commentStart = 0;
    bool inComment = false;
    for (size_t i = 0; i < lines.size(); ++i) {
        // Trim leading whitespace
        size_t firstNonWs = 0;
        while (firstNonWs < lines[i].size() &&
               (lines[i][firstNonWs] == ' ' || lines[i][firstNonWs] == '\t'))
            ++firstNonWs;
        bool isComment = (firstNonWs + 1 < lines[i].size() &&
                          lines[i][firstNonWs] == '/' &&
                          lines[i][firstNonWs + 1] == '/');
        if (isComment) {
            if (!inComment) {
                commentStart = i;
                inComment = true;
            }
        } else {
            if (inComment && i - commentStart >= 2) {
                auto range = JSONValue::object();
                range.set("startLine", JSONValue(static_cast<int64_t>(commentStart)));
                range.set("endLine", JSONValue(static_cast<int64_t>(i - 1)));
                range.set("kind", JSONValue("comment"));
                result.push(std::move(range));
            }
            inComment = false;
        }
    }
    // Handle comment block at end of file
    if (inComment && lines.size() - commentStart >= 2) {
        auto range = JSONValue::object();
        range.set("startLine", JSONValue(static_cast<int64_t>(commentStart)));
        range.set("endLine", JSONValue(static_cast<int64_t>(lines.size() - 1)));
        range.set("kind", JSONValue("comment"));
        result.push(std::move(range));
    }

    return makeResponse(id, std::move(result));
}

// ============================================================
// Selection Range
// ============================================================

JSONValue LSPServer::handleSelectionRange(const JSONValue &id,
                                          const JSONValue &params) {
    std::string uri = params["textDocument"]["uri"].getString();
    auto it = documents_.find(uri);
    if (it == documents_.end()) {
        return makeResponse(id, JSONValue::array());
    }

    const std::string &content = it->second.content;
    const auto &positions = params["positions"].getArray();

    // Split content into lines for line length info
    std::vector<std::string> lines;
    std::string currentLine;
    for (size_t i = 0; i < content.size(); ++i) {
        if (content[i] == '\n') {
            lines.push_back(currentLine);
            currentLine.clear();
        } else if (content[i] != '\r') {
            currentLine += content[i];
        }
    }
    if (!currentLine.empty() || (!content.empty() && content.back() == '\n')) {
        lines.push_back(currentLine);
    }

    // Pre-compute brace nesting structure: for each '{' record matching '}'
    struct BracePair { uint32_t openLine; uint32_t openCol;
                       uint32_t closeLine; uint32_t closeCol; };
    std::vector<BracePair> bracePairs;

    struct OpenBrace { uint32_t line; uint32_t col; };
    std::vector<OpenBrace> braceStack;
    uint32_t lineNum = 0;
    uint32_t colNum = 0;
    for (size_t i = 0; i < content.size(); ++i) {
        char c = content[i];
        if (c == '\n') { ++lineNum; colNum = 0; continue; }
        if (c == '{') {
            braceStack.push_back({lineNum, colNum});
        } else if (c == '}') {
            if (!braceStack.empty()) {
                OpenBrace ob = braceStack.back();
                braceStack.pop_back();
                bracePairs.push_back({ob.line, ob.col, lineNum, colNum});
            }
        }
        ++colNum;
    }

    auto result = JSONValue::array();

    for (const auto &pos : positions) {
        uint32_t posLine = static_cast<uint32_t>(pos["line"].getInteger());
        uint32_t posChar = static_cast<uint32_t>(pos["character"].getInteger());

        // Build selection ranges from innermost to outermost:
        // 1. Word at cursor
        // 2. Current line
        // 3. Enclosing brace block(s) (innermost first)
        // 4. Entire document

        // Helper to make a range object
        auto makeRange = [](uint32_t sl, uint32_t sc, uint32_t el, uint32_t ec) {
            auto start = JSONValue::object();
            start.set("line", JSONValue(static_cast<int64_t>(sl)));
            start.set("character", JSONValue(static_cast<int64_t>(sc)));
            auto end = JSONValue::object();
            end.set("line", JSONValue(static_cast<int64_t>(el)));
            end.set("character", JSONValue(static_cast<int64_t>(ec)));
            auto range = JSONValue::object();
            range.set("start", std::move(start));
            range.set("end", std::move(end));
            return range;
        };

        // Collect brace blocks that contain the position (sorted innermost first)
        struct BlockSpan {
            uint32_t openLine, openCol, closeLine, closeCol;
            uint32_t span; // number of lines
        };
        std::vector<BlockSpan> enclosingBlocks;
        for (const auto &bp : bracePairs) {
            bool after_open = (posLine > bp.openLine) ||
                              (posLine == bp.openLine && posChar >= bp.openCol);
            bool before_close = (posLine < bp.closeLine) ||
                                (posLine == bp.closeLine && posChar <= bp.closeCol);
            if (after_open && before_close) {
                uint32_t span = bp.closeLine - bp.openLine;
                enclosingBlocks.push_back({bp.openLine, bp.openCol,
                                           bp.closeLine, bp.closeCol, span});
            }
        }
        // Sort by span ascending (innermost first)
        std::sort(enclosingBlocks.begin(), enclosingBlocks.end(),
                  [](const BlockSpan &a, const BlockSpan &b) {
                      return a.span < b.span;
                  });

        // Now build the chain: word -> line -> blocks -> document
        // Start from outermost and nest inward

        // Outermost: entire document
        uint32_t lastLine = lines.empty() ? 0 : static_cast<uint32_t>(lines.size() - 1);
        uint32_t lastCol = lines.empty() ? 0 : static_cast<uint32_t>(lines.back().size());

        auto docRange = makeRange(0, 0, lastLine, lastCol);
        auto outermost = JSONValue::object();
        outermost.set("range", std::move(docRange));
        // no parent for outermost

        // Build chain from outermost to innermost
        JSONValue currentSel = std::move(outermost);

        // Add enclosing brace blocks from outermost to innermost
        for (int i = static_cast<int>(enclosingBlocks.size()) - 1; i >= 0; --i) {
            const auto &blk = enclosingBlocks[static_cast<size_t>(i)];
            auto blockRange = makeRange(blk.openLine, blk.openCol,
                                        blk.closeLine, blk.closeCol + 1);
            auto sel = JSONValue::object();
            sel.set("range", std::move(blockRange));
            sel.set("parent", std::move(currentSel));
            currentSel = std::move(sel);
        }

        // Add current line
        if (posLine < lines.size()) {
            uint32_t lineLen = static_cast<uint32_t>(lines[posLine].size());
            auto lineRange = makeRange(posLine, 0, posLine, lineLen);
            auto sel = JSONValue::object();
            sel.set("range", std::move(lineRange));
            sel.set("parent", std::move(currentSel));
            currentSel = std::move(sel);
        }

        // Add word at cursor
        if (posLine < lines.size()) {
            const std::string &line = lines[posLine];
            uint32_t wordStart = posChar;
            uint32_t wordEnd = posChar;
            // Expand word boundaries
            while (wordStart > 0 &&
                   (std::isalnum(static_cast<unsigned char>(line[wordStart - 1])) ||
                    line[wordStart - 1] == '_'))
                --wordStart;
            while (wordEnd < line.size() &&
                   (std::isalnum(static_cast<unsigned char>(line[wordEnd])) ||
                    line[wordEnd] == '_'))
                ++wordEnd;
            if (wordEnd > wordStart) {
                auto wordRange = makeRange(posLine, wordStart, posLine, wordEnd);
                auto sel = JSONValue::object();
                sel.set("range", std::move(wordRange));
                sel.set("parent", std::move(currentSel));
                currentSel = std::move(sel);
            }
        }

        result.push(std::move(currentSel));
    }

    return makeResponse(id, std::move(result));
}

// ============================================================
// Document Highlight
// ============================================================

JSONValue LSPServer::handleDocumentHighlight(const JSONValue &id,
                                             const JSONValue &params) {
    std::string uri = params["textDocument"]["uri"].getString();
    uint32_t line = static_cast<uint32_t>(params["position"]["line"].getInteger()) + 1;
    uint32_t col = static_cast<uint32_t>(params["position"]["character"].getInteger()) + 1;

    auto it = documents_.find(uri);
    if (it == documents_.end() || !it->second.tu) {
        return makeResponse(id, JSONValue::array());
    }

    // Find the name at cursor (use AST first, then fall back to word extraction)
    std::string name = getDeclNameAtPosition(it->second.tu.get(), line, col);

    // If AST lookup didn't find anything, extract word from source
    if (name.empty()) {
        const std::string &content = it->second.content;
        // Convert to 0-indexed
        uint32_t targetLine = line - 1;
        uint32_t targetCol = col - 1;
        // Find the line
        uint32_t currentLine = 0;
        size_t lineStart = 0;
        bool lineFound = false;
        for (size_t i = 0; i < content.size(); ++i) {
            if (currentLine == targetLine) {
                lineStart = i;
                lineFound = true;
                break;
            }
            if (content[i] == '\n') ++currentLine;
        }
        // If the target line doesn't exist in the document, return empty
        if (!lineFound) {
            return makeResponse(id, JSONValue::array());
        }
        size_t pos = lineStart + targetCol;
        if (pos < content.size()) {
            // Extract word at position
            size_t wordStart = pos;
            size_t wordEnd = pos;
            while (wordStart > 0 &&
                   (std::isalnum(static_cast<unsigned char>(content[wordStart - 1])) ||
                    content[wordStart - 1] == '_'))
                --wordStart;
            while (wordEnd < content.size() &&
                   (std::isalnum(static_cast<unsigned char>(content[wordEnd])) ||
                    content[wordEnd] == '_'))
                ++wordEnd;
            if (wordEnd > wordStart) {
                name = content.substr(wordStart, wordEnd - wordStart);
            }
        }
    }

    if (name.empty()) {
        return makeResponse(id, JSONValue::array());
    }

    auto locations = findNameOccurrences(it->second.content, name);
    auto result = JSONValue::array();

    // Determine if the symbol is a declaration for the first occurrence
    // DocumentHighlightKind: 1=Text, 2=Read, 3=Write
    for (const auto &loc : locations) {
        auto start = JSONValue::object();
        start.set("line", JSONValue(static_cast<int64_t>(loc.line)));
        start.set("character", JSONValue(static_cast<int64_t>(loc.col)));
        auto end = JSONValue::object();
        end.set("line", JSONValue(static_cast<int64_t>(loc.line)));
        end.set("character", JSONValue(static_cast<int64_t>(loc.endCol)));
        auto range = JSONValue::object();
        range.set("start", std::move(start));
        range.set("end", std::move(end));

        auto highlight = JSONValue::object();
        highlight.set("range", std::move(range));
        // Kind: check if this occurrence is the declaration site
        // Simple heuristic: check if preceded by func/let/var/struct/enum keywords
        // For now, mark first occurrence as Write (3), rest as Read (2)
        // A better approach: check the line content
        int64_t kind = 1; // Text (default)
        const std::string &content = it->second.content;
        // Find the start of the name in the content
        uint32_t nameLine = loc.line;
        uint32_t nameCol = loc.col;
        // Check preceding text on the line for declaration keywords
        uint32_t currentLineNum = 0;
        size_t lineStartPos = 0;
        for (size_t i = 0; i < content.size(); ++i) {
            if (currentLineNum == nameLine) { lineStartPos = i; break; }
            if (content[i] == '\n') ++currentLineNum;
        }
        std::string linePrefix = content.substr(lineStartPos, nameCol);
        // Trim whitespace
        size_t trimEnd = linePrefix.size();
        while (trimEnd > 0 && (linePrefix[trimEnd - 1] == ' ' || linePrefix[trimEnd - 1] == '\t'))
            --trimEnd;
        linePrefix = linePrefix.substr(0, trimEnd);
        // Check if line prefix ends with a declaration keyword
        bool isDecl = (linePrefix == "func" || linePrefix == "let" || linePrefix == "var" ||
            linePrefix == "struct" || linePrefix == "enum" || linePrefix == "protocol" ||
            linePrefix == "type");
        if (!isDecl) {
            // Check if prefix ends with a declaration keyword preceded by space
            if ((linePrefix.size() >= 5 && linePrefix.substr(linePrefix.size() - 5) == " func") ||
                (linePrefix.size() >= 4 && linePrefix.substr(linePrefix.size() - 4) == " let") ||
                (linePrefix.size() >= 4 && linePrefix.substr(linePrefix.size() - 4) == " var") ||
                (linePrefix.size() >= 7 && linePrefix.substr(linePrefix.size() - 7) == " struct") ||
                (linePrefix.size() >= 5 && linePrefix.substr(linePrefix.size() - 5) == " enum") ||
                (linePrefix.size() >= 9 && linePrefix.substr(linePrefix.size() - 9) == " protocol") ||
                (linePrefix.size() >= 5 && linePrefix.substr(linePrefix.size() - 5) == " type"))
                isDecl = true;
        }
        if (isDecl) {
            kind = 3; // Write (declaration)
        } else {
            kind = 2; // Read (usage)
        }
        highlight.set("kind", JSONValue(kind));
        result.push(std::move(highlight));
    }

    return makeResponse(id, std::move(result));
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
        } else if (auto *cd = dynamic_cast<ClassDecl *>(node.get())) {
            if (cd->getName() == name) return cd;
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
// Name occurrence / rename helpers
// ============================================================

std::string LSPServer::getDeclNameAtPosition(const TranslationUnit *tu,
                                             uint32_t line, uint32_t col) const {
    const ASTNode *node = findNodeAtPosition(tu, line, col);
    if (!node) return "";

    if (auto *fd = dynamic_cast<const FuncDecl *>(node)) return fd->getName();
    if (auto *vd = dynamic_cast<const VarDecl *>(node)) return vd->getName();
    if (auto *sd = dynamic_cast<const StructDecl *>(node)) return sd->getName();
    if (auto *cd = dynamic_cast<const ClassDecl *>(node)) return cd->getName();
    if (auto *ed = dynamic_cast<const EnumDecl *>(node)) return ed->getName();
    if (auto *pd = dynamic_cast<const ProtocolDecl *>(node)) return pd->getName();
    if (auto *td = dynamic_cast<const TypeAliasDecl *>(node)) return td->getName();
    return "";
}

std::vector<LSPServer::TextLocation>
LSPServer::findNameOccurrences(const std::string &content,
                               const std::string &name) const {
    std::vector<TextLocation> results;
    if (name.empty()) return results;

    uint32_t lineNum = 0;
    uint32_t colNum = 0;

    for (size_t i = 0; i < content.size(); ++i) {
        if (content[i] == '\n') {
            ++lineNum;
            colNum = 0;
            continue;
        }

        // Check if this position starts the name
        if (content.compare(i, name.size(), name) == 0) {
            // Verify word boundary: char before should not be alphanumeric/underscore
            bool prevOk = (i == 0) ||
                          !(std::isalnum(static_cast<unsigned char>(content[i - 1])) ||
                            content[i - 1] == '_');
            size_t afterPos = i + name.size();
            bool nextOk = (afterPos >= content.size()) ||
                          !(std::isalnum(static_cast<unsigned char>(content[afterPos])) ||
                            content[afterPos] == '_');
            if (prevOk && nextOk) {
                results.push_back({lineNum, colNum,
                                   colNum + static_cast<uint32_t>(name.size())});
            }
        }

        ++colNum;
    }

    return results;
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

// ============================================================
// Inlay Hints
// ============================================================

void LSPServer::collectVarDeclHints(const ASTNode *node, uint32_t startLine,
                                     uint32_t endLine, JSONValue &hints) {
    if (!node) return;

    if (auto *vd = dynamic_cast<const VarDecl *>(node)) {
        if (!vd->hasTypeAnnotation() && !vd->isDestructured() &&
            vd->hasInit() && vd->getInit()->getResolvedType()) {
            uint32_t line = vd->getRange().start.line; // 1-indexed
            if (line >= startLine && line <= endLine) {
                // Compute hint position: after the variable name
                // "let name" → keyword_len + 1 (space) + name.length()
                // "var name" → 4, "let name" → 4, "const name" → 6
                uint32_t keywordLen = vd->isConst() ? 6 : 4; // const=6, let/var=4
                uint32_t hintCol = keywordLen + static_cast<uint32_t>(vd->getName().length());

                auto hint = JSONValue::object();

                // Position (0-indexed)
                auto pos = JSONValue::object();
                pos.set("line", JSONValue(static_cast<int64_t>(line - 1)));
                pos.set("character", JSONValue(static_cast<int64_t>(hintCol)));
                hint.set("position", std::move(pos));

                // Label
                std::string label = ": " + vd->getInit()->getResolvedType()->toString();
                hint.set("label", JSONValue(label));

                // Kind = 1 (Type)
                hint.set("kind", JSONValue(static_cast<int64_t>(1)));

                hints.push(std::move(hint));
            }
        }
        return;
    }

    // Recurse into function bodies
    if (auto *fd = dynamic_cast<const FuncDecl *>(node)) {
        if (fd->getBody()) {
            for (const auto &stmt : fd->getBody()->getStatements()) {
                collectVarDeclHints(stmt.get(), startLine, endLine, hints);
            }
        }
        return;
    }

    // Recurse into block statements
    if (auto *bs = dynamic_cast<const BlockStmt *>(node)) {
        for (const auto &stmt : bs->getStatements()) {
            collectVarDeclHints(stmt.get(), startLine, endLine, hints);
        }
        return;
    }

    // Recurse into if statements
    if (auto *is = dynamic_cast<const IfStmt *>(node)) {
        collectVarDeclHints(is->getThenBody(), startLine, endLine, hints);
        if (is->hasElse())
            collectVarDeclHints(is->getElseBody(), startLine, endLine, hints);
        return;
    }

    // Recurse into while statements
    if (auto *ws = dynamic_cast<const WhileStmt *>(node)) {
        collectVarDeclHints(ws->getBody(), startLine, endLine, hints);
        return;
    }

    // Recurse into for statements
    if (auto *fs = dynamic_cast<const ForStmt *>(node)) {
        collectVarDeclHints(fs->getBody(), startLine, endLine, hints);
        return;
    }
}

// ============================================================
// Workspace Symbol
// ============================================================

JSONValue LSPServer::handleWorkspaceSymbol(const JSONValue &id,
                                           const JSONValue &params) {
    std::string query = params["query"].getString();

    // Convert query to lowercase for case-insensitive matching
    std::string queryLower;
    queryLower.reserve(query.size());
    for (char c : query) queryLower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    auto symbols = JSONValue::array();

    for (const auto &[docUri, docState] : documents_) {
        if (!docState.tu) continue;

        for (const auto &node : docState.tu->getDeclarations()) {
            auto *decl = node.get();
            std::string name;
            int kind = 0;

            if (auto *fd = dynamic_cast<FuncDecl *>(decl)) {
                name = fd->getName();
                kind = 12; // Function
            } else if (auto *sd = dynamic_cast<StructDecl *>(decl)) {
                name = sd->getName();
                kind = 23; // Struct
            } else if (auto *cd = dynamic_cast<ClassDecl *>(decl)) {
                name = cd->getName();
                kind = 5; // Class
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

            // Filter by query (case-insensitive substring match)
            if (!queryLower.empty()) {
                std::string nameLower;
                nameLower.reserve(name.size());
                for (char c : name) nameLower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                if (nameLower.find(queryLower) == std::string::npos) continue;
            }

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

            auto location = JSONValue::object();
            location.set("uri", JSONValue(docUri));
            location.set("range", std::move(range));

            auto sym = JSONValue::object();
            sym.set("name", JSONValue(name));
            sym.set("kind", JSONValue(static_cast<int64_t>(kind)));
            sym.set("location", std::move(location));

            symbols.push(std::move(sym));
        }
    }

    return makeResponse(id, std::move(symbols));
}

// ============================================================
// Inlay Hints
// ============================================================

JSONValue LSPServer::handleInlayHint(const JSONValue &id,
                                      const JSONValue &params) {
    std::string uri = params["textDocument"]["uri"].getString();
    auto it = documents_.find(uri);
    if (it == documents_.end() || !it->second.tu) {
        return makeResponse(id, JSONValue::array());
    }

    // Range (0-indexed from client, convert to 1-indexed for AST)
    uint32_t startLine = static_cast<uint32_t>(
        params["range"]["start"]["line"].getInteger()) + 1;
    uint32_t endLine = static_cast<uint32_t>(
        params["range"]["end"]["line"].getInteger()) + 1;

    auto hints = JSONValue::array();
    for (const auto &decl : it->second.tu->getDeclarations()) {
        collectVarDeclHints(decl.get(), startLine, endLine, hints);
    }

    return makeResponse(id, std::move(hints));
}

// ============================================================
// Code Lens
// ============================================================

int LSPServer::countReferences(const std::string &name) const {
    int count = 0;
    for (const auto &[docUri, docState] : documents_) {
        auto locs = findNameOccurrences(docState.content, name);
        count += static_cast<int>(locs.size());
    }
    // Subtract 1 for the declaration itself (if it was found)
    return count > 0 ? count - 1 : 0;
}

JSONValue LSPServer::handleCodeLens(const JSONValue &id,
                                     const JSONValue &params) {
    std::string uri = params["textDocument"]["uri"].getString();
    auto it = documents_.find(uri);
    if (it == documents_.end() || !it->second.tu)
        return makeResponse(id, JSONValue::array());

    auto lenses = JSONValue::array();
    for (const auto &node : it->second.tu->getDeclarations()) {
        std::string name;
        int declLine = 0;
        if (auto *fd = dynamic_cast<FuncDecl *>(node.get())) {
            name = fd->getName();
            declLine = fd->getRange().start.line;
        } else if (auto *sd = dynamic_cast<StructDecl *>(node.get())) {
            name = sd->getName();
            declLine = sd->getRange().start.line;
        } else if (auto *cd = dynamic_cast<ClassDecl *>(node.get())) {
            name = cd->getName();
            declLine = cd->getRange().start.line;
        } else if (auto *ed = dynamic_cast<EnumDecl *>(node.get())) {
            name = ed->getName();
            declLine = ed->getRange().start.line;
        }
        if (name.empty()) continue;

        int refs = countReferences(name);

        auto lens = JSONValue::object();
        auto range = JSONValue::object();
        auto startPos = JSONValue::object();
        int line0 = declLine > 0 ? declLine - 1 : 0;
        startPos.set("line", JSONValue(static_cast<int64_t>(line0)));
        startPos.set("character", JSONValue(static_cast<int64_t>(0)));
        auto endPos = JSONValue::object();
        endPos.set("line", JSONValue(static_cast<int64_t>(line0)));
        endPos.set("character", JSONValue(static_cast<int64_t>(0)));
        range.set("start", std::move(startPos));
        range.set("end", std::move(endPos));
        lens.set("range", std::move(range));

        auto cmd = JSONValue::object();
        cmd.set("title", JSONValue(std::to_string(refs) + " references"));
        cmd.set("command", JSONValue("liva.showReferences"));
        lens.set("command", std::move(cmd));
        lenses.push(std::move(lens));
    }
    return makeResponse(id, std::move(lenses));
}

// ============================================================
// Call Hierarchy
// ============================================================

void LSPServer::collectCallsInExpr(const Expr *expr,
                                    std::vector<CallInfo> &calls) const {
    if (!expr) return;
    if (auto *ce = dynamic_cast<const CallExpr *>(expr)) {
        std::string name;
        if (auto *ie = dynamic_cast<const IdentifierExpr *>(ce->getCallee()))
            name = ie->getName();
        if (!name.empty())
            calls.push_back({name, ce->getRange()});
        // Recurse into args
        for (const auto &arg : ce->getArgs())
            collectCallsInExpr(arg.get(), calls);
    } else if (auto *be = dynamic_cast<const BinaryExpr *>(expr)) {
        collectCallsInExpr(be->getLHS(), calls);
        collectCallsInExpr(be->getRHS(), calls);
    } else if (auto *ue = dynamic_cast<const UnaryExpr *>(expr)) {
        collectCallsInExpr(ue->getOperand(), calls);
    }
}

void LSPServer::collectCalls(const ASTNode *node,
                              std::vector<CallInfo> &calls) const {
    if (!node) return;
    if (auto *es = dynamic_cast<const ExprStmt *>(node)) {
        collectCallsInExpr(es->getExpr(), calls);
    } else if (auto *rs = dynamic_cast<const ReturnStmt *>(node)) {
        if (rs->hasValue()) collectCallsInExpr(rs->getValue(), calls);
    } else if (auto *bs = dynamic_cast<const BlockStmt *>(node)) {
        for (const auto &s : bs->getStatements())
            collectCalls(s.get(), calls);
    } else if (auto *is = dynamic_cast<const IfStmt *>(node)) {
        collectCallsInExpr(is->getCondition(), calls);
        collectCalls(is->getThenBody(), calls);
        if (is->hasElse()) collectCalls(is->getElseBody(), calls);
    } else if (auto *ws = dynamic_cast<const WhileStmt *>(node)) {
        collectCallsInExpr(ws->getCondition(), calls);
        collectCalls(ws->getBody(), calls);
    } else if (auto *fs = dynamic_cast<const ForStmt *>(node)) {
        collectCallsInExpr(fs->getIterable(), calls);
        collectCalls(fs->getBody(), calls);
    } else if (auto *vd = dynamic_cast<const VarDecl *>(node)) {
        if (vd->getInit()) collectCallsInExpr(vd->getInit(), calls);
    }
}

JSONValue LSPServer::handleCallHierarchyPrepare(const JSONValue &id,
                                                 const JSONValue &params) {
    std::string uri = params["textDocument"]["uri"].getString();
    auto it = documents_.find(uri);
    if (it == documents_.end() || !it->second.tu)
        return makeResponse(id, JSONValue::array());

    uint32_t line = static_cast<uint32_t>(
        params["position"]["line"].getInteger()) + 1;
    uint32_t col = static_cast<uint32_t>(
        params["position"]["character"].getInteger()) + 1;

    const ASTNode *node = findNodeAtPosition(it->second.tu.get(), line, col);
    auto *fd = node ? dynamic_cast<const FuncDecl *>(node) : nullptr;
    if (!fd)
        return makeResponse(id, JSONValue::array());

    auto items = JSONValue::array();
    auto item = JSONValue::object();
    item.set("name", JSONValue(fd->getName()));
    item.set("kind", JSONValue(static_cast<int64_t>(12))); // Function
    item.set("uri", JSONValue(uri));

    int startLine = fd->getRange().start.line > 0
                        ? static_cast<int>(fd->getRange().start.line) - 1 : 0;
    int endLine = fd->getRange().end.line > 0
                      ? static_cast<int>(fd->getRange().end.line) - 1 : 0;

    auto range = JSONValue::object();
    auto rs = JSONValue::object();
    rs.set("line", JSONValue(static_cast<int64_t>(startLine)));
    rs.set("character", JSONValue(static_cast<int64_t>(0)));
    auto re = JSONValue::object();
    re.set("line", JSONValue(static_cast<int64_t>(endLine)));
    re.set("character", JSONValue(static_cast<int64_t>(0)));
    range.set("start", std::move(rs));
    range.set("end", std::move(re));
    item.set("range", JSONValue(range));
    item.set("selectionRange", std::move(range));

    items.push(std::move(item));
    return makeResponse(id, std::move(items));
}

JSONValue LSPServer::handleCallHierarchyIncoming(const JSONValue &id,
                                                  const JSONValue &params) {
    std::string targetName = params["item"]["name"].getString();
    if (targetName.empty())
        return makeResponse(id, JSONValue::array());

    auto result = JSONValue::array();

    for (const auto &[docUri, docState] : documents_) {
        if (!docState.tu) continue;
        for (const auto &node : docState.tu->getDeclarations()) {
            auto *fd = dynamic_cast<FuncDecl *>(node.get());
            if (!fd || !fd->getBody()) continue;

            std::vector<CallInfo> calls;
            collectCalls(fd->getBody(), calls);

            // Check if any call targets the function we're looking for
            std::vector<CallInfo> matchingCalls;
            for (const auto &c : calls) {
                if (c.callee == targetName)
                    matchingCalls.push_back(c);
            }
            if (matchingCalls.empty()) continue;

            // Build the "from" CallHierarchyItem
            auto from = JSONValue::object();
            from.set("name", JSONValue(fd->getName()));
            from.set("kind", JSONValue(static_cast<int64_t>(12)));
            from.set("uri", JSONValue(docUri));

            int fStartLine = fd->getRange().start.line > 0
                                 ? static_cast<int>(fd->getRange().start.line) - 1 : 0;
            int fEndLine = fd->getRange().end.line > 0
                               ? static_cast<int>(fd->getRange().end.line) - 1 : 0;
            auto fRange = JSONValue::object();
            auto frs = JSONValue::object();
            frs.set("line", JSONValue(static_cast<int64_t>(fStartLine)));
            frs.set("character", JSONValue(static_cast<int64_t>(0)));
            auto fre = JSONValue::object();
            fre.set("line", JSONValue(static_cast<int64_t>(fEndLine)));
            fre.set("character", JSONValue(static_cast<int64_t>(0)));
            fRange.set("start", std::move(frs));
            fRange.set("end", std::move(fre));
            from.set("range", JSONValue(fRange));
            from.set("selectionRange", std::move(fRange));

            // Build fromRanges
            auto fromRanges = JSONValue::array();
            for (const auto &mc : matchingCalls) {
                auto cr = JSONValue::object();
                auto crs = JSONValue::object();
                int cLine = mc.range.start.line > 0
                                ? static_cast<int>(mc.range.start.line) - 1 : 0;
                int cCol = mc.range.start.column > 0
                               ? static_cast<int>(mc.range.start.column) - 1 : 0;
                int ceCol = mc.range.end.column > 0
                                ? static_cast<int>(mc.range.end.column) - 1 : 0;
                crs.set("line", JSONValue(static_cast<int64_t>(cLine)));
                crs.set("character", JSONValue(static_cast<int64_t>(cCol)));
                auto cre = JSONValue::object();
                cre.set("line", JSONValue(static_cast<int64_t>(cLine)));
                cre.set("character", JSONValue(static_cast<int64_t>(ceCol)));
                cr.set("start", std::move(crs));
                cr.set("end", std::move(cre));
                fromRanges.push(std::move(cr));
            }

            auto incoming = JSONValue::object();
            incoming.set("from", std::move(from));
            incoming.set("fromRanges", std::move(fromRanges));
            result.push(std::move(incoming));
        }
    }
    return makeResponse(id, std::move(result));
}

JSONValue LSPServer::handleCallHierarchyOutgoing(const JSONValue &id,
                                                  const JSONValue &params) {
    std::string funcName = params["item"]["name"].getString();
    std::string itemUri = params["item"]["uri"].getString();
    if (funcName.empty())
        return makeResponse(id, JSONValue::array());

    auto result = JSONValue::array();

    // Find the function in the specified document
    auto it = documents_.find(itemUri);
    if (it == documents_.end() || !it->second.tu)
        return makeResponse(id, JSONValue::array());

    for (const auto &node : it->second.tu->getDeclarations()) {
        auto *fd = dynamic_cast<FuncDecl *>(node.get());
        if (!fd || fd->getName() != funcName || !fd->getBody()) continue;

        std::vector<CallInfo> calls;
        collectCalls(fd->getBody(), calls);

        // Deduplicate by callee name
        std::map<std::string, std::vector<CallInfo>> callsByName;
        for (const auto &c : calls)
            callsByName[c.callee].push_back(c);

        for (const auto &[calleeName, calleeCalls] : callsByName) {
            // Build the "to" CallHierarchyItem
            auto to = JSONValue::object();
            to.set("name", JSONValue(calleeName));
            to.set("kind", JSONValue(static_cast<int64_t>(12)));

            // Try to find the declaration for the callee
            std::string toUri = itemUri;
            int toStartLine = 0, toEndLine = 0;
            for (const auto &[dUri, dState] : documents_) {
                if (!dState.tu) continue;
                for (const auto &dn : dState.tu->getDeclarations()) {
                    if (auto *tfd = dynamic_cast<FuncDecl *>(dn.get())) {
                        if (tfd->getName() == calleeName) {
                            toUri = dUri;
                            toStartLine = tfd->getRange().start.line > 0
                                              ? static_cast<int>(tfd->getRange().start.line) - 1 : 0;
                            toEndLine = tfd->getRange().end.line > 0
                                            ? static_cast<int>(tfd->getRange().end.line) - 1 : 0;
                        }
                    }
                }
            }
            to.set("uri", JSONValue(toUri));

            auto toRange = JSONValue::object();
            auto trs = JSONValue::object();
            trs.set("line", JSONValue(static_cast<int64_t>(toStartLine)));
            trs.set("character", JSONValue(static_cast<int64_t>(0)));
            auto tre = JSONValue::object();
            tre.set("line", JSONValue(static_cast<int64_t>(toEndLine)));
            tre.set("character", JSONValue(static_cast<int64_t>(0)));
            toRange.set("start", std::move(trs));
            toRange.set("end", std::move(tre));
            to.set("range", JSONValue(toRange));
            to.set("selectionRange", std::move(toRange));

            // Build fromRanges (call sites)
            auto fromRanges = JSONValue::array();
            for (const auto &cc : calleeCalls) {
                auto cr = JSONValue::object();
                auto crs = JSONValue::object();
                int cLine = cc.range.start.line > 0
                                ? static_cast<int>(cc.range.start.line) - 1 : 0;
                int cCol = cc.range.start.column > 0
                               ? static_cast<int>(cc.range.start.column) - 1 : 0;
                int ceCol = cc.range.end.column > 0
                                ? static_cast<int>(cc.range.end.column) - 1 : 0;
                crs.set("line", JSONValue(static_cast<int64_t>(cLine)));
                crs.set("character", JSONValue(static_cast<int64_t>(cCol)));
                auto cre = JSONValue::object();
                cre.set("line", JSONValue(static_cast<int64_t>(cLine)));
                cre.set("character", JSONValue(static_cast<int64_t>(ceCol)));
                cr.set("start", std::move(crs));
                cr.set("end", std::move(cre));
                fromRanges.push(std::move(cr));
            }

            auto outgoing = JSONValue::object();
            outgoing.set("to", std::move(to));
            outgoing.set("fromRanges", std::move(fromRanges));
            result.push(std::move(outgoing));
        }
        break; // Found the function
    }
    return makeResponse(id, std::move(result));
}

} // namespace liva
