#pragma once

#include <string>
#include <vector>

namespace liva {

/// Result of processing a single REPL input line
struct REPLResult {
    enum Kind {
        Empty,        // Blank line
        Incomplete,   // Multi-line input continues
        Quit,         // :quit command
        Help,         // :help command
        Reset,        // :reset command
        ShowDecls,    // :declarations command
        CommandError, // Unknown command
        Declaration,  // Declaration added to session
        Statement,    // Statement processed (if/while/for)
        Expression,   // Expression processed
        Error         // Parse/sema error
    };

    Kind kind = Empty;
    std::string output;          // Text to display
    std::string generatedCode;   // Full .liva source for compile+execute
    bool needsExecution = false; // If true, Driver should compile+execute
};

/// Interactive REPL session that accumulates declarations
class REPLSession {
public:
    REPLSession();

    /// Process a single input line, returning the result
    REPLResult processLine(const std::string &line);

    /// Whether we are in the middle of multi-line input
    bool isIncomplete() const;

    /// Get accumulated declarations
    const std::vector<std::string> &getDeclarations() const;

    /// Get prompt string (">>> " or "... ")
    std::string getPrompt() const;

    /// Reset session state
    void reset();

private:
    enum class InputKind { Empty, Command, Declaration, Statement, Expression };

    InputKind classifyInput(const std::string &input) const;
    bool hasUnclosedDelimiters(const std::string &input) const;
    std::string generateFullSource(const std::string &exprOrStmt,
                                   bool isExpression) const;
    std::string wrapExpression(const std::string &expr) const;
    bool validateSource(const std::string &source,
                        std::string &errorOut) const;
    bool validateDeclaration(const std::string &decl,
                             std::string &errorOut) const;
    REPLResult handleCommand(const std::string &cmd);

    std::vector<std::string> declarations_;
    std::string multilineBuffer_;
    int lineNumber_ = 1;
};

} // namespace liva
