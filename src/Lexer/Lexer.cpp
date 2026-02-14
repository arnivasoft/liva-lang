#include "liva/Lexer/Lexer.h"
#include <cctype>

namespace liva {

Lexer::Lexer(const SourceManager &sm, DiagnosticsEngine &diag)
    : diag_(diag), source_(sm.getSource()) {}

void Lexer::advance() {
    if (currentPos_ < source_.size()) {
        if (source_[currentPos_] == '\n') {
            ++currentLine_;
            currentColumn_ = 1;
        } else {
            ++currentColumn_;
        }
        ++currentPos_;
    }
}

char Lexer::peek() const {
    if (currentPos_ < source_.size())
        return source_[currentPos_];
    return '\0';
}

char Lexer::peekNext() const {
    if (currentPos_ + 1 < source_.size())
        return source_[currentPos_ + 1];
    return '\0';
}

char Lexer::current() const { return peek(); }

bool Lexer::match(char expected) {
    if (peek() == expected) {
        advance();
        return true;
    }
    return false;
}

SourceLocation Lexer::currentLocation() const {
    return {currentLine_, currentColumn_, static_cast<uint32_t>(currentPos_)};
}

void Lexer::skipWhitespace() {
    while (currentPos_ < source_.size()) {
        char c = source_[currentPos_];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            advance();
        } else if (c == '/' && peekNext() == '/') {
            // Check for doc comment (///) — don't skip, let nextToken() handle it
            if (currentPos_ + 2 < source_.size() && source_[currentPos_ + 2] == '/')
                break;
            skipLineComment();
        } else if (c == '/' && peekNext() == '*') {
            if (!skipBlockComment())
                return;
        } else {
            break;
        }
    }
}

void Lexer::skipLineComment() {
    // Skip //
    advance();
    advance();
    while (currentPos_ < source_.size() && source_[currentPos_] != '\n') {
        advance();
    }
}

bool Lexer::skipBlockComment() {
    auto startLoc = currentLocation();
    // Skip /*
    advance();
    advance();

    int depth = 1;
    while (currentPos_ < source_.size() && depth > 0) {
        if (source_[currentPos_] == '/' && peekNext() == '*') {
            advance();
            advance();
            ++depth;
        } else if (source_[currentPos_] == '*' && peekNext() == '/') {
            advance();
            advance();
            --depth;
        } else {
            advance();
        }
    }

    if (depth > 0) {
        diag_.report(startLoc, DiagID::err_unterminated_block_comment);
        return false;
    }
    return true;
}

