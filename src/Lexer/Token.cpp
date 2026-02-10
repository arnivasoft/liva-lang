#include "liva/Lexer/Token.h"
#include <charconv>
#include <cstdlib>
#include <unordered_map>

namespace liva {

const char *getTokenSpelling(TokenKind kind) {
    switch (kind) {
#define TOKEN(ID, Spelling)                                                                        \
    case TokenKind::ID:                                                                            \
        return Spelling;
#include "liva/Lexer/TokenKinds.def"
    default:
        return "<unknown>";
    }
}

const char *getTokenKindName(TokenKind kind) {
    switch (kind) {
#define TOKEN(ID, Spelling)                                                                        \
    case TokenKind::ID:                                                                            \
        return #ID;
#include "liva/Lexer/TokenKinds.def"
    default:
        return "<unknown>";
    }
}

bool isKeyword(TokenKind kind) {
    switch (kind) {
#define KEYWORD(ID, Spelling)                                                                      \
    case TokenKind::kw_##ID:                                                                       \
        return true;
#define TOKEN(ID, Spelling)
#define PUNCTUATOR(ID, Spelling)
#include "liva/Lexer/TokenKinds.def"
    default:
        return false;
    }
}

TokenKind lookupKeyword(std::string_view identifier) {
    static const std::unordered_map<std::string_view, TokenKind> keywords = {
#define TOKEN(ID, Spelling)
#define PUNCTUATOR(ID, Spelling)
#define KEYWORD(ID, Spelling) {Spelling, TokenKind::kw_##ID},
#include "liva/Lexer/TokenKinds.def"
    };

    auto it = keywords.find(identifier);
    if (it != keywords.end()) {
        return it->second;
    }
    return TokenKind::identifier;
}

std::string Token::getStringValue() const {
    if (text_.empty())
        return "";

    // Determine the raw content range based on token kind
    size_t start = 0;
    size_t end = text_.size();

    if (kind_ == TokenKind::string_literal) {
        // Normal string: skip opening and closing quotes
        if (text_.size() < 2) return "";
        start = 1;
        end = text_.size() - 1;
    } else if (kind_ == TokenKind::string_interp_begin) {
        // Token text: "hello  (from " up to \( exclusive, \( not consumed yet)
        // Strip only the opening "
        start = 1;
        end = text_.size();
    } else if (kind_ == TokenKind::string_interp_mid) {
        // Token text: raw content between ) and \( (no quotes, no \()
        start = 0;
        end = text_.size();
    } else if (kind_ == TokenKind::string_interp_end) {
        // Token text: content" (closing " consumed by advance before makeToken)
        // Strip the closing "
        start = 0;
        end = (text_.size() >= 1) ? text_.size() - 1 : text_.size();
    } else {
        // Fallback for other token types
        if (text_.size() < 2) return "";
        start = 1;
        end = text_.size() - 1;
    }

    // Process escape sequences
    std::string result;
    for (size_t i = start; i < end; ++i) {
        if (text_[i] == '\\' && i + 1 < end) {
            ++i;
            switch (text_[i]) {
            case 'n':
                result += '\n';
                break;
            case 't':
                result += '\t';
                break;
            case 'r':
                result += '\r';
                break;
            case '\\':
                result += '\\';
                break;
            case '"':
                result += '"';
                break;
            case '\'':
                result += '\'';
                break;
            case '0':
                result += '\0';
                break;
            default:
                result += text_[i];
                break;
            }
        } else {
            result += text_[i];
        }
    }
    return result;
}

int64_t Token::getIntegerValue() const {
    int64_t result = 0;
    auto sv = text_;

    // Handle hex, octal, binary prefixes
    if (sv.size() > 2 && sv[0] == '0') {
        if (sv[1] == 'x' || sv[1] == 'X') {
            std::from_chars(sv.data() + 2, sv.data() + sv.size(), result, 16);
            return result;
        }
        if (sv[1] == 'b' || sv[1] == 'B') {
            std::from_chars(sv.data() + 2, sv.data() + sv.size(), result, 2);
            return result;
        }
        if (sv[1] == 'o' || sv[1] == 'O') {
            std::from_chars(sv.data() + 2, sv.data() + sv.size(), result, 8);
            return result;
        }
    }

    std::from_chars(sv.data(), sv.data() + sv.size(), result);
    return result;
}

double Token::getFloatValue() const {
    // std::from_chars for double may not be available on all platforms
    return std::strtod(std::string(text_).c_str(), nullptr);
}

std::string Token::toString() const {
    std::string result;
    result += getTokenKindName(kind_);
    if (kind_ == TokenKind::identifier || kind_ == TokenKind::integer_literal ||
        kind_ == TokenKind::float_literal || kind_ == TokenKind::string_literal ||
        kind_ == TokenKind::string_interp_begin || kind_ == TokenKind::string_interp_mid ||
        kind_ == TokenKind::string_interp_end) {
        result += " '";
        result += text_;
        result += "'";
    }
    result += " [";
    result += std::to_string(location_.line);
    result += ":";
    result += std::to_string(location_.column);
    result += "]";
    return result;
}

} // namespace liva
