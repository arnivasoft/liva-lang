#include "liva/Driver/ProjectConfig.h"
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace liva {

// === TOMLDocument accessors ===

const TOMLValue *TOMLDocument::get(const std::string &section,
                                   const std::string &key) const {
    auto secIt = sections.find(section);
    if (secIt == sections.end())
        return nullptr;
    auto keyIt = secIt->second.find(key);
    if (keyIt == secIt->second.end())
        return nullptr;
    return &keyIt->second;
}

std::string TOMLDocument::getString(const std::string &sec,
                                    const std::string &key,
                                    const std::string &def) const {
    auto *v = get(sec, key);
    return (v && v->kind == TOMLValue::String) ? v->stringVal : def;
}

long TOMLDocument::getInt(const std::string &sec, const std::string &key,
                          long def) const {
    auto *v = get(sec, key);
    return (v && v->kind == TOMLValue::Integer) ? v->intVal : def;
}

bool TOMLDocument::getBool(const std::string &sec, const std::string &key,
                           bool def) const {
    auto *v = get(sec, key);
    return (v && v->kind == TOMLValue::Boolean) ? v->boolVal : def;
}

std::vector<std::string> TOMLDocument::getStringArray(const std::string &sec,
                                                      const std::string &key) const {
    auto *v = get(sec, key);
    if (v && v->kind == TOMLValue::StringArray)
        return v->arrayVal;
    return {};
}

// === TOML Parser ===

static std::string trim(const std::string &s) {
    size_t start = s.find_first_not_of(" \t\r");
    if (start == std::string::npos)
        return "";
    size_t end = s.find_last_not_of(" \t\r");
    return s.substr(start, end - start + 1);
}

static bool parseStringLiteral(const std::string &raw, size_t &pos,
                               std::string &out) {
    if (pos >= raw.size() || raw[pos] != '"')
        return false;
    ++pos; // skip opening quote
    out.clear();
    while (pos < raw.size()) {
        char c = raw[pos++];
        if (c == '"')
            return true;
        if (c == '\\' && pos < raw.size()) {
            char esc = raw[pos++];
            switch (esc) {
            case 'n':  out += '\n'; break;
            case 't':  out += '\t'; break;
            case '\\': out += '\\'; break;
            case '"':  out += '"';  break;
            default:   out += '\\'; out += esc; break;
            }
        } else {
            out += c;
        }
    }
    return false; // unterminated
}

TOMLParseResult parseTOML(const std::string &content) {
    TOMLParseResult result;
    std::string currentSection;
    std::istringstream stream(content);
    std::string rawLine;
    int lineNum = 0;

    while (std::getline(stream, rawLine)) {
        ++lineNum;

        // Remove \r if present
        if (!rawLine.empty() && rawLine.back() == '\r')
            rawLine.pop_back();

        std::string line = trim(rawLine);

        // Skip empty lines and comments
        if (line.empty() || line[0] == '#')
            continue;

        // Section header: [section]
        if (line[0] == '[') {
            auto close = line.find(']');
            if (close == std::string::npos) {
                result.success = false;
                result.errorMsg = "unterminated section header";
                result.errorLine = lineNum;
                return result;
            }
            currentSection = trim(line.substr(1, close - 1));
            continue;
        }

        // Key = value
        auto eqPos = line.find('=');
        if (eqPos == std::string::npos) {
            result.success = false;
            result.errorMsg = "expected '=' in key-value pair";
            result.errorLine = lineNum;
            return result;
        }

        std::string key = trim(line.substr(0, eqPos));
        std::string valStr = trim(line.substr(eqPos + 1));

        // Strip inline comment (not inside string)
        if (!valStr.empty() && valStr[0] != '"' && valStr[0] != '[') {
            auto hashPos = valStr.find('#');
            if (hashPos != std::string::npos)
                valStr = trim(valStr.substr(0, hashPos));
        }

        TOMLValue val;

        if (!valStr.empty() && valStr[0] == '"') {
            // String value
            size_t pos = 0;
            std::string parsed;
            if (!parseStringLiteral(valStr, pos, parsed)) {
                result.success = false;
                result.errorMsg = "unterminated string";
                result.errorLine = lineNum;
                return result;
            }
            val.kind = TOMLValue::String;
            val.stringVal = parsed;
            // Handle inline comment after string
        } else if (valStr == "true" || valStr == "false") {
            val.kind = TOMLValue::Boolean;
            val.boolVal = (valStr == "true");
        } else if (!valStr.empty() && valStr[0] == '[') {
            // String array
            auto closeB = valStr.find(']');
            if (closeB == std::string::npos) {
                result.success = false;
                result.errorMsg = "unterminated array";
                result.errorLine = lineNum;
                return result;
            }
            val.kind = TOMLValue::StringArray;
            std::string inner = valStr.substr(1, closeB - 1);
            inner = trim(inner);
            if (!inner.empty()) {
                // Parse comma-separated strings
                size_t p = 0;
                while (p < inner.size()) {
                    // Skip whitespace and commas
                    while (p < inner.size() && (inner[p] == ' ' || inner[p] == '\t' || inner[p] == ','))
                        ++p;
                    if (p >= inner.size())
                        break;
                    if (inner[p] == '"') {
                        std::string elem;
                        if (!parseStringLiteral(inner, p, elem)) {
                            result.success = false;
                            result.errorMsg = "unterminated string in array";
                            result.errorLine = lineNum;
                            return result;
                        }
                        val.arrayVal.push_back(elem);
                    } else {
                        break;
                    }
                }
            }
        } else {
            // Try integer
            char *endptr = nullptr;
            long num = strtol(valStr.c_str(), &endptr, 10);
            if (endptr != valStr.c_str() && (*endptr == '\0' || *endptr == ' ' || *endptr == '\t')) {
                val.kind = TOMLValue::Integer;
                val.intVal = num;
            } else {
                val.kind = TOMLValue::String;
                val.stringVal = valStr;
            }
        }

        result.doc.sections[currentSection][key] = val;
    }

    result.success = true;
    return result;
}

// === ProjectConfig ===

ProjectConfig loadProjectConfig(const TOMLDocument &doc) {
    ProjectConfig cfg;
    cfg.name = doc.getString("project", "name", "untitled");
    cfg.version = doc.getString("project", "version", "0.1.0");
    cfg.entry = doc.getString("project", "entry", "main.liva");
    cfg.optLevel = (int)doc.getInt("build", "opt-level", 0);
    cfg.debugInfo = doc.getBool("build", "debug-info", false);
    cfg.modulePaths = doc.getStringArray("paths", "modules");
    return cfg;
}

// === Path Utilities ===

std::string getCurrentDirectory() {
    char buf[4096];
#ifdef _WIN32
    if (_getcwd(buf, sizeof(buf)))
        return std::string(buf);
#else
    if (getcwd(buf, sizeof(buf)))
        return std::string(buf);
#endif
    return ".";
}

std::string getDirectoryOf(const std::string &path) {
    auto pos = path.find_last_of("/\\");
    if (pos == std::string::npos)
        return ".";
    return path.substr(0, pos);
}

std::string joinPath(const std::string &base, const std::string &relative) {
    if (base.empty())
        return relative;
    if (relative.empty())
        return base;
    char last = base.back();
    if (last == '/' || last == '\\')
        return base + relative;
    return base + "/" + relative;
}

bool fileExists(const std::string &path) {
    std::ifstream f(path);
    return f.is_open();
}

bool createDirectory(const std::string &path) {
#ifdef _WIN32
    return _mkdir(path.c_str()) == 0 || errno == EEXIST;
#else
    return mkdir(path.c_str(), 0755) == 0 || errno == EEXIST;
#endif
}

bool createDirectories(const std::string &path) {
    if (path.empty())
        return false;
    // Try creating the directory directly first
    if (createDirectory(path))
        return true;
    // Create parent directories recursively
    auto pos = path.find_last_of("/\\");
    if (pos != std::string::npos && pos > 0) {
        std::string parent = path.substr(0, pos);
        if (!createDirectories(parent))
            return false;
    }
    return createDirectory(path);
}

std::string findProjectFile(const std::string &startDir) {
    std::string dir = startDir;
    while (!dir.empty()) {
        std::string candidate = joinPath(dir, "liva.toml");
        if (fileExists(candidate))
            return candidate;
        std::string parent = getDirectoryOf(dir);
        if (parent == dir || parent == ".")
            break;
        dir = parent;
    }
    return "";
}

} // namespace liva
