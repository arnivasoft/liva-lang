#include "liva/Driver/ProjectConfig.h"
#include <cerrno>
#include <cstdio>
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

// === SemVer ===

bool SemVer::operator==(const SemVer &o) const {
    return major == o.major && minor == o.minor && patch == o.patch;
}

bool SemVer::operator<(const SemVer &o) const {
    if (major != o.major) return major < o.major;
    if (minor != o.minor) return minor < o.minor;
    return patch < o.patch;
}

bool SemVer::operator!=(const SemVer &o) const { return !(*this == o); }
bool SemVer::operator<=(const SemVer &o) const { return !(o < *this); }
bool SemVer::operator>(const SemVer &o) const { return o < *this; }
bool SemVer::operator>=(const SemVer &o) const { return !(*this < o); }

std::string SemVer::toString() const {
    // Manual int-to-string to avoid std::to_string (MinGW compat)
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%d.%d.%d", major, minor, patch);
    return std::string(buf);
}

SemVerParseResult parseSemVer(const std::string &str) {
    SemVerParseResult result;
    if (str.empty()) {
        result.errorMsg = "empty version string";
        return result;
    }
    const char *p = str.c_str();
    char *end = nullptr;

    errno = 0;
    long maj = strtol(p, &end, 10);
    if (end == p || *end != '.' || maj < 0) {
        result.errorMsg = "invalid major version";
        return result;
    }
    p = end + 1;

    long min = strtol(p, &end, 10);
    if (end == p || *end != '.' || min < 0) {
        result.errorMsg = "invalid minor version";
        return result;
    }
    p = end + 1;

    long pat = strtol(p, &end, 10);
    if (end == p || *end != '\0' || pat < 0) {
        result.errorMsg = "invalid patch version";
        return result;
    }

    result.success = true;
    result.version.major = (int)maj;
    result.version.minor = (int)min;
    result.version.patch = (int)pat;
    return result;
}

// === VersionConstraint ===

bool VersionConstraint::satisfiedBy(const SemVer &v) const {
    switch (kind) {
    case Exact:   return v == min;
    case Minimum: return v >= min;
    case Range:   return v >= min && v < max;
    }
    return false;
}

std::string VersionConstraint::toString() const {
    switch (kind) {
    case Exact:   return min.toString();
    case Minimum: return ">=" + min.toString();
    case Range:   return ">=" + min.toString() + ",<" + max.toString();
    }
    return "";
}

static std::string trimStr(const std::string &s) {
    size_t start = s.find_first_not_of(" \t");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t");
    return s.substr(start, end - start + 1);
}

ConstraintParseResult parseVersionConstraint(const std::string &str) {
    ConstraintParseResult result;
    std::string s = trimStr(str);
    if (s.empty()) {
        result.errorMsg = "empty constraint string";
        return result;
    }

    // Check for range: ">=X.Y.Z,<A.B.C"
    auto commaPos = s.find(',');
    if (commaPos != std::string::npos) {
        std::string left = trimStr(s.substr(0, commaPos));
        std::string right = trimStr(s.substr(commaPos + 1));
        // left must start with ">="
        if (left.size() < 3 || left[0] != '>' || left[1] != '=') {
            result.errorMsg = "range constraint must start with '>='";
            return result;
        }
        // right must start with "<"
        if (right.size() < 2 || right[0] != '<') {
            result.errorMsg = "range constraint must have '<' upper bound";
            return result;
        }
        auto minRes = parseSemVer(trimStr(left.substr(2)));
        if (!minRes.success) {
            result.errorMsg = "invalid minimum version: " + minRes.errorMsg;
            return result;
        }
        auto maxRes = parseSemVer(trimStr(right.substr(1)));
        if (!maxRes.success) {
            result.errorMsg = "invalid maximum version: " + maxRes.errorMsg;
            return result;
        }
        result.success = true;
        result.constraint.kind = VersionConstraint::Range;
        result.constraint.min = minRes.version;
        result.constraint.max = maxRes.version;
        return result;
    }

    // Check for minimum: ">=X.Y.Z"
    if (s.size() >= 3 && s[0] == '>' && s[1] == '=') {
        auto verRes = parseSemVer(trimStr(s.substr(2)));
        if (!verRes.success) {
            result.errorMsg = "invalid version: " + verRes.errorMsg;
            return result;
        }
        result.success = true;
        result.constraint.kind = VersionConstraint::Minimum;
        result.constraint.min = verRes.version;
        return result;
    }

    // Reject other operators
    if (s[0] == '>' || s[0] == '<' || s[0] == '!' || s[0] == '~' || s[0] == '^') {
        result.errorMsg = "unsupported constraint operator";
        return result;
    }

    // Exact: "X.Y.Z"
    auto verRes = parseSemVer(s);
    if (!verRes.success) {
        result.errorMsg = verRes.errorMsg;
        return result;
    }
    result.success = true;
    result.constraint.kind = VersionConstraint::Exact;
    result.constraint.min = verRes.version;
    return result;
}

// === Dependency Parsing ===