Token Lexer::nextToken() {
    if (hasPeeked_) {
        hasPeeked_ = false;
        return peekedToken_;
    }

    skipWhitespace();

    if (isEof()) {
        return Token(TokenKind::eof, currentLocation(), "");
    }

    size_t startOffset = currentPos_;
    auto startLoc = currentLocation();
    char c = source_[currentPos_];

    // Doc comments: /// ...
    if (c == '/' && currentPos_ + 2 < source_.size() &&
        source_[currentPos_ + 1] == '/' && source_[currentPos_ + 2] == '/') {
        return lexDocComment();
    }

    // Identifiers and keywords
    if (std::isalpha(c) || c == '_') {
        return lexIdentifierOrKeyword();
    }

    // Number literals
    if (std::isdigit(c)) {
        return lexNumber();
    }

    // String continuation after interpolation closing )
    if (continueString_) {
        return lexStringContinuation();
    }

    // String literals
    if (c == '"') {
        return lexString();
    }

    // Character literals
    if (c == '\'') {
        return lexChar();
    }

    // Punctuators and operators
    advance();
    switch (c) {
    case '(':
        if (inInterpolation_) ++interpParenDepth_;
        return makeToken(TokenKind::l_paren, startOffset);
    case ')':
        if (inInterpolation_) {
            --interpParenDepth_;
            if (interpParenDepth_ == 0) {
                inInterpolation_ = false;
                continueString_ = true;
                // Don't emit r_paren — jump straight to string continuation
                return lexStringContinuation();
            }
        }
        return makeToken(TokenKind::r_paren, startOffset);
    case '{':
        return makeToken(TokenKind::l_brace, startOffset);
    case '}':
        return makeToken(TokenKind::r_brace, startOffset);
    case '[':
        return makeToken(TokenKind::l_bracket, startOffset);
    case ']':
        return makeToken(TokenKind::r_bracket, startOffset);
    case ',':
        return makeToken(TokenKind::comma, startOffset);
    case ';':
        return makeToken(TokenKind::semicolon, startOffset);
    case '@':
        return makeToken(TokenKind::at, startOffset);
    case '#':
        return makeToken(TokenKind::hash, startOffset);
    case '?':
        if (match('?'))
            return makeToken(TokenKind::question_question, startOffset);
        if (match('.'))
            return makeToken(TokenKind::question_dot, startOffset);
        return makeToken(TokenKind::question, startOffset);
    case '~':
        return makeToken(TokenKind::tilde, startOffset);
    case '$':
        return makeToken(TokenKind::dollar, startOffset);

    case ':':
        if (match(':'))
            return makeToken(TokenKind::coloncolon, startOffset);
        return makeToken(TokenKind::colon, startOffset);

    case '.':
        if (match('.')) {
            if (match('.'))
                return makeToken(TokenKind::ellipsis, startOffset);
            return makeToken(TokenKind::dotdot, startOffset);
        }
        return makeToken(TokenKind::dot, startOffset);

    case '+':
        if (match('='))
            return makeToken(TokenKind::plus_equal, startOffset);
        return makeToken(TokenKind::plus, startOffset);

    case '-':
        if (match('>'))
            return makeToken(TokenKind::arrow, startOffset);
        if (match('='))
            return makeToken(TokenKind::minus_equal, startOffset);
        return makeToken(TokenKind::minus, startOffset);

    case '*':
        if (match('='))
            return makeToken(TokenKind::star_equal, startOffset);
        return makeToken(TokenKind::star, startOffset);

    case '/':
        if (match('='))
            return makeToken(TokenKind::slash_equal, startOffset);
        return makeToken(TokenKind::slash, startOffset);

    case '%':
        if (match('='))
            return makeToken(TokenKind::percent_equal, startOffset);
        return makeToken(TokenKind::percent, startOffset);

    case '=':
        if (match('='))
            return makeToken(TokenKind::equal_equal, startOffset);
        if (match('>'))
            return makeToken(TokenKind::fat_arrow, startOffset);
        return makeToken(TokenKind::equal, startOffset);

    case '!':
        if (match('='))
            return makeToken(TokenKind::bang_equal, startOffset);
        return makeToken(TokenKind::bang, startOffset);

    case '<':
        if (match('='))
            return makeToken(TokenKind::less_equal, startOffset);
        if (match('<'))
            return makeToken(TokenKind::less_less, startOffset);
        return makeToken(TokenKind::less, startOffset);

    case '>':
        if (match('='))
            return makeToken(TokenKind::greater_equal, startOffset);
        if (match('>'))
            return makeToken(TokenKind::greater_greater, startOffset);
        return makeToken(TokenKind::greater, startOffset);

    case '&':
        if (match('&'))
            return makeToken(TokenKind::amp_amp, startOffset);
        return makeToken(TokenKind::amp, startOffset);

    case '|':
        if (match('|'))
            return makeToken(TokenKind::pipe_pipe, startOffset);
        return makeToken(TokenKind::pipe, startOffset);

    case '^':
        return makeToken(TokenKind::caret, startOffset);

    default:
        diag_.report(startLoc, DiagID::err_unexpected_character, std::string(1, c));
        return nextToken(); // Skip and continue
    }
}

Token Lexer::peekToken() {
    if (!hasPeeked_) {
        peekedToken_ = nextToken();
        hasPeeked_ = true;
    }
    return peekedToken_;
}

std::vector<Token> Lexer::lexAll() {
    std::vector<Token> tokens;
    while (true) {
        Token tok = nextToken();
        tokens.push_back(tok);
        if (tok.is(TokenKind::eof))
            break;
    }
    return tokens;
}

Token Lexer::lexIdentifierOrKeyword() {
    size_t startOffset = currentPos_;
    while (currentPos_ < source_.size() &&
           (std::isalnum(source_[currentPos_]) || source_[currentPos_] == '_')) {
        advance();
    }

    std::string_view text = source_.substr(startOffset, currentPos_ - startOffset);
    TokenKind kind = lookupKeyword(text);

    // Handle true/false as bool_literal
    if (kind == TokenKind::kw_true || kind == TokenKind::kw_false) {
        return makeToken(TokenKind::bool_literal, startOffset, text);
    }

    // Handle _ as underscore token
    if (text == "_") {
        return makeToken(TokenKind::underscore, startOffset, text);
    }

    return makeToken(kind, startOffset, text);
}

