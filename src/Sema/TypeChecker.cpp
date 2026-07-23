#include "liva/Sema/TypeChecker.h"
#include "liva/Lexer/Lexer.h"
#include "liva/Parser/Parser.h"
#include "liva/Sema/ModuleLoader.h"
#include <set>

namespace liva {

TypeChecker::TypeChecker(DiagnosticsEngine &diag, ModuleLoader *loader)
    : diag_(diag), moduleLoader_(loader) { registerBuiltins(); }

void TypeChecker::registerBuiltins() {
    // Register built-in functions
    Symbol printSym;
    printSym.name = "print";
    printSym.kind = Symbol::Kind::Function;
    scopes_.declare("print", printSym);

    Symbol printlnSym;
    printlnSym.name = "println";
    printlnSym.kind = Symbol::Kind::Function;
    scopes_.declare("println", printlnSym);

    Symbol lenSym;
    lenSym.name = "len";
    lenSym.kind = Symbol::Kind::Function;
    scopes_.declare("len", lenSym);

    Symbol toStringSym;
    toStringSym.name = "toString";
    toStringSym.kind = Symbol::Kind::Function;
    scopes_.declare("toString", toStringSym);

    for (auto &name : {"abs", "min", "max", "sqrt", "pow", "floor", "ceil",
                        "log", "log10", "sin", "cos", "tan", "round"}) {
        Symbol sym;
        sym.name = name;
        sym.kind = Symbol::Kind::Function;
        scopes_.declare(name, sym);
    }

    Symbol readLineSym;
    readLineSym.name = "readLine";
    readLineSym.kind = Symbol::Kind::Function;
    scopes_.declare("readLine", readLineSym);

    Symbol formatSym;
    formatSym.name = "format";
    formatSym.kind = Symbol::Kind::Function;
    scopes_.declare("format", formatSym);

    Symbol fileSym;
    fileSym.name = "File";
    fileSym.kind = Symbol::Kind::StructType;
    scopes_.declare("File", fileSym);

    for (auto &name : {"parseInt", "parseInt64", "parseFloat"}) {
        Symbol sym;
        sym.name = name;
        sym.kind = Symbol::Kind::Function;
        scopes_.declare(name, sym);
    }

    // Stdlib: Random, Process/Env, Date/Time, Regex, Networking
    for (auto &name : {"randInt", "randFloat", "randSeed", "randI64", "randUuid", "randUuidV7",
                        "env", "exit", "args",
                        "clock", "clockMs", "sleep", "isCancelled",
                        "regexMatch", "regexFind", "regexFindAll", "regexReplace",
                        "regexFindGroups", "regexSplit",
                        "regexCompile", "regexTest", "regexExec",
                        "regexExecGroups", "regexReplaceCompiled", "regexFree",
                        "httpRequestEx", "httpStatus", "httpBody",
                        "httpRawHeaders", "httpHeaderLookup", "httpClose",
                        "wsConnectEx", "wsSend", "wsSendBinary",
                        "wsRecvKind", "wsMsgText", "wsMsgBytes",
                        "wsClose", "wsIsOpen",
                        "sqliteOpen", "sqliteClose", "sqliteExec",
                        "sqliteQueryFirst", "sqliteQueryInt", "sqliteQueryColumn",
                        "sqliteLastInsertRowid", "sqliteChanges", "sqliteErrmsg",
                        "sqlitePrepare", "sqliteBindText", "sqliteBindInt",
                        "sqliteBindDouble", "sqliteBindNull",
                        "sqliteStep", "sqliteReset", "sqliteColumnCount",
                        "sqliteColumnText", "sqliteColumnInt", "sqliteColumnDouble",
                        "sqliteColumnName", "sqliteColumnType", "sqliteColumnIsNull",
                        "sqliteBindByName",
                        "sqliteBindBlob", "sqliteColumnBlob",
                        "sqliteFinalize"}) {
        Symbol sym;
        sym.name = name;
        sym.kind = Symbol::Kind::Function;
        scopes_.declare(name, sym);
    }

    // Stdlib: PostgreSQL
    for (const char *name : {"pgConnect", "pgClose", "pgExec", "pgErrmsg",
                             "pgNormalizeParams", "pgQuery", "pgQueryParams",
                             "pgResultRows", "pgResultCols", "pgResultText",
                             "pgResultIsNull", "pgColumnName", "pgClear"}) {
        Symbol sym;
        sym.name = name;
        sym.kind = Symbol::Kind::Function;
        scopes_.declare(name, sym);
    }

    // Stdlib: Directory and Path operations
    for (auto &name : {"dirList", "dirCreate", "dirRemove", "dirExists",
                        "pathJoin", "pathDirname", "pathBasename",
                        "pathExtension", "pathExists", "isFile", "isDir",
                        "fileSize", "fileModifiedTime",
                        "fileRead", "fileWrite", "fileAppend",
                        "fileRemove", "fileCopy", "pathAbsolute"}) {
        Symbol sym;
        sym.name = name;
        sym.kind = Symbol::Kind::Function;
        scopes_.declare(name, sym);
    }

    // Stdlib: Subprocess
    for (auto &name : {"exec", "execOutput", "processStart",
                        "processWait", "processKill", "processRead",
                        "processClose"}) {
        Symbol sym;
        sym.name = name;
        sym.kind = Symbol::Kind::Function;
        scopes_.declare(name, sym);
    }

    // Stdlib: JSON
    for (auto &name : {"jsonParse", "jsonFreeDoc", "jsonRoot", "jsonNodeKind",
                        "jsonNodeAsInt", "jsonNodeAsFloat", "jsonNodeAsBool", "jsonNodeAsString",
                        "jsonToString", "jsonToStringPretty",
                        "jsonObjGet", "jsonObjHas", "jsonObjCount",
                        "jsonArrCount", "jsonArrAt",
                        "jsonObjKeys",
                        "jsonNewObject", "jsonNewArray",
                        "jsonObjSetString", "jsonObjSetInt", "jsonObjSetFloat", "jsonObjSetBool", "jsonObjSetNull",
                        "jsonObjSetObject", "jsonObjSetArray", "jsonObjRemove",
                        "jsonArrAddString", "jsonArrAddInt", "jsonArrAddFloat", "jsonArrAddBool", "jsonArrAddNull",
                        "jsonArrAddObject", "jsonArrAddArray",
                        "jsonPathGet",
                        "jsonPathSetString", "jsonPathSetInt", "jsonPathSetFloat", "jsonPathSetBool"}) {
        Symbol sym;
        sym.name = name;
        sym.kind = Symbol::Kind::Function;
        scopes_.declare(name, sym);
    }

    // Stdlib: Logging
    for (auto &name : {"logDebug", "logInfo", "logWarn", "logError",
                        "logSetLevel"}) {
        Symbol sym;
        sym.name = name;
        sym.kind = Symbol::Kind::Function;
        scopes_.declare(name, sym);
    }

    // Stdlib: Testing
    for (auto &name : {"assert", "assertMsg", "assertEq",
                        "assertEqStr", "assertEqFloat",
                        "testRunClosure"}) {
        Symbol sym;
        sym.name = name;
        sym.kind = Symbol::Kind::Function;
        scopes_.declare(name, sym);
    }

    // Stdlib: DateTime
    for (auto &name : {"dateNow", "timeNow", "datetimeNow", "dateFormat",
                        "dateYear", "dateMonth", "dateDay", "dateWeekday",
                        "dateTimestamp", "dateParse", "dateAdd", "dateDiff",
                        "dateHour", "dateMinute", "dateSecond",
                        "isoFormatUtc", "isoParse"}) {
        Symbol sym;
        sym.name = name;
        sym.kind = Symbol::Kind::Function;
        scopes_.declare(name, sym);
    }

    // Stdlib: Encoding/Compression
    for (auto &name : {"base64Encode", "base64Decode", "hexEncode",
                        "hexDecode", "urlEncode", "urlDecode", "crc32",
                        "urlScheme", "urlHost", "urlPort", "urlPath",
                        "urlQuery", "urlFragment",
                        "base64UrlEncode", "base64UrlDecode",
                        "strToBytes", "bytesToStr",
                        "hexEncodeBytes", "hexDecodeBytes",
                        "base64UrlEncodeBytes", "base64UrlDecodeBytes",
                        "gzipEncode", "gzipDecode"}) {
        Symbol sym;
        sym.name = name;
        sym.kind = Symbol::Kind::Function;
        scopes_.declare(name, sym);
    }

    // Stdlib: Crypto
    for (auto &name : {"sha256", "md5", "hmacSha256",
                       "sha1", "sha512", "hmacSha1", "hmacSha512",
                       "jwtHS256Sig", "jwtHS512Sig",
                       "constTimeEq"}) {
        Symbol sym;
        sym.name = name;
        sym.kind = Symbol::Kind::Function;
        scopes_.declare(name, sym);
    }

    // Stdlib: Benchmarking
    for (auto &name : {"benchStart", "benchIter", "benchDone",
                        "benchReport", "benchReset"}) {
        Symbol sym;
        sym.name = name;
        sym.kind = Symbol::Kind::Function;
        scopes_.declare(name, sym);
    }

    // Stdlib: TOML
    for (auto &name : {"tomlParse", "tomlGetString", "tomlGetInt",
                        "tomlGetBool", "tomlHasKey", "tomlFree"}) {
        Symbol sym;
        sym.name = name;
        sym.kind = Symbol::Kind::Function;
        scopes_.declare(name, sym);
    }

    // Stdlib: Synchronization (Mutex + Atomic + RWLock + CondVar + Channel + TaskGroup)
    for (auto &name : {"mutexCreate", "mutexLock", "mutexUnlock",
                        "mutexTryLock", "mutexFree",
                        "atomicCreate", "atomicLoad", "atomicStore",
                        "atomicAdd", "atomicSub", "atomicCas", "atomicFree",
                        "rwlockCreate", "rwlockReadLock", "rwlockReadUnlock",
                        "rwlockWriteLock", "rwlockWriteUnlock",
                        "rwlockTryReadLock", "rwlockTryWriteLock", "rwlockFree",
                        "condVarCreate", "condVarWait",
                        "condVarNotifyOne", "condVarNotifyAll", "condVarFree",
                        "channelCreate", "channelSend", "channelReceive",
                        "channelTrySend", "channelTryReceive",
                        "channelClose", "channelLen", "channelFree",
                        "taskGroupCreate", "taskGroupSpawn", "taskGroupAwaitAll",
                        "taskGroupCancelAll", "taskGroupCount", "taskGroupFree",
                        "taskSelect", "withTimeout",
                        "taskIsDone", "taskCancel", "taskIsCancelled",
                        "schedulerInit", "schedulerShutdown", "schedulerWorkerCount",
                        "asyncFileRead", "asyncFileWrite"}) {
        Symbol sym;
        sym.name = name;
        sym.kind = Symbol::Kind::Function;
        scopes_.declare(name, sym);
    }

    // Stdlib: Collections utility functions (top-level)
    for (auto &name : {"forEach", "enumerate", "zip", "sorted",
                        "reversed", "flatten", "any", "all", "count"}) {
        Symbol sym;
        sym.name = name;
        sym.kind = Symbol::Kind::Function;
        scopes_.declare(name, sym);
    }

    // Stdlib: String utility functions (top-level)
    for (auto &name : {"strRepeat", "strPadLeft", "strPadRight",
                        "strContains", "strReplace", "strSplit",
                        "strJoin", "strTrim", "strTrimLeft", "strTrimRight",
                        "strStartsWith", "strEndsWith",
                        "strToUpper", "strToLower",
                        "strReverse", "strChars", "strLines",
                        "strCharCount", "strCodepointAt", "strIsAscii",
                        "charIsAlpha", "charIsDigit", "charIsAlnum",
                        "charIsSpace", "charIsUpper", "charIsLower",
                        "charToUpper", "charToLower"}) {
        Symbol sym;
        sym.name = name;
        sym.kind = Symbol::Kind::Function;
        scopes_.declare(name, sym);
    }

    // Built-in + stdlib iterables conform to Iterator implicitly. IRGen
    // continues to use hardcoded fast paths for these types; the conformance
    // entry exists so that `where T: Iterator` accepts them in generic
    // constraints. Stack and Queue are stdlib structs (collections::collections);
    // their entry here lets generic constraint solving recognize them even
    // when the module is imported.
    for (const char *name : {"Range", "Array", "DynArray", "Map", "Set",
                              "Generator", "Stack", "Queue"}) {
        protocolConformances_["Iterator"].push_back(name);
    }
    protocolConformances_["AsyncIterator"].push_back("Generator");

    // P1-8 alt-spec 2: built-in types conform to Hashable. The hash() method
    // is dispatched at IRGen to runtime liva_hash_* functions; here we only
    // register conformance so generic `where T: Hashable` accepts primitives.
    for (const char *name : {"i8", "i16", "i32", "i64",
                              "u8", "u16", "u32", "u64",
                              "string", "bool", "Char"}) {
        protocolConformances_["Hashable"].push_back(name);
    }
}

// === "Did you mean?" suggestion helpers ===

size_t TypeChecker::editDistance(const std::string &a, const std::string &b) {
    size_t m = a.size(), n = b.size();
    // Use single-row DP for O(min(m,n)) space
    std::vector<size_t> prev(n + 1), curr(n + 1);
    for (size_t j = 0; j <= n; ++j)
        prev[j] = j;
    for (size_t i = 1; i <= m; ++i) {
        curr[0] = i;
        for (size_t j = 1; j <= n; ++j) {
            size_t cost = (a[i - 1] == b[j - 1]) ? 0 : 1;
            curr[j] = prev[j] + 1;             // deletion
            if (curr[j] > curr[j - 1] + 1)
                curr[j] = curr[j - 1] + 1;     // insertion
            if (curr[j] > prev[j - 1] + cost)
                curr[j] = prev[j - 1] + cost;  // substitution
        }
        std::swap(prev, curr);
    }
    return prev[n];
}

std::string TypeChecker::findClosestMatch(const std::string &name,
                                           const std::vector<std::string> &candidates,
                                           size_t maxDist) {
    if (candidates.empty())
        return "";
    // Default threshold: at most len/3 + 1, minimum 2
    if (maxDist == 0) {
        maxDist = name.size() / 3 + 1;
        if (maxDist < 2)
            maxDist = 2;
    }
    std::string best;
    size_t bestDist = maxDist + 1;
    for (const auto &c : candidates) {
        // Quick reject: length difference too large
        size_t lenDiff = (c.size() > name.size()) ? c.size() - name.size()
                                                   : name.size() - c.size();
        if (lenDiff > maxDist)
            continue;
        size_t d = editDistance(name, c);
        if (d < bestDist) {
            bestDist = d;
            best = c;
        }
    }
    return (bestDist <= maxDist) ? best : "";
}

void TypeChecker::suggestSimilar(SourceLocation loc, const std::string &name,
                                  const std::vector<std::string> &candidates) {
    std::string match = findClosestMatch(name, candidates);
    if (!match.empty())
        diag_.reportHelp(loc, static_cast<uint32_t>(name.size()),
                         "did you mean '" + match + "'?", match,
                         DiagID::note_did_you_mean);
}

void TypeChecker::check(TranslationUnit &tu) {
    // First pass: register all top-level declarations
    for (auto &decl : tu.getDeclarations()) {
        if (decl->getKind() == ASTNode::NodeKind::ImportDecl) {
            auto *importDecl = static_cast<ImportDecl *>(decl.get());
            if (moduleLoader_) {
                auto *mod = moduleLoader_->loadModule(
                    importDecl->getPath(), diag_, importDecl->getStartLoc());
                if (mod) {
                    for (auto &sym : mod->exportedSymbols) {
                        scopes_.declare(sym.name, sym);
                    }
                    // Propagate impl method return types from the module's TU
                    // into this TypeChecker, for methods returning array, optional,
                    // or named struct types ([T] / T? / StructName). This enables
                    // instance method calls like stmt2.columnBlob(0) -> [u8] to get
                    // their resolved type set correctly in visitCallExpr so
                    // varDynArrayTypes is populated. Also enables propagation through
                    // Optional-returning methods (e.g. db.prepare() -> Stmt?) for
                    // multi-step chains, and Named-returning methods (e.g.
                    // stmt.columnDate(i) -> Date) so chained calls like
                    // stmt.columnDate(i).year() resolve correctly.
                    //
                    // For Named returns: only register when the return type name
                    // DIFFERS from the receiver struct name. This avoids marking
                    // builder-style methods (e.g. DateTime::addSeconds → DateTime)
                    // as returning a struct type, which would cause the
                    // OwnershipChecker to treat the variables as non-Copy and
                    // flag spurious use-after-move errors on pass-by-value usage.
                    if (mod->tu) {
                        for (auto &topDecl : mod->tu->getDeclarations()) {
                            if (topDecl->getKind() == ASTNode::NodeKind::ImplDecl) {
                                auto *implD = static_cast<ImplDecl *>(topDecl.get());
                                for (auto &method : implD->getMethods()) {
                                    auto *rt = method->getReturnType();
                                    bool shouldRegister = false;
                                    if (rt) {
                                        auto retKind = rt->getKind();
                                        if (retKind == TypeRepr::Kind::Array ||
                                            retKind == TypeRepr::Kind::Optional) {
                                            shouldRegister = true;
                                        } else if (retKind == TypeRepr::Kind::Named) {
                                            // Only register cross-struct Named returns:
                                            // the return type must name a DIFFERENT struct
                                            // than the receiver (e.g. Stmt→Date is ok;
                                            // DateTime→DateTime is skipped to avoid
                                            // non-Copy inference on builder-style chains).
                                            auto *named = static_cast<const NamedTypeRepr *>(rt);
                                            if (named->getName() != implD->getTypeName()) {
                                                shouldRegister = true;
                                            }
                                        }
                                    }
                                    if (shouldRegister) {
                                        std::string key = implD->getTypeName() + "::" + method->getName();
                                        typeMethodReturnTypes_[key] = rt;
                                    }
                                }
                            } else if (topDecl->getKind() == ASTNode::NodeKind::ProtocolDecl) {
                                // Also import protocol method return types so that
                                // `dyn Protocol` call sites resolve the return type.
                                auto *protoD = static_cast<ProtocolDecl *>(topDecl.get());
                                for (auto &method : protoD->getMethods()) {
                                    auto *rt = method->getReturnType();
                                    if (rt) {
                                        std::string key = protoD->getName() + "::" + method->getName();
                                        typeMethodReturnTypes_[key] = rt;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        } else if (decl->getKind() == ASTNode::NodeKind::FuncDecl) {
            auto *funcDecl = static_cast<FuncDecl *>(decl.get());
            // Track async functions
            if (funcDecl->isAsync()) {
                asyncFuncNames_.insert(funcDecl->getName());
            }
            Symbol sym;
            sym.name = funcDecl->getName();
            sym.kind = Symbol::Kind::Function;
            sym.funcDecl = funcDecl;
            sym.type = funcDecl->getReturnType();
            sym.declLoc = decl->getStartLoc();
            if (!scopes_.declare(sym.name, sym)) {
                diag_.report(decl->getStartLoc(), DiagID::err_redefinition,
                             funcDecl->getName());
                auto *prev = scopes_.lookup(sym.name);
                if (prev && prev->declLoc.isValid())
                    diag_.report(prev->declLoc, DiagID::note_previous_declaration, sym.name);
            }
        } else if (decl->getKind() == ASTNode::NodeKind::StructDecl) {
            auto *structDecl = static_cast<StructDecl *>(decl.get());
            Symbol sym;
            sym.name = structDecl->getName();
            sym.kind = Symbol::Kind::StructType;
            sym.structDecl = structDecl;
            sym.declLoc = decl->getStartLoc();
            if (!scopes_.declare(sym.name, sym)) {
                diag_.report(decl->getStartLoc(), DiagID::err_redefinition,
                             structDecl->getName());
                auto *prev = scopes_.lookup(sym.name);
                if (prev && prev->declLoc.isValid())
                    diag_.report(prev->declLoc, DiagID::note_previous_declaration, sym.name);
            }
        } else if (decl->getKind() == ASTNode::NodeKind::EnumDecl) {
            auto *enumDecl = static_cast<EnumDecl *>(decl.get());
            Symbol sym;
            sym.name = enumDecl->getName();
            sym.kind = Symbol::Kind::EnumType;
            sym.enumDecl = enumDecl;
            sym.declLoc = decl->getStartLoc();
            if (!scopes_.declare(sym.name, sym)) {
                diag_.report(decl->getStartLoc(), DiagID::err_redefinition,
                             enumDecl->getName());
                auto *prev = scopes_.lookup(sym.name);
                if (prev && prev->declLoc.isValid())
                    diag_.report(prev->declLoc, DiagID::note_previous_declaration, sym.name);
            }
        } else if (decl->getKind() == ASTNode::NodeKind::ProtocolDecl) {
            auto *protocolDecl = static_cast<ProtocolDecl *>(decl.get());
            Symbol sym;
            sym.name = protocolDecl->getName();
            sym.kind = Symbol::Kind::ProtocolType;
            sym.protocolDecl = protocolDecl;
            sym.declLoc = decl->getStartLoc();
            if (!scopes_.declare(sym.name, sym)) {
                diag_.report(decl->getStartLoc(), DiagID::err_redefinition,
                             protocolDecl->getName());
                auto *prev = scopes_.lookup(sym.name);
                if (prev && prev->declLoc.isValid())
                    diag_.report(prev->declLoc, DiagID::note_previous_declaration, sym.name);
            }
        } else if (decl->getKind() == ASTNode::NodeKind::TypeAliasDecl) {
            auto *aliasDecl = static_cast<TypeAliasDecl *>(decl.get());
            Symbol sym;
            sym.name = aliasDecl->getName();
            sym.kind = Symbol::Kind::TypeAlias;
            sym.aliasTarget = aliasDecl->getTargetType();
            sym.declLoc = decl->getStartLoc();
            if (!scopes_.declare(sym.name, sym)) {
                diag_.report(decl->getStartLoc(), DiagID::err_redefinition,
                             aliasDecl->getName());
                auto *prev = scopes_.lookup(sym.name);
                if (prev && prev->declLoc.isValid())
                    diag_.report(prev->declLoc, DiagID::note_previous_declaration, sym.name);
            }
            typeAliases_[aliasDecl->getName()] = aliasDecl->getTargetType();
        } else if (decl->getKind() == ASTNode::NodeKind::ClassDecl) {
            auto *classDecl = static_cast<ClassDecl *>(decl.get());
            Symbol sym;
            sym.name = classDecl->getName();
            sym.kind = Symbol::Kind::ClassType;
            sym.classDecl = classDecl;
            sym.declLoc = decl->getStartLoc();
            if (!scopes_.declare(sym.name, sym)) {
                diag_.report(decl->getStartLoc(), DiagID::err_redefinition,
                             classDecl->getName());
                auto *prev = scopes_.lookup(sym.name);
                if (prev && prev->declLoc.isValid())
                    diag_.report(prev->declLoc, DiagID::note_previous_declaration, sym.name);
            }
            classDecls_[classDecl->getName()] = classDecl;
            if (classDecl->hasParentClass()) {
                classParent_[classDecl->getName()] = classDecl->getParentClass();
            }
            // Track private/fileprivate members (both hidden outside declaring class)
            for (auto &m : classDecl->getMembers()) {
                if (m.access == AccessModifier::Private ||
                    m.access == AccessModifier::FilePrivate) {
                    if (m.field)
                        classPrivateMembers_[classDecl->getName()].insert(m.field->getName());
                    if (m.method)
                        classPrivateMembers_[classDecl->getName()].insert(m.method->getName());
                }
            }
        } else if (decl->getKind() == ASTNode::NodeKind::MacroDecl) {
            auto *macroDecl = static_cast<MacroDecl *>(decl.get());
            if (macroExpander_.hasMacro(macroDecl->getName())) {
                diag_.report(decl->getStartLoc(), DiagID::err_macro_redefinition,
                             macroDecl->getName());
            } else {
                auto def = MacroExpander::parseMacroDef(
                    macroDecl->getName(), macroDecl->isPublic(),
                    macroDecl->getRawSource(), macroDecl->getRange());
                macroExpander_.registerMacro(def);
            }
        }
    }

    // Second pass: check declarations
    for (auto &decl : tu.getDeclarations()) {
        visit(decl.get());
    }
}

static bool isFFISafeType(const TypeRepr *type) {
    if (!type) return true;  // void
    if (type->isPrimitive()) return true;  // Bool, I8-I64, U8-U64, F32, F64, String, Void
    if (type->isReference()) return true;  // raw pointer
    if (type->getKind() == TypeRepr::Kind::Named) {
        if (static_cast<const NamedTypeRepr *>(type)->getName() == "String") return true;
        return true;  // user-defined structs passed by pointer are FFI-safe
    }
    return false;  // Array, Optional, Tuple, Result, Function, DynProtocol → unsafe
}

void TypeChecker::visitFuncDecl(FuncDecl *node) {
    scopes_.pushScope();

    // Note: err_async_main intentionally not emitted — Liva supports async main

    // Save/restore async and generator state
    bool prevIsAsync = currentIsAsync_;
    bool prevIsGenerator = currentIsGenerator_;
    currentIsAsync_ = node->isAsync();
    currentIsGenerator_ = node->isGenerator();

    // Save/restore type-param bounds for this (possibly generic) function
    auto prevTypeParamBounds = std::move(currentTypeParamBounds_);
    currentTypeParamBounds_.clear();
    for (auto &[paramName, boundProtos] : node->getTypeParamBounds())
        currentTypeParamBounds_[paramName] = boundProtos;

    // Auto-detect generator: if body contains yield, mark as generator
    if (!currentIsGenerator_ && node->hasBody()) {
        std::function<bool(const ASTNode *)> hasYield = [&](const ASTNode *n) -> bool {
            if (!n) return false;
            if (n->getKind() == ASTNode::NodeKind::YieldExpr) return true;
            if (auto *bs = dynamic_cast<const BlockStmt *>(n)) {
                for (auto &s : bs->getStatements())
                    if (hasYield(s.get())) return true;
            }
            if (auto *es = dynamic_cast<const ExprStmt *>(n)) return hasYield(es->getExpr());
            if (auto *is = dynamic_cast<const IfStmt *>(n)) {
                if (hasYield(is->getThenBody())) return true;
                if (is->hasElse() && hasYield(is->getElseBody())) return true;
            }
            if (auto *ws = dynamic_cast<const WhileStmt *>(n)) return hasYield(ws->getBody());
            if (auto *fs = dynamic_cast<const ForStmt *>(n)) return hasYield(fs->getBody());
            return false;
        };
        if (hasYield(node->getBody())) {
            node->setGenerator(true);
            currentIsGenerator_ = true;
        }
    }

    // Save unused-variable tracking state (for nested functions/closures)
    auto prevUsedSymbols = std::move(usedSymbols_);
    auto prevFuncVars = std::move(currentFuncVars_);
    auto prevForLoopVars = std::move(forLoopVars_);
    usedSymbols_.clear();
    currentFuncVars_.clear();
    forLoopVars_.clear();

    // Validate extern function constraints
    if (node->isExtern()) {
        if (node->hasBody()) {
            diag_.report(node->getStartLoc(), DiagID::err_extern_with_body, node->getName());
        }
        if (node->isAsync()) {
            diag_.report(node->getStartLoc(), DiagID::err_extern_async, node->getName());
        }
        if (node->isGeneric()) {
            diag_.report(node->getStartLoc(), DiagID::err_extern_generic, node->getName());
        }
        // Check FFI type safety
        for (auto &param : node->getParams()) {
            if (param.type && !isFFISafeType(param.type.get())) {
                diag_.report(param.location, DiagID::warn_extern_param_type,
                             param.type->toString());
            }
        }
        if (node->getReturnType() && !isFFISafeType(node->getReturnType())) {
            diag_.report(node->getStartLoc(), DiagID::warn_extern_return_type,
                         node->getReturnType()->toString());
        }
        // Register params in scope but skip body analysis
        for (auto &param : node->getParams()) {
            Symbol sym;
            sym.name = param.name;
            sym.kind = Symbol::Kind::Parameter;
            sym.type = param.type.get();
            sym.isMutable = param.isMutRef;
            sym.declLoc = param.location;
            scopes_.declare(sym.name, sym);
        }
        currentReturnType_ = node->getReturnType();
        // Restore state and return early
        currentIsAsync_ = prevIsAsync;
        currentIsGenerator_ = prevIsGenerator;
        usedSymbols_ = std::move(prevUsedSymbols);
        currentFuncVars_ = std::move(prevFuncVars);
        forLoopVars_ = std::move(prevForLoopVars);
        scopes_.popScope();
        return;
    }

    // Validate C varargs only in extern
    if (node->isCVarargs() && !node->isExtern()) {
        diag_.report(node->getStartLoc(), DiagID::err_cvarargs_not_extern, node->getName());
    }

    // Apply lifetime elision rules (before registration)
    elideFunctionLifetimes(node);

    // Register lifetime parameters in scope (includes elided ones)
    for (const auto &lp : node->getLifetimeParams()) {
        Symbol sym;
        sym.name = lp;
        sym.kind = Symbol::Kind::LifetimeParam;
        sym.declLoc = node->getStartLoc();
        scopes_.declare(lp, sym);
    }

    // Register type parameters in scope
    for (const auto &tp : node->getTypeParams()) {
        Symbol sym;
        sym.name = tp;
        sym.kind = Symbol::Kind::TypeParam;
        sym.declLoc = node->getStartLoc();
        scopes_.declare(tp, sym);
    }

    // Register const generic parameters in scope
    for (const auto &cp : node->getConstParams()) {
        Symbol sym;
        sym.name = cp.name;
        sym.kind = Symbol::Kind::ConstParam;
        sym.type = cp.type.get();
        sym.declLoc = node->getStartLoc();
        scopes_.declare(cp.name, sym);
    }

    // Validate trait bounds reference real protocols
    for (auto &[paramName, boundProtos] : node->getTypeParamBounds()) {
        for (auto &boundProto : boundProtos) {
            auto *protoSym = scopes_.lookup(boundProto);
            if (!protoSym || protoSym->kind != Symbol::Kind::ProtocolType) {
                diag_.report(node->getStartLoc(), DiagID::err_undefined_protocol, boundProto);
                std::vector<std::string> protoCandidates;
                scopes_.collectNames(Symbol::Kind::ProtocolType, protoCandidates);
                suggestSimilar(node->getStartLoc(), boundProto, protoCandidates);
            }
        }
    }

    // Validate variadic parameters
    bool seenVariadic = false;
    for (size_t pi = 0; pi < node->getParams().size(); ++pi) {
        auto &param = node->getParams()[pi];
        if (param.isVariadic) {
            if (seenVariadic) {
                diag_.report(param.location, DiagID::err_multiple_variadic, param.name);
            }
            seenVariadic = true;
            // Check it's the last non-self param
            bool isLast = true;
            for (size_t pj = pi + 1; pj < node->getParams().size(); ++pj) {
                if (!node->getParams()[pj].isSelf) {
                    isLast = false;
                    break;
                }
            }
            if (!isLast) {
                diag_.report(param.location, DiagID::err_variadic_not_last, param.name);
            }
        }
    }

    // Validate dyn Protocol parameters
    for (auto &param : node->getParams()) {
        if (param.type && param.type->getKind() == TypeRepr::Kind::DynProtocol) {
            auto *dynType = static_cast<const DynProtocolTypeRepr *>(param.type.get());
            auto *protoSym = scopes_.lookup(dynType->getProtocolName());
            if (!protoSym || protoSym->kind != Symbol::Kind::ProtocolType) {
                diag_.report(param.location, DiagID::err_undefined_protocol,
                             dynType->getProtocolName());
                std::vector<std::string> protoCandidates;
                scopes_.collectNames(Symbol::Kind::ProtocolType, protoCandidates);
                suggestSimilar(param.location, dynType->getProtocolName(), protoCandidates);
            } else {
                // Object safety check
                std::string unsafeMethod;
                if (!isObjectSafe(dynType->getProtocolName(), unsafeMethod)) {
                    diag_.report(param.location, DiagID::err_protocol_not_object_safe,
                                 dynType->getProtocolName(), unsafeMethod);
                }
            }
        }
    }

    // Register parameters
    for (auto &param : node->getParams()) {
        Symbol sym;
        sym.name = param.name;
        sym.kind = Symbol::Kind::Parameter;
        if (param.isVariadic && param.type) {
            // Variadic param: type in scope is [T] (DynArray)
            auto arrType = std::make_unique<ArrayTypeRepr>(cloneTypeRepr(param.type.get()), -1);
            sym.type = arrType.get();
            variadicArrayTypes_.push_back(std::move(arrType));
        } else {
            sym.type = param.type.get();
        }
        sym.isMutable = param.isMutRef;
        sym.declLoc = param.location;
        scopes_.declare(sym.name, sym);
    }

    currentReturnType_ = node->getReturnType();

    if (node->getBody()) {
        visitBlockStmt(const_cast<BlockStmt *>(node->getBody()));
    }

    // Check that non-void functions always return a value
    if (node->getReturnType() && !node->getReturnType()->isVoid() &&
        !node->getReturnType()->isInferred() && node->getBody()) {
        if (!alwaysReturns(node->getBody())) {
            diag_.report(node->getStartLoc(), DiagID::err_no_return,
                         node->getName(), typeToString(node->getReturnType()));
        }
    }

    // warn_unused_variable: check all vars declared in this function
    for (auto &[varName, varLoc] : currentFuncVars_) {
        if (usedSymbols_.count(varName) == 0 &&
            forLoopVars_.count(varName) == 0) {
            diag_.report(varLoc, DiagID::warn_unused_variable, varName);
        }
    }

    // Restore previous unused-variable tracking state
    usedSymbols_ = std::move(prevUsedSymbols);
    currentFuncVars_ = std::move(prevFuncVars);
    forLoopVars_ = std::move(prevForLoopVars);

    currentReturnType_ = nullptr;
    currentIsAsync_ = prevIsAsync;
    currentIsGenerator_ = prevIsGenerator;
    currentTypeParamBounds_ = std::move(prevTypeParamBounds);
    scopes_.popScope();
}

void TypeChecker::visitVarDecl(VarDecl *node) {
    // Const declaration: compile-time constant
    if (node->isConst()) {
        if (!node->hasInit()) {
            diag_.report(node->getStartLoc(), DiagID::err_const_requires_init);
            return;
        }
        visit(const_cast<Expr *>(node->getInit()));
        auto constVal = evaluateConstExpr(node->getInit());
        if (!constVal) {
            diag_.report(node->getStartLoc(), DiagID::err_const_init_not_constant);
            return;
        }
        constValues_[node->getName()] = *constVal;

        Symbol sym;
        sym.name = node->getName();
        sym.kind = Symbol::Kind::Variable;
        sym.type = node->getType();
        sym.isMutable = false;
        sym.isConstant = true;
        sym.declLoc = node->getStartLoc();
        if ((!sym.type || sym.type->isInferred()) && node->getInit()->getResolvedType()) {
            sym.type = node->getInit()->getResolvedType();
        }
        if (!scopes_.declare(sym.name, sym)) {
            diag_.report(node->getStartLoc(), DiagID::err_redefinition, node->getName());
            auto *prev = scopes_.lookup(sym.name);
            if (prev && prev->declLoc.isValid())
                diag_.report(prev->declLoc, DiagID::note_previous_declaration, sym.name);
        }
        return;
    }

    // Tuple destructuring: let (x, y) = expr
    if (node->isDestructured()) {
        if (node->hasInit()) {
            visit(const_cast<Expr *>(node->getInit()));
            auto *initType = node->getInit()->getResolvedType();
            if (initType && initType->getKind() == TypeRepr::Kind::Tuple) {
                auto *tupleType = static_cast<const TupleTypeRepr *>(initType);
                if (tupleType->getArity() == node->getDestructuredNames().size()) {
                    for (size_t i = 0; i < node->getDestructuredNames().size(); ++i) {
                        Symbol sym;
                        sym.name = node->getDestructuredNames()[i];
                        sym.kind = Symbol::Kind::Variable;
                        sym.isMutable = node->isMutable();
                        sym.type = tupleType->getElements()[i].get();
                        sym.declLoc = node->getStartLoc();
                        scopes_.declare(sym.name, sym);
                    }
                } else {
                    diag_.report(node->getStartLoc(), DiagID::err_tuple_arity_mismatch,
                                 std::to_string(tupleType->getArity()),
                                 std::to_string(node->getDestructuredNames().size()));
                }
            } else if (initType) {
                // Non-tuple type assigned to destructuring pattern
                diag_.report(node->getStartLoc(), DiagID::err_type_mismatch,
                             "tuple", initType->toString());
            }
        }
        return;
    }

    // Propagate function type annotation to untyped closure params
    if (node->hasTypeAnnotation() && node->getType() &&
        node->getType()->getKind() == TypeRepr::Kind::Function &&
        node->hasInit() &&
        node->getInit()->getKind() == ASTNode::NodeKind::ClosureExpr) {
        auto *funcType = static_cast<const FunctionTypeRepr *>(node->getType());
        auto *closure = static_cast<ClosureExpr *>(const_cast<Expr *>(node->getInit()));
        for (size_t i = 0; i < closure->getParams().size() && i < funcType->getParams().size(); ++i) {
            if (!closure->getParams()[i].type) {
                closure->setParamType(i, cloneTypeRepr(funcType->getParams()[i].get()));
            }
        }
    }

    if (node->hasInit()) {
        visit(const_cast<Expr *>(node->getInit()));
    }

    if (node->hasInit() &&
        node->getInit()->getKind() == ASTNode::NodeKind::NilLiteralExpr) {
        if (!node->hasTypeAnnotation() ||
            node->getType()->getKind() != TypeRepr::Kind::Optional) {
            diag_.report(node->getStartLoc(), DiagID::err_nil_without_optional);
        }
    }

    Symbol sym;
    sym.name = node->getName();
    sym.kind = Symbol::Kind::Variable;
    sym.type = node->getType();
    sym.isMutable = node->isMutable();
    sym.declLoc = node->getStartLoc();

    // Propagate init's resolved type when annotation is inferred
    if ((!sym.type || sym.type->isInferred()) && node->hasInit() &&
        node->getInit()->getResolvedType()) {
        sym.type = node->getInit()->getResolvedType();
    }

    // Report if type cannot be inferred (no annotation, init didn't resolve)
    if (!sym.type && node->hasInit() && !node->getInit()->getResolvedType()
        && node->getInit()->getKind() != ASTNode::NodeKind::NilLiteralExpr) {
        diag_.report(node->getStartLoc(), DiagID::err_cannot_infer_type, node->getName());
    }

    // Object safety check for dyn Protocol annotations (independent of type mismatch)
    if (node->hasTypeAnnotation() && node->getType() &&
        node->getType()->getKind() == TypeRepr::Kind::DynProtocol) {
        auto *dynType = static_cast<const DynProtocolTypeRepr *>(node->getType());
        auto *protoSym = scopes_.lookup(dynType->getProtocolName());
        if (protoSym && protoSym->kind == Symbol::Kind::ProtocolType) {
            std::string unsafeMethod;
            if (!isObjectSafe(dynType->getProtocolName(), unsafeMethod)) {
                diag_.report(node->getStartLoc(), DiagID::err_protocol_not_object_safe,
                             dynType->getProtocolName(), unsafeMethod);
            }
        }
    }

    // Check type mismatch between annotation and init
    if (node->hasTypeAnnotation() && node->hasInit() &&
        node->getType() && node->getInit()->getResolvedType() &&
        !node->getType()->isInferred() &&
        !node->getInit()->getResolvedType()->isInferred()) {
        auto *annType = node->getType();
        auto *initType = node->getInit()->getResolvedType();
        if (!typesCompatible(annType, initType)) {
            bool compat = false;
            // Allow T → T? (optional wrapping)
            if (annType->getKind() == TypeRepr::Kind::Optional) {
                auto *optType = static_cast<const OptionalTypeRepr *>(annType);
                compat = typesCompatible(optType->getInner(), initType);
            }
            // Allow concrete → ref Protocol (trait object)
            if (annType->getKind() == TypeRepr::Kind::Reference) {
                compat = true;
            }
            // Allow concrete → dyn Protocol (trait object)
            if (annType->getKind() == TypeRepr::Kind::DynProtocol) {
                auto *dynType = static_cast<const DynProtocolTypeRepr *>(annType);
                auto *protoSym = scopes_.lookup(dynType->getProtocolName());
                if (protoSym && protoSym->kind == Symbol::Kind::ProtocolType) {
                    compat = true;
                } else {
                    diag_.report(node->getStartLoc(), DiagID::err_undefined_protocol,
                                 dynType->getProtocolName());
                    std::vector<std::string> protoCandidates;
                    scopes_.collectNames(Symbol::Kind::ProtocolType, protoCandidates);
                    suggestSimilar(node->getStartLoc(), dynType->getProtocolName(), protoCandidates);
                    compat = true;
                }
            }
            if (!compat) {
                diag_.report(node->getStartLoc(), DiagID::err_type_mismatch,
                             typeToString(annType), typeToString(initType));
            }
        }
    }

    // Check void variable
    if (sym.type && sym.type->isVoid()) {
        diag_.report(node->getStartLoc(), DiagID::err_void_variable);
    }

    // warn_shadowed_variable: check if outer scope has same name
    if (!node->getName().empty()) {
        auto *outerSym = scopes_.lookup(node->getName());
        if (outerSym && !scopes_.lookupLocal(node->getName())) {
            // Skip if outer symbol is a function, type, or protocol
            if (outerSym->kind != Symbol::Kind::Function &&
                outerSym->kind != Symbol::Kind::StructType &&
                outerSym->kind != Symbol::Kind::EnumType &&
                outerSym->kind != Symbol::Kind::ProtocolType &&
                outerSym->kind != Symbol::Kind::TypeAlias) {
                diag_.report(node->getStartLoc(), DiagID::warn_shadowed_variable,
                             node->getName());
            }
        }
    }

    if (!scopes_.declare(sym.name, sym)) {
        diag_.report(node->getStartLoc(), DiagID::err_redefinition, node->getName());
        auto *prev = scopes_.lookup(sym.name);
        if (prev && prev->declLoc.isValid())
            diag_.report(prev->declLoc, DiagID::note_previous_declaration, sym.name);
    }

    // Track variable for unused variable warnings
    if (!node->getName().empty() && node->getName()[0] != '_') {
        currentFuncVars_.push_back({node->getName(), node->getStartLoc()});
    }

    // Track File-typed variables for method resolution
    if (sym.type && sym.type->getKind() == TypeRepr::Kind::Named) {
        auto *named = static_cast<const NamedTypeRepr *>(sym.type);
        if (named->getName() == "File") {
            fileVariables_.insert(node->getName());
        }
    }
}

void TypeChecker::visitStructDecl(StructDecl *node) {
    scopes_.pushScope();
    for (const auto &tp : node->getTypeParams()) {
        Symbol sym;
        sym.name = tp;
        sym.kind = Symbol::Kind::TypeParam;
        sym.declLoc = node->getStartLoc();
        scopes_.declare(tp, sym);
    }
    // Validate trait bounds reference real protocols
    for (auto &[paramName, boundProtos] : node->getTypeParamBounds()) {
        for (auto &boundProto : boundProtos) {
            auto *protoSym = scopes_.lookup(boundProto);
            if (!protoSym || protoSym->kind != Symbol::Kind::ProtocolType) {
                diag_.report(node->getStartLoc(), DiagID::err_undefined_protocol, boundProto);
                std::vector<std::string> protoCandidates;
                scopes_.collectNames(Symbol::Kind::ProtocolType, protoCandidates);
                suggestSimilar(node->getStartLoc(), boundProto, protoCandidates);
            }
        }
    }
    for (auto &field : node->getFields()) {
        visitFieldDecl(field.get());
    }
    scopes_.popScope();
}

void TypeChecker::visitClassDecl(ClassDecl *node) {
    scopes_.pushScope();
    std::string prevClass = currentClassName_;
    currentClassName_ = node->getName();

    // Register type parameters
    for (const auto &tp : node->getTypeParams()) {
        Symbol sym;
        sym.name = tp;
        sym.kind = Symbol::Kind::TypeParam;
        sym.declLoc = node->getStartLoc();
        scopes_.declare(tp, sym);
    }

    // Validate parent class — may be reclassified as protocol
    if (node->hasParentClass()) {
        auto parentName = node->getParentClass();
        auto it = classDecls_.find(parentName);
        if (it == classDecls_.end()) {
            auto *parentSym = scopes_.lookup(parentName);
            if (parentSym && parentSym->kind == Symbol::Kind::ClassType &&
                parentSym->classDecl) {
                // Parent is a class imported from another module — register it
                // locally so inheritance, field, and method resolution can walk
                // the chain. (Imported ClassType symbols carry their ClassDecl.)
                classDecls_[parentName] = parentSym->classDecl;
                if (parentSym->classDecl->hasParentClass())
                    classParent_[parentName] = parentSym->classDecl->getParentClass();
                it = classDecls_.find(parentName);
            }
        }
        if (it == classDecls_.end()) {
            auto *parentSym = scopes_.lookup(parentName);
            if (parentSym && parentSym->kind == Symbol::Kind::StructType) {
                diag_.report(node->getStartLoc(), DiagID::err_class_inherits_nonclass,
                             node->getName(), parentName);
            } else if (parentSym && parentSym->kind == Symbol::Kind::ProtocolType) {
                // First name is a protocol, not a parent class — reclassify
                node->reclassifyParentAsProtocol();
                classParent_.erase(node->getName());
            } else {
                diag_.report(node->getStartLoc(), DiagID::err_class_parent_not_found,
                             parentName);
            }
        } else {
            // Check if parent is final
            if (it->second->isFinal()) {
                diag_.report(node->getStartLoc(), DiagID::err_class_final_inherit,
                             parentName);
            }
            // Check if parent is open (only open classes can be subclassed)
            // Internal/FilePrivate classes allowed same-module (permissive MVP)
            auto parentAcc = it->second->getAccess();
            if (parentAcc == AccessModifier::Public && !it->second->isFinal()) {
                diag_.report(node->getStartLoc(), DiagID::err_class_non_open_inherit,
                             parentName);
            }
            // Check for circular inheritance
            std::set<std::string> visited;
            std::string cur = node->getName();
            while (!cur.empty()) {
                if (visited.count(cur)) {
                    diag_.report(node->getStartLoc(), DiagID::err_class_circular_inheritance,
                                 node->getName());
                    break;
                }
                visited.insert(cur);
                auto pit = classParent_.find(cur);
                if (pit != classParent_.end())
                    cur = pit->second;
                else
                    cur.clear();
            }
        }
    }

    // Validate protocol conformance for class
    for (auto &protoName : node->getProtocols()) {
        auto *protoSym = scopes_.lookup(protoName);
        if (!protoSym || protoSym->kind != Symbol::Kind::ProtocolType) {
            diag_.report(node->getStartLoc(), DiagID::err_undefined_protocol, protoName);
            continue;
        }
        if (protoSym->protocolDecl) {
            // Check all required methods are implemented
            for (auto &protoMethod : protoSym->protocolDecl->getMethods()) {
                if (protoMethod->hasBody()) continue; // default impl
                bool found = false;
                for (auto *classMethod : node->getMethods()) {
                    if (classMethod->getName() == protoMethod->getName()) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    diag_.report(node->getStartLoc(),
                                 DiagID::err_missing_protocol_method,
                                 protoMethod->getName(), protoName);
                }
            }
        }
        protocolConformances_[protoName].push_back(node->getName());
    }

    // Register 'self' parameter in scope
    Symbol selfSym;
    selfSym.name = "self";
    selfSym.kind = Symbol::Kind::Parameter;
    selfSym.isMutable = true;
    scopes_.declare("self", selfSym);

    // Validate fields
    std::set<std::string> fieldNames;
    for (auto &m : node->getMembers()) {
        if (m.field) {
            if (fieldNames.count(m.field->getName())) {
                diag_.report(m.field->getStartLoc(), DiagID::err_class_duplicate_field,
                             node->getName(), m.field->getName());
            }
            fieldNames.insert(m.field->getName());
            // Check field type is valid
            if (m.field->getType() && m.field->getType()->getKind() == TypeRepr::Kind::Named) {
                auto *named = static_cast<const NamedTypeRepr *>(m.field->getType());
                auto *sym = scopes_.lookup(named->getName());
                if (!sym) {
                    diag_.reportRange(m.field->getStartLoc(),
                                      static_cast<uint32_t>(named->getName().size()),
                                      DiagID::err_undefined_type,
                                      named->getName());
                }
            }
        }
    }

    // Validate methods
    for (auto &m : node->getMembers()) {
        if (!m.method) continue;

        // Validate static + override conflict
        if (m.isStatic && m.isOverride) {
            diag_.report(m.method->getStartLoc(), DiagID::err_class_static_override,
                         m.method->getName());
        }

        // Validate override
        if (m.isOverride) {
            if (!node->hasParentClass()) {
                diag_.report(m.method->getStartLoc(),
                             DiagID::err_class_override_no_parent_method,
                             m.method->getName());
            } else {
                // Check parent has the method and signature matches
                bool foundInParent = false;
                const FuncDecl *parentMethod = nullptr;
                std::string cur = node->getParentClass();
                while (!cur.empty() && !foundInParent) {
                    auto pit = classDecls_.find(cur);
                    if (pit != classDecls_.end()) {
                        for (auto *pm : pit->second->getMethods()) {
                            if (pm->getName() == m.method->getName()) {
                                foundInParent = true;
                                parentMethod = pm;
                                break;
                            }
                        }
                        auto ppit = classParent_.find(cur);
                        cur = (ppit != classParent_.end()) ? ppit->second : "";
                    } else {
                        cur.clear();
                    }
                }
                if (!foundInParent) {
                    diag_.report(m.method->getStartLoc(),
                                 DiagID::err_class_override_no_parent_method,
                                 m.method->getName());
                } else if (parentMethod) {
                    // Check if parent method is final
                    bool isFinalMethod = false;
                    std::string pc = node->getParentClass();
                    while (!pc.empty() && !isFinalMethod) {
                        auto pcIt = classDecls_.find(pc);
                        if (pcIt != classDecls_.end()) {
                            for (auto &pm : pcIt->second->getMembers()) {
                                if (pm.method && pm.method->getName() == m.method->getName() && pm.isFinal) {
                                    isFinalMethod = true;
                                    break;
                                }
                            }
                            auto ppIt = classParent_.find(pc);
                            pc = (ppIt != classParent_.end()) ? ppIt->second : "";
                        } else break;
                    }
                    if (isFinalMethod) {
                        diag_.report(m.method->getStartLoc(),
                                     DiagID::err_class_final_override,
                                     m.method->getName());
                    }
                    // Check parameter count matches
                    bool sigMismatch =
                        m.method->getParams().size() != parentMethod->getParams().size();

                    // Check return type matches
                    if (!sigMismatch) {
                        auto *childRet = m.method->getReturnType();
                        auto *parentRet = parentMethod->getReturnType();
                        if (childRet && parentRet &&
                            !typesCompatible(parentRet, childRet)) {
                            sigMismatch = true;
                        }
                    }

                    // Check each parameter type matches
                    if (!sigMismatch) {
                        auto &cparams = m.method->getParams();
                        auto &pparams = parentMethod->getParams();
                        for (size_t pi = 0; pi < cparams.size(); ++pi) {
                            if (cparams[pi].type && pparams[pi].type &&
                                !typesCompatible(pparams[pi].type.get(),
                                                 cparams[pi].type.get())) {
                                sigMismatch = true;
                                break;
                            }
                        }
                    }

                    if (sigMismatch) {
                        diag_.report(m.method->getStartLoc(),
                                     DiagID::err_class_override_signature,
                                     m.method->getName());
                    }
                }
            }
        }

        // Validate deinit — should have no params (self is implicit)
        if (m.method->getName() == "deinit") {
            auto &params = m.method->getParams();
            if (!params.empty()) {
                diag_.report(m.method->getStartLoc(), DiagID::err_class_deinit_params);
            }
        }

        // Type-check method body — self is implicit in non-static class methods
        if (m.method->getBody()) {
            scopes_.pushScope();
            if (m.isStatic) {
                // Static methods don't have self — shadow it so uses get caught
                // (self is declared in outer class scope; don't re-declare here)
            }
            for (auto &param : m.method->getParams()) {
                Symbol paramSym;
                paramSym.name = param.name;
                paramSym.kind = Symbol::Kind::Parameter;
                paramSym.type = param.type.get();
                paramSym.isMutable = param.isMutRef;
                scopes_.declare(param.name, paramSym);
            }
            auto *prevRetType = currentReturnType_;
            currentReturnType_ = m.method->getReturnType();
            visitBlockStmt(const_cast<BlockStmt *>(m.method->getBody()));
            currentReturnType_ = prevRetType;
            scopes_.popScope();
        }
    }

    // Convenience init: skip init-all-fields check (delegates to designated init)
    bool hasConvenienceInit = false;
    for (auto &m : node->getMembers()) {
        if (m.method && m.method->getName() == "init" && m.isConvenienceInit) {
            hasConvenienceInit = true;
            break;
        }
    }

    // Validate init initializes all own fields (skip if convenience init — delegated)
    auto *initDecl = (hasConvenienceInit ? nullptr : node->getInit());
    if (initDecl && initDecl->getBody()) {
        auto ownFields = node->getFields();
        if (!ownFields.empty()) {
            // Collect field names assigned in init body via self.field = ...
            std::set<std::string> initializedFields;
            for (auto &stmt : initDecl->getBody()->getStatements()) {
                if (stmt->getKind() == ASTNode::NodeKind::ExprStmt) {
                    auto *exprStmt = static_cast<const ExprStmt *>(stmt.get());
                    auto *expr = exprStmt->getExpr();
                    if (expr && expr->getKind() == ASTNode::NodeKind::AssignExpr) {
                        auto *assign = static_cast<const AssignExpr *>(expr);
                        auto *target = assign->getTarget();
                        if (target && target->getKind() == ASTNode::NodeKind::MemberExpr) {
                            auto *member = static_cast<const MemberExpr *>(target);
                            if (member->getObject()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
                                auto *ident = static_cast<const IdentifierExpr *>(member->getObject());
                                if (ident->getName() == "self") {
                                    initializedFields.insert(member->getMember());
                                }
                            }
                        }
                    }
                }
            }
            // Collect lazy field names (skip them in init-all-fields check)
            std::set<std::string> lazyFields;
            for (auto &m : node->getMembers()) {
                if (m.isLazy && m.field) lazyFields.insert(m.field->getName());
            }
            // Check all own stored fields are initialized (skip computed & lazy properties)
            for (auto *field : ownFields) {
                if (field->isComputed()) continue;
                if (lazyFields.count(field->getName())) continue;
                if (!initializedFields.count(field->getName())) {
                    diag_.report(initDecl->getStartLoc(), DiagID::err_class_init_not_all_fields,
                                 node->getName());
                    break;
                }
            }
        }
    }

    currentClassName_ = prevClass;
    scopes_.popScope();
}

void TypeChecker::visitTypeAliasDecl(TypeAliasDecl *node) {
    // Validate that target type is a known type
    auto *target = node->getTargetType();
    if (target && target->getKind() == TypeRepr::Kind::Named) {
        auto *named = static_cast<const NamedTypeRepr *>(target);
        auto *sym = scopes_.lookup(named->getName());
        if (!sym) {
            diag_.reportRange(node->getStartLoc(),
                              static_cast<uint32_t>(named->getName().size()),
                              DiagID::err_undefined_type, named->getName());
            std::vector<std::string> typeCandidates;
            scopes_.collectNames(Symbol::Kind::StructType, typeCandidates);
            scopes_.collectNames(Symbol::Kind::EnumType, typeCandidates);
            scopes_.collectNames(Symbol::Kind::ProtocolType, typeCandidates);
            scopes_.collectNames(Symbol::Kind::TypeAlias, typeCandidates);
            suggestSimilar(node->getStartLoc(), named->getName(), typeCandidates);
        }
    }
}

void TypeChecker::visitEnumDecl(EnumDecl *node) {
    for (auto &c : node->getCases()) {
        visit(c.get());
    }
}

void TypeChecker::visitImplDecl(ImplDecl *node) {
    // Look up the type (struct, enum, or class)
    auto *sym = scopes_.lookup(node->getTypeName());
    if (!sym) {
        diag_.reportRange(node->getStartLoc(),
                          static_cast<uint32_t>(node->getTypeName().size()),
                          DiagID::err_undefined_type, node->getTypeName());
        std::vector<std::string> typeCandidates;
        scopes_.collectNames(Symbol::Kind::StructType, typeCandidates);
        scopes_.collectNames(Symbol::Kind::EnumType, typeCandidates);
        scopes_.collectNames(Symbol::Kind::ClassType, typeCandidates);
        suggestSimilar(node->getStartLoc(), node->getTypeName(), typeCandidates);
        return;
    }

    // Protocol conformance check
    if (node->hasProtocol()) {
        auto *protoSym = scopes_.lookup(node->getProtocolName());
        if (!protoSym || protoSym->kind != Symbol::Kind::ProtocolType) {
            diag_.report(node->getStartLoc(), DiagID::err_undefined_protocol,
                         node->getProtocolName());
            std::vector<std::string> protoCandidates;
            scopes_.collectNames(Symbol::Kind::ProtocolType, protoCandidates);
            suggestSimilar(node->getStartLoc(), node->getProtocolName(), protoCandidates);
        } else if (protoSym->protocolDecl) {
            for (auto &protoMethod : protoSym->protocolDecl->getMethods()) {
                bool found = false;
                for (auto &implMethod : node->getMethods()) {
                    if (implMethod->getName() == protoMethod->getName()) {
                        found = true;
                        break;
                    }
                }
                if (!found && !protoMethod->hasBody()) {
                    diag_.report(node->getStartLoc(),
                                 DiagID::err_missing_protocol_method,
                                 protoMethod->getName(), node->getProtocolName());
                }
            }
            // Check associated types are provided
            auto assocIt = protocolAssociatedTypes_.find(node->getProtocolName());
            if (assocIt != protocolAssociatedTypes_.end()) {
                for (auto &assocTypeName : assocIt->second) {
                    auto &implAssocTypes = node->getAssociatedTypes();
                    if (implAssocTypes.find(assocTypeName) == implAssocTypes.end()) {
                        diag_.report(node->getStartLoc(),
                                     DiagID::err_missing_associated_type,
                                     node->getTypeName(), assocTypeName,
                                     node->getProtocolName());
                    }
                }
            }
            // Record successful conformance
            protocolConformances_[node->getProtocolName()].push_back(node->getTypeName());

            // Record associated type resolutions
            for (auto &[assocName, concreteName] : node->getAssociatedTypes()) {
                std::string key = node->getTypeName() + "::" + node->getProtocolName() + "::" + assocName;
                implAssociatedTypeResolutions_[key] = concreteName;
            }

            // Iter protocol: extract element type from next() -> T?
            if (node->getProtocolName() == "Iterator") {
                for (auto &method : node->getMethods()) {
                    if (method->getName() == "next") {
                        auto *retType = method->getReturnType();
                        if (retType && retType->getKind() == TypeRepr::Kind::Optional) {
                            auto *optType = static_cast<const OptionalTypeRepr *>(retType);
                            iteratorItemTypes_[node->getTypeName()] = optType->getInner();
                        }
                        break;
                    }
                }
            }

            // AsyncIterator protocol: extract element type from next() -> T?
            if (node->getProtocolName() == "AsyncIterator") {
                for (auto &method : node->getMethods()) {
                    if (method->getName() == "next") {
                        auto *retType = method->getReturnType();
                        if (retType && retType->getKind() == TypeRepr::Kind::Optional) {
                            auto *optType = static_cast<const OptionalTypeRepr *>(retType);
                            asyncIteratorItemTypes_[node->getTypeName()] = optType->getInner();
                        }
                        break;
                    }
                }
            }

            // Drop protocol validation
            if (node->getProtocolName() == "Drop") {
                bool validDrop = false;
                for (auto &method : node->getMethods()) {
                    if (method->getName() == "drop" && method->isMethod() &&
                        method->getParams().size() == 1 &&
                        (!method->getReturnType() || method->getReturnType()->isVoid())) {
                        validDrop = true;
                    }
                }
                if (!validDrop && !node->getMethods().empty()) {
                    diag_.report(node->getStartLoc(), DiagID::err_drop_method_signature);
                }
            }
        }
    }

    scopes_.pushScope();
    std::string prevImplType = currentImplTypeName_;
    currentImplTypeName_ = node->getTypeName();
    for (const auto &tp : node->getTypeParams()) {
        Symbol tpSym;
        tpSym.name = tp;
        tpSym.kind = Symbol::Kind::TypeParam;
        scopes_.declare(tp, tpSym);
    }
    // Validate trait bounds reference real protocols
    for (auto &[paramName, boundProtos] : node->getTypeParamBounds()) {
        for (auto &boundProto : boundProtos) {
            auto *protoSym = scopes_.lookup(boundProto);
            if (!protoSym || protoSym->kind != Symbol::Kind::ProtocolType) {
                diag_.report(node->getStartLoc(), DiagID::err_undefined_protocol, boundProto);
                std::vector<std::string> protoCandidates;
                scopes_.collectNames(Symbol::Kind::ProtocolType, protoCandidates);
                suggestSimilar(node->getStartLoc(), boundProto, protoCandidates);
            }
        }
    }
    for (auto &method : node->getMethods()) {
        visitFuncDecl(method.get());
        // Record return type so Type.method(args) calls can be resolved later.
        if (method->getReturnType()) {
            std::string key = node->getTypeName() + "::" + method->getName();
            typeMethodReturnTypes_[key] = method->getReturnType();
        }
    }
    currentImplTypeName_ = prevImplType;
    scopes_.popScope();
}

void TypeChecker::visitProtocolDecl(ProtocolDecl *node) {
    // Record method names in order (for vtable index)
    std::vector<std::string> methodNames;
    for (auto &method : node->getMethods()) {
        methodNames.push_back(method->getName());
        // Type-check default method bodies
        if (method->hasBody()) {
            visitFuncDecl(method.get());
        }
        // Record protocol method return types so dyn Protocol call sites can
        // resolve the return type (e.g. `db.query(...)` where db: dyn Database).
        if (method->getReturnType()) {
            std::string key = node->getName() + "::" + method->getName();
            typeMethodReturnTypes_[key] = method->getReturnType();
        }
    }
    protocolMethods_[node->getName()] = std::move(methodNames);

    // Record associated type names for this protocol
    if (!node->getAssociatedTypes().empty()) {
        protocolAssociatedTypes_[node->getName()] = node->getAssociatedTypes();
    }

    // Record GAT param counts for conformance checking
    for (const auto &atDecl : node->getAssociatedTypeDecls()) {
        if (!atDecl.lifetimeParams.empty() || !atDecl.typeParams.empty()) {
            std::string key = node->getName() + "::" + atDecl.name;
            protocolGATParamCounts_[key] = {
                static_cast<int>(atDecl.lifetimeParams.size()),
                static_cast<int>(atDecl.typeParams.size())
            };
        }
    }
}

void TypeChecker::visitExprStmt(ExprStmt *node) { visit(node->getExpr()); }

void TypeChecker::visitReturnStmt(ReturnStmt *node) {
    if (node->hasValue()) {
        visit(node->getValue());
        // Check return type mismatch
        if (currentReturnType_ && node->getValue()->getResolvedType() &&
            !currentReturnType_->isInferred() &&
            !node->getValue()->getResolvedType()->isInferred()) {
            if (!typesCompatible(currentReturnType_, node->getValue()->getResolvedType())) {
                bool compat = false;
                // Allow T → T? (optional wrapping in return)
                if (currentReturnType_->getKind() == TypeRepr::Kind::Optional) {
                    auto *optType = static_cast<const OptionalTypeRepr *>(currentReturnType_);
                    compat = typesCompatible(optType->getInner(),
                                             node->getValue()->getResolvedType());
                }
                // Allow concrete → ref Protocol
                if (currentReturnType_->getKind() == TypeRepr::Kind::Reference) {
                    compat = true;
                }
                // Allow concrete → dyn Protocol
                if (currentReturnType_->getKind() == TypeRepr::Kind::DynProtocol) {
                    compat = true;
                }
                // Allow concrete → T when T is a generic type parameter
                if (!compat && currentReturnType_->getKind() == TypeRepr::Kind::Named) {
                    auto *named = static_cast<const NamedTypeRepr *>(currentReturnType_);
                    auto *sym = scopes_.lookup(named->getName());
                    if (sym && sym->kind == Symbol::Kind::TypeParam)
                        compat = true;
                }
                // Allow integer literal (i32) → wider integer type (i8/i16/i64/u8/u16/u32/u64).
                // Integer literals default to i32 but are inherently typeless and can
                // satisfy any integer return type without an explicit cast.
                if (!compat &&
                    node->getValue()->getKind() == ASTNode::NodeKind::IntegerLiteralExpr) {
                    auto retKind = currentReturnType_->getKind();
                    if (retKind == TypeRepr::Kind::I8  || retKind == TypeRepr::Kind::I16 ||
                        retKind == TypeRepr::Kind::I64 || retKind == TypeRepr::Kind::U8  ||
                        retKind == TypeRepr::Kind::U16 || retKind == TypeRepr::Kind::U32 ||
                        retKind == TypeRepr::Kind::U64)
                        compat = true;
                }
                if (!compat) {
                    diag_.report(node->getStartLoc(), DiagID::err_return_type_mismatch,
                                 typeToString(currentReturnType_),
                                 typeToString(node->getValue()->getResolvedType()));
                }
            }
        }
    }
}

void TypeChecker::visitIfStmt(IfStmt *node) {
    visit(const_cast<Expr *>(node->getCondition()));

    // Check condition is bool
    auto *condType = node->getCondition()->getResolvedType();
    if (condType && !condType->isInferred() && !condType->isBool()) {
        diag_.report(node->getCondition()->getStartLoc(), DiagID::err_condition_not_bool,
                     typeToString(condType));
    }

    visit(node->getThenBody());

    if (node->hasElse()) {
        visit(node->getElseBody());
    }
}

void TypeChecker::visitIfLetStmt(IfLetStmt *node) {
    visit(node->getOptionalExpr());

    scopes_.pushScope();
    Symbol sym;
    sym.name = node->getBindingName();
    sym.kind = Symbol::Kind::Variable;
    sym.isMutable = false;
    sym.declLoc = node->getStartLoc();

    // Default: unwrap the Optional<T> source's inner T as the binding's
    // type. This makes `b.length`, `b[i]`, member access, etc. work the
    // same way they do for a let-bound `T`. Special cases below override
    // (e.g. File-typed bindings).
    if (node->getOptionalExpr()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
        auto *ident = static_cast<IdentifierExpr *>(node->getOptionalExpr());
        auto *optSym = scopes_.lookup(ident->getName());
        if (optSym && optSym->type &&
            optSym->type->getKind() == TypeRepr::Kind::Optional) {
            auto *optTy = static_cast<const OptionalTypeRepr *>(optSym->type);
            sym.type = optTy->getInner();
        }
    } else if (auto *resolved = node->getOptionalExpr()->getResolvedType()) {
        if (resolved->getKind() == TypeRepr::Kind::Optional) {
            auto *optTy = static_cast<const OptionalTypeRepr *>(resolved);
            sym.type = optTy->getInner();
        }
    }

    // If unwrapping File.open() result, mark binding as File-typed
    if (node->getOptionalExpr()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
        auto *ident = static_cast<IdentifierExpr *>(node->getOptionalExpr());
        auto *optSym = scopes_.lookup(ident->getName());
        if (optSym && optSym->type &&
            optSym->type->getKind() == TypeRepr::Kind::Optional) {
            auto *optTy = static_cast<const OptionalTypeRepr *>(optSym->type);
            if (optTy->getInner()->getKind() == TypeRepr::Kind::Named) {
                auto *namedInner = static_cast<const NamedTypeRepr *>(optTy->getInner());
                if (namedInner->getName() == "File") {
                    fileVariables_.insert(node->getBindingName());
                }
            }
        }
    }
    // Also handle direct File.open() call: if let file = File.open(...)
    if (node->getOptionalExpr()->getKind() == ASTNode::NodeKind::CallExpr) {
        auto *callExpr = static_cast<CallExpr *>(node->getOptionalExpr());
        if (callExpr->getCallee()->getKind() == ASTNode::NodeKind::MemberExpr) {
            auto *me = static_cast<MemberExpr *>(callExpr->getCallee());
            if (me->getObject()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
                auto *id = static_cast<IdentifierExpr *>(me->getObject());
                if (id->getName() == "File" && me->getMember() == "open") {
                    fileVariables_.insert(node->getBindingName());
                }
            }
        }
    }

    scopes_.declare(sym.name, sym);
    visitBlockStmt(node->getThenBody());
    scopes_.popScope();

    if (node->hasElse()) {
        visit(node->getElseBody());
    }
}

void TypeChecker::visitWhileStmt(WhileStmt *node) {
    visit(const_cast<Expr *>(node->getCondition()));

    // Check condition is bool
    auto *condType = node->getCondition()->getResolvedType();
    if (condType && !condType->isInferred() && !condType->isBool()) {
        diag_.report(node->getCondition()->getStartLoc(), DiagID::err_condition_not_bool,
                     typeToString(condType));
    }

    ++loopDepth_;
    visit(const_cast<ASTNode *>(node->getBody()));
    --loopDepth_;
}

void TypeChecker::visitWhileLetStmt(WhileLetStmt *node) {
    visit(node->getOptionalExpr());

    scopes_.pushScope();
    Symbol sym;
    sym.name = node->getBindingName();
    sym.kind = Symbol::Kind::Variable;
    sym.isMutable = false;
    sym.declLoc = node->getStartLoc();

    // Unwrap optional type for binding — look up from scope, not resolvedType
    if (node->getOptionalExpr()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
        auto *ident = static_cast<IdentifierExpr *>(node->getOptionalExpr());
        auto *optSym = scopes_.lookup(ident->getName());
        if (optSym && optSym->type &&
            optSym->type->getKind() == TypeRepr::Kind::Optional) {
            auto *optTy = static_cast<const OptionalTypeRepr *>(optSym->type);
            sym.type = optTy->getInner();
        }
    }

    scopes_.declare(sym.name, sym);
    ++loopDepth_;
    visitBlockStmt(node->getBody());
    --loopDepth_;
    scopes_.popScope();
}

void TypeChecker::visitForStmt(ForStmt *node) {
    // Validate for await context
    if (node->isAwait() && !currentIsAsync_) {
        diag_.report(node->getStartLoc(), DiagID::err_for_await_outside_async);
    }

    visit(const_cast<Expr *>(node->getIterable()));

    scopes_.pushScope();

    // Mark for-in loop variables as exempt from unused warnings
    forLoopVars_.insert(node->getVarName());
    if (node->hasTuplePattern()) {
        forLoopVars_.insert(node->getVarName2());
    }

    // Determine element type from iterable
    const TypeRepr *iterableType = nullptr;

    // If iterable is an identifier, look up its declared type
    if (node->getIterable()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
        auto *ident = static_cast<const IdentifierExpr *>(node->getIterable());
        auto *sym = scopes_.lookup(ident->getName());
        if (sym && sym->type) {
            iterableType = sym->type;
        }
    }
    // Also check resolved type from visit
    if (!iterableType && node->getIterable()->getResolvedType()) {
        iterableType = node->getIterable()->getResolvedType();
    }

    if (node->hasTuplePattern()) {
        // Tuple pattern: (k, v) in map — must be Map<K,V>
        bool isMap = false;
        if (iterableType && iterableType->getKind() == TypeRepr::Kind::Generic) {
            auto *genType = static_cast<const GenericTypeRepr *>(iterableType);
            if (genType->getBaseName() == "Map" && genType->getTypeArgs().size() >= 2) {
                isMap = true;
                Symbol sym1;
                sym1.name = node->getVarName();
                sym1.kind = Symbol::Kind::Variable;
                sym1.isMutable = false;
                sym1.type = genType->getTypeArgs()[0].get();
                sym1.declLoc = node->getStartLoc();
                scopes_.declare(sym1.name, sym1);

                Symbol sym2;
                sym2.name = node->getVarName2();
                sym2.kind = Symbol::Kind::Variable;
                sym2.isMutable = false;
                sym2.type = genType->getTypeArgs()[1].get();
                sym2.declLoc = node->getStartLoc();
                scopes_.declare(sym2.name, sym2);
            }
        }
        if (!isMap) {
            diag_.report(node->getStartLoc(), DiagID::err_tuple_for_requires_map);
            // Still declare variables to avoid cascading errors
            Symbol sym1;
            sym1.name = node->getVarName();
            sym1.kind = Symbol::Kind::Variable;
            sym1.isMutable = false;
            sym1.declLoc = node->getStartLoc();
            scopes_.declare(sym1.name, sym1);
            Symbol sym2;
            sym2.name = node->getVarName2();
            sym2.kind = Symbol::Kind::Variable;
            sym2.isMutable = false;
            sym2.declLoc = node->getStartLoc();
            scopes_.declare(sym2.name, sym2);
        }
    } else {
        // Single variable pattern
        Symbol sym;
        sym.name = node->getVarName();
        sym.kind = Symbol::Kind::Variable;
        sym.isMutable = false;
        sym.declLoc = node->getStartLoc();

        if (iterableType) {
            // Dynamic array [T] → element type T
            if (iterableType->getKind() == TypeRepr::Kind::Array) {
                auto *arrType = static_cast<const ArrayTypeRepr *>(iterableType);
                if (arrType->isDynamic()) {
                    sym.type = arrType->getElement();
                }
            }
            // Map<K,V> → key type K
            else if (iterableType->getKind() == TypeRepr::Kind::Generic) {
                auto *genType = static_cast<const GenericTypeRepr *>(iterableType);
                if (genType->getBaseName() == "Map" && genType->getTypeArgs().size() >= 1) {
                    sym.type = genType->getTypeArgs()[0].get();
                } else if (genType->getBaseName() == "Set" && genType->getTypeArgs().size() >= 1) {
                    sym.type = genType->getTypeArgs()[0].get();
                } else if (genType->getBaseName() == "Generator" && genType->getTypeArgs().size() >= 1) {
                    // Generator<T>: bind loop var to T (yielded value type).
                    // Mirrors the wrap site at TypeChecker.cpp ~line 2697.
                    sym.type = genType->getTypeArgs()[0].get();
                }
            }
            // Custom Iterator (Iter protocol): Named type → check conformance
            else if (iterableType->getKind() == TypeRepr::Kind::Named) {
                auto *namedType = static_cast<const NamedTypeRepr *>(iterableType);
                const std::string &typeName = namedType->getName();
                bool foundConformance = false;

                if (node->isAwait()) {
                    // for-await: check AsyncIterator conformance
                    auto asyncConfIt = protocolConformances_.find("AsyncIterator");
                    if (asyncConfIt != protocolConformances_.end()) {
                        for (auto &t : asyncConfIt->second) {
                            if (t == typeName) {
                                foundConformance = true;
                                auto elemIt = asyncIteratorItemTypes_.find(typeName);
                                if (elemIt != asyncIteratorItemTypes_.end()) {
                                    sym.type = elemIt->second;
                                }
                                break;
                            }
                        }
                    }
                    if (!foundConformance) {
                        diag_.report(node->getStartLoc(),
                                     DiagID::err_for_await_requires_async_iterator, typeName);
                    }
                } else {
                    // for-in: check Iterator conformance.
                    // (a) Concrete named type: look up in protocolConformances_.
                    auto confIt = protocolConformances_.find("Iterator");
                    if (confIt != protocolConformances_.end()) {
                        for (auto &t : confIt->second) {
                            if (t == typeName) {
                                foundConformance = true;
                                auto elemIt = iteratorItemTypes_.find(typeName);
                                if (elemIt != iteratorItemTypes_.end()) {
                                    sym.type = elemIt->second;
                                }
                                break;
                            }
                        }
                    }
                    // (b) Generic type param (e.g. `I` in `func sum<I>(iter: I) where I: Iterator`):
                    // the name is a type parameter rather than a concrete conformer.
                    // Accept it if the current function declared an Iterator bound for it.
                    if (!foundConformance) {
                        auto boundsIt = currentTypeParamBounds_.find(typeName);
                        if (boundsIt != currentTypeParamBounds_.end()) {
                            for (auto &bp : boundsIt->second) {
                                if (bp == "Iterator") { foundConformance = true; break; }
                            }
                        }
                    }
                    // Sync for-in over a named type that does not conform to Iterator
                    // is a type error.
                    if (!foundConformance) {
                        diag_.report(node->getStartLoc(),
                                     DiagID::err_for_in_not_iterable, typeName);
                    }
                }
            }
        }

        scopes_.declare(sym.name, sym);
    }

    ++loopDepth_;
    visit(const_cast<ASTNode *>(node->getBody()));
    --loopDepth_;

    scopes_.popScope();
}

void TypeChecker::visitBlockStmt(BlockStmt *node) {
    scopes_.pushScope();
    bool seenTerminator = false;
    std::string terminatorName;
    for (size_t i = 0; i < node->getStatements().size(); ++i) {
        auto &stmt = node->getStatements()[i];
        if (seenTerminator) {
            // Emit warning only on the first unreachable statement
            diag_.report(stmt->getStartLoc(), DiagID::warn_unreachable_code,
                         terminatorName);
            // Still visit remaining statements for other diagnostics, but no more
            // unreachable warnings
            for (size_t j = i; j < node->getStatements().size(); ++j) {
                visit(node->getStatements()[j].get());
            }
            break;
        }
        visit(stmt.get());
        // Check if this statement is a terminator
        auto kind = stmt->getKind();
        if (kind == ASTNode::NodeKind::ReturnStmt) {
            seenTerminator = true;
            terminatorName = "return";
        } else if (kind == ASTNode::NodeKind::BreakStmt) {
            seenTerminator = true;
            terminatorName = "break";
        } else if (kind == ASTNode::NodeKind::ContinueStmt) {
            seenTerminator = true;
            terminatorName = "continue";
        }
    }
    scopes_.popScope();
}

void TypeChecker::visitBreakStmt(BreakStmt *node) {
    if (loopDepth_ == 0) {
        diag_.report(node->getStartLoc(), DiagID::err_break_outside_loop);
    }
}

void TypeChecker::visitContinueStmt(ContinueStmt *node) {
    if (loopDepth_ == 0) {
        diag_.report(node->getStartLoc(), DiagID::err_continue_outside_loop);
    }
}

void TypeChecker::visitIntegerLiteralExpr(IntegerLiteralExpr *node) {
    node->setResolvedType(makeI32Type());
}

void TypeChecker::visitFloatLiteralExpr(FloatLiteralExpr *node) {
    node->setResolvedType(makeF64Type());
}

void TypeChecker::visitBoolLiteralExpr(BoolLiteralExpr *node) {
    node->setResolvedType(makeBoolType());
}

void TypeChecker::visitStringLiteralExpr(StringLiteralExpr *node) {
    node->setResolvedType(makeStringType());
}

void TypeChecker::visitNilLiteralExpr(NilLiteralExpr *) {
    // nil type is resolved contextually
}

void TypeChecker::visitIdentifierExpr(IdentifierExpr *node) {
    // Track usage for unused variable warnings
    usedSymbols_.insert(node->getName());

    // 'super' is valid inside class methods when the class has a parent
    if (node->getName() == "super") {
        if (!currentClassName_.empty()) {
            auto pit = classParent_.find(currentClassName_);
            if (pit != classParent_.end() && !pit->second.empty()) {
                node->setResolvedType(makeNamedType(pit->second));
                return;
            }
        }
    }

    auto *sym = scopes_.lookup(node->getName());
    if (!sym) {
        // Result is a built-in type constructor, not a declared identifier
        if (node->getName() == "Result") return;
        // Stack and Queue are stdlib generic structs (collections::collections).
        // When no ModuleLoader is present (e.g. unit-test check() helpers) the
        // import is silently skipped; treat these names as valid type constructors
        // so that `var s: Stack<i64> = Stack.new()` does not produce a spurious
        // err_undeclared_identifier — the variable's type comes from the annotation.
        if (node->getName() == "Stack" || node->getName() == "Queue") return;
        diag_.reportRangeLabel(node->getStartLoc(),
                               static_cast<uint32_t>(node->getName().size()),
                               "not found in this scope",
                               DiagID::err_undeclared_identifier, node->getName());
        // "Did you mean?" suggestion
        std::vector<std::string> candidates;
        scopes_.collectAllNames(candidates);
        suggestSimilar(node->getStartLoc(), node->getName(), candidates);
        return;
    }

    if (sym->type) {
        if (sym->type->getKind() == TypeRepr::Kind::Named) {
            auto *namedType = static_cast<const NamedTypeRepr *>(sym->type);
            node->setResolvedType(makeNamedType(namedType->getName()));
        } else if (sym->type->getKind() == TypeRepr::Kind::Tuple ||
                   sym->type->getKind() == TypeRepr::Kind::Optional ||
                   sym->type->getKind() == TypeRepr::Kind::Array ||
                   sym->type->getKind() == TypeRepr::Kind::Function ||
                   sym->type->getKind() == TypeRepr::Kind::Result ||
                   sym->type->getKind() == TypeRepr::Kind::Reference ||
                   sym->type->getKind() == TypeRepr::Kind::Generic) {
            node->setResolvedType(cloneTypeRepr(sym->type));
        } else {
            node->setResolvedType(makePrimitiveType(sym->type->getKind()));
        }
    }
}

namespace {
struct OpProtoInfo { const char *proto; const char *method; };
const OpProtoInfo *getOpProto(BinaryExpr::Op op) {
    static const std::pair<BinaryExpr::Op, OpProtoInfo> table[] = {
        {BinaryExpr::Op::Add,       {"Add", "add"}},
        {BinaryExpr::Op::Sub,       {"Sub", "sub"}},
        {BinaryExpr::Op::Mul,       {"Mul", "mul"}},
        {BinaryExpr::Op::Div,       {"Div", "div"}},
        {BinaryExpr::Op::Mod,       {"Mod", "mod"}},
        {BinaryExpr::Op::Eq,        {"Eq", "eq"}},
        {BinaryExpr::Op::NotEq,     {"Eq", "eq"}},
        {BinaryExpr::Op::Less,      {"Less", "less"}},
        {BinaryExpr::Op::LessEq,    {"Less", "less"}},
        {BinaryExpr::Op::Greater,   {"Less", "less"}},
        {BinaryExpr::Op::GreaterEq, {"Less", "less"}},
    };
    for (auto &[o, info] : table)
        if (o == op) return &info;
    return nullptr;
}
} // anon

void TypeChecker::visitBinaryExpr(BinaryExpr *node) {
    visit(node->getLHS());
    visit(node->getRHS());

    // Struct operator overload dispatch
    auto *lhsType = node->getLHS()->getResolvedType();
    if (lhsType && lhsType->getKind() == TypeRepr::Kind::Named) {
        auto *named = static_cast<const NamedTypeRepr *>(lhsType);
        const std::string &typeName = named->getName();
        auto *info = getOpProto(node->getOp());
        if (info) {
            bool conforms = false;
            auto confIt = protocolConformances_.find(std::string(info->proto));
            if (confIt != protocolConformances_.end()) {
                for (auto &t : confIt->second)
                    if (t == typeName) { conforms = true; break; }
            }
            if (conforms) {
                switch (node->getOp()) {
                case BinaryExpr::Op::Eq: case BinaryExpr::Op::NotEq:
                case BinaryExpr::Op::Less: case BinaryExpr::Op::LessEq:
                case BinaryExpr::Op::Greater: case BinaryExpr::Op::GreaterEq:
                    node->setResolvedType(makeBoolType());
                    break;
                default:
                    node->setResolvedType(makeNamedType(typeName));
                    break;
                }
                return;
            }
            diag_.report(node->getStartLoc(), DiagID::err_binary_op_on_struct,
                         node->getOpSpelling(), typeName, info->proto);
            return;
        }
    }

    // Result type depends on operator
    switch (node->getOp()) {
    case BinaryExpr::Op::Eq:
    case BinaryExpr::Op::NotEq:
    case BinaryExpr::Op::Less:
    case BinaryExpr::Op::LessEq:
    case BinaryExpr::Op::Greater:
    case BinaryExpr::Op::GreaterEq:
    case BinaryExpr::Op::And:
    case BinaryExpr::Op::Or:
        node->setResolvedType(makeBoolType());
        break;
    case BinaryExpr::Op::NilCoalesce:
        if (node->getRHS()->getResolvedType()) {
            node->setResolvedType(
                makePrimitiveType(node->getRHS()->getResolvedType()->getKind()));
        }
        break;
    default:
        // Arithmetic: result type matches operand types
        if (node->getLHS()->getResolvedType()) {
            if (node->getLHS()->getResolvedType()->getKind() == TypeRepr::Kind::String) {
                node->setResolvedType(makeStringType());
            } else {
                node->setResolvedType(
                    makePrimitiveType(node->getLHS()->getResolvedType()->getKind()));
            }
        }
        break;
    }
}

void TypeChecker::visitUnaryExpr(UnaryExpr *node) {
    visit(node->getOperand());
    if (node->getOp() == UnaryExpr::Op::Not) {
        node->setResolvedType(makeBoolType());
    } else if (node->getOperand()->getResolvedType()) {
        node->setResolvedType(
            makePrimitiveType(node->getOperand()->getResolvedType()->getKind()));
    }
}

void TypeChecker::visitCallExpr(CallExpr *node) {
    visit(node->getCallee());
    propagateClosureParamTypes(node);
    propagateDynArrayClosureTypes(node);
    checkCallArgCount(node);

    for (auto &arg : node->getArgs()) {
        visit(arg.get());
    }

    resolveCallReturnType(node);
    resolveMapSetMethodCall(node);
}

void TypeChecker::visitMemberExpr(MemberExpr *node) {
    visit(node->getObject());

    // Tuple element access: tuple.0, tuple.1
    auto *baseType = node->getObject()->getResolvedType();
    if (baseType && baseType->getKind() == TypeRepr::Kind::Tuple) {
        auto *tupleType = static_cast<const TupleTypeRepr *>(baseType);
        const auto &member = node->getMember();
        // Check if member is a numeric index
        bool isNumeric = !member.empty();
        for (char c : member) {
            if (c < '0' || c > '9') { isNumeric = false; break; }
        }
        if (isNumeric) {
            long idx = strtol(member.c_str(), nullptr, 10);
            if (idx >= 0 && (size_t)idx < tupleType->getArity()) {
                node->setResolvedType(cloneTypeRepr(tupleType->getElements()[idx].get()));
            } else {
                diag_.report(node->getStartLoc(), DiagID::err_tuple_index_out_of_range,
                             member, std::to_string(tupleType->getArity()));
            }
            return;
        }
    }

    // Struct/class field access: resolve field type from StructDecl/ClassDecl
    if (!node->getResolvedType()) {
        // Determine the struct/class name from the base object's resolved type
        std::string typeName;
        if (baseType && baseType->getKind() == TypeRepr::Kind::Named) {
            typeName = static_cast<const NamedTypeRepr *>(baseType)->getName();
        } else if (!baseType && node->getObject()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
            auto *ident = static_cast<IdentifierExpr *>(node->getObject());
            if (ident->getName() == "self" && !currentImplTypeName_.empty()) {
                typeName = currentImplTypeName_;
            }
        }
        if (!typeName.empty()) {
            auto *typeSym = scopes_.lookup(typeName);
            if (typeSym && typeSym->kind == Symbol::Kind::StructType && typeSym->structDecl) {
                for (auto &field : typeSym->structDecl->getFields()) {
                    if (field->getName() == node->getMember()) {
                        node->setResolvedType(cloneTypeRepr(field->getType()));
                        break;
                    }
                }
            } else if (typeSym && typeSym->kind == Symbol::Kind::ClassType && typeSym->classDecl) {
                for (auto *field : typeSym->classDecl->getFields()) {
                    if (field->getName() == node->getMember()) {
                        node->setResolvedType(cloneTypeRepr(field->getType()));
                        break;
                    }
                }
            }
        }
    }

    // string.length → i64 (UTF-8 code point count)
    if (node->getObject()->getResolvedType() &&
        node->getObject()->getResolvedType()->getKind() == TypeRepr::Kind::String &&
        node->getMember() == "length") {
        node->setResolvedType(makeI64Type());
    }

    // string.byteLength → i64 (byte count)
    if (node->getObject()->getResolvedType() &&
        node->getObject()->getResolvedType()->getKind() == TypeRepr::Kind::String &&
        node->getMember() == "byteLength") {
        node->setResolvedType(makeI64Type());
    }

    // DynArray .length → i64, .isEmpty → bool
    if (node->getObject()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
        auto *ident = static_cast<IdentifierExpr *>(node->getObject());
        auto *sym = scopes_.lookup(ident->getName());
        if (sym && sym->type && sym->type->getKind() == TypeRepr::Kind::Array) {
            auto *arrType = static_cast<const ArrayTypeRepr *>(sym->type);
            if (arrType->isDynamic()) {
                if (node->getMember() == "length") {
                    node->setResolvedType(makeI64Type());
                } else if (node->getMember() == "isEmpty") {
                    node->setResolvedType(makeBoolType());
                }
            }
        }
    }

    // Map/Set .size → i64, .isEmpty → bool
    if (node->getObject()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
        auto *ident = static_cast<IdentifierExpr *>(node->getObject());
        auto *sym = scopes_.lookup(ident->getName());
        if (sym && sym->type && sym->type->getKind() == TypeRepr::Kind::Generic) {
            auto *genType = static_cast<const GenericTypeRepr *>(sym->type);
            if (genType->getBaseName() == "Map" || genType->getBaseName() == "Set") {
                if (node->getMember() == "size") {
                    node->setResolvedType(makeI64Type());
                } else if (node->getMember() == "isEmpty") {
                    node->setResolvedType(makeBoolType());
                }
            }
        }
    }

    // Result.isOk / Result.isErr → bool
    if (node->getMember() == "isOk" || node->getMember() == "isErr") {
        node->setResolvedType(makeBoolType());
    }

    // Result.ok / Result.err — static constructor access (no error)
    if (node->getObject()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
        auto *ident = static_cast<IdentifierExpr *>(node->getObject());
        if (ident->getName() == "Result") {
            // Result.ok or Result.err — accepted
        }
    }

    // Class private member access check
    if (node->getObject()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
        auto *ident = static_cast<IdentifierExpr *>(node->getObject());
        auto *sym = scopes_.lookup(ident->getName());
        if (sym && ident->getName() != "self") {
            // Determine the class type of the variable
            std::string varClassName;
            if (sym->type && sym->type->getKind() == TypeRepr::Kind::Named) {
                auto *named = static_cast<const NamedTypeRepr *>(sym->type);
                if (classDecls_.count(named->getName()))
                    varClassName = named->getName();
            }
            // If no explicit type, check all classes (backward compat)
            if (varClassName.empty()) {
                for (auto &[cn, privMembers] : classPrivateMembers_) {
                    if (privMembers.count(node->getMember()) && cn != currentClassName_) {
                        diag_.report(node->getStartLoc(), DiagID::err_class_private_access,
                                     node->getMember(), cn);
                        break;
                    }
                }
            } else if (varClassName != currentClassName_) {
                auto pit = classPrivateMembers_.find(varClassName);
                if (pit != classPrivateMembers_.end() && pit->second.count(node->getMember())) {
                    diag_.report(node->getStartLoc(), DiagID::err_class_private_access,
                                 node->getMember(), varClassName);
                }
            }
        }
    }

    // Optional chaining: wrap resolved type in Optional
    if (node->isOptionalChain() && node->getResolvedType()) {
        auto optType = std::make_unique<OptionalTypeRepr>(
            cloneTypeRepr(node->getResolvedType()));
        node->setResolvedType(std::move(optType));
    }
}

void TypeChecker::visitIndexExpr(IndexExpr *node) {
    visit(const_cast<Expr *>(node->getBase()));
    visit(const_cast<Expr *>(node->getIndex()));

    // String indexing/slicing: s[i] -> string, s[1..3] -> string
    if (node->getBase()->getResolvedType() &&
        node->getBase()->getResolvedType()->getKind() == TypeRepr::Kind::String) {
        node->setResolvedType(makePrimitiveType(TypeRepr::Kind::String));
    }

    // Array slicing: arr[1..3] -> same array type
    if (node->getIndex()->getKind() == ASTNode::NodeKind::RangeExpr &&
        node->getBase()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
        auto *ident = static_cast<IdentifierExpr *>(const_cast<Expr *>(node->getBase()));
        auto *sym = scopes_.lookup(ident->getName());
        if (sym && sym->type && sym->type->getKind() == TypeRepr::Kind::Array) {
            node->setResolvedType(cloneTypeRepr(sym->type));
        }
    }

    // Array element access: arr[i] -> element type T for dynamic arrays,
    // but ONLY when the element is a Named struct (Row/Date/Time/DateTime/etc).
    // Without this, `let x = arr[i]` leaves x untyped, which breaks chained
    // method calls like `arr[i].method().method()` (they silently emit nothing).
    // Restricting to Named avoids assigning a resolved type to primitive-element
    // arrays like [u8]/[i32]; doing so changed byte sign/zero-extension and broke
    // self-hosted gzip (0x8B read as signed -117 instead of unsigned 139).
    if (!node->getResolvedType() &&
        node->getIndex()->getKind() != ASTNode::NodeKind::RangeExpr &&
        node->getBase()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
        auto *ident = static_cast<IdentifierExpr *>(const_cast<Expr *>(node->getBase()));
        auto *sym = scopes_.lookup(ident->getName());
        if (sym && sym->type && sym->type->getKind() == TypeRepr::Kind::Array) {
            auto *arrType = static_cast<const ArrayTypeRepr *>(sym->type);
            if (arrType->isDynamic() && arrType->getElement() &&
                arrType->getElement()->getKind() == TypeRepr::Kind::Named)
                node->setResolvedType(cloneTypeRepr(arrType->getElement()));
        }
    }

    // Struct/class subscript: base[idx] -> the `subscript` method's return type.
    // Fires ONLY when the base is an identifier of a Named struct/class type that
    // actually declares a `subscript` method (recorded in typeMethodReturnTypes_
    // from its impl block / class body). Strictly guarded by !getResolvedType() so
    // String-indexing and array-element handling above remain untouched.
    if (!node->getResolvedType() &&
        node->getBase()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
        auto *ident = static_cast<IdentifierExpr *>(const_cast<Expr *>(node->getBase()));
        auto *sym = scopes_.lookup(ident->getName());
        if (sym && sym->type && sym->type->getKind() == TypeRepr::Kind::Named) {
            auto *named = static_cast<const NamedTypeRepr *>(sym->type);
            std::string key = named->getName() + "::subscript";
            auto retIt = typeMethodReturnTypes_.find(key);
            if (retIt != typeMethodReturnTypes_.end() && retIt->second)
                node->setResolvedType(cloneTypeRepr(retIt->second));
        }
    }
}

void TypeChecker::visitAssignExpr(AssignExpr *node) {
    visit(node->getTarget());
    visit(node->getValue());

    // Check mutability
    if (node->getTarget()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
        auto *ident = static_cast<IdentifierExpr *>(node->getTarget());
        auto *sym = scopes_.lookup(ident->getName());
        if (sym && !sym->isMutable) {
            diag_.reportRange(node->getStartLoc(),
                              static_cast<uint32_t>(ident->getName().size()),
                              DiagID::err_assign_to_immutable,
                              ident->getName());
            diag_.reportHelp(node->getStartLoc(),
                             static_cast<uint32_t>(ident->getName().size()),
                             "declare with 'var' instead of 'let' to make it mutable",
                             "", DiagID::note_use_var_for_mutable);
        }
    }
}

void TypeChecker::visitStructLiteralExpr(StructLiteralExpr *node) {
    auto *sym = scopes_.lookup(node->getTypeName());
    // Resolve type alias to underlying struct
    if (sym && sym->kind == Symbol::Kind::TypeAlias && sym->aliasTarget) {
        if (sym->aliasTarget->getKind() == TypeRepr::Kind::Named) {
            auto *named = static_cast<const NamedTypeRepr *>(sym->aliasTarget);
            sym = scopes_.lookup(named->getName());
        }
    }
    if (!sym || sym->kind != Symbol::Kind::StructType) {
        diag_.reportRange(node->getStartLoc(),
                          static_cast<uint32_t>(node->getTypeName().size()),
                          DiagID::err_undefined_type, node->getTypeName());
        std::vector<std::string> typeCandidates;
        scopes_.collectNames(Symbol::Kind::StructType, typeCandidates);
        suggestSimilar(node->getStartLoc(), node->getTypeName(), typeCandidates);
        return;
    }

    for (auto &field : node->getFields()) {
        visit(field.value.get());
    }

    // Infer type bindings from field values (for generic structs)
    std::unordered_map<std::string, const TypeRepr *> typeBindings;
    if (sym->structDecl && sym->structDecl->isGeneric()) {
        const auto &typeParams = sym->structDecl->getTypeParams();
        const auto &fields = sym->structDecl->getFields();
        // Map field name -> declared type for unordered struct literal lookup
        std::unordered_map<std::string, const TypeRepr *> declaredFieldTypes;
        for (auto &fd : fields) {
            declaredFieldTypes[fd->getName()] = fd->getType();
        }
        for (auto &litField : node->getFields()) {
            auto it = declaredFieldTypes.find(litField.name);
            if (it == declaredFieldTypes.end()) continue;
            const TypeRepr *fieldType = it->second;
            if (!fieldType || fieldType->getKind() != TypeRepr::Kind::Named) continue;
            auto *named = static_cast<const NamedTypeRepr *>(fieldType);
            for (const auto &tp : typeParams) {
                if (named->getName() == tp) {
                    const TypeRepr *argType = litField.value->getResolvedType();
                    if (argType && typeBindings.find(tp) == typeBindings.end()) {
                        typeBindings[tp] = argType;
                    }
                    break;
                }
            }
        }

        // Check trait bounds
        const auto &bounds = sym->structDecl->getTypeParamBounds();
        for (auto &[pName, boundProtos] : bounds) {
            auto bindIt = typeBindings.find(pName);
            if (bindIt == typeBindings.end()) continue;
            std::string concreteName = typeToString(bindIt->second);
            for (auto &boundProto : boundProtos) {
                auto confIt = protocolConformances_.find(boundProto);
                bool conforms = false;
                if (confIt != protocolConformances_.end()) {
                    for (const auto &t : confIt->second)
                        if (t == concreteName) { conforms = true; break; }
                }
                if (!conforms)
                    diag_.report(node->getStartLoc(), DiagID::err_no_conformance, concreteName, boundProto);
            }
        }
    }

    // Build resolved type: GenericTypeRepr if struct is generic.
    // - Infer args from field types when possible.
    // - For unbound params (e.g. when a field is an empty array), fall back to
    //   the type param name itself so the literal in a generic method body
    //   types as Stack<T> rather than bare Stack.
    if (sym->structDecl && sym->structDecl->isGeneric()) {
        const auto &typeParams = sym->structDecl->getTypeParams();
        std::vector<std::unique_ptr<TypeRepr>> args;
        for (const auto &tp : typeParams) {
            auto it = typeBindings.find(tp);
            if (it != typeBindings.end() && it->second) {
                args.push_back(cloneTypeRepr(it->second));
            } else {
                args.push_back(makeNamedType(tp));
            }
        }
        node->setResolvedType(std::make_unique<GenericTypeRepr>(
            node->getTypeName(), std::move(args)));
    } else {
        node->setResolvedType(makeNamedType(node->getTypeName()));
    }
}

void TypeChecker::visitArrayLiteralExpr(ArrayLiteralExpr *node) {
    for (auto &elem : node->getElements()) {
        visit(elem.get());
    }
}

void TypeChecker::visitTupleLiteralExpr(TupleLiteralExpr *node) {
    std::vector<std::unique_ptr<TypeRepr>> elemTypes;
    for (auto &elem : node->getElements()) {
        visit(elem.get());
        if (elem->getResolvedType())
            elemTypes.push_back(cloneTypeRepr(elem->getResolvedType()));
        else
            elemTypes.push_back(makeInferredType());
    }
    node->setResolvedType(std::make_unique<TupleTypeRepr>(std::move(elemTypes)));
}

void TypeChecker::visitCastExpr(CastExpr *node) {
    visit(const_cast<Expr *>(node->getExpr()));
    if (node->isOptional()) {
        // Check class hierarchy for as? cast
        auto *exprType = node->getExpr()->getResolvedType();
        auto *targetType = node->getTargetType();
        if (exprType && targetType &&
            exprType->getKind() == TypeRepr::Kind::Named &&
            targetType->getKind() == TypeRepr::Kind::Named) {
            auto *exprNamed = static_cast<const NamedTypeRepr *>(exprType);
            auto *targetNamed = static_cast<const NamedTypeRepr *>(targetType);
            auto eIt = classDecls_.find(exprNamed->getName());
            auto tIt = classDecls_.find(targetNamed->getName());
            if (eIt != classDecls_.end() && tIt != classDecls_.end()) {
                auto walkParents = [this](std::string cur, const std::string &target) {
                    while (!cur.empty()) {
                        if (cur == target) return true;
                        auto pit = classParent_.find(cur);
                        if (pit != classParent_.end()) cur = pit->second;
                        else break;
                    }
                    return false;
                };
                bool rel = walkParents(exprNamed->getName(), targetNamed->getName()) ||
                           walkParents(targetNamed->getName(), exprNamed->getName()) ||
                           exprNamed->getName() == targetNamed->getName();
                if (!rel) {
                    diag_.report(node->getStartLoc(), DiagID::warn_as_unrelated_type,
                                 targetNamed->getName(), exprNamed->getName());
                }
            }
        }
        // as? → result is Optional<TargetType>
        auto optType = std::make_unique<OptionalTypeRepr>(
            cloneTypeRepr(node->getTargetType()));
        node->setResolvedType(std::move(optType));
    } else {
        node->setResolvedType(makePrimitiveType(node->getTargetType()->getKind()));
    }
}

void TypeChecker::visitIsExpr(IsExpr *node) {
    visit(const_cast<Expr *>(node->getExpr()));
    // Check that expr type and target type share a class hierarchy
    auto *exprType = node->getExpr()->getResolvedType();
    auto *targetType = node->getTargetType();
    if (exprType && targetType &&
        exprType->getKind() == TypeRepr::Kind::Named &&
        targetType->getKind() == TypeRepr::Kind::Named) {
        auto *exprNamed = static_cast<const NamedTypeRepr *>(exprType);
        auto *targetNamed = static_cast<const NamedTypeRepr *>(targetType);
        auto eIt = classDecls_.find(exprNamed->getName());
        auto tIt = classDecls_.find(targetNamed->getName());
        if (eIt != classDecls_.end() && tIt != classDecls_.end()) {
            // Both classes: they must be in the same hierarchy (one is ancestor of other)
            auto walkParents = [this](std::string cur, const std::string &target) {
                while (!cur.empty()) {
                    if (cur == target) return true;
                    auto pit = classParent_.find(cur);
                    if (pit != classParent_.end()) cur = pit->second;
                    else break;
                }
                return false;
            };
            bool targetIsAncestor = walkParents(exprNamed->getName(), targetNamed->getName());
            bool exprIsAncestor = walkParents(targetNamed->getName(), exprNamed->getName());
            if (!targetIsAncestor && !exprIsAncestor &&
                exprNamed->getName() != targetNamed->getName()) {
                diag_.report(node->getStartLoc(), DiagID::warn_is_unrelated_type,
                             targetNamed->getName(), exprNamed->getName());
            }
        }
    }
    node->setResolvedType(makeBoolType());
}

void TypeChecker::visitRefExpr(RefExpr *node) {
    visit(const_cast<Expr *>(node->getExpr()));
    // Propagate ReferenceTypeRepr wrapping the inner type
    if (auto *innerType = node->getExpr()->getResolvedType()) {
        auto refType = std::make_unique<ReferenceTypeRepr>(
            cloneTypeRepr(innerType), node->isMutable());
        node->setResolvedType(std::move(refType));
    }
}

void TypeChecker::visitGroupExpr(GroupExpr *node) {
    visit(node->getExpr());
    if (node->getExpr()->getResolvedType()) {
        // Use cloneTypeRepr so non-primitive inner types (Named/Array/
        // Optional/...) keep their concrete subclass. makePrimitiveType
        // only constructs a TypeRepr base with the kind tag, which slices
        // away NamedTypeRepr's name field and similar — later code that
        // downcasts to the subclass then reads garbage and crashes.
        node->setResolvedType(
            cloneTypeRepr(node->getExpr()->getResolvedType()));
    }
}

bool TypeChecker::typesCompatible(const TypeRepr *expected, const TypeRepr *actual) const {
    if (!expected || !actual)
        return true;
    if (expected->isInferred() || actual->isInferred())
        return true;
    auto *exp = resolveAlias(expected);
    auto *act = resolveAlias(actual);
    // dyn Protocol accepts any concrete type
    if (exp->getKind() == TypeRepr::Kind::DynProtocol)
        return true;
    // String ↔ string compatibility (NamedTypeRepr("String") == TypeRepr::Kind::String)
    auto isStringType = [](const TypeRepr *t) -> bool {
        if (t->getKind() == TypeRepr::Kind::String) return true;
        if (t->getKind() == TypeRepr::Kind::Named) {
            auto *n = static_cast<const NamedTypeRepr *>(t);
            return n->getName() == "String";
        }
        return false;
    };
    if (isStringType(exp) && isStringType(act))
        return true;
    if (exp->getKind() != act->getKind())
        return false;
    // Deep compare for Named types (struct/enum names must match; classes allow subtype)
    if (exp->getKind() == TypeRepr::Kind::Named) {
        auto *expNamed = static_cast<const NamedTypeRepr *>(exp);
        auto *actNamed = static_cast<const NamedTypeRepr *>(act);
        if (expNamed->getName() != actNamed->getName()) {
            // Class subtype: expected is a class, actual is the same class or a descendant
            auto expIt = classDecls_.find(expNamed->getName());
            auto actIt = classDecls_.find(actNamed->getName());
            if (expIt != classDecls_.end() && actIt != classDecls_.end()) {
                // Walk actual's parent chain looking for expected
                std::string cur = actNamed->getName();
                while (!cur.empty()) {
                    if (cur == expNamed->getName()) return true;
                    auto pit = classParent_.find(cur);
                    if (pit != classParent_.end()) cur = pit->second;
                    else break;
                }
            }
            return false;
        }
    }
    // Deep compare for tuples
    if (exp->getKind() == TypeRepr::Kind::Tuple) {
        auto *expTuple = static_cast<const TupleTypeRepr *>(exp);
        auto *actTuple = static_cast<const TupleTypeRepr *>(act);
        if (expTuple->getArity() != actTuple->getArity()) return false;
        for (size_t i = 0; i < expTuple->getArity(); ++i) {
            if (!typesCompatible(expTuple->getElements()[i].get(),
                                 actTuple->getElements()[i].get()))
                return false;
        }
    }
    return true;
}

std::string TypeChecker::typeToString(const TypeRepr *type) const {
    if (!type)
        return "<unknown>";
    return type->toString();
}

/*static*/ bool TypeChecker::isDynArrayType(const TypeRepr *type) {
    if (!type) return false;
    // [T] with no fixed size is a DynArray
    if (type->getKind() == TypeRepr::Kind::Array) {
        auto *a = static_cast<const ArrayTypeRepr *>(type);
        return a->isDynamic();
    }
    return false;
}

const TypeRepr *TypeChecker::resolveAlias(const TypeRepr *type) const {
    if (!type || type->getKind() != TypeRepr::Kind::Named)
        return type;
    auto *named = static_cast<const NamedTypeRepr *>(type);
    auto it = typeAliases_.find(named->getName());
    if (it != typeAliases_.end())
        return it->second;
    return type;
}

bool TypeChecker::alwaysReturns(const ASTNode *node) const {
    if (!node) return false;
    switch (node->getKind()) {
    case ASTNode::NodeKind::ReturnStmt:
        return true;
    case ASTNode::NodeKind::BlockStmt: {
        auto *block = static_cast<const BlockStmt *>(node);
        if (block->getStatements().empty()) return false;
        // Check if any statement in the block always returns
        for (auto &stmt : block->getStatements()) {
            if (alwaysReturns(stmt.get())) return true;
        }
        return false;
    }
    case ASTNode::NodeKind::IfStmt: {
        auto *ifStmt = static_cast<const IfStmt *>(node);
        if (!ifStmt->hasElse()) return false;
        return alwaysReturns(ifStmt->getThenBody()) &&
               alwaysReturns(ifStmt->getElseBody());
    }
    case ASTNode::NodeKind::IfLetStmt: {
        auto *ifLet = static_cast<const IfLetStmt *>(node);
        if (!ifLet->hasElse()) return false;
        return alwaysReturns(ifLet->getThenBody()) &&
               alwaysReturns(ifLet->getElseBody());
    }
    case ASTNode::NodeKind::ExprStmt: {
        // Check if the expression is a MatchExpr with wildcard (exhaustive)
        auto *exprStmt = static_cast<const ExprStmt *>(node);
        if (exprStmt->getExpr()->getKind() == ASTNode::NodeKind::MatchExpr) {
            auto *match = static_cast<const MatchExpr *>(exprStmt->getExpr());
            bool hasWildcard = false;
            for (auto &arm : match->getArms()) {
                if (arm.patternNode && arm.patternNode->getKind() == Pattern::Kind::Wildcard)
                    hasWildcard = true;
            }
            if (hasWildcard) {
                // Match with wildcard is exhaustive — check all arm bodies
                bool allArmsReturn = true;
                for (auto &arm : match->getArms()) {
                    if (!alwaysReturns(arm.body.get())) {
                        allArmsReturn = false;
                        break;
                    }
                }
                return allArmsReturn;
            }
        }
        return false;
    }
    default:
        return false;
    }
}

const TypeRepr *TypeChecker::resolveExprType(Expr *expr) {
    visit(expr);
    return expr->getResolvedType();
}

void TypeChecker::extractPatternBindings(const Pattern *pattern) {
    if (!pattern) return;

    switch (pattern->getKind()) {
    case Pattern::Kind::EnumCase: {
        auto *ec = static_cast<const EnumCasePattern *>(pattern);
        if (ec->hasParens()) {
            for (auto &sub : ec->getSubpatterns())
                declarePatternSubBinding(sub.get());
        }
        // Bare "Enum.Case" (no payload, no parens) — nothing to bind, same
        // as the legacy string check (a pattern containing '.' but no '('
        // never reached the variable-binding branch).
        return;
    }

    case Pattern::Kind::Identifier: {
        auto *idp = static_cast<const IdentifierPattern *>(pattern);
        Symbol sym;
        sym.name = idp->getName();
        sym.kind = Symbol::Kind::Variable;
        sym.isMutable = false;
        scopes_.declare(sym.name, sym);
        return;
    }

    case Pattern::Kind::Wildcard:
    case Pattern::Kind::IntLiteral:
        // "_" and a top-level integer literal (e.g. "1 where true") bind
        // nothing — matches the legacy no-parens branch's "_"/int-literal
        // exclusion.
        return;
    }
}

void TypeChecker::declarePatternSubBinding(const Pattern *sub) {
    if (!sub) return;

    if (sub->getKind() == Pattern::Kind::EnumCase) {
        // A nested enum-case sub-pattern (e.g. "Inner.Val(n)" inside
        // "Outer.Some(Inner.Val(n))") recurses through the top-level
        // dispatch above, which itself branches on hasParens(). This is a
        // sane deviation from the legacy string splitter for the one
        // untested combination of a nested *unqualified* payload case
        // (empty enumName, e.g. hypothetical "Wrapped(Circle(r))"): the old
        // code only recursed on slots containing '.', so such a slot would
        // instead have been declared as one broken binding named
        // "Circle(r)" verbatim. Zero occurrences in tests/examples/stdlib
        // (Task 0); recursing here is strictly more correct.
        extractPatternBindings(sub);
        return;
    }

    // Identifier/Wildcard/IntLiteral subslot: the legacy string splitter
    // declared the *whole slot text* as a binding unconditionally once it
    // determined the slot had no '.' — no "_"/int-literal exclusion inside
    // parens (unlike the top-level no-parens branch). Preserve that
    // byte-for-byte via toString().
    Symbol sym;
    sym.name = sub->toString();
    sym.kind = Symbol::Kind::Variable;
    sym.isMutable = false;
    scopes_.declare(sym.name, sym);
}

void TypeChecker::visitMatchExpr(MatchExpr *node) {
    visit(const_cast<Expr *>(node->getSubject()));

    // Determine if subject is an enum type by examining arm patterns
    const EnumDecl *subjectEnum = nullptr;
    std::string enumName;
    for (auto &arm : node->getArms()) {
        // Only a qualified "Enum.Case[(...)]" pattern (non-empty enumName)
        // reveals the subject's enum type — matches the legacy '.'-search.
        auto *ec = (arm.patternNode && arm.patternNode->getKind() == Pattern::Kind::EnumCase)
                       ? static_cast<const EnumCasePattern *>(arm.patternNode.get())
                       : nullptr;
        if (ec && !ec->getEnumName().empty()) {
            enumName = ec->getEnumName();
            auto *sym = scopes_.lookup(enumName);
            if (sym && sym->kind == Symbol::Kind::EnumType && sym->enumDecl) {
                subjectEnum = sym->enumDecl;
            }
            break;
        }
    }

    bool hasWildcard = false;
    std::set<std::string> coveredCases;

    for (auto &arm : node->getArms()) {
        // Check for wildcard
        if (arm.patternNode && arm.patternNode->getKind() == Pattern::Kind::Wildcard) {
            hasWildcard = true;
        } else if (subjectEnum) {
            // Extract case name from a qualified pattern like "Color.Red" or
            // "Shape.Circle(r)" (unqualified/bare-payload patterns, like the
            // legacy '.'-less branch, don't participate in coverage tracking).
            auto *ec = (arm.patternNode && arm.patternNode->getKind() == Pattern::Kind::EnumCase)
                           ? static_cast<const EnumCasePattern *>(arm.patternNode.get())
                           : nullptr;
            if (ec && !ec->getEnumName().empty()) {
                const std::string &caseName = ec->getCaseName();
                if (coveredCases.count(caseName)) {
                    if (!arm.guard) {
                        diag_.report(node->getStartLoc(), DiagID::warn_unreachable_match_arm,
                                     arm.patternNode->toString());
                    }
                } else if (!arm.guard) {
                    coveredCases.insert(caseName);
                }
            }
        }

        if (arm.body) {
            // Extract bindings from pattern (supports nested patterns)
            scopes_.pushScope();
            extractPatternBindings(arm.patternNode.get());
            if (arm.guard) {
                visit(arm.guard.get());
            }
            visit(arm.body.get());
            scopes_.popScope();
        }
    }

    // Exhaustiveness check for enum matches without wildcard
    if (subjectEnum && !hasWildcard) {
        for (auto &c : subjectEnum->getCases()) {
            if (!coveredCases.count(c->getName())) {
                diag_.report(node->getStartLoc(), DiagID::err_nonexhaustive_match,
                             enumName + "." + c->getName());
            }
        }
    }

    // Check for Result type match exhaustiveness
    auto asResultCase = [](const MatchArm &arm) -> const EnumCasePattern * {
        if (!arm.patternNode || arm.patternNode->getKind() != Pattern::Kind::EnumCase)
            return nullptr;
        auto *ec = static_cast<const EnumCasePattern *>(arm.patternNode.get());
        return ec->getEnumName() == "Result" ? ec : nullptr;
    };
    bool isResultMatch = false;
    for (auto &arm : node->getArms()) {
        auto *ec = asResultCase(arm);
        if (ec && (ec->getCaseName() == "Ok" || ec->getCaseName() == "Err")) {
            isResultMatch = true;
            break;
        }
    }
    if (isResultMatch && !hasWildcard) {
        bool hasOk = false, hasErr = false;
        for (auto &arm : node->getArms()) {
            auto *ec = asResultCase(arm);
            if (ec && ec->getCaseName() == "Ok") hasOk = true;
            if (ec && ec->getCaseName() == "Err") hasErr = true;
        }
        if (!hasOk) diag_.report(node->getStartLoc(), DiagID::err_nonexhaustive_match, "Result.Ok");
        if (!hasErr) diag_.report(node->getStartLoc(), DiagID::err_nonexhaustive_match, "Result.Err");
    }
}

void TypeChecker::visitRangeExpr(RangeExpr *node) {
    visit(node->getStart());
    visit(node->getEnd());
}

void TypeChecker::visitUnwrapExpr(UnwrapExpr *node) {
    visit(node->getOperand());
}

void TypeChecker::visitTryExpr(TryExpr *node) {
    visit(const_cast<Expr *>(node->getOperand()));

    // Check: try/? can only be applied to Result type expressions
    auto *operandType = node->getOperand()->getResolvedType();
    if (operandType && !operandType->isInferred() &&
        operandType->getKind() != TypeRepr::Kind::Result) {
        diag_.report(node->getStartLoc(), DiagID::err_try_on_non_result);
    }

    // Check: enclosing function must return Result
    // currentReturnType_ is nullptr for void functions → also an error
    if (!currentReturnType_ ||
        (!currentReturnType_->isInferred() &&
         currentReturnType_->getKind() != TypeRepr::Kind::Result)) {
        diag_.report(node->getStartLoc(), DiagID::err_try_outside_result_func);
    }

    // Unwrap Result<T, E> → T for type propagation
    if (operandType && operandType->getKind() == TypeRepr::Kind::Result) {
        auto *resType = static_cast<const ResultTypeRepr *>(operandType);
        if (resType->getOkType()) {
            node->setResolvedType(cloneTypeRepr(resType->getOkType()));
        }
    }
}

void TypeChecker::visitTernaryExpr(TernaryExpr *node) {
    visit(node->getCondition());
    visit(node->getThenExpr());
    visit(node->getElseExpr());
    // Propagate then-branch type as result type
    if (node->getThenExpr()->getResolvedType()) {
        node->setResolvedType(cloneTypeRepr(node->getThenExpr()->getResolvedType()));
    }
}

void TypeChecker::visitAwaitExpr(AwaitExpr *node) {
    if (!currentIsAsync_) {
        diag_.report(node->getStartLoc(), DiagID::err_await_outside_async);
    }
    visit(node->getOperand());
    // Unwrap Task<T> to T
    auto *operandType = node->getOperand()->getResolvedType();
    if (operandType && operandType->getKind() == TypeRepr::Kind::Generic) {
        auto *genType = static_cast<const GenericTypeRepr *>(operandType);
        if (genType->getBaseName() == "Task" && !genType->getTypeArgs().empty()) {
            node->setResolvedType(cloneTypeRepr(genType->getTypeArgs()[0].get()));
            return;
        }
    }
    // If not a Task type, propagate operand type (tolerant for non-async calls)
    if (operandType) {
        node->setResolvedType(cloneTypeRepr(operandType));
    }
}

void TypeChecker::visitYieldExpr(YieldExpr *node) {
    if (!currentIsGenerator_) {
        diag_.report(node->getStartLoc(), DiagID::err_yield_outside_generator);
    }
    visit(node->getValue());
    // Yield propagates the value type
    auto *valueType = node->getValue()->getResolvedType();
    if (valueType) {
        node->setResolvedType(cloneTypeRepr(valueType));
    }
}

// ============================================================
// Lifetime Elision Rules (Rust-style)
// ============================================================

void TypeChecker::elideFunctionLifetimes(FuncDecl *node) {
    // Rule 0: If user provided explicit lifetime params, don't elide
    if (node->hasLifetimeParams()) return;

    // Collect input reference parameters
    struct RefParam {
        size_t index;
        ReferenceTypeRepr *refType;
        bool isSelf;
    };
    std::vector<RefParam> inputRefs;

    for (size_t i = 0; i < node->getParams().size(); ++i) {
        const auto &param = node->getParams()[i];
        if (param.type && param.type->getKind() == TypeRepr::Kind::Reference) {
            auto *refType = const_cast<ReferenceTypeRepr *>(
                static_cast<const ReferenceTypeRepr *>(param.type.get()));
            if (!refType->hasLifetime()) {
                inputRefs.push_back({i, refType, param.isSelf});
            }
        }
    }

    // No reference params → nothing to elide
    if (inputRefs.empty()) return;

    // Rule 1: Each input ref param gets its own lifetime
    std::vector<std::string> elidedLifetimes;
    for (size_t i = 0; i < inputRefs.size(); ++i) {
        std::string lt = "'_" + std::to_string(i);
        elidedLifetimes.push_back(lt);
        inputRefs[i].refType->setLifetime(lt);
    }

    // Check if return type is a reference without explicit lifetime
    ReferenceTypeRepr *outputRef = nullptr;
    if (node->getReturnType() &&
        node->getReturnType()->getKind() == TypeRepr::Kind::Reference) {
        auto *retRef = const_cast<ReferenceTypeRepr *>(
            static_cast<const ReferenceTypeRepr *>(node->getReturnType()));
        if (!retRef->hasLifetime()) {
            outputRef = retRef;
        }
    }

    if (outputRef) {
        // Rule 3: If method with &self, output gets self's lifetime
        bool appliedRule3 = false;
        if (node->isMethod()) {
            for (auto &rp : inputRefs) {
                if (rp.isSelf) {
                    outputRef->setLifetime(elidedLifetimes[&rp - inputRefs.data()]);
                    appliedRule3 = true;
                    break;
                }
            }
        }

        // Rule 2: If exactly one input ref, output gets same lifetime
        if (!appliedRule3 && inputRefs.size() == 1) {
            outputRef->setLifetime(elidedLifetimes[0]);
        }
    }

    // Register the elided lifetimes on the FuncDecl
    node->setLifetimeParams(std::move(elidedLifetimes));
}

void TypeChecker::visitClosureExpr(ClosureExpr *node) {
    scopes_.pushScope();

    for (auto &param : node->getParams()) {
        Symbol sym;
        sym.name = param.name;
        sym.kind = Symbol::Kind::Parameter;
        sym.type = param.type.get();
        scopes_.declare(param.name, sym);
    }

    auto *prevReturn = currentReturnType_;
    currentReturnType_ = node->getReturnType();

    if (node->getBody()) {
        visitBlockStmt(node->getBody());
    }

    currentReturnType_ = prevReturn;
    scopes_.popScope();

    // Build a FunctionTypeRepr matching the closure's signature so callers
    // (e.g. method-level type-param inference) can unify against it.
    std::vector<std::unique_ptr<TypeRepr>> paramTypes;
    paramTypes.reserve(node->getParams().size());
    for (auto &p : node->getParams()) {
        paramTypes.push_back(p.type ? cloneTypeRepr(p.type.get())
                                     : makePrimitiveType(TypeRepr::Kind::Inferred));
    }
    auto retClone = node->getReturnType()
        ? cloneTypeRepr(node->getReturnType())
        : makePrimitiveType(TypeRepr::Kind::Void);
    node->setResolvedType(std::make_unique<FunctionTypeRepr>(
        std::move(paramTypes), std::move(retClone)));
}

void TypeChecker::visitComptimeExpr(ComptimeExpr *node) {
    // Save and restore comptimeLocals_ at the top-level entry point
    auto savedLocals = comptimeLocals_;
    auto result = evaluateComptimeBlock(node->getBody());
    comptimeLocals_ = savedLocals;
    if (!result) {
        diag_.report(node->getStartLoc(), DiagID::err_comptime_not_constant);
        return;
    }
    switch (result->kind) {
    case ConstValue::Integer:
        node->setResolvedType(std::make_unique<TypeRepr>(TypeRepr::Kind::I32));
        break;
    case ConstValue::Float:
        node->setResolvedType(std::make_unique<TypeRepr>(TypeRepr::Kind::F64));
        break;
    case ConstValue::Bool:
        node->setResolvedType(std::make_unique<TypeRepr>(TypeRepr::Kind::Bool));
        break;
    case ConstValue::String:
        node->setResolvedType(std::make_unique<TypeRepr>(TypeRepr::Kind::String));
        break;
    }
}

std::optional<TypeChecker::ConstValue> TypeChecker::evaluateComptimeBlock(const BlockStmt *block) {
    if (!block) return std::nullopt;

    auto &stmts = block->getStatements();
    if (stmts.empty()) return std::nullopt;

    std::optional<ConstValue> result;

    for (size_t i = 0; i < stmts.size(); ++i) {
        auto *stmt = stmts[i].get();
        bool isLast = (i == stmts.size() - 1);

        switch (stmt->getKind()) {
        case ASTNode::NodeKind::VarDecl: {
            auto *var = static_cast<VarDecl *>(stmt);
            if (!var->hasInit()) return std::nullopt;
            auto val = evaluateConstExpr(var->getInit());
            if (!val) return std::nullopt;
            comptimeLocals_[var->getName()] = *val;
            if (isLast) result = val;
            break;
        }
        case ASTNode::NodeKind::ExprStmt: {
            auto *exprStmt = static_cast<ExprStmt *>(stmt);
            auto *expr = exprStmt->getExpr();
            // Check for assignment: x = expr or x += expr etc.
            if (expr->getKind() == ASTNode::NodeKind::AssignExpr) {
                auto *assign = static_cast<AssignExpr *>(const_cast<Expr *>(expr));
                auto *target = assign->getTarget();
                if (target->getKind() == ASTNode::NodeKind::IdentifierExpr) {
                    auto *ident = static_cast<const IdentifierExpr *>(target);
                    auto val = evaluateConstExpr(assign->getValue());
                    if (!val) return std::nullopt;
                    auto it = comptimeLocals_.find(ident->getName());
                    if (it == comptimeLocals_.end()) return std::nullopt;
                    switch (assign->getOp()) {
                    case AssignExpr::Op::Assign:
                        it->second = *val;
                        break;
                    case AssignExpr::Op::AddAssign:
                        if (it->second.kind == ConstValue::Integer && val->kind == ConstValue::Integer)
                            it->second.intVal += val->intVal;
                        else if (it->second.kind == ConstValue::Float && val->kind == ConstValue::Float)
                            it->second.floatVal += val->floatVal;
                        else return std::nullopt;
                        break;
                    case AssignExpr::Op::SubAssign:
                        if (it->second.kind == ConstValue::Integer && val->kind == ConstValue::Integer)
                            it->second.intVal -= val->intVal;
                        else if (it->second.kind == ConstValue::Float && val->kind == ConstValue::Float)
                            it->second.floatVal -= val->floatVal;
                        else return std::nullopt;
                        break;
                    case AssignExpr::Op::MulAssign:
                        if (it->second.kind == ConstValue::Integer && val->kind == ConstValue::Integer)
                            it->second.intVal *= val->intVal;
                        else if (it->second.kind == ConstValue::Float && val->kind == ConstValue::Float)
                            it->second.floatVal *= val->floatVal;
                        else return std::nullopt;
                        break;
                    case AssignExpr::Op::DivAssign:
                        if (it->second.kind == ConstValue::Integer && val->kind == ConstValue::Integer) {
                            if (val->intVal == 0) return std::nullopt;
                            it->second.intVal /= val->intVal;
                        } else if (it->second.kind == ConstValue::Float && val->kind == ConstValue::Float) {
                            if (val->floatVal == 0.0) return std::nullopt;
                            it->second.floatVal /= val->floatVal;
                        } else return std::nullopt;
                        break;
                    case AssignExpr::Op::ModAssign:
                        if (it->second.kind == ConstValue::Integer && val->kind == ConstValue::Integer) {
                            if (val->intVal == 0) return std::nullopt;
                            it->second.intVal %= val->intVal;
                        } else return std::nullopt;
                        break;
                    }
                    if (isLast) result = it->second;
                } else return std::nullopt;
            } else {
                auto val = evaluateConstExpr(expr);
                if (!val) return std::nullopt;
                if (isLast) result = val;
            }
            break;
        }
        case ASTNode::NodeKind::IfStmt: {
            auto *ifStmt = static_cast<IfStmt *>(stmt);
            auto cond = evaluateConstExpr(ifStmt->getCondition());
            if (!cond || cond->kind != ConstValue::Bool) return std::nullopt;
            const ASTNode *branch = cond->boolVal ? ifStmt->getThenBody() : ifStmt->getElseBody();
            if (!branch) {
                if (isLast) return std::nullopt;
                break;
            }
            if (branch->getKind() == ASTNode::NodeKind::BlockStmt) {
                auto branchResult = evaluateComptimeBlock(static_cast<const BlockStmt *>(branch));
                if (isLast) {
                    if (!branchResult) return std::nullopt;
                    result = branchResult;
                }
            } else return std::nullopt;
            break;
        }
        case ASTNode::NodeKind::WhileStmt: {
            auto *whileStmt = static_cast<WhileStmt *>(stmt);
            const int maxIter = 10000;
            int iter = 0;
            while (true) {
                if (iter++ >= maxIter) {
                    diag_.report(whileStmt->getStartLoc(), DiagID::err_comptime_loop_limit);
                    return std::nullopt;
                }
                auto cond = evaluateConstExpr(whileStmt->getCondition());
                if (!cond || cond->kind != ConstValue::Bool) return std::nullopt;
                if (!cond->boolVal) break;
                auto *body = whileStmt->getBody();
                if (body->getKind() == ASTNode::NodeKind::BlockStmt) {
                    evaluateComptimeBlock(static_cast<const BlockStmt *>(body));
                } else return std::nullopt;
            }
            break;
        }
        case ASTNode::NodeKind::ReturnStmt: {
            auto *ret = static_cast<ReturnStmt *>(stmt);
            if (ret->hasValue()) {
                return evaluateConstExpr(ret->getValue());
            }
            return std::nullopt;
        }
        default:
            return std::nullopt;
        }
    }

    return result;
}

std::optional<TypeChecker::ConstValue> TypeChecker::evaluateConstExpr(const Expr *expr) {
    if (!expr) return std::nullopt;

    switch (expr->getKind()) {
    case ASTNode::NodeKind::IntegerLiteralExpr: {
        auto *lit = static_cast<const IntegerLiteralExpr *>(expr);
        ConstValue v;
        v.kind = ConstValue::Integer;
        v.intVal = lit->getValue();
        return v;
    }
    case ASTNode::NodeKind::FloatLiteralExpr: {
        auto *lit = static_cast<const FloatLiteralExpr *>(expr);
        ConstValue v;
        v.kind = ConstValue::Float;
        v.floatVal = lit->getValue();
        return v;
    }
    case ASTNode::NodeKind::BoolLiteralExpr: {
        auto *lit = static_cast<const BoolLiteralExpr *>(expr);
        ConstValue v;
        v.kind = ConstValue::Bool;
        v.boolVal = lit->getValue();
        return v;
    }
    case ASTNode::NodeKind::StringLiteralExpr: {
        auto *lit = static_cast<const StringLiteralExpr *>(expr);
        ConstValue v;
        v.kind = ConstValue::String;
        v.strVal = lit->getValue();
        return v;
    }
    case ASTNode::NodeKind::IdentifierExpr: {
        auto *ident = static_cast<const IdentifierExpr *>(expr);
        auto cit = comptimeLocals_.find(ident->getName());
        if (cit != comptimeLocals_.end()) return cit->second;
        auto it = constValues_.find(ident->getName());
        if (it != constValues_.end()) return it->second;
        return std::nullopt;
    }
    case ASTNode::NodeKind::GroupExpr: {
        auto *group = static_cast<const GroupExpr *>(expr);
        return evaluateConstExpr(group->getExpr());
    }
    case ASTNode::NodeKind::UnaryExpr: {
        auto *unary = static_cast<const UnaryExpr *>(expr);
        auto operand = evaluateConstExpr(unary->getOperand());
        if (!operand) return std::nullopt;
        switch (unary->getOp()) {
        case UnaryExpr::Op::Negate:
            if (operand->kind == ConstValue::Integer) {
                operand->intVal = -operand->intVal;
                return operand;
            }
            if (operand->kind == ConstValue::Float) {
                operand->floatVal = -operand->floatVal;
                return operand;
            }
            return std::nullopt;
        case UnaryExpr::Op::Not:
            if (operand->kind == ConstValue::Bool) {
                operand->boolVal = !operand->boolVal;
                return operand;
            }
            return std::nullopt;
        case UnaryExpr::Op::BitNot:
            if (operand->kind == ConstValue::Integer) {
                operand->intVal = ~operand->intVal;
                return operand;
            }
            return std::nullopt;
        }
        return std::nullopt;
    }
    case ASTNode::NodeKind::BinaryExpr: {
        auto *bin = static_cast<const BinaryExpr *>(expr);
        auto lhs = evaluateConstExpr(bin->getLHS());
        auto rhs = evaluateConstExpr(bin->getRHS());
        if (!lhs || !rhs) return std::nullopt;

        // Integer arithmetic
        if (lhs->kind == ConstValue::Integer && rhs->kind == ConstValue::Integer) {
            ConstValue v;
            v.kind = ConstValue::Integer;
            switch (bin->getOp()) {
            case BinaryExpr::Op::Add: v.intVal = lhs->intVal + rhs->intVal; return v;
            case BinaryExpr::Op::Sub: v.intVal = lhs->intVal - rhs->intVal; return v;
            case BinaryExpr::Op::Mul: v.intVal = lhs->intVal * rhs->intVal; return v;
            case BinaryExpr::Op::Div:
                if (rhs->intVal == 0) return std::nullopt;
                v.intVal = lhs->intVal / rhs->intVal; return v;
            case BinaryExpr::Op::Mod:
                if (rhs->intVal == 0) return std::nullopt;
                v.intVal = lhs->intVal % rhs->intVal; return v;
            case BinaryExpr::Op::BitAnd: v.intVal = lhs->intVal & rhs->intVal; return v;
            case BinaryExpr::Op::BitOr:  v.intVal = lhs->intVal | rhs->intVal; return v;
            case BinaryExpr::Op::BitXor: v.intVal = lhs->intVal ^ rhs->intVal; return v;
            case BinaryExpr::Op::Shl:    v.intVal = lhs->intVal << rhs->intVal; return v;
            case BinaryExpr::Op::Shr:    v.intVal = lhs->intVal >> rhs->intVal; return v;
            case BinaryExpr::Op::Eq:  v.kind = ConstValue::Bool; v.boolVal = (lhs->intVal == rhs->intVal); return v;
            case BinaryExpr::Op::NotEq: v.kind = ConstValue::Bool; v.boolVal = (lhs->intVal != rhs->intVal); return v;
            case BinaryExpr::Op::Less: v.kind = ConstValue::Bool; v.boolVal = (lhs->intVal < rhs->intVal); return v;
            case BinaryExpr::Op::LessEq: v.kind = ConstValue::Bool; v.boolVal = (lhs->intVal <= rhs->intVal); return v;
            case BinaryExpr::Op::Greater: v.kind = ConstValue::Bool; v.boolVal = (lhs->intVal > rhs->intVal); return v;
            case BinaryExpr::Op::GreaterEq: v.kind = ConstValue::Bool; v.boolVal = (lhs->intVal >= rhs->intVal); return v;
            default: return std::nullopt;
            }
        }

        // Float arithmetic
        if (lhs->kind == ConstValue::Float && rhs->kind == ConstValue::Float) {
            ConstValue v;
            v.kind = ConstValue::Float;
            switch (bin->getOp()) {
            case BinaryExpr::Op::Add: v.floatVal = lhs->floatVal + rhs->floatVal; return v;
            case BinaryExpr::Op::Sub: v.floatVal = lhs->floatVal - rhs->floatVal; return v;
            case BinaryExpr::Op::Mul: v.floatVal = lhs->floatVal * rhs->floatVal; return v;
            case BinaryExpr::Op::Div:
                if (rhs->floatVal == 0.0) return std::nullopt;
                v.floatVal = lhs->floatVal / rhs->floatVal; return v;
            case BinaryExpr::Op::Eq:  v.kind = ConstValue::Bool; v.boolVal = (lhs->floatVal == rhs->floatVal); return v;
            case BinaryExpr::Op::NotEq: v.kind = ConstValue::Bool; v.boolVal = (lhs->floatVal != rhs->floatVal); return v;
            case BinaryExpr::Op::Less: v.kind = ConstValue::Bool; v.boolVal = (lhs->floatVal < rhs->floatVal); return v;
            case BinaryExpr::Op::LessEq: v.kind = ConstValue::Bool; v.boolVal = (lhs->floatVal <= rhs->floatVal); return v;
            case BinaryExpr::Op::Greater: v.kind = ConstValue::Bool; v.boolVal = (lhs->floatVal > rhs->floatVal); return v;
            case BinaryExpr::Op::GreaterEq: v.kind = ConstValue::Bool; v.boolVal = (lhs->floatVal >= rhs->floatVal); return v;
            default: return std::nullopt;
            }
        }

        // Bool logic
        if (lhs->kind == ConstValue::Bool && rhs->kind == ConstValue::Bool) {
            ConstValue v;
            v.kind = ConstValue::Bool;
            switch (bin->getOp()) {
            case BinaryExpr::Op::And: v.boolVal = lhs->boolVal && rhs->boolVal; return v;
            case BinaryExpr::Op::Or:  v.boolVal = lhs->boolVal || rhs->boolVal; return v;
            case BinaryExpr::Op::Eq:  v.boolVal = (lhs->boolVal == rhs->boolVal); return v;
            case BinaryExpr::Op::NotEq: v.boolVal = (lhs->boolVal != rhs->boolVal); return v;
            default: return std::nullopt;
            }
        }

        // String concatenation
        if (lhs->kind == ConstValue::String && rhs->kind == ConstValue::String) {
            if (bin->getOp() == BinaryExpr::Op::Add) {
                ConstValue v;
                v.kind = ConstValue::String;
                v.strVal = lhs->strVal + rhs->strVal;
                return v;
            }
        }

        return std::nullopt;
    }
    case ASTNode::NodeKind::TernaryExpr: {
        auto *ternary = static_cast<const TernaryExpr *>(expr);
        auto cond = evaluateConstExpr(ternary->getCondition());
        auto then = evaluateConstExpr(ternary->getThenExpr());
        auto els = evaluateConstExpr(ternary->getElseExpr());
        if (!cond || !then || !els) return std::nullopt;
        if (cond->kind != ConstValue::Bool) return std::nullopt;
        return cond->boolVal ? then : els;
    }
    case ASTNode::NodeKind::CastExpr: {
        auto *cast = static_cast<const CastExpr *>(expr);
        auto operand = evaluateConstExpr(cast->getExpr());
        if (!operand) return std::nullopt;
        auto *targetType = cast->getTargetType();
        if (!targetType) return std::nullopt;

        auto kind = targetType->getKind();
        bool isIntTarget = (kind == TypeRepr::Kind::I8 || kind == TypeRepr::Kind::I16 ||
                            kind == TypeRepr::Kind::I32 || kind == TypeRepr::Kind::I64 ||
                            kind == TypeRepr::Kind::U8 || kind == TypeRepr::Kind::U16 ||
                            kind == TypeRepr::Kind::U32 || kind == TypeRepr::Kind::U64);
        bool isFloatTarget = (kind == TypeRepr::Kind::F32 || kind == TypeRepr::Kind::F64);

        if (operand->kind == ConstValue::Integer) {
            if (isIntTarget) return operand;
            if (isFloatTarget) {
                ConstValue v;
                v.kind = ConstValue::Float;
                v.floatVal = static_cast<double>(operand->intVal);
                return v;
            }
        }
        if (operand->kind == ConstValue::Float) {
            if (isIntTarget) {
                ConstValue v;
                v.kind = ConstValue::Integer;
                v.intVal = static_cast<int64_t>(operand->floatVal);
                return v;
            }
            if (isFloatTarget) return operand;
        }
        return std::nullopt;
    }
    case ASTNode::NodeKind::ComptimeExpr: {
        auto *comptime = static_cast<const ComptimeExpr *>(expr);
        return evaluateComptimeBlock(comptime->getBody());
    }
    default:
        return std::nullopt;
    }
}

// === Macro Support ===

void TypeChecker::visitMacroDecl(MacroDecl *) {
    // Already registered in pass 1; nothing to do in pass 2
}

void TypeChecker::visitMacroInvokeExpr(MacroInvokeExpr *node) {
    // Expand the macro on first visit
    if (!node->getExpanded()) {
        std::string expanded = macroExpander_.expand(
            node->getName(), node->getArgTokens(), diag_, node->getStartLoc());
        if (expanded.empty()) {
            // macroExpander may have already reported err_macro_no_matching_arm;
            // if not, report a generic failure as a root-cause diagnostic
            diag_.report(node->getStartLoc(), DiagID::err_macro_expansion_failed,
                         node->getName(), "expansion produced empty result");
            return;
        }

        node->setExpandedSource(expanded);

        // Re-lex and re-parse the expanded source as an expression
        SourceManager sm("<macro-expansion>", expanded);
        DiagnosticsEngine tempDiag(&sm);
        Lexer lexer(sm, tempDiag);
        Parser parser(lexer, tempDiag);

        auto expandedExpr = parser.parseExpression();
        if (expandedExpr && !tempDiag.hasErrors()) {
            node->setExpanded(std::move(expandedExpr));
        } else {
            std::string detail = "parse error in expanded code: " + expanded;
            diag_.report(node->getStartLoc(), DiagID::err_macro_expansion_failed,
                         node->getName(), detail);
            return;
        }
    }

    // Visit the expanded expression for type checking
    if (node->getExpanded()) {
        visit(node->getExpanded());
        if (node->getExpanded()->getResolvedType()) {
            node->setResolvedType(
                std::make_unique<TypeRepr>(node->getExpanded()->getResolvedType()->getKind()));
        }
    }
}

void TypeChecker::expandMacros(TranslationUnit &) {
    // Expansion now happens lazily in visitMacroInvokeExpr
}

void TypeChecker::expandMacrosInExpr(std::unique_ptr<Expr> &) {
    // Expansion now happens lazily in visitMacroInvokeExpr
}

void TypeChecker::expandMacrosInStmt(ASTNode *) {
    // Expansion now happens lazily in visitMacroInvokeExpr
}

bool TypeChecker::isObjectSafe(const std::string &protocolName, std::string &unsafeMethodName) {
    auto *sym = scopes_.lookup(protocolName);
    if (!sym || !sym->protocolDecl) return true;
    for (auto &method : sym->protocolDecl->getMethods()) {
        if (method->isGeneric()) {
            unsafeMethodName = method->getName();
            return false;
        }
    }
    return true;
}

void TypeChecker::visitTestDecl(TestDecl *node) {
    scopes_.pushScope();
    if (node->getBody())
        visitBlockStmt(const_cast<BlockStmt *>(node->getBody()));
    scopes_.popScope();
}

} // namespace liva
