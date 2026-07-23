#pragma once

#include "liva/Common/SourceLocation.h"
#include <memory>
#include <string>
#include <vector>

namespace liva {

/// Base class for match-arm patterns (Pattern AST — Faz A).
///
/// toString() reproduces the legacy parser's whitespace-free token
/// concatenation byte-for-byte (see MatchArm::pattern in Expr.h); this
/// invariant is what lets the parser produce both representations from a
/// single token pass during the transitional period.
class Pattern {
public:
    enum class Kind {
        Wildcard, Identifier, EnumCase, IntLiteral,
        // Pattern Types Faz B, Task 2:
        BoolLiteral, StringLiteral, FloatLiteral
    };

    explicit Pattern(Kind k, SourceRange r) : kind_(k), range_(r) {}
    virtual ~Pattern() = default;

    Kind getKind() const { return kind_; }
    SourceRange getRange() const { return range_; }

    /// Legacy-identical text: whitespace-free token concatenation.
    /// DISPLAY ONLY (ASTPrinter, diagnostics) — do not use this to derive a
    /// binding/identifier name; use getSpelling() for that (Pattern Types
    /// Faz B, Task 1: the two calls used to be aliases, and code that wanted
    /// a load-bearing name reused this display helper by coincidence).
    std::string toString() const;

    /// Load-bearing identifier derivation: the exact text used to name a
    /// variable binding introduced by this (sub)pattern (e.g. an
    /// IntLiteral subslot's binding name in the legacy string-splitter
    /// fallback, or declarePatternSubBinding's symbol name). Currently
    /// identical to toString() byte-for-byte, but the two are kept as
    /// separate entry points so a future pattern kind can make toString()'s
    /// display form diverge from the spelling used for binding-name
    /// derivation without silently breaking the other.
    std::string getSpelling() const;

private:
    Kind kind_;
    SourceRange range_;
};

/// `_`
class WildcardPattern : public Pattern {
public:
    explicit WildcardPattern(SourceRange r) : Pattern(Kind::Wildcard, r) {}

    static bool classof(const Pattern *p) { return p->getKind() == Kind::Wildcard; }
};

/// Bare identifier: either a binding (`x`) or an unqualified enum case
/// without a payload (`Red`) — indistinguishable at parse time.
class IdentifierPattern : public Pattern {
public:
    IdentifierPattern(std::string name, SourceRange r)
        : Pattern(Kind::Identifier, r), name_(std::move(name)) {}

    const std::string &getName() const { return name_; }

    static bool classof(const Pattern *p) { return p->getKind() == Kind::Identifier; }

private:
    std::string name_;
};

/// `Case`, `Case(sub, ...)`, `Enum.Case`, `Enum.Case(sub, ...)`.
/// `enumName_` is empty for the unqualified `Case(sub, ...)` form.
class EnumCasePattern : public Pattern {
public:
    EnumCasePattern(std::string enumName, std::string caseName,
                    std::vector<std::unique_ptr<Pattern>> subs, SourceRange r)
        : Pattern(Kind::EnumCase, r), enumName_(std::move(enumName)),
          caseName_(std::move(caseName)), subpatterns_(std::move(subs)) {}

    const std::string &getEnumName() const { return enumName_; } // bare Case => empty
    const std::string &getCaseName() const { return caseName_; }

    /// Distinguishes `Case()`/`Case(...)` from bare `Case` for toString().
    bool hasParens() const { return hasParens_; }
    void setHasParens(bool v) { hasParens_ = v; }

    const std::vector<std::unique_ptr<Pattern>> &getSubpatterns() const { return subpatterns_; }

    static bool classof(const Pattern *p) { return p->getKind() == Kind::EnumCase; }

private:
    std::string enumName_, caseName_;
    bool hasParens_ = false;
    std::vector<std::unique_ptr<Pattern>> subpatterns_;
};

/// Integer literal pattern, including negative (`-1`): the token spelling
/// `-` + digits collapses into a single IntLiteralPattern to match the
/// legacy parser's blind whitespace-free token concatenation.
class IntLiteralPattern : public Pattern {
public:
    IntLiteralPattern(int64_t value, std::string text, SourceRange r)
        : Pattern(Kind::IntLiteral, r), value_(value), text_(std::move(text)) {}

    int64_t getValue() const { return value_; }
    const std::string &getText() const { return text_; } // original spelling (e.g. "-1")

    static bool classof(const Pattern *p) { return p->getKind() == Kind::IntLiteral; }

private:
    int64_t value_;
    std::string text_;
};

/// `true` / `false`.
class BoolLiteralPattern : public Pattern {
public:
    BoolLiteralPattern(bool value, SourceRange r)
        : Pattern(Kind::BoolLiteral, r), value_(value) {}

    bool getValue() const { return value_; }

    static bool classof(const Pattern *p) { return p->getKind() == Kind::BoolLiteral; }

private:
    bool value_;
};

/// `"GET"`, etc. `value_` is the unescaped string content — the value used
/// for runtime comparison (liva_str_equal). `sourceText_` is the original
/// raw source spelling INCLUDING the surrounding quotes (e.g. `"GET"`,
/// quotes and all) — used for toString()/getSpelling() display purposes,
/// mirroring IntLiteralPattern's getText() convention.
class StringLiteralPattern : public Pattern {
public:
    StringLiteralPattern(std::string value, std::string sourceText, SourceRange r)
        : Pattern(Kind::StringLiteral, r), value_(std::move(value)),
          sourceText_(std::move(sourceText)) {}

    const std::string &getValue() const { return value_; }          // unescaped
    const std::string &getSourceText() const { return sourceText_; } // e.g. "\"GET\""

    static bool classof(const Pattern *p) { return p->getKind() == Kind::StringLiteral; }

private:
    std::string value_;
    std::string sourceText_;
};

/// `3.14`, etc.
class FloatLiteralPattern : public Pattern {
public:
    FloatLiteralPattern(double value, std::string text, SourceRange r)
        : Pattern(Kind::FloatLiteral, r), value_(value), text_(std::move(text)) {}

    double getValue() const { return value_; }
    const std::string &getText() const { return text_; } // original spelling (e.g. "3.14")

    static bool classof(const Pattern *p) { return p->getKind() == Kind::FloatLiteral; }

private:
    double value_;
    std::string text_;
};

} // namespace liva
