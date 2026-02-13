#pragma once
#include <string>
#include <vector>

namespace liva {

struct SourceFileEntry {
    std::string path;
    std::string hash; // FNV-1a 64-bit hex
};

struct BuildCacheManifest {
    std::vector<SourceFileEntry> sources;
    int optLevel = 0;
    bool debugInfo = false;
    std::string objFile; // cached object file name
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

    // Exposed for testing
    std::string hashFileContent(const std::string &path);

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
};

} // namespace liva
