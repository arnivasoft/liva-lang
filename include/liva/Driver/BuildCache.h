#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace liva {

struct SourceFileEntry {
    std::string path;
    std::string hash; // FNV-1a 64-bit hex
    std::string objFile; // per-file cached .o name (empty if not cached)
    int64_t mtime = 0;  // last modification time (epoch seconds)
};

struct BuildCacheManifest {
    std::vector<SourceFileEntry> sources;
    int optLevel = 0;
    bool debugInfo = false;
    std::string objFile; // cached object file name (legacy whole-project)
};

/// Per-file compile status for incremental builds
struct FileCompileStatus {
    std::string sourcePath;
    std::string currentHash;
    std::string cachedObjPath; // non-empty if cache hit (absolute path)
    bool needsRecompile;
};

class BuildCache {
public:
    explicit BuildCache(const std::string &projectRoot);

    /// Lightweight import scanning: recursively find all .liva files
    /// starting from entryFile using text scan of `import` statements
    std::vector<std::string> scanDependencies(
        const std::string &entryFile,
        const std::vector<std::string> &searchPaths);

    /// Hash all source files, check if cache is valid.
    /// Returns cached object file path if valid, empty string otherwise.
    std::string checkCache(const std::vector<std::string> &sourceFiles,
                           int optLevel, bool debugInfo);

    /// Store compiled object file in cache
    bool storeCache(const std::vector<std::string> &sourceFiles,
                    const std::string &objPath,
                    int optLevel, bool debugInfo);

    /// Remove cache directory
    void clean();

    /// Per-file cache check: returns status for each source file
    std::vector<FileCompileStatus> checkFilesCache(
        const std::vector<std::string> &sourceFiles, int optLevel, bool debugInfo);

    /// Store a single file's compiled object in cache
    bool storeFileObject(const std::string &sourcePath, const std::string &hash,
                         const std::string &objPath, int optLevel, bool debugInfo);

    /// Generate a unique .o filename for a source path
    std::string objectPathForSource(const std::string &sourcePath);

    /// Remove cache entries for source files no longer in the project
    void pruneStaleEntries(const std::vector<std::string> &currentSources);

    // Exposed for testing
    std::string hashFileContent(const std::string &path);
    std::string hashFileContent(const std::string &path, int64_t cachedMtime,
                                const std::string &cachedHash);
    const std::string &getCacheDir() const { return cacheDir_; }

private:
    std::string cacheDir_;
    std::string manifestPath_;

    std::string computeCombinedHash(const std::vector<SourceFileEntry> &entries,
                                    int optLevel, bool debugInfo);

    std::vector<std::string> extractImports(const std::string &filePath);
    std::string resolveImportPath(const std::string &importName,
                                  const std::string &baseDir,
                                  const std::vector<std::string> &searchPaths);

    bool loadManifest(BuildCacheManifest &out);
    bool saveManifest(const BuildCacheManifest &manifest);

    /// Per-file manifest (separate from whole-project manifest)
    std::string perFileManifestPath_;
    bool loadPerFileManifest(std::vector<SourceFileEntry> &out, int &optLevel, bool &debugInfo);
    bool savePerFileManifest(const std::vector<SourceFileEntry> &entries, int optLevel, bool debugInfo);
};

} // namespace liva
