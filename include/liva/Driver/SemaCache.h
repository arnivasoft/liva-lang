#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace liva {

class TranslationUnit;
struct FileCompileStatus;

/// Per-file sema cache entry stored in the manifest
struct SemaCacheEntry {
    std::string path;           // normalized source path
    std::string sourceHash;     // FNV-1a of source content
    std::string interfaceHash;  // FNV-1a of public API signatures
    std::vector<std::string> imports;  // imported file paths (normalized)
    /// Snapshot of dependency interface hashes at compile time
    std::unordered_map<std::string, std::string> depInterfaceHashes;
};

/// Result of SemaCache::check() for a single file
struct SemaCacheStatus {
    std::string sourcePath;
    bool needsResema;           // true if sema must re-run
    std::string cachedInterfaceHash;  // previous interface hash (for cascade check)
};

/// Dependency-aware sema cache for incremental builds.
/// Tracks per-file interface hashes and invalidates dependents
/// when a file's public API changes.
class SemaCache {
public:
    explicit SemaCache(const std::string &projectRoot);

    /// Check which files need re-sema based on source changes + dependency cascade.
    /// sourceFiles: list of all source file paths (from scanDependencies)
    /// fileStatuses: per-file compile status with current source hashes
    std::vector<SemaCacheStatus> check(
        const std::vector<std::string> &sourceFiles,
        const std::vector<FileCompileStatus> &fileStatuses);

    /// Store sema result for a file after successful sema.
    /// imports: list of imported file paths (normalized)
    void store(const std::string &sourcePath, const std::string &sourceHash,
               const std::string &interfaceHash,
               const std::vector<std::string> &imports);

    /// Compute interface hash from a TranslationUnit's public declarations.
    /// Uses FNV-1a hash of deterministic signature strings for all pub decls.
    static std::string computeInterfaceHash(const TranslationUnit &tu);

    /// Save manifest to disk (.liva-cache/sema_manifest)
    bool saveManifest();

    /// Access entries for testing
    const std::unordered_map<std::string, SemaCacheEntry> &getEntries() const {
        return entries_;
    }

private:
    std::string cacheDir_;
    std::string manifestPath_;
    std::unordered_map<std::string, SemaCacheEntry> entries_;

    bool loadManifest();
};

} // namespace liva