std::vector<PackageDep> parseDependencies(const TOMLDocument &doc) {
    std::vector<PackageDep> deps;
    auto secIt = doc.sections.find("dependencies");
    if (secIt == doc.sections.end())
        return deps;
    for (const auto &kv : secIt->second) {
        if (kv.second.kind != TOMLValue::String)
            continue;
        auto cr = parseVersionConstraint(kv.second.stringVal);
        if (cr.success) {
            PackageDep dep;
            dep.name = kv.first;
            dep.constraint = cr.constraint;
            deps.push_back(dep);
        }
    }
    return deps;
}

// === Package Resolution ===

SinglePackageResult validatePackageToml(const PackageDep &dep,
                                        const std::string &tomlContent) {
    SinglePackageResult result;
    auto parsed = parseTOML(tomlContent);
    if (!parsed.success) {
        result.errorMsg = "invalid liva.toml: " + parsed.errorMsg;
        return result;
    }
    std::string pkgName = parsed.doc.getString("project", "name", "");
    if (pkgName.empty()) {
        result.errorMsg = "package liva.toml missing [project] name";
        return result;
    }
    if (pkgName != dep.name) {
        result.errorMsg = "package name mismatch: expected '" + dep.name +
                          "', got '" + pkgName + "'";
        return result;
    }
    std::string verStr = parsed.doc.getString("project", "version", "");
    auto verRes = parseSemVer(verStr);
    if (!verRes.success) {
        result.errorMsg = "invalid package version: " + verRes.errorMsg;
        return result;
    }
    if (!dep.constraint.satisfiedBy(verRes.version)) {
        result.errorMsg = "version " + verRes.version.toString() +
                          " does not satisfy constraint " +
                          dep.constraint.toString();
        return result;
    }
    result.success = true;
    result.version = verRes.version;
    return result;
}

PackageResolutionResult resolvePackages(const std::vector<PackageDep> &deps,
                                        const std::string &packagesDir) {
    PackageResolutionResult result;
    for (const auto &dep : deps) {
        std::string pkgDir = joinPath(packagesDir, dep.name);
        std::string tomlPath = joinPath(pkgDir, "liva.toml");

        std::ifstream f(tomlPath);
        if (!f.is_open()) {
            result.errorMsg = "package '" + dep.name +
                              "' not found in " + packagesDir;
            return result;
        }
        std::stringstream ss;
        ss << f.rdbuf();

        auto valResult = validatePackageToml(dep, ss.str());
        if (!valResult.success) {
            result.errorMsg = "package '" + dep.name + "': " +
                              valResult.errorMsg;
            return result;
        }

        ResolvedPackage pkg;
        pkg.name = dep.name;
        pkg.version = valResult.version;
        pkg.path = pkgDir;
        pkg.srcPath = joinPath(pkgDir, "src");
        result.packages.push_back(pkg);
    }
    result.success = true;
    return result;
}

// === Lock File ===

std::string generateLockFile(const std::vector<ResolvedPackage> &packages,
                             const std::vector<LockFileEntry> &checksums) {
    std::string out = "[lock]\n";
    for (const auto &pkg : packages)
        out += pkg.name + " = \"" + pkg.version.toString() + "\"\n";

    // Write checksums section if any entries have checksums
    bool hasChecksums = false;
    for (const auto &cs : checksums) {
        if (!cs.checksum.empty()) { hasChecksums = true; break; }
    }
    if (hasChecksums) {
        out += "\n[checksums]\n";
        for (const auto &cs : checksums) {
            if (!cs.checksum.empty())
                out += cs.name + " = \"" + cs.checksum + "\"\n";
        }
    }
    return out;
}

std::vector<LockFileEntry> parseLockFile(const std::string &content) {
    std::vector<LockFileEntry> entries;
    auto parsed = parseTOML(content);
    if (!parsed.success)
        return entries;
    auto secIt = parsed.doc.sections.find("lock");
    if (secIt == parsed.doc.sections.end())
        return entries;
    for (const auto &kv : secIt->second) {
        if (kv.second.kind == TOMLValue::String) {
            LockFileEntry e;
            e.name = kv.first;
            e.version = kv.second.stringVal;
            entries.push_back(e);
        }
    }
    // Read checksums section if present
    auto csIt = parsed.doc.sections.find("checksums");
    if (csIt != parsed.doc.sections.end()) {
        for (auto &e : entries) {
            auto kvIt = csIt->second.find(e.name);
            if (kvIt != csIt->second.end() &&
                kvIt->second.kind == TOMLValue::String) {
                e.checksum = kvIt->second.stringVal;
            }
        }
    }
    return entries;
}

bool isLockFileCurrent(const std::vector<PackageDep> &deps,
                       const std::vector<LockFileEntry> &lockEntries) {
    if (deps.size() != lockEntries.size())
        return false;
    for (const auto &dep : deps) {
        bool found = false;
        for (const auto &entry : lockEntries) {
            if (entry.name == dep.name) {
                auto verRes = parseSemVer(entry.version);
                if (verRes.success &&
                    dep.constraint.satisfiedBy(verRes.version))
                    found = true;
                break;
            }
        }
        if (!found)
            return false;
    }
    return true;
}

