#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace liva {

// ============================================================
// JSONValue — minimal JSON value type (no exceptions)
// ============================================================

class JSONValue {
public:
    enum Kind { Null, Bool, Integer, Double, String, Array, Object };

    JSONValue() : kind_(Null), boolVal_(false), intVal_(0), doubleVal_(0.0) {}
    explicit JSONValue(bool v) : kind_(Bool), boolVal_(v), intVal_(0), doubleVal_(0.0) {}
    explicit JSONValue(int64_t v) : kind_(Integer), boolVal_(false), intVal_(v), doubleVal_(0.0) {}
    explicit JSONValue(int v) : kind_(Integer), boolVal_(false), intVal_(v), doubleVal_(0.0) {}
    explicit JSONValue(double v) : kind_(Double), boolVal_(false), intVal_(0), doubleVal_(v) {}
    explicit JSONValue(const std::string &v)
        : kind_(String), boolVal_(false), intVal_(0), doubleVal_(0.0), stringVal_(v) {}
    explicit JSONValue(const char *v)
        : kind_(String), boolVal_(false), intVal_(0), doubleVal_(0.0), stringVal_(v ? v : "") {}
    explicit JSONValue(std::vector<JSONValue> v)
        : kind_(Array), boolVal_(false), intVal_(0), doubleVal_(0.0),
          arrayVal_(std::move(v)) {}
    explicit JSONValue(std::map<std::string, JSONValue> v)
        : kind_(Object), boolVal_(false), intVal_(0), doubleVal_(0.0),
          objectVal_(std::move(v)) {}

    JSONValue(const JSONValue &other);
    JSONValue(JSONValue &&other) noexcept;
    JSONValue &operator=(const JSONValue &other);
    JSONValue &operator=(JSONValue &&other) noexcept;

    Kind getKind() const { return kind_; }
    bool isNull() const { return kind_ == Null; }
    bool isBool() const { return kind_ == Bool; }
    bool isInteger() const { return kind_ == Integer; }
    bool isDouble() const { return kind_ == Double; }
    bool isString() const { return kind_ == String; }
    bool isArray() const { return kind_ == Array; }
    bool isObject() const { return kind_ == Object; }

    bool getBool(bool def = false) const { return kind_ == Bool ? boolVal_ : def; }
    int64_t getInteger(int64_t def = 0) const { return kind_ == Integer ? intVal_ : def; }
    double getDouble(double def = 0.0) const { return kind_ == Double ? doubleVal_ : def; }
    const std::string &getString() const;
    const std::vector<JSONValue> &getArray() const;
    const std::map<std::string, JSONValue> &getObject() const;

    const JSONValue &operator[](const std::string &key) const;
    bool hasKey(const std::string &key) const;

    std::string serialize() const;

    void set(const std::string &key, JSONValue val);
    void push(JSONValue val);

    static JSONValue object();
    static JSONValue array();

private:
    Kind kind_;
    bool boolVal_;
    int64_t intVal_;
    double doubleVal_;
    std::string stringVal_;
    std::vector<JSONValue> arrayVal_;
    std::map<std::string, JSONValue> objectVal_;
};

struct JSONParseResult {
    JSONValue value;
    bool success = false;
    std::string error;
};

JSONParseResult parseJSON(const std::string &input);

// ============================================================
// json:: convenience helpers
// ============================================================

namespace json {

// JSONValue-based helpers
std::string getString(const JSONValue &obj, const std::string &key);
std::vector<std::string> getStringArray(const JSONValue &obj, const std::string &key);
std::map<std::string, std::string> getObject(const JSONValue &obj, const std::string &key);

// Raw string overloads (backward compatible with PackageManager usage)
std::string getString(const std::string &json, const std::string &key);
std::vector<std::string> getStringArray(const std::string &json, const std::string &key);
std::unordered_map<std::string, std::string> getObject(const std::string &json, const std::string &key);

} // namespace json

} // namespace liva
