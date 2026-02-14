#pragma once
#include "liva/Driver/ProjectConfig.h"
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace liva {

/// Registry metadata for a single package version
struct RegistryEntry {
    std::string name;
    SemVer version;
    std::string downloadUrl;   // URL to .tar.gz package
    std::string checksum;      // "sha256:hexstring"
    std::vector<PackageDep> dependencies; // transitive deps
};

/// Result of installing a single package
struct InstallResult {
    bool success = false;
    std::string name;
    SemVer version;
    std::string checksum;       // "sha256:..." (downloaded package's hash)
    std::string errorMsg;
};

/// Package manager: local + remote resolution, dependency tree, checksum
class PackageManager {
public:
    PackageManager(const std::string &projectRoot,
                   const std::string &registryUrl);

    /// Resolve all dependencies (local first, then remote registry).
    /// Downloads and installs missing packages. Resolves transitive deps.
    PackageResolutionResult resolveAndInstall(
        const std::vector<PackageDep> &deps);

    /// Install a single package by name and optional version constraint.
    /// Downloads from registry if not locally available.
    InstallResult installSingle(const std::string &pkgName,
                                const std::string &versionStr);

    // --- Testable helpers ---

    /// SHA-256 hash of a string, returns hex digest
    static std::string sha256(const std::string &data);

    /// SHA-256 hash of a file, returns hex digest (empty on error)
    static std::string sha256File(const std::string &path);

    /// Verify checksum: expected format "sha256:hexdigest"
    static bool verifyChecksum(const std::string &data,
                               const std::string &expected);

    /// Parse a registry JSON response for a single version:
    /// {"name":"x","version":"1.0.0","url":"...","checksum":"sha256:...",
    ///  "dependencies":{"dep":">=1.0.0"}}
    static bool parseRegistryEntry(const std::string &json,
                                   RegistryEntry &out);

    /// Parse a version list response: {"versions":["1.0.0","2.0.0"]}
    static std::vector<std::string> parseVersionList(const std::string &json);

private:
    std::string projectRoot_;
    std::string packagesDir_;  // projectRoot/packages/
    std::string registryUrl_;  // e.g., "https://registry.liva-lang.org"

    /// Try to resolve a single package from local packages/ directory
    bool resolveLocal(const PackageDep &dep, ResolvedPackage &out);

    /// Query registry for best version matching constraint
    bool queryRegistry(const PackageDep &dep, RegistryEntry &out);

    /// Download and install a package from registry
    bool installPackage(const RegistryEntry &entry, std::string &errorMsg);

    /// Recursive dependency tree resolution (DFS)
    bool resolveDependencyTree(
        const std::vector<PackageDep> &deps,
        std::vector<ResolvedPackage> &resolved,
        std::unordered_set<std::string> &resolving,
        std::string &errorMsg);

    /// HTTP GET returning response body (empty on failure)
    static std::string httpGet(const std::string &url);

    /// Download file from URL to local path
    static bool downloadFile(const std::string &url,
                             const std::string &destPath);
};

// --- Simple JSON helpers for registry responses ---
namespace json {
    std::string getString(const std::string &json, const std::string &key);
    std::vector<std::string> getStringArray(const std::string &json,
                                            const std::string &key);
    std::unordered_map<std::string, std::string> getObject(
        const std::string &json, const std::string &key);
}

} // namespace liva
