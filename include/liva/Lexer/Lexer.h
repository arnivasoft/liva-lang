#pragma once

#include "liva/Common/Diagnostics.h"
#include "liva/Common/SourceLocation.h"
#include "liva/Lexer/Token.h"
#include <string_view>
#include <vector>

namespace liva {

/// Lexer: converts source text into a stream of tokens
class Lexer {
public:
    Lexer(const SourceManager &sm, DiagnosticsEngine &diag);

    /// Lex the next token
    Token nextToken();

    /// Lex all tokens at once
    std::vector<Token> lexAll();

    /// Peek at the next token without consuming
    Token peekToken();

    /// Check if we've reached end of input
    bool isEof() const { return currentPos_ >= source_.size(); }

private:
    void advance();
    char peek() const;
    char peekNext() const;
    char current() const;
    bool match(char expected);

    void skipWhitespace();
    void skipLineComment();
    bool skipBlockComment();

    Token lexIdentifierOrKeyword();
    Token lexNumber();
    Token lexString();
    Token lexStringContinuation();
    Token lexChar();
    Token lexDocComment();

    Token makeToken(TokenKind kind, size_t startOffset);
    Token makeToken(TokenKind kind, size_t startOffset, std::string_view text);

    SourceLocation currentLocation() const;

    DiagnosticsEngine &diag_;
    std::string_view source_;

    size_t currentPos_ = 0;
    uint32_t currentLine_ = 1;
    uint32_t currentColumn_ = 1;

    // For peekToken
    bool hasPeeked_ = false;
    Token peekedToken_;

    // String interpolation state
    bool inInterpolation_ = false;
    int interpParenDepth_ = 0;
    bool continueString_ = false;
};

} // namespace liva