Token Lexer::lexNumber() {
    size_t startOffset = currentPos_;
    bool isFloat = false;

    // Check for hex, binary, octal prefix
    if (source_[currentPos_] == '0' && currentPos_ + 1 < source_.size()) {
        char next = source_[currentPos_ + 1];
        if (next == 'x' || next == 'X') {
            advance(); // skip 0
            advance(); // skip x
            while (currentPos_ < source_.size() &&
                   (std::isxdigit(source_[currentPos_]) || source_[currentPos_] == '_')) {
                advance();
            }
            return makeToken(TokenKind::integer_literal, startOffset);
        }
        if (next == 'b' || next == 'B') {
            advance(); // skip 0
            advance(); // skip b
            while (currentPos_ < source_.size() &&
                   (source_[currentPos_] == '0' || source_[currentPos_] == '1' ||
                    source_[currentPos_] == '_')) {
                advance();
            }
            return makeToken(TokenKind::integer_literal, startOffset);
        }
        if (next == 'o' || next == 'O') {
            advance(); // skip 0
            advance(); // skip o
            while (currentPos_ < source_.size() &&
                   ((source_[currentPos_] >= '0' && source_[currentPos_] <= '7') ||
                    source_[currentPos_] == '_')) {
                advance();
            }
            return makeToken(TokenKind::integer_literal, startOffset);
        }
    }

    // Decimal digits
    while (currentPos_ < source_.size() &&
           (std::isdigit(source_[currentPos_]) || source_[currentPos_] == '_')) {
        advance();
    }

    // Decimal point
    if (currentPos_ < source_.size() && source_[currentPos_] == '.' &&
        currentPos_ + 1 < source_.size() && std::isdigit(source_[currentPos_ + 1])) {
        isFloat = true;
        advance(); // skip .
        while (currentPos_ < source_.size() &&
               (std::isdigit(source_[currentPos_]) || source_[currentPos_] == '_')) {
            advance();
        }
    }

    // Exponent
    if (currentPos_ < source_.size() &&
        (source_[currentPos_] == 'e' || source_[currentPos_] == 'E')) {
        isFloat = true;
        advance();
        if (currentPos_ < source_.size() &&
            (source_[currentPos_] == '+' || source_[currentPos_] == '-')) {
            advance();
        }
        while (currentPos_ < source_.size() && std::isdigit(source_[currentPos_])) {
            advance();
        }
    }

    // Type suffix (u8, i32, f64, etc.)
    // We don't consume type suffixes as part of the number literal for now

    TokenKind kind = isFloat ? TokenKind::float_literal : TokenKind::integer_literal;
    return makeToken(kind, startOffset);
}

Token Lexer::lexString() {
    auto startLoc = currentLocation();
    size_t startOffset = currentPos_;
    advance(); // skip opening "

    // Check for triple-quoted multi-line string: """..."""
    if (currentPos_ + 1 < source_.size() &&
        source_[currentPos_] == '"' && source_[currentPos_ + 1] == '"') {
        advance(); // skip second "
        advance(); // skip third "
        // Skip optional newline right after opening """
        if (currentPos_ < source_.size() && source_[currentPos_] == '\n')
            advance();
        else if (currentPos_ + 1 < source_.size() &&
                 source_[currentPos_] == '\r' && source_[currentPos_ + 1] == '\n') {
            advance(); advance();
        }
        // Scan until closing """
        while (currentPos_ + 2 < source_.size()) {
            if (source_[currentPos_] == '"' &&
                source_[currentPos_ + 1] == '"' &&
                source_[currentPos_ + 2] == '"') {
                advance(); advance(); advance(); // skip closing """
                return makeToken(TokenKind::string_literal, startOffset);
            }
            if (source_[currentPos_] == '\\') {
                advance(); // skip backslash
                if (currentPos_ < source_.size()) advance(); // skip escaped char
            } else {
                advance();
            }
        }
        diag_.report(startLoc, DiagID::err_unterminated_string);
        return makeToken(TokenKind::string_literal, startOffset);
    }

    while (currentPos_ < source_.size() && source_[currentPos_] != '"') {
        if (source_[currentPos_] == '\\') {
            // Check for string interpolation: \(
            if (currentPos_ + 1 < source_.size() && source_[currentPos_ + 1] == '(') {
                // Everything from opening " to here is string_interp_begin
                // We include the opening " but NOT the \(
                auto tok = makeToken(TokenKind::string_interp_begin, startOffset);
                advance(); // skip backslash
                advance(); // skip (
                inInterpolation_ = true;
                interpParenDepth_ = 1;
                return tok;
            }
            advance(); // skip backslash
            if (currentPos_ < source_.size()) {
                char escaped = source_[currentPos_];
                if (escaped == 'u') {
                    // Unicode escape: \u{XXXX}
                    advance(); // skip 'u'
                    if (currentPos_ < source_.size() && source_[currentPos_] == '{') {
                        advance(); // skip '{'
                        while (currentPos_ < source_.size() && source_[currentPos_] != '}') {
                            advance();
                        }
                        if (currentPos_ < source_.size()) {
                            advance(); // skip '}'
                        }
                    } else {
                        diag_.report(currentLocation(), DiagID::err_invalid_escape_sequence,
                                     std::string("u (expected \\u{XXXX})"));
                    }
                    continue;
                }
                if (escaped != 'n' && escaped != 't' && escaped != 'r' && escaped != '\\' &&
                    escaped != '"' && escaped != '\'' && escaped != '0') {
                    diag_.report(currentLocation(), DiagID::err_invalid_escape_sequence,
                                 std::string(1, escaped));
                }
                advance(); // skip escaped char
            }
        } else if (source_[currentPos_] == '\n') {
            diag_.report(startLoc, DiagID::err_unterminated_string);
            break;
        } else {
            advance();
        }
    }

    if (currentPos_ >= source_.size()) {
        diag_.report(startLoc, DiagID::err_unterminated_string);
    } else {
        advance(); // skip closing "
    }

    return makeToken(TokenKind::string_literal, startOffset);
}

