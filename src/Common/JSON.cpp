#include "liva/Common/JSON.h"
#include <cstdio>
#include <cstdlib>

namespace liva {

// ============================================================
// JSONValue implementation
// ============================================================

static const std::string kEmptyString;
static const std::vector<JSONValue> kEmptyArray;
static const std::map<std::string, JSONValue> kEmptyObject;
static const JSONValue kNullValue;

JSONValue::JSONValue(const JSONValue &other)
    : kind_(other.kind_), boolVal_(other.boolVal_), intVal_(other.intVal_),
      doubleVal_(other.doubleVal_), stringVal_(other.stringVal_),
      arrayVal_(other.arrayVal_), objectVal_(other.objectVal_) {}

JSONValue::JSONValue(JSONValue &&other) noexcept
    : kind_(other.kind_), boolVal_(other.boolVal_), intVal_(other.intVal_),
      doubleVal_(other.doubleVal_), stringVal_(std::move(other.stringVal_)),
      arrayVal_(std::move(other.arrayVal_)),
      objectVal_(std::move(other.objectVal_)) {
    other.kind_ = Null;
}

JSONValue &JSONValue::operator=(const JSONValue &other) {
    if (this != &other) {
        kind_ = other.kind_;
        boolVal_ = other.boolVal_;
        intVal_ = other.intVal_;
        doubleVal_ = other.doubleVal_;
        stringVal_ = other.stringVal_;
        arrayVal_ = other.arrayVal_;
        objectVal_ = other.objectVal_;
    }
    return *this;
}

JSONValue &JSONValue::operator=(JSONValue &&other) noexcept {
    if (this != &other) {
        kind_ = other.kind_;
        boolVal_ = other.boolVal_;
        intVal_ = other.intVal_;
        doubleVal_ = other.doubleVal_;
        stringVal_ = std::move(other.stringVal_);
        arrayVal_ = std::move(other.arrayVal_);
        objectVal_ = std::move(other.objectVal_);
        other.kind_ = Null;
    }
    return *this;
}

const std::string &JSONValue::getString() const {
    return kind_ == String ? stringVal_ : kEmptyString;
}

const std::vector<JSONValue> &JSONValue::getArray() const {
    return kind_ == Array ? arrayVal_ : kEmptyArray;
}

const std::map<std::string, JSONValue> &JSONValue::getObject() const {
    return kind_ == Object ? objectVal_ : kEmptyObject;
}

const JSONValue &JSONValue::operator[](const std::string &key) const {
    if (kind_ != Object) return kNullValue;
    auto it = objectVal_.find(key);
    return it != objectVal_.end() ? it->second : kNullValue;
}

bool JSONValue::hasKey(const std::string &key) const {
    if (kind_ != Object) return false;
    return objectVal_.find(key) != objectVal_.end();
}

void JSONValue::set(const std::string &key, JSONValue val) {
    if (kind_ == Object) {
        objectVal_[key] = std::move(val);
    }
}

void JSONValue::push(JSONValue val) {
    if (kind_ == Array) {
        arrayVal_.push_back(std::move(val));
    }
}

JSONValue JSONValue::object() {
    return JSONValue(std::map<std::string, JSONValue>{});
}

JSONValue JSONValue::array() {
    return JSONValue(std::vector<JSONValue>{});
}

// ============================================================
// JSON Serialization
// ============================================================

static std::string escapeJSONString(const std::string &s) {
    std::string result;
    result.reserve(s.size() + 4);
    for (char c : s) {
        switch (c) {
        case '"':  result += "\\\""; break;
        case '\\': result += "\\\\"; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        case '\b': result += "\\b"; break;
        case '\f': result += "\\f"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x",
                              static_cast<unsigned>(static_cast<unsigned char>(c)));
                result += buf;
            } else {
                result += c;
            }
            break;
        }
    }
    return result;
}

std::string JSONValue::serialize() const {
    switch (kind_) {
    case Null: return "null";
    case Bool: return boolVal_ ? "true" : "false";
    case Integer: {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(intVal_));
        return buf;
    }
    case Double: {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.17g", doubleVal_);
        return buf;
    }
    case String:
        return "\"" + escapeJSONString(stringVal_) + "\"";
    case Array: {
        std::string result = "[";
        for (size_t i = 0; i < arrayVal_.size(); ++i) {
            if (i > 0) result += ",";
            result += arrayVal_[i].serialize();
        }
        result += "]";
        return result;
    }
    case Object: {
        std::string result = "{";
        bool first = true;
        for (const auto &kv : objectVal_) {
            if (!first) result += ",";
            first = false;
            result += "\"" + escapeJSONString(kv.first) + "\":" + kv.second.serialize();
        }
        result += "}";
        return result;
    }
    }
    return "null";
}

