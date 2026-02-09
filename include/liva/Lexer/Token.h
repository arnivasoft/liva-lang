#pragma once

#include "liva/Common/SourceLocation.h"
#include <cstdint>
#include <string>
#include <string_view>

namespace liva {

/// Enumeration of all token kinds
enum class TokenKind : uint16_t {
#define TOKEN(ID, Spelling) ID,
#include "liva/Lexer/TokenKinds.def"
    NUM_TOKENS
};

/// Returns the spelling of a token kind (e.g., "let", "+", "identifier")
const char *getTokenSpelling(TokenKind kind);

/// Returns the token kind name (e.g., "kw_let", "plus", "identifier")
const char *getTokenKindName(TokenKind kind);

/// Check if token kind is a keyword
bool isKeyword(TokenKind kind);

/// Lookup an identifier to see if it's a keyword
TokenKind lookupKeyword(std::string_view identifier);

/// A single token produced by the lexer
class Token {
public:
    Token() : kind_(TokenKind::eof) {}
    Token(TokenKind kind, SourceLocation loc, std::string_view text)
        : kind_(kind), location_(loc), text_(text) {}

    TokenKind getKind() const { return kind_; }
    SourceLocation getLocation() const { return location_; }
    std::string_view getText() const { return text_; }

    bool is(TokenKind kind) const { return kind_ == kind; }

    template <typename... Kinds>
    bool isOneOf(TokenKind first, Kinds... rest) const {
        return is(first) || isOneOf(rest...);
    }
    bool isOneOf(TokenKind kind) const { return is(kind); }

    bool isNot(TokenKind kind) const { return kind_ != kind; }

    bool isKeyword() const { return liva::isKeyword(kind_); }

    /// For integer/float literals, get the numeric value as string
    std::string_view getLiteralValue() const { return text_; }

    /// For string literals, get the string content (without quotes)
    std::string getStringValue() const;

    /// For integer literals, get the integer value
    int64_t getIntegerValue() const;

    /// For float literals, get the float value
    double getFloatValue() const;

    /// Pretty-print the token
    std::string toString() const;

private:
    TokenKind kind_;
    SourceLocation location_;
    std::string_view text_;
};

} // namespace liva