Token Lexer::lexStringContinuation() {
    continueString_ = false;
    size_t startOffset = currentPos_;

    while (currentPos_ < source_.size() && source_[currentPos_] != '"') {
        if (source_[currentPos_] == '\\') {
            // Check for another interpolation: \(
            if (currentPos_ + 1 < source_.size() && source_[currentPos_ + 1] == '(') {
                auto tok = makeToken(TokenKind::string_interp_mid, startOffset);
                advance(); // skip backslash
                advance(); // skip (
                inInterpolation_ = true;
                interpParenDepth_ = 1;
                return tok;
            }
            advance(); // skip backslash
            if (currentPos_ < source_.size())
                advance(); // skip escaped char
        } else if (source_[currentPos_] == '\n') {
            break;
        } else {
            advance();
        }
    }

    if (currentPos_ < source_.size() && source_[currentPos_] == '"') {
        advance(); // skip closing "
    }

    return makeToken(TokenKind::string_interp_end, startOffset);
}

Token Lexer::lexChar() {
    auto startLoc = currentLocation();
    size_t startOffset = currentPos_;
    advance(); // skip opening '

    if (currentPos_ < source_.size() && source_[currentPos_] == '\'') {
        diag_.report(startLoc, DiagID::err_empty_char_literal);
        advance();
        return makeToken(TokenKind::char_literal, startOffset);
    }

    if (currentPos_ < source_.size() && source_[currentPos_] == '\\') {
        advance(); // skip backslash
        if (currentPos_ < source_.size())
            advance(); // skip escaped char
    } else if (currentPos_ < source_.size()) {
        advance();
    }

    if (currentPos_ < source_.size() && source_[currentPos_] == '\'') {
        advance(); // skip closing '
    }

    return makeToken(TokenKind::char_literal, startOffset);
}

Token Lexer::lexDocComment() {
    size_t startOffset = currentPos_;
    // Skip the ///
    advance(); // /
    advance(); // /
    advance(); // /
    // Skip optional leading space
    if (currentPos_ < source_.size() && source_[currentPos_] == ' ')
        advance();
    // Consume until end of line
    size_t textStart = currentPos_;
    while (currentPos_ < source_.size() && source_[currentPos_] != '\n') {
        advance();
    }
    std::string_view text = source_.substr(textStart, currentPos_ - textStart);
    return makeToken(TokenKind::doc_comment, startOffset, text);
}

Token Lexer::makeToken(TokenKind kind, size_t startOffset) {
    std::string_view text = source_.substr(startOffset, currentPos_ - startOffset);
    SourceLocation loc = {
        currentLine_,
        static_cast<uint32_t>(currentColumn_ - (currentPos_ - startOffset)),
        static_cast<uint32_t>(startOffset)};
    return Token(kind, loc, text);
}

Token Lexer::makeToken(TokenKind kind, size_t startOffset, std::string_view text) {
    SourceLocation loc = {
        currentLine_,
        static_cast<uint32_t>(currentColumn_ - (currentPos_ - startOffset)),
        static_cast<uint32_t>(startOffset)};
    return Token(kind, loc, text);
}

} // namespace liva