// === ProjectConfig ===

ProjectConfig loadProjectConfig(const TOMLDocument &doc) {
    ProjectConfig cfg;
    cfg.name = doc.getString("project", "name", "untitled");
    cfg.version = doc.getString("project", "version", "0.1.0");
    cfg.entry = doc.getString("project", "entry", "main.liva");
    cfg.optLevel = (int)doc.getInt("build", "opt-level", 0);
    cfg.debugInfo = doc.getBool("build", "debug-info", false);
    cfg.lto = doc.getString("build", "lto", "none");
    cfg.pgo = doc.getString("build", "pgo", "none");
    cfg.pgoProfile = doc.getString("build", "pgo-profile", "");
    cfg.modulePaths = doc.getStringArray("paths", "modules");
    cfg.dependencies = parseDependencies(doc);

    // Registry URL: liva.toml [registry] section or LIVA_REGISTRY_URL env
    cfg.registryUrl = doc.getString("registry", "url", "");
    if (cfg.registryUrl.empty()) {
        const char *envUrl = std::getenv("LIVA_REGISTRY_URL");
        if (envUrl)
            cfg.registryUrl = envUrl;
    }
    return cfg;
}

// === addDependencyToToml ===

bool addDependencyToToml(const std::string &tomlPath,
                         const std::string &pkgName,
                         const std::string &versionConstraint) {
    // Read existing file content
    std::string content;
    {
        std::ifstream f(tomlPath);
        if (f.is_open()) {
            std::stringstream ss;
            ss << f.rdbuf();
            content = ss.str();
        }
    }

    // Build the new entry line
    std::string entryLine = pkgName + " = \"" + versionConstraint + "\"";

    // Find [dependencies] section
    std::string sectionHeader = "[dependencies]";
    size_t secPos = content.find(sectionHeader);

    if (secPos == std::string::npos) {
        // No [dependencies] section — append one at end
        if (!content.empty() && content.back() != '\n')
            content += "\n";
        content += "\n" + sectionHeader + "\n" + entryLine + "\n";
    } else {
        // Found [dependencies] section
        size_t afterHeader = secPos + sectionHeader.size();
        // Skip to end of header line
        if (afterHeader < content.size() && content[afterHeader] == '\r')
            ++afterHeader;
        if (afterHeader < content.size() && content[afterHeader] == '\n')
            ++afterHeader;

        // Check if pkgName already exists in the section
        // Scan lines from afterHeader until next section or EOF
        size_t searchPos = afterHeader;
        size_t existingLineStart = std::string::npos;
        size_t existingLineEnd = std::string::npos;

        while (searchPos < content.size()) {
            // Find end of current line
            size_t lineEnd = content.find('\n', searchPos);
            if (lineEnd == std::string::npos)
                lineEnd = content.size();

            std::string line = content.substr(searchPos, lineEnd - searchPos);

            // Trim \r if present
            if (!line.empty() && line.back() == '\r')
                line.pop_back();

            // Trim leading whitespace
            size_t firstNonSpace = line.find_first_not_of(" \t");
            if (firstNonSpace != std::string::npos)
                line = line.substr(firstNonSpace);

            // Hit next section?
            if (!line.empty() && line[0] == '[')
                break;

            // Skip comments and empty lines
            if (!line.empty() && line[0] != '#') {
                // Check if this line starts with pkgName
                size_t eqPos = line.find('=');
                if (eqPos != std::string::npos) {
                    std::string key = line.substr(0, eqPos);
                    // Trim key
                    size_t kEnd = key.find_last_not_of(" \t");
                    if (kEnd != std::string::npos)
                        key = key.substr(0, kEnd + 1);
                    size_t kStart = key.find_first_not_of(" \t");
                    if (kStart != std::string::npos)
                        key = key.substr(kStart);

                    if (key == pkgName) {
                        existingLineStart = searchPos;
                        existingLineEnd = lineEnd;
                        break;
                    }
                }
            }

            searchPos = (lineEnd < content.size()) ? lineEnd + 1 : content.size();
        }

        if (existingLineStart != std::string::npos) {
            // Replace existing line
            content = content.substr(0, existingLineStart) +
                      entryLine + "\n" +
                      ((existingLineEnd < content.size())
                           ? content.substr(existingLineEnd + 1)
                           : "");
        } else {
            // Insert new entry right after [dependencies] header
            content.insert(afterHeader, entryLine + "\n");
        }
    }

    // Write back
    std::ofstream f(tomlPath);
    if (!f.is_open())
        return false;
    f << content;
    return true;
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

bool removeDirectoryRecursive(const std::string &path) {
    if (path.empty()) return false;
#ifdef _WIN32
    std::string cmd = "\"rmdir /s /q \"" + path + "\"\"";
#else
    std::string cmd = "rm -rf \"" + path + "\"";
#endif
    return std::system(cmd.c_str()) == 0;
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
