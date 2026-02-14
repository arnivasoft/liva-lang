#pragma once

#include "liva/Common/Diagnostics.h"
#include "liva/Lexer/Token.h"
#include <map>
#include <string>
#include <vector>

namespace liva {

/// Token wrapper for macro patterns and expansions
struct MacroToken {
    enum Kind { Regular, Variable, RepStart, RepEnd };
    Kind kind = Regular;
    Token token;              // Regular: actual token
    std::string varName;      // Variable: $name
    std::string fragSpec;     // Variable: "expr", "ident", "literal", "block"
    char repSeparator = ',';  // RepEnd: separator char
    bool repOneOrMore = false; // RepEnd: + vs *
    std::string ownedText;    // Regular: owned copy of token text (avoids dangling string_view)
};

/// A single arm of a macro definition
struct MacroArm {
    std::vector<MacroToken> pattern;
    std::vector<MacroToken> expansion;
};

/// Complete macro definition
struct MacroDef {
    std::string name;
    bool isPublic = false;
    std::vector<MacroArm> arms;
    SourceRange range;
};

/// Captured token sequence for a macro variable
struct MacroFragment {
    std::vector<Token> tokens;
};

/// Expands macro invocations by pattern matching and token substitution
class MacroExpander {
public:
    /// Register a macro definition
    void registerMacro(const MacroDef &def);

    /// Check if a macro is registered
    bool hasMacro(const std::string &name) const;

    /// Expand a macro invocation, returning the expanded source string.
    /// Returns empty string on failure (no matching arm).
    std::string expand(const std::string &name,
                       const std::vector<Token> &argTokens,
                       DiagnosticsEngine &diag,
                       SourceLocation invokeLoc) const;

    /// Parse raw macro body source into MacroDef arms
    static MacroDef parseMacroDef(const std::string &name, bool isPublic,
                                  const std::string &rawSource, SourceRange range);

private:
    /// Try to match argument tokens against a pattern arm
    bool matchArm(const MacroArm &arm,
                  const std::vector<Token> &tokens,
                  std::map<std::string, MacroFragment> &captures,
                  std::map<std::string, std::vector<MacroFragment>> &repCaptures) const;

    /// Substitute captures into an expansion template
    std::string substituteArm(
        const MacroArm &arm,
        const std::map<std::string, MacroFragment> &captures,
        const std::map<std::string, std::vector<MacroFragment>> &repCaptures) const;

    /// Convert a token sequence to source text
    std::string tokensToString(const std::vector<Token> &tokens) const;

    /// Token-to-string for a single token
    std::string tokenToString(const Token &tok) const;

    /// Capture a fragment according to its specifier
    bool captureFragment(const std::string &fragSpec,
                         const std::vector<Token> &tokens,
                         size_t &pos,
                         MacroFragment &out) const;

    std::map<std::string, MacroDef> macros_;
};

} // namespace liva
