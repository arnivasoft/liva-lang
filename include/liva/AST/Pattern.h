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
    enum class Kind { Wildcard, Identifier, EnumCase, IntLiteral };

    explicit Pattern(Kind k, SourceRange r) : kind_(k), range_(r) {}
    virtual ~Pattern() = default;

    Kind getKind() const { return kind_; }
    SourceRange getRange() const { return range_; }

    /// Legacy-identical text: whitespace-free token concatenation.
    std::string toString() const;

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

} // namespace liva
