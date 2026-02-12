#pragma once
#include <string>
#include <unordered_map>
#include <vector>

namespace liva {

// --- Minimal TOML ---
struct TOMLValue {
    enum Kind { String, Integer, Boolean, StringArray };
    Kind kind;
    std::string stringVal;
    long intVal = 0;
    bool boolVal = false;
    std::vector<std::string> arrayVal;
};

struct TOMLDocument {
    std::unordered_map<std::string,
        std::unordered_map<std::string, TOMLValue>> sections;

    const TOMLValue *get(const std::string &section, const std::string &key) const;
    std::string getString(const std::string &sec, const std::string &key,
                          const std::string &def = "") const;
    long getInt(const std::string &sec, const std::string &key, long def = 0) const;
    bool getBool(const std::string &sec, const std::string &key, bool def = false) const;
    std::vector<std::string> getStringArray(const std::string &sec,
                                            const std::string &key) const;
};

struct TOMLParseResult {
    TOMLDocument doc;
    bool success = false;
    std::string errorMsg;
    int errorLine = 0;
};

TOMLParseResult parseTOML(const std::string &content);

// --- Project Config ---
struct ProjectConfig {
    std::string name = "untitled";
    std::string version = "0.1.0";
    std::string entry = "main.liva";
    int optLevel = 0;
    bool debugInfo = false;
    std::vector<std::string> modulePaths;
    std::string projectRoot;
};

ProjectConfig loadProjectConfig(const TOMLDocument &doc);

// --- Path Utils ---
std::string findProjectFile(const std::string &startDir);
std::string getCurrentDirectory();
std::string getDirectoryOf(const std::string &path);
std::string joinPath(const std::string &base, const std::string &relative);
bool fileExists(const std::string &path);
bool createDirectory(const std::string &path);
bool createDirectories(const std::string &path);

} // namespace liva
