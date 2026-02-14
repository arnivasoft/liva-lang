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

// --- SemVer ---
struct SemVer {
    int major = 0, minor = 0, patch = 0;
    bool operator==(const SemVer &o) const;
    bool operator<(const SemVer &o) const;
    bool operator!=(const SemVer &o) const;
    bool operator<=(const SemVer &o) const;
    bool operator>(const SemVer &o) const;
    bool operator>=(const SemVer &o) const;
    std::string toString() const;
};

struct SemVerParseResult {
    bool success = false;
    SemVer version;
    std::string errorMsg;
};

SemVerParseResult parseSemVer(const std::string &str);

// --- Version Constraints ---
struct VersionConstraint {
    enum Kind { Exact, Minimum, Range };
    Kind kind = Exact;
    SemVer min; // Exact: exact version, Minimum/Range: minimum
    SemVer max; // Range only
    bool satisfiedBy(const SemVer &v) const;
    std::string toString() const;
};

struct ConstraintParseResult {
    bool success = false;
    VersionConstraint constraint;
    std::string errorMsg;
};

ConstraintParseResult parseVersionConstraint(const std::string &str);

// --- Package Dependencies ---
struct PackageDep {
    std::string name;
    VersionConstraint constraint;
};

std::vector<PackageDep> parseDependencies(const TOMLDocument &doc);

// --- Resolved Packages ---
struct ResolvedPackage {
    std::string name;
    SemVer version;
    std::string path;    // package directory
    std::string srcPath; // package src/ directory
};

struct LockFileEntry {
    std::string name;
    std::string version;
    std::string checksum; // "sha256:..." or empty (local packages)
};

struct PackageResolutionResult {
    bool success = false;
    std::vector<ResolvedPackage> packages;
    std::string errorMsg;
};

PackageResolutionResult resolvePackages(const std::vector<PackageDep> &deps,
                                        const std::string &packagesDir);

std::string generateLockFile(const std::vector<ResolvedPackage> &packages,
                             const std::vector<LockFileEntry> &checksums = {});
std::vector<LockFileEntry> parseLockFile(const std::string &content);
bool isLockFileCurrent(const std::vector<PackageDep> &deps,
                       const std::vector<LockFileEntry> &lockEntries);

struct SinglePackageResult {
    bool success = false;
    SemVer version;
    std::string errorMsg;
};

SinglePackageResult validatePackageToml(const PackageDep &dep,
                                        const std::string &tomlContent);

// --- Project Config ---
struct ProjectConfig {
    std::string name = "untitled";
    std::string version = "0.1.0";
    std::string entry = "main.liva";
    int optLevel = 0;
    bool debugInfo = false;
    std::string lto = "none";        // "none", "thin", "full"
    std::string pgo = "none";        // "none", "generate", "use"
    std::string pgoProfile;          // profile data path (for pgo=use)
    std::vector<std::string> modulePaths;
    std::string projectRoot;
    std::vector<PackageDep> dependencies;
    std::string registryUrl; // from [registry] url or LIVA_REGISTRY_URL env
};

ProjectConfig loadProjectConfig(const TOMLDocument &doc);

/// Add or update a dependency in liva.toml file.
/// Preserves existing content, only modifies [dependencies] section.
bool addDependencyToToml(const std::string &tomlPath,
                         const std::string &pkgName,
                         const std::string &versionConstraint);

// --- Path Utils ---
std::string findProjectFile(const std::string &startDir);
std::string getCurrentDirectory();
std::string getDirectoryOf(const std::string &path);
std::string joinPath(const std::string &base, const std::string &relative);
bool fileExists(const std::string &path);
bool createDirectory(const std::string &path);
bool createDirectories(const std::string &path);
bool removeDirectoryRecursive(const std::string &path);

} // namespace liva
