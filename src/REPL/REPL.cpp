#include "liva/REPL/REPL.h"
#include "liva/Common/Diagnostics.h"
#include "liva/Common/SourceLocation.h"
#include "liva/Lexer/Lexer.h"
#include "liva/Parser/Parser.h"
#include "liva/Sema/Sema.h"
#include <algorithm>
#include <sstream>

namespace liva {

// ── Helper: extract symbol name from a declaration string ───────────────

static std::string extractDeclName(const std::string &decl) {
    size_t pos = decl.find_first_not_of(" \t");
    if (pos == std::string::npos)
        return "";
    size_t wend = decl.find_first_of(" \t\r\n({", pos);
    std::string kw = (wend != std::string::npos) ? decl.substr(pos, wend - pos)
                                                  : decl.substr(pos);
    if (kw == "pub") {
        pos = decl.find_first_not_of(" \t", wend);
        if (pos == std::string::npos)
            return "";
        wend = decl.find_first_of(" \t\r\n({", pos);
        kw = (wend != std::string::npos) ? decl.substr(pos, wend - pos)
                                          : decl.substr(pos);
    }
    if (kw == "func" || kw == "struct" || kw == "class" || kw == "enum" ||
        kw == "protocol" || kw == "type" || kw == "let" || kw == "var" ||
        kw == "const" || kw == "macro") {
        pos = decl.find_first_not_of(" \t", wend);
        if (pos == std::string::npos)
            return "";
        wend = decl.find_first_of(" \t\r\n({:=<", pos);
        return (wend != std::string::npos) ? decl.substr(pos, wend - pos)
                                            : decl.substr(pos);
    }
    return "";
}

// ── Constructor / Reset ─────────────────────────────────────────────────

REPLSession::REPLSession() = default;

void REPLSession::reset() {
    declarations_.clear();
    declaredSymbols_.clear();
    multilineBuffer_.clear();
    lineNumber_ = 1;
}

// ── Public Queries ──────────────────────────────────────────────────────

bool REPLSession::isIncomplete() const {
    return !multilineBuffer_.empty();
}

const std::vector<std::string> &REPLSession::getDeclarations() const {
    return declarations_;
}

std::string REPLSession::getPrompt() const {
    return isIncomplete() ? "... " : ">>> ";
}

// ── processLine — Main Entry Point ──────────────────────────────────────

REPLResult REPLSession::processLine(const std::string &line) {
    // If we're accumulating multi-line input, append this line
    if (!multilineBuffer_.empty()) {
        multilineBuffer_ += "\n" + line;
        if (hasUnclosedDelimiters(multilineBuffer_)) {
            return REPLResult{REPLResult::Incomplete, {}, {}, false};
        }
        // Multi-line input is now complete — process accumulated buffer
        std::string completeInput = multilineBuffer_;
        multilineBuffer_.clear();

        InputKind kind = classifyInput(completeInput);
        if (kind == InputKind::Declaration) {
            std::string errorMsg;
            if (!validateDeclaration(completeInput, errorMsg)) {
                REPLResult r;
                r.kind = REPLResult::Error;
                r.output = errorMsg;
                return r;
            }
            declarations_.push_back(completeInput);
            { auto sym = extractDeclName(completeInput);
              if (!sym.empty()) declaredSymbols_.push_back(sym); }
            REPLResult r;
            r.kind = REPLResult::Declaration;
            r.output = "Declaration added.";
            return r;
        }
        // Statement (if/while/for) — put directly in main without wrapping
        if (kind == InputKind::Statement) {
            std::string source = generateFullSource(completeInput, false);
            std::string errorMsg;
            if (!validateSource(source, errorMsg)) {
                REPLResult r;
                r.kind = REPLResult::Error;
                r.output = errorMsg;
                return r;
            }
            REPLResult r;
            r.kind = REPLResult::Statement;
            r.generatedCode = source;
            r.needsExecution = true;
            return r;
        }
        // Expression
        std::string wrapped = wrapExpression(completeInput);
        std::string source = generateFullSource(wrapped, true);
        std::string errorMsg;
        if (!validateSource(source, errorMsg)) {
            REPLResult r;
            r.kind = REPLResult::Error;
            r.output = errorMsg;
            return r;
        }
        REPLResult r;
        r.kind = REPLResult::Expression;
        r.generatedCode = source;
        r.needsExecution = true;
        return r;
    }

    // Trim leading whitespace for classification
    size_t firstNonSpace = line.find_first_not_of(" \t\r\n");
    if (firstNonSpace == std::string::npos) {
        return REPLResult{REPLResult::Empty, {}, {}, false};
    }

    std::string trimmed = line.substr(firstNonSpace);

    InputKind kind = classifyInput(trimmed);

    switch (kind) {
    case InputKind::Empty:
        return REPLResult{REPLResult::Empty, {}, {}, false};

    case InputKind::Command:
        return handleCommand(trimmed);

    case InputKind::Declaration: {
        // Import declarations: add directly without sema validation
        // (ModuleLoader is not available in REPL, but imports are needed
        // for generated code to work when compiled)
        {
            size_t ws = trimmed.find_first_of(" \t");
            std::string fw = (ws != std::string::npos) ? trimmed.substr(0, ws) : trimmed;
            if (fw == "import") {
                declarations_.push_back(trimmed);
                REPLResult r;
                r.kind = REPLResult::Declaration;
                r.output = "Import added.";
                return r;
            }
        }
        // Check for unclosed delimiters — start multi-line
        if (hasUnclosedDelimiters(trimmed)) {
            multilineBuffer_ = trimmed;
            return REPLResult{REPLResult::Incomplete, {}, {}, false};
        }
        std::string errorMsg;
        if (!validateDeclaration(trimmed, errorMsg)) {
            REPLResult r;
            r.kind = REPLResult::Error;
            r.output = errorMsg;
            return r;
        }
        declarations_.push_back(trimmed);
        { auto sym = extractDeclName(trimmed);
          if (!sym.empty()) declaredSymbols_.push_back(sym); }
        REPLResult r;
        r.kind = REPLResult::Declaration;
        r.output = "Declaration added.";
        return r;
    }

    case InputKind::Statement: {
        // Check for unclosed delimiters — start multi-line
        if (hasUnclosedDelimiters(trimmed)) {
            multilineBuffer_ = trimmed;
            return REPLResult{REPLResult::Incomplete, {}, {}, false};
        }
        std::string source = generateFullSource(trimmed, false);
        std::string errorMsg;
        if (!validateSource(source, errorMsg)) {
            REPLResult r;
            r.kind = REPLResult::Error;
            r.output = errorMsg;
            return r;
        }
        REPLResult r;
        r.kind = REPLResult::Statement;
        r.generatedCode = source;
        r.needsExecution = true;
        return r;
    }

    case InputKind::Expression: {
        // Check for unclosed delimiters — start multi-line
        if (hasUnclosedDelimiters(trimmed)) {
            multilineBuffer_ = trimmed;
            return REPLResult{REPLResult::Incomplete, {}, {}, false};
        }
        std::string wrapped = wrapExpression(trimmed);
        std::string source = generateFullSource(wrapped, true);
        std::string errorMsg;
        if (!validateSource(source, errorMsg)) {
            REPLResult r;
            r.kind = REPLResult::Error;
            r.output = errorMsg;
            return r;
        }
        REPLResult r;
        r.kind = REPLResult::Expression;
        r.generatedCode = source;
        r.needsExecution = true;
        return r;
    }
    }

    return REPLResult{REPLResult::Empty, {}, {}, false};
}

// ── classifyInput ───────────────────────────────────────────────────────

REPLSession::InputKind
REPLSession::classifyInput(const std::string &input) const {
    if (input.empty())
        return InputKind::Empty;

    // Commands start with ':'
    if (input[0] == ':')
        return InputKind::Command;

    // Extract first word
    size_t wordEnd = input.find_first_of(" \t\r\n({");
    std::string firstWord =
        (wordEnd != std::string::npos) ? input.substr(0, wordEnd) : input;

    // Declaration keywords
    if (firstWord == "func" || firstWord == "struct" || firstWord == "class" ||
        firstWord == "enum" || firstWord == "protocol" || firstWord == "impl" ||
        firstWord == "type" || firstWord == "import" || firstWord == "pub" ||
        firstWord == "let" || firstWord == "var" || firstWord == "const" ||
        firstWord == "extern")
        return InputKind::Declaration;

    // Statement keywords (executed inside main)
    if (firstWord == "if" || firstWord == "while" || firstWord == "for" ||
        firstWord == "guard" || firstWord == "return" || firstWord == "break" ||
        firstWord == "continue")
        return InputKind::Statement;

    return InputKind::Expression;
}

// ── hasUnclosedDelimiters ───────────────────────────────────────────────

bool REPLSession::hasUnclosedDelimiters(const std::string &input) const {
    int braces = 0;   // { }
    int parens = 0;    // ( )
    int brackets = 0;  // [ ]
    bool inString = false;

    for (size_t i = 0; i < input.size(); ++i) {
        char c = input[i];

        // Handle escape sequences inside strings
        if (inString && c == '\\') {
            ++i; // skip next char
            continue;
        }

        if (c == '"') {
            inString = !inString;
            continue;
        }

        if (inString)
            continue;

        // Skip single-line comments
        if (c == '/' && i + 1 < input.size() && input[i + 1] == '/') {
            // Skip to end of line
            while (i < input.size() && input[i] != '\n')
                ++i;
            continue;
        }

        switch (c) {
        case '{': ++braces; break;
        case '}': --braces; break;
        case '(': ++parens; break;
        case ')': --parens; break;
        case '[': ++brackets; break;
        case ']': --brackets; break;
        default: break;
        }
    }

    return braces > 0 || parens > 0 || brackets > 0;
}

// ── generateFullSource ──────────────────────────────────────────────────

// Top-level `var`/`let` are rejected by Sema (err_global_var_unsupported):
// the REPL keeps interactive variable declarations in declarations_, but
// they must be emitted INSIDE the synthesized main body, where they are
// legal. Everything else (func/struct/const/...) stays at top level.
static bool isVarLetDecl(const std::string &decl) {
    size_t start = decl.find_first_not_of(" \t");
    if (start == std::string::npos)
        return false;
    size_t end = decl.find_first_of(" \t(", start);
    std::string first = decl.substr(
        start, end == std::string::npos ? std::string::npos : end - start);
    return first == "let" || first == "var";
}

std::string
REPLSession::generateFullSource(const std::string &exprOrStmt,
                                bool isExpression) const {
    std::string source;
    for (const auto &decl : declarations_)
        if (!isVarLetDecl(decl))
            source += decl + "\n";

    source += "func main() {\n";
    for (const auto &decl : declarations_)
        if (isVarLetDecl(decl))
            source += "    " + decl + "\n";
    if (isExpression)
        source += "    " + exprOrStmt + "\n";
    else
        source += "    " + exprOrStmt + "\n";
    source += "}\n";
    return source;
}

// ── wrapExpression ──────────────────────────────────────────────────────

std::string REPLSession::wrapExpression(const std::string &expr) const {
    // Don't double-wrap if already println() or print()
    // Trim for comparison
    std::string trimmed = expr;
    size_t start = trimmed.find_first_not_of(" \t");
    if (start != std::string::npos)
        trimmed = trimmed.substr(start);

    if (trimmed.substr(0, 8) == "println(" ||
        trimmed.substr(0, 6) == "print(")
        return expr;

    // Also don't wrap statements that aren't expressions (let, var, etc.)
    size_t ws = trimmed.find_first_of(" \t(");
    std::string first =
        (ws != std::string::npos) ? trimmed.substr(0, ws) : trimmed;
    if (first == "let" || first == "var" || first == "const" ||
        first == "return" || first == "if" || first == "while" ||
        first == "for" || first == "guard")
        return expr;

    return "println(" + expr + ")";
}

// ── validateSource ──────────────────────────────────────────────────────

bool REPLSession::validateSource(const std::string &source,
                                 std::string &errorOut) const {
    SourceManager sm("<repl>", source);
    DiagnosticsEngine diag(&sm);

    Lexer lexer(sm, diag);
    Parser parser(lexer, diag);
    auto tu = parser.parseTranslationUnit();

    if (diag.hasErrors()) {
        std::ostringstream oss;
        for (const auto &d : diag.getDiagnostics()) {
            if (d.level == DiagLevel::Error)
                oss << "error: " << d.message << "\n";
        }
        errorOut = oss.str();
        return false;
    }

    if (tu) {
        Sema sema(diag, nullptr);
        sema.analyze(*tu);
        if (diag.hasErrors()) {
            std::ostringstream oss;
            for (const auto &d : diag.getDiagnostics()) {
                if (d.level == DiagLevel::Error)
                    oss << "error: " << d.message << "\n";
            }
            errorOut = oss.str();
            return false;
        }
    }

    return true;
}

// ── validateDeclaration ─────────────────────────────────────────────────

bool REPLSession::validateDeclaration(const std::string &decl,
                                      std::string &errorOut) const {
    // Build a source with all existing declarations + new one. `var`/`let`
    // declarations go INSIDE the synthesized main (top-level var/let is a
    // Sema error — err_global_var_unsupported); the rest stay top-level.
    std::string source;
    for (const auto &d : declarations_)
        if (!isVarLetDecl(d))
            source += d + "\n";
    if (!isVarLetDecl(decl))
        source += decl + "\n";

    source += "func main() {\n";
    for (const auto &d : declarations_)
        if (isVarLetDecl(d))
            source += "    " + d + "\n";
    if (isVarLetDecl(decl))
        source += "    " + decl + "\n";
    source += "}\n";

    SourceManager sm("<repl>", source);
    DiagnosticsEngine diag(&sm);

    Lexer lexer(sm, diag);
    Parser parser(lexer, diag);
    auto tu = parser.parseTranslationUnit();

    if (diag.hasErrors()) {
        std::ostringstream oss;
        for (const auto &d : diag.getDiagnostics()) {
            if (d.level == DiagLevel::Error)
                oss << "error: " << d.message << "\n";
        }
        errorOut = oss.str();
        return false;
    }

    if (tu) {
        Sema sema(diag, nullptr);
        sema.analyze(*tu);
        if (diag.hasErrors()) {
            std::ostringstream oss;
            for (const auto &d : diag.getDiagnostics()) {
                if (d.level == DiagLevel::Error)
                    oss << "error: " << d.message << "\n";
            }
            errorOut = oss.str();
            return false;
        }
    }

    return true;
}

// ── handleCommand ───────────────────────────────────────────────────────

REPLResult REPLSession::handleCommand(const std::string &cmd) {
    // Extract command name (strip leading ':')
    std::string name = cmd.substr(1);
    // Trim trailing whitespace
    size_t end = name.find_last_not_of(" \t\r\n");
    if (end != std::string::npos)
        name = name.substr(0, end + 1);

    if (name == "quit" || name == "q") {
        REPLResult r;
        r.kind = REPLResult::Quit;
        r.output = "Goodbye!";
        return r;
    }

    if (name == "help" || name == "h") {
        REPLResult r;
        r.kind = REPLResult::Help;
        r.output =
            "Liva REPL Commands:\n"
            "  :help, :h           Show this help\n"
            "  :quit, :q           Exit the REPL\n"
            "  :reset, :r          Clear all declarations\n"
            "  :declarations, :decls  Show accumulated declarations\n"
            "\n"
            "Enter expressions to evaluate or declarations to accumulate.\n"
            "Multi-line input is supported with unclosed braces/parens.\n";
        return r;
    }

    if (name == "reset" || name == "r") {
        reset();
        REPLResult r;
        r.kind = REPLResult::Reset;
        r.output = "Session reset.";
        return r;
    }

    if (name == "declarations" || name == "decls") {
        REPLResult r;
        r.kind = REPLResult::ShowDecls;
        if (declarations_.empty()) {
            r.output = "No declarations.";
        } else {
            std::ostringstream oss;
            for (size_t i = 0; i < declarations_.size(); ++i) {
                oss << "[" << i << "] " << declarations_[i] << "\n";
            }
            r.output = oss.str();
        }
        return r;
    }

    REPLResult r;
    r.kind = REPLResult::CommandError;
    r.output = "Unknown command: " + cmd;
    return r;
}

// ── extractCurrentWord ──────────────────────────────────────────────────

std::string REPLSession::extractCurrentWord(const std::string &line,
                                            size_t cursorPos) {
    if (cursorPos == 0 || line.empty())
        return "";
    if (cursorPos > line.size())
        cursorPos = line.size();

    // For command lines starting with ':', include ':' in the word
    size_t firstNonSpace = line.find_first_not_of(" \t");
    bool isCommand = (firstNonSpace != std::string::npos && line[firstNonSpace] == ':');

    size_t end = cursorPos;
    size_t start = end;
    while (start > 0) {
        char c = line[start - 1];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '_' ||
            (isCommand && c == ':')) {
            --start;
        } else {
            break;
        }
    }
    return line.substr(start, end - start);
}

