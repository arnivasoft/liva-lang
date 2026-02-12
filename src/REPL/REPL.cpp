#include "liva/REPL/REPL.h"
#include "liva/Common/Diagnostics.h"
#include "liva/Common/SourceLocation.h"
#include "liva/Lexer/Lexer.h"
#include "liva/Parser/Parser.h"
#include "liva/Sema/Sema.h"
#include <algorithm>
#include <sstream>

namespace liva {

// ── Constructor / Reset ─────────────────────────────────────────────────

REPLSession::REPLSession() = default;

void REPLSession::reset() {
    declarations_.clear();
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
            return REPLResult{REPLResult::Incomplete};
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
            REPLResult r;
            r.kind = REPLResult::Declaration;
            r.output = "Declaration added.";
            return r;
        }
        // Expression or statement
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
        return REPLResult{REPLResult::Empty};
    }

    std::string trimmed = line.substr(firstNonSpace);

    InputKind kind = classifyInput(trimmed);

    switch (kind) {
    case InputKind::Empty:
        return REPLResult{REPLResult::Empty};

    case InputKind::Command:
        return handleCommand(trimmed);

    case InputKind::Declaration: {
        // Check for unclosed delimiters — start multi-line
        if (hasUnclosedDelimiters(trimmed)) {
            multilineBuffer_ = trimmed;
            return REPLResult{REPLResult::Incomplete};
        }
        std::string errorMsg;
        if (!validateDeclaration(trimmed, errorMsg)) {
            REPLResult r;
            r.kind = REPLResult::Error;
            r.output = errorMsg;
            return r;
        }
        declarations_.push_back(trimmed);
        REPLResult r;
        r.kind = REPLResult::Declaration;
        r.output = "Declaration added.";
        return r;
    }

    case InputKind::Expression: {
        // Check for unclosed delimiters — start multi-line
        if (hasUnclosedDelimiters(trimmed)) {
            multilineBuffer_ = trimmed;
            return REPLResult{REPLResult::Incomplete};
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

    return REPLResult{REPLResult::Empty};
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
    if (firstWord == "func" || firstWord == "struct" || firstWord == "enum" ||
        firstWord == "protocol" || firstWord == "impl" || firstWord == "type" ||
        firstWord == "import" || firstWord == "pub" || firstWord == "let" ||
        firstWord == "var" || firstWord == "const")
        return InputKind::Declaration;

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

std::string
REPLSession::generateFullSource(const std::string &exprOrStmt,
                                bool isExpression) const {
    std::string source;
    for (const auto &decl : declarations_)
        source += decl + "\n";

    source += "func main() {\n";
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
    // Build a source with all existing declarations + new one
    std::string source;
    for (const auto &d : declarations_)
        source += d + "\n";
    source += decl + "\n";

    // Add an empty main() so the source is a valid translation unit
    source += "func main() {}\n";

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

} // namespace liva