// ============================================================
// JSON Parser (recursive descent)
// ============================================================

namespace {

class JSONParser {
public:
    explicit JSONParser(const std::string &input) : input_(input), pos_(0) {}

    JSONParseResult parse() {
        JSONParseResult result;
        skipWhitespace();
        if (pos_ >= input_.size()) {
            result.error = "empty input";
            return result;
        }
        result.value = parseValue();
        if (!error_.empty()) {
            result.error = error_;
            return result;
        }
        result.success = true;
        return result;
    }

private:
    const std::string &input_;
    size_t pos_;
    std::string error_;

    void skipWhitespace() {
        while (pos_ < input_.size() &&
               (input_[pos_] == ' ' || input_[pos_] == '\t' ||
                input_[pos_] == '\n' || input_[pos_] == '\r')) {
            ++pos_;
        }
    }

    char peek() const {
        return pos_ < input_.size() ? input_[pos_] : '\0';
    }

    char advance() {
        return pos_ < input_.size() ? input_[pos_++] : '\0';
    }

    JSONValue parseValue() {
        skipWhitespace();
        if (pos_ >= input_.size()) {
            error_ = "unexpected end of input";
            return JSONValue();
        }
        char c = peek();
        if (c == '"') return parseString();
        if (c == '{') return parseObject();
        if (c == '[') return parseArray();
        if (c == 't' || c == 'f') return parseBool();
        if (c == 'n') return parseNull();
        if (c == '-' || (c >= '0' && c <= '9')) return parseNumber();
        error_ = std::string("unexpected character '") + c + "'";
        return JSONValue();
    }

    JSONValue parseString() {
        if (advance() != '"') {
            error_ = "expected '\"'";
            return JSONValue();
        }
        std::string result;
        while (pos_ < input_.size()) {
            char c = advance();
            if (c == '"') return JSONValue(result);
            if (c == '\\') {
                if (pos_ >= input_.size()) {
                    error_ = "unterminated escape";
                    return JSONValue();
                }
                char esc = advance();
                switch (esc) {
                case '"':  result += '"'; break;
                case '\\': result += '\\'; break;
                case '/':  result += '/'; break;
                case 'b':  result += '\b'; break;
                case 'f':  result += '\f'; break;
                case 'n':  result += '\n'; break;
                case 'r':  result += '\r'; break;
                case 't':  result += '\t'; break;
                case 'u': {
                    if (pos_ + 4 > input_.size()) {
                        error_ = "incomplete unicode escape";
                        return JSONValue();
                    }
                    std::string hex = input_.substr(pos_, 4);
                    pos_ += 4;
                    unsigned long cp = std::strtoul(hex.c_str(), nullptr, 16);
                    if (cp < 0x80) {
                        result += static_cast<char>(cp);
                    } else if (cp < 0x800) {
                        result += static_cast<char>(0xC0 | (cp >> 6));
                        result += static_cast<char>(0x80 | (cp & 0x3F));
                    } else {
                        result += static_cast<char>(0xE0 | (cp >> 12));
                        result += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                        result += static_cast<char>(0x80 | (cp & 0x3F));
                    }
                    break;
                }
                default:
                    error_ = std::string("unknown escape '\\") + esc + "'";
                    return JSONValue();
                }
            } else {
                result += c;
            }
        }
        error_ = "unterminated string";
        return JSONValue();
    }

    JSONValue parseNumber() {
        size_t start = pos_;
        if (peek() == '-') ++pos_;
        while (pos_ < input_.size() && input_[pos_] >= '0' && input_[pos_] <= '9')
            ++pos_;
        bool isFloat = false;
        if (pos_ < input_.size() && input_[pos_] == '.') {
            isFloat = true;
            ++pos_;
            while (pos_ < input_.size() && input_[pos_] >= '0' && input_[pos_] <= '9')
                ++pos_;
        }
        if (pos_ < input_.size() && (input_[pos_] == 'e' || input_[pos_] == 'E')) {
            isFloat = true;
            ++pos_;
            if (pos_ < input_.size() && (input_[pos_] == '+' || input_[pos_] == '-'))
                ++pos_;
            while (pos_ < input_.size() && input_[pos_] >= '0' && input_[pos_] <= '9')
                ++pos_;
        }
        std::string numStr = input_.substr(start, pos_ - start);
        if (isFloat) {
            char *end = nullptr;
            double d = std::strtod(numStr.c_str(), &end);
            return JSONValue(d);
        } else {
            char *end = nullptr;
            long long ll = std::strtoll(numStr.c_str(), &end, 10);
            return JSONValue(static_cast<int64_t>(ll));
        }
    }

