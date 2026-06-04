#include "liva/Sema/ModuleLoader.h"
#include "liva/Lexer/Lexer.h"
#include "liva/Parser/Parser.h"
#include "liva/Sema/Sema.h"
#include <fstream>
#include <sstream>

namespace liva {

ModuleLoader::ModuleLoader() {
    registerBuiltinModules();
}

std::unique_ptr<ModuleLoader::Module> ModuleLoader::createBuiltinModule(
    const std::string &name,
    const std::vector<std::string> &funcNames,
    const std::vector<std::string> &structNames) {
    auto mod = std::make_unique<Module>();
    mod->name = name;
    // sm and tu remain nullptr — virtual module
    for (const auto &fn : funcNames) {
        Symbol sym;
        sym.name = fn;
        sym.kind = Symbol::Kind::Function;
        sym.isPublic = true;
        mod->exportedSymbols.push_back(sym);
    }
    for (const auto &sn : structNames) {
        Symbol sym;
        sym.name = sn;
        sym.kind = Symbol::Kind::StructType;
        sym.isPublic = true;
        mod->exportedSymbols.push_back(sym);
    }
    return mod;
}

void ModuleLoader::registerBuiltinModules() {
    cache_["std::math"] = createBuiltinModule("std::math",
        {"abs", "min", "max", "sqrt", "pow", "floor", "ceil",
         "log", "log10", "sin", "cos", "tan", "round"});

    cache_["std::io"] = createBuiltinModule("std::io",
        {"print", "println", "readLine", "format",
         "dirList", "dirCreate", "dirRemove", "dirExists",
         "pathJoin", "pathDirname", "pathBasename", "pathExtension",
         "pathExists", "isFile", "isDir",
         "fileSize", "fileModifiedTime",
         "fileRead", "fileWrite", "fileAppend",
         "fileRemove", "fileCopy", "pathAbsolute"}, {"File"});

    cache_["std::convert"] = createBuiltinModule("std::convert",
        {"parseInt", "parseInt64", "parseFloat", "toString", "charToString"});

    cache_["std::os"] = createBuiltinModule("std::os",
        {"env", "exit", "args", "clock", "clockMs", "sleep",
         "exec", "execOutput", "processStart", "processWait",
         "processKill", "processRead", "processClose"});

    cache_["std::random"] = createBuiltinModule("std::random",
        {"randInt", "randFloat", "randSeed", "randI64", "randUuid", "randUuidV7"});

    cache_["std::regex"] = createBuiltinModule("std::regex",
        {"regexMatch", "regexFind", "regexFindAll", "regexReplace",
         "regexFindGroups", "regexSplit",
         "regexCompile", "regexTest", "regexExec",
         "regexExecGroups", "regexReplaceCompiled", "regexFree"});

    cache_["std::net"] = createBuiltinModule("std::net",
        {"httpRequestEx", "httpStatus", "httpBody",
         "httpRawHeaders", "httpHeaderLookup", "httpClose"});

    cache_["std::websocket"] = createBuiltinModule("std::websocket",
        {"wsConnect", "wsSend", "wsRecv", "wsClose", "wsIsOpen"});

    cache_["std::sqlite"] = createBuiltinModule("std::sqlite",
        {"sqliteOpen", "sqliteClose", "sqliteExec",
         "sqliteQueryFirst", "sqliteQueryInt", "sqliteQueryColumn",
         "sqliteLastInsertRowid", "sqliteChanges", "sqliteErrmsg",
         "sqlitePrepare", "sqliteBindText", "sqliteBindInt",
         "sqliteBindDouble", "sqliteBindNull",
         "sqliteStep", "sqliteReset", "sqliteColumnCount",
         "sqliteColumnText", "sqliteColumnInt", "sqliteColumnDouble",
         "sqliteColumnName", "sqliteColumnType", "sqliteColumnIsNull",
         "sqliteBindByName",
         "sqliteBindBlob", "sqliteColumnBlob",
         "sqliteFinalize"});

    cache_["std::postgres"] = createBuiltinModule("std::postgres",
        {"pgConnect", "pgClose", "pgExec", "pgErrmsg", "pgNormalizeParams",
         "pgQuery", "pgQueryParams", "pgResultRows", "pgResultCols",
         "pgResultText", "pgResultIsNull", "pgColumnName", "pgClear"});

    cache_["std::json"] = createBuiltinModule("std::json",
        {"jsonParse", "jsonFreeDoc", "jsonRoot", "jsonNodeKind",
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
         "jsonPathSetString", "jsonPathSetInt", "jsonPathSetFloat", "jsonPathSetBool"});

    cache_["std::log"] = createBuiltinModule("std::log",
        {"logDebug", "logInfo", "logWarn", "logError", "logSetLevel"});

    cache_["std::test"] = createBuiltinModule("std::test",
        {"assert", "assertMsg", "assertEq", "assertEqStr", "assertEqFloat",
         "testRunClosure"});

    cache_["std::datetime"] = createBuiltinModule("std::datetime",
        {"dateNow", "timeNow", "datetimeNow", "dateFormat",
         "dateYear", "dateMonth", "dateDay", "dateWeekday",
         "dateTimestamp", "dateParse", "dateAdd", "dateDiff",
         "dateHour", "dateMinute", "dateSecond",
         "isoFormatUtc", "isoParse"});

    cache_["std::compress"] = createBuiltinModule("std::compress",
        {"base64Encode", "base64Decode", "hexEncode", "hexDecode",
         "urlEncode", "urlDecode", "crc32",
         "urlScheme", "urlHost", "urlPort", "urlPath", "urlQuery", "urlFragment",
         "base64UrlEncode", "base64UrlDecode",
         "strToBytes", "bytesToStr",
         "hexEncodeBytes", "hexDecodeBytes",
         "base64UrlEncodeBytes", "base64UrlDecodeBytes",
         "gzipEncode", "gzipDecode"});

    cache_["std::crypto"] = createBuiltinModule("std::crypto",
        {"sha256", "md5", "hmacSha256",
         "sha1", "sha512", "hmacSha1", "hmacSha512",
         "base64Encode", "base64Decode", "hexEncode", "hexDecode",
         "urlEncode", "urlDecode", "crc32",
         "urlScheme", "urlHost", "urlPort", "urlPath", "urlQuery", "urlFragment",
         "base64UrlEncode", "base64UrlDecode",
         "jwtHS256Sig", "jwtHS512Sig",
         "constTimeEq"});

    cache_["std::toml"] = createBuiltinModule("std::toml",
        {"tomlParse", "tomlGetString", "tomlGetInt",
         "tomlGetBool", "tomlHasKey", "tomlFree"});

    cache_["std::sync"] = createBuiltinModule("std::sync",
        {"mutexCreate", "mutexLock", "mutexUnlock", "mutexTryLock", "mutexFree",
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
         "taskIsDone", "taskCancel", "taskIsCancelled"});

    // std::async — async concurrency utilities
    cache_["std::async"] = createBuiltinModule("std::async",
        {"taskSelect", "withTimeout",
         "taskIsDone", "taskCancel", "taskIsCancelled",
         "taskGroupCreate", "taskGroupSpawn", "taskGroupAwaitAll",
         "taskGroupCancelAll", "taskGroupCount", "taskGroupFree",
         "channelCreate", "channelSend", "channelReceive",
         "channelTrySend", "channelTryReceive",
         "channelClose", "channelLen", "channelFree",
         "schedulerInit", "schedulerShutdown", "schedulerWorkerCount",
         "asyncFileRead", "asyncFileWrite"});

    // std::collections — collection utility functions
    cache_["std::collections"] = createBuiltinModule("std::collections",
        {"forEach", "map", "filter", "reduce",
         "enumerate", "zip", "sorted", "reversed",
         "flatten", "any", "all", "count"},
        {"Map", "Set"});

    // std::strings — string utility functions
    cache_["std::strings"] = createBuiltinModule("std::strings",
        {"strRepeat", "strPadLeft", "strPadRight",
         "strContains", "strReplace", "strSplit",
         "strJoin", "strTrim", "strTrimLeft", "strTrimRight",
         "strStartsWith", "strEndsWith",
         "strToUpper", "strToLower",
         "strReverse", "strChars", "strLines",
         "strCharCount", "strCodepointAt", "strIsAscii",
         "charIsAlpha", "charIsDigit", "charIsAlnum",
         "charIsSpace", "charIsUpper", "charIsLower",
         "charToUpper", "charToLower"});

    // std::ui — UI library (wxWidgets backend)
    cache_["std::ui"] = createBuiltinModule("std::ui",
        {// App lifecycle
         "appInit", "appRun", "appQuit",
         // Window
         "createWindow", "windowShow", "windowSetTitle",
         "windowGetWidth", "windowGetHeight", "windowOnClose",
         // Widget creation
         "createPanel", "createButton", "createLabel", "createTextInput",
         "createCheckbox", "createSlider", "createProgressBar",
         "createRadioGroup", "createDropdown", "createTextArea",
         "createListBox", "createTabView", "createScrollView",
         "createImageView", "createDivider",
         // Widget properties
         "setText", "getText", "setValue", "getValue",
         "setEnabled", "setVisible", "setWidgetSize", "setWidgetFont",
         "setBgColor", "setFgColor", "setTooltip", "destroyWidget", "setBounds",
         // Layout (sizers)
         "createVBoxSizer", "createHBoxSizer", "createGridSizer",
         "createFlexGridSizer", "sizerAdd", "setSizer",
         // Events
         "onClick", "onChange", "onSelect", "onKey",
         // List / Tab operations
         "listAddItem", "listClear", "listGetSelection",
         "tabAddPage", "tabGetSelection",
         // Dialogs
         "messageBox", "fileDialog", "colorDialog",
         // Timer
         "createTimer", "stopTimer",
         // Clipboard
         "getClipboardText", "setClipboardText",
         // Canvas / custom drawing
         "createCanvas", "canvasOnPaint", "canvasRefresh",
         "dcClear", "dcDrawRect", "dcDrawText", "dcDrawLine", "dcDrawCircle",
         // Menu / app frame (Phase 2)
         "createMenuBar", "createMenu", "menuAddItem", "menuAddCheckItem",
         "menuAddSeparator", "menuAddSubmenu", "menuBarAddMenu", "windowSetMenuBar",
         "menuItemSetEnabled", "menuItemSetChecked", "menuItemOnClick", "menuPopup",
         "onRightClick", "createStatusBar", "statusBarSetText",
         "createToolbar", "toolbarAddTool", "toolbarAddSeparator", "toolbarRealize",
         "toolItemSetEnabled", "toolItemOnClick",
         // Phase 3: new widgets
         "createSpinCtrl", "createDatePicker", "dateGetValue",
         "createComboBox", "comboAddItem",
         "createTreeView", "treeAddRoot", "treeAddNode", "treeGetSelection",
         "createDataGrid", "gridSetCell", "gridGetCell", "gridSetColLabel",
         "createSplitter", "splitterSplitV", "splitterSplitH", "splitterSetSash",
         // Phase 4: align/anchors
         "setAlign", "setAnchors",
         // Phase 5: data binding
         "modelCreate", "modelSetText", "modelGetText", "modelBindText",
         "modelSetInt", "modelGetInt", "modelBindInt"});

    // std — umbrella module (union of all sub-modules + len)
    auto umbrella = std::make_unique<Module>();
    umbrella->name = "std";
    for (const auto &sub : {"std::math", "std::io", "std::convert",
                             "std::os", "std::random", "std::regex", "std::net",
                             "std::json", "std::log", "std::test",
                             "std::datetime", "std::compress", "std::crypto",
                             "std::sync", "std::async",
                             "std::collections", "std::strings", "std::ui"}) {
        for (const auto &sym : cache_[sub]->exportedSymbols)
            umbrella->exportedSymbols.push_back(sym);
    }
    Symbol lenSym;
    lenSym.name = "len";
    lenSym.kind = Symbol::Kind::Function;
    lenSym.isPublic = true;
    umbrella->exportedSymbols.push_back(lenSym);
    cache_["std"] = std::move(umbrella);
}

void ModuleLoader::registerSource(const std::string &name, const std::string &source) {
    testSources_[name] = source;
}

std::string ModuleLoader::resolveModuleName(const std::vector<std::string> &path) {
    std::string result;
    for (size_t i = 0; i < path.size(); ++i) {
        if (i > 0)
            result += "::";
        result += path[i];
    }
    return result;
}

std::string ModuleLoader::resolveFilePath(const std::vector<std::string> &path) {
    // Build relative filename from import path
    std::string relative;
    for (size_t i = 0; i < path.size(); ++i) {
        if (i > 0)
            relative += "/";
        relative += path[i];
    }
    relative += ".liva";

    // 1. Try basePath (existing behavior)
    std::string candidate = basePath_ + relative;
    { std::ifstream f(candidate); if (f.is_open()) return candidate; }

    // 2. Try additional search paths
    for (const auto &sp : searchPaths_) {
        candidate = sp;
        if (!candidate.empty() && candidate.back() != '/' && candidate.back() != '\\')
            candidate += "/";
        candidate += relative;
        std::ifstream f(candidate);
        if (f.is_open()) return candidate;
    }

    // Fallback to basePath (will produce appropriate error)
    return basePath_ + relative;
}

void ModuleLoader::collectExportedSymbols(TranslationUnit &tu, std::vector<Symbol> &out) {
    for (auto &decl : tu.getDeclarations()) {
        // Re-export symbols from imported modules (transitive export)
        if (auto *imp = dynamic_cast<ImportDecl *>(decl.get())) {
            std::string modName;
            for (size_t i = 0; i < imp->getPath().size(); ++i) {
                if (i > 0) modName += "::";
                modName += imp->getPath()[i];
            }
            auto cacheIt = cache_.find(modName);
            if (cacheIt != cache_.end()) {
                for (auto &sym : cacheIt->second->exportedSymbols) {
                    out.push_back(sym);
                }
            }
        } else if (auto *f = dynamic_cast<FuncDecl *>(decl.get())) {
            if (f->isPublic()) {
                Symbol sym;
                sym.name = f->getName();
                sym.kind = Symbol::Kind::Function;
                sym.funcDecl = f;
                sym.type = f->getReturnType();
                sym.isPublic = true;
                out.push_back(sym);
            }
        } else if (auto *s = dynamic_cast<StructDecl *>(decl.get())) {
            if (s->isPublic()) {
                Symbol sym;
                sym.name = s->getName();
                sym.kind = Symbol::Kind::StructType;
                sym.structDecl = s;
                sym.isPublic = true;
                out.push_back(sym);
            }
        } else if (auto *e = dynamic_cast<EnumDecl *>(decl.get())) {
            if (e->isPublic()) {
                Symbol sym;
                sym.name = e->getName();
                sym.kind = Symbol::Kind::EnumType;
                sym.enumDecl = e;
                sym.isPublic = true;
                out.push_back(sym);
            }
        } else if (auto *p = dynamic_cast<ProtocolDecl *>(decl.get())) {
            if (p->isPublic()) {
                Symbol sym;
                sym.name = p->getName();
                sym.kind = Symbol::Kind::ProtocolType;
                sym.protocolDecl = p;
                sym.isPublic = true;
                out.push_back(sym);
            }
        } else if (auto *c = dynamic_cast<ClassDecl *>(decl.get())) {
            if (c->isPublic()) {
                Symbol sym;
                sym.name = c->getName();
                sym.kind = Symbol::Kind::ClassType;
                sym.classDecl = c;
                sym.isPublic = true;
                out.push_back(sym);
            }
        }
    }
}

ModuleLoader::Module *ModuleLoader::loadModule(
    const std::vector<std::string> &importPath,
    DiagnosticsEngine &callerDiag,
    SourceLocation loc) {

    std::string moduleName = resolveModuleName(importPath);

    // Check cache
    auto cacheIt = cache_.find(moduleName);
    if (cacheIt != cache_.end())
        return cacheIt->second.get();

    // Circular import detection
    if (loading_.count(moduleName)) {
        callerDiag.report(loc, DiagID::err_circular_import, moduleName);
        return nullptr;
    }
    loading_.insert(moduleName);

    // Find source code
    std::string source;
    std::string filename;
    auto testIt = testSources_.find(moduleName);
    if (testIt != testSources_.end()) {
        source = testIt->second;
        filename = moduleName + ".liva";
    } else {
        filename = resolveFilePath(importPath);
        std::ifstream file(filename);
        if (!file.is_open()) {
            loading_.erase(moduleName);
            callerDiag.report(loc, DiagID::err_module_not_found, moduleName);
            return nullptr;
        }
        std::stringstream ss;
        ss << file.rdbuf();
        source = ss.str();
    }

    // Parse
    auto mod = std::make_unique<Module>();
    mod->name = moduleName;
    mod->sm = std::make_unique<SourceManager>(filename, source);
    mod->diag.setSourceManager(mod->sm.get());

    Lexer lexer(*mod->sm, mod->diag);
    Parser parser(lexer, mod->diag);
    mod->tu = parser.parseTranslationUnit();

    if (mod->diag.hasErrors()) {
        loading_.erase(moduleName);
        callerDiag.report(loc, DiagID::err_module_not_found, moduleName);
        return nullptr;
    }

    // Type-check the module (with this loader for recursive imports)
    Sema sema(mod->diag, this);
    sema.analyze(*mod->tu);

    if (mod->diag.hasErrors()) {
        loading_.erase(moduleName);
        callerDiag.report(loc, DiagID::err_module_not_found, moduleName);
        return nullptr;
    }

    // Collect exported symbols
    collectExportedSymbols(*mod->tu, mod->exportedSymbols);

    loading_.erase(moduleName);
    auto *ptr = mod.get();
    cache_[moduleName] = std::move(mod);
    return ptr;
}

} // namespace liva