// ── getCompletions ──────────────────────────────────────────────────────

std::vector<std::string>
REPLSession::getCompletions(const std::string &lineBuffer,
                            size_t cursorPos) const {
    std::string prefix = extractCurrentWord(lineBuffer, cursorPos);

    // Command completions
    size_t firstNonSpace = lineBuffer.find_first_not_of(" \t");
    bool isCommand = (firstNonSpace != std::string::npos &&
                      lineBuffer[firstNonSpace] == ':');

    std::vector<std::string> candidates;

    if (isCommand) {
        static const char *commands[] = {
            ":declarations", ":decls", ":h", ":help", ":q", ":quit", ":r", ":reset"
        };
        for (const char *cmd : commands) {
            std::string s(cmd);
            if (prefix.empty() || s.substr(0, prefix.size()) == prefix)
                candidates.push_back(s);
        }
    } else {
        // Keywords
        static const char *keywords[] = {
            "as",       "async",    "await",   "break",    "class",
            "comptime", "const",    "continue","defer",    "dyn",
            "else",     "enum",     "extern",  "false",    "for",      "func",
            "guard",    "if",       "impl",    "import",   "in",
            "let",      "match",    "nil",     "protocol", "pub",
            "ref",      "return",   "struct",  "true",     "type",
            "var",      "while",    "macro"
        };
        // Builtins
        static const char *builtins[] = {
            "abs",      "len",      "max",     "min",      "print",
            "println",  "readLine", "toFloat", "toInt",    "toString",
            "typeof",   "assert",   "panic",   "drop",     "clone",
            "sorted",   "reversed", "enumerate","zip",     "flatten",
            "any",      "all",      "count",   "forEach",
            "strRepeat","strPadLeft","strPadRight","strJoin",
            "strTrim",  "strTrimLeft","strTrimRight",
            "strReverse","strChars","strLines",
            "strContains","strStartsWith","strEndsWith",
            "strReplace","strSplit","strToUpper","strToLower"
        };
        // Primitive types
        static const char *primitives[] = {
            "bool",  "f32",   "f64",   "i16",  "i32",
            "i64",   "i8",    "string","u16",  "u32",
            "u64",   "u8"
        };

        for (const char *kw : keywords) {
            std::string s(kw);
            if (prefix.empty() || s.substr(0, prefix.size()) == prefix)
                candidates.push_back(s);
        }
        for (const char *bi : builtins) {
            std::string s(bi);
            if (prefix.empty() || s.substr(0, prefix.size()) == prefix)
                candidates.push_back(s);
        }
        for (const char *pt : primitives) {
            std::string s(pt);
            if (prefix.empty() || s.substr(0, prefix.size()) == prefix)
                candidates.push_back(s);
        }
        for (const auto &sym : declaredSymbols_) {
            if (prefix.empty() || sym.substr(0, prefix.size()) == prefix)
                candidates.push_back(sym);
        }
    }

    std::sort(candidates.begin(), candidates.end());
    // Remove duplicates
    candidates.erase(std::unique(candidates.begin(), candidates.end()),
                     candidates.end());
    return candidates;
}

} // namespace liva