    JSONValue parseArray() {
        advance(); // '['
        skipWhitespace();
        auto arr = JSONValue::array();
        if (peek() == ']') {
            advance();
            return arr;
        }
        while (true) {
            JSONValue val = parseValue();
            if (!error_.empty()) return JSONValue();
            arr.push(std::move(val));
            skipWhitespace();
            if (peek() == ',') {
                advance();
                skipWhitespace();
            } else if (peek() == ']') {
                advance();
                return arr;
            } else {
                error_ = "expected ',' or ']' in array";
                return JSONValue();
            }
        }
    }

    JSONValue parseObject() {
        advance(); // '{'
        skipWhitespace();
        auto obj = JSONValue::object();
        if (peek() == '}') {
            advance();
            return obj;
        }
        while (true) {
            skipWhitespace();
            if (peek() != '"') {
                error_ = "expected string key in object";
                return JSONValue();
            }
            JSONValue keyVal = parseString();
            if (!error_.empty()) return JSONValue();
            skipWhitespace();
            if (advance() != ':') {
                error_ = "expected ':' after object key";
                return JSONValue();
            }
            JSONValue val = parseValue();
            if (!error_.empty()) return JSONValue();
            obj.set(keyVal.getString(), std::move(val));
            skipWhitespace();
            if (peek() == ',') {
                advance();
                skipWhitespace();
            } else if (peek() == '}') {
                advance();
                return obj;
            } else {
                error_ = "expected ',' or '}' in object";
                return JSONValue();
            }
        }
    }

    JSONValue parseBool() {
        if (input_.compare(pos_, 4, "true") == 0) {
            pos_ += 4;
            return JSONValue(true);
        }
        if (input_.compare(pos_, 5, "false") == 0) {
            pos_ += 5;
            return JSONValue(false);
        }
        error_ = "invalid literal";
        return JSONValue();
    }

    JSONValue parseNull() {
        if (input_.compare(pos_, 4, "null") == 0) {
            pos_ += 4;
            return JSONValue();
        }
        error_ = "invalid literal";
        return JSONValue();
    }
};

} // anonymous namespace

JSONParseResult parseJSON(const std::string &input) {
    JSONParser parser(input);
    return parser.parse();
}

// ============================================================
// json:: convenience helpers
// ============================================================

namespace json {

// --- JSONValue-based overloads ---

std::string getString(const JSONValue &obj, const std::string &key) {
    return obj[key].getString();
}

std::vector<std::string> getStringArray(const JSONValue &obj, const std::string &key) {
    std::vector<std::string> result;
    const auto &arr = obj[key].getArray();
    for (const auto &elem : arr) {
        if (elem.isString())
            result.push_back(elem.getString());
    }
    return result;
}

std::map<std::string, std::string> getObject(const JSONValue &obj, const std::string &key) {
    std::map<std::string, std::string> result;
    const auto &inner = obj[key].getObject();
    for (const auto &kv : inner) {
        if (kv.second.isString())
            result[kv.first] = kv.second.getString();
        else
            result[kv.first] = kv.second.serialize();
    }
    return result;
}

// --- Raw string overloads (backward compatible) ---

std::string getString(const std::string &jsonStr, const std::string &key) {
    auto result = parseJSON(jsonStr);
    if (!result.success) return "";
    return result.value[key].getString();
}

std::vector<std::string> getStringArray(const std::string &jsonStr, const std::string &key) {
    auto result = parseJSON(jsonStr);
    if (!result.success) return {};
    std::vector<std::string> out;
    const auto &arr = result.value[key].getArray();
    for (const auto &elem : arr) {
        if (elem.isString())
            out.push_back(elem.getString());
    }
    return out;
}

std::unordered_map<std::string, std::string> getObject(const std::string &jsonStr, const std::string &key) {
    auto result = parseJSON(jsonStr);
    if (!result.success) return {};
    std::unordered_map<std::string, std::string> out;
    const auto &inner = result.value[key].getObject();
    for (const auto &kv : inner) {
        if (kv.second.isString())
            out[kv.first] = kv.second.getString();
        else
            out[kv.first] = kv.second.serialize();
    }
    return out;
}

} // namespace json

} // namespace liva
