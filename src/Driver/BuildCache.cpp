#include "liva/Driver/BuildCache.h"
#include "liva/Driver/ProjectConfig.h"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <unordered_set>

namespace liva {

// FNV-1a 64-bit hash
static uint64_t fnv1a_64(const char *data, size_t len) {
    uint64_t hash = 0xcbf29ce484222325ULL;
    for (size_t i = 0; i < len; i++) {
        hash ^= static_cast<uint8_t>(data[i]);
        hash *= 0x100000001b3ULL;
    }
    return hash;
}

static std::string uint64ToHex(uint64_t val) {
    char buf[17];
    std::snprintf(buf, sizeof(buf), "%016llx",
                  static_cast<unsigned long long>(val));
    return std::string(buf);
}

// Normalize path separators to forward slash for consistent hashing
static std::string normalizePath(const std::string &path) {
    std::string result = path;
    for (auto &c : result) {
        if (c == '\\')
            c = '/';
    }
    return result;
}

static bool startsWith(const std::string &str, const std::string &prefix) {
    return str.size() >= prefix.size() &&
           str.compare(0, prefix.size(), prefix) == 0;
}

static std::string trim(const std::string &s) {
    size_t start = 0;
    while (start < s.size() && (s[start] == ' ' || s[start] == '\t' ||
                                 s[start] == '\r' || s[start] == '\n'))
        ++start;
    size_t end = s.size();
    while (end > start && (s[end - 1] == ' ' || s[end - 1] == '\t' ||
                            s[end - 1] == '\r' || s[end - 1] == '\n'))
        --end;
    return s.substr(start, end - start);
}

static std::string getDirectoryOfFile(const std::string &path) {
    auto pos = path.find_last_of("/\\");
    return (pos != std::string::npos) ? path.substr(0, pos) : ".";
}

BuildCache::BuildCache(const std::string &projectRoot) {
    cacheDir_ = joinPath(projectRoot, ".liva-cache");
    manifestPath_ = joinPath(cacheDir_, "manifest");
    perFileManifestPath_ = joinPath(cacheDir_, "files_manifest");
}

std::string BuildCache::hashFileContent(const std::string &path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open())
        return "";

    std::ostringstream ss;
    ss << file.rdbuf();
    std::string content = ss.str();

    uint64_t hash = fnv1a_64(content.data(), content.size());
    return uint64ToHex(hash);
}

std::string BuildCache::computeCombinedHash(
    const std::vector<SourceFileEntry> &entries, int optLevel, bool debugInfo) {
    // Sort entries by normalized path for deterministic hashing
    std::vector<SourceFileEntry> sorted = entries;
    std::sort(sorted.begin(), sorted.end(),
              [](const SourceFileEntry &a, const SourceFileEntry &b) {
                  return normalizePath(a.path) < normalizePath(b.path);
              });

    std::string combined;
    for (const auto &e : sorted) {
        combined += normalizePath(e.path);
        combined += ':';
        combined += e.hash;
        combined += '\n';
    }
    // Include build config in hash
    combined += "OPT:";
    combined += std::to_string(optLevel);
    combined += "\nDEBUG:";
    combined += (debugInfo ? "1" : "0");
    combined += '\n';

    uint64_t hash = fnv1a_64(combined.data(), combined.size());
    return uint64ToHex(hash);
}

std::vector<std::string> BuildCache::extractImports(const std::string &filePath) {
    std::vector<std::string> imports;
    std::ifstream file(filePath);
    if (!file.is_open())
        return imports;

    std::string line;
    while (std::getline(file, line)) {
        std::string trimmed = trim(line);

        // Skip comments and empty lines
        if (trimmed.empty() || trimmed[0] == '/' || trimmed[0] == '#')
            continue;

        // Look for: import "name" or import name
        if (!startsWith(trimmed, "import "))
            continue;

        std::string rest = trim(trimmed.substr(7));
        if (rest.empty())
            continue;

        std::string importName;
        if (rest[0] == '"') {
            // import "name"
            auto end = rest.find('"', 1);
            if (end != std::string::npos)
                importName = rest.substr(1, end - 1);
        } else {
            // import name or import name::sub
            size_t end = 0;
            while (end < rest.size() &&
                   (std::isalnum(static_cast<unsigned char>(rest[end])) ||
                    rest[end] == '_' || rest[end] == ':'))
                ++end;
            importName = rest.substr(0, end);
        }

        if (!importName.empty())
            imports.push_back(importName);
    }

    return imports;
}

std::string BuildCache::resolveImportPath(
    const std::string &importName,
    const std::string &baseDir,
    const std::vector<std::string> &searchPaths) {

    // Skip builtin std:: imports
    if (startsWith(importName, "std:") || startsWith(importName, "std."))
        return "";

    // Try .liva extension
    std::string filename = importName + ".liva";

    // Check relative to base directory
    std::string candidate = joinPath(baseDir, filename);
    if (fileExists(candidate))
        return candidate;

    // Check search paths
    for (const auto &sp : searchPaths) {
        candidate = joinPath(sp, filename);
        if (fileExists(candidate))
            return candidate;
    }

    return "";
}

std::vector<std::string> BuildCache::scanDependencies(
    const std::string &entryFile,
    const std::vector<std::string> &searchPaths) {

    std::vector<std::string> result;
    std::unordered_set<std::string> visited;

    // BFS queue
    std::vector<std::string> queue;
    queue.push_back(entryFile);
    visited.insert(normalizePath(entryFile));

    while (!queue.empty()) {
        std::string current = queue.back();
        queue.pop_back();

        result.push_back(current);

        std::string baseDir = getDirectoryOfFile(current);
        auto imports = extractImports(current);

        for (const auto &imp : imports) {
            std::string resolved = resolveImportPath(imp, baseDir, searchPaths);
            if (resolved.empty())
                continue;

            std::string normalized = normalizePath(resolved);
            if (visited.count(normalized))
                continue;

            visited.insert(normalized);
            queue.push_back(resolved);
        }
    }

    return result;
}

std::string BuildCache::checkCache(const std::vector<std::string> &sourceFiles,
                                   int optLevel, bool debugInfo) {
    // Hash all source files
    std::vector<SourceFileEntry> entries;
    entries.reserve(sourceFiles.size());
    for (const auto &path : sourceFiles) {
        SourceFileEntry entry;
        entry.path = path;
        entry.hash = hashFileContent(path);
        if (entry.hash.empty())
            return ""; // Can't read file — cache miss
        entries.push_back(entry);
    }

    // Compute combined hash
    std::string currentHash = computeCombinedHash(entries, optLevel, debugInfo);

    // Load manifest
    BuildCacheManifest manifest;
    if (!loadManifest(manifest))
        return ""; // No manifest — cache miss

    // Check opt level and debug match
    if (manifest.optLevel != optLevel || manifest.debugInfo != debugInfo)
        return "";

    // Check source count matches
    if (manifest.sources.size() != entries.size())
        return "";

    // Compute manifest's combined hash
    std::string cachedHash =
        computeCombinedHash(manifest.sources, manifest.optLevel, manifest.debugInfo);

    if (currentHash != cachedHash)
        return "";

    // Verify cached object file exists
    std::string cachedObjPath = joinPath(cacheDir_, manifest.objFile);
    if (!fileExists(cachedObjPath))
        return "";

    return cachedObjPath;
}

bool BuildCache::storeCache(const std::vector<std::string> &sourceFiles,
                            const std::string &objPath,
                            int optLevel, bool debugInfo) {
    // Create cache directory
    if (!createDirectories(cacheDir_))
        return false;

    // Hash source files
    BuildCacheManifest manifest;
    manifest.optLevel = optLevel;
    manifest.debugInfo = debugInfo;
    manifest.objFile = "cached.o";

    for (const auto &path : sourceFiles) {
        SourceFileEntry entry;
        entry.path = path;
        entry.hash = hashFileContent(path);
        if (entry.hash.empty())
            return false;
        manifest.sources.push_back(entry);
    }

    // Copy object file to cache
    std::string destPath = joinPath(cacheDir_, manifest.objFile);
    {
        std::ifstream src(objPath, std::ios::binary);
        if (!src.is_open())
            return false;

        std::ofstream dst(destPath, std::ios::binary);
        if (!dst.is_open())
            return false;

        dst << src.rdbuf();
        if (!dst.good())
            return false;
    }

    // Save manifest
    return saveManifest(manifest);
}

void BuildCache::clean() {
    removeDirectoryRecursive(cacheDir_);
}

bool BuildCache::loadManifest(BuildCacheManifest &out) {
    std::ifstream file(manifestPath_);
    if (!file.is_open())
        return false;

    out.sources.clear();
    out.optLevel = 0;
    out.debugInfo = false;
    out.objFile.clear();

    std::string line;
    while (std::getline(file, line)) {
        std::string trimmed = trim(line);
        if (trimmed.empty())
            continue;

        if (startsWith(trimmed, "OPT:")) {
            long val = strtol(trimmed.c_str() + 4, nullptr, 10);
            out.optLevel = static_cast<int>(val);
        } else if (startsWith(trimmed, "DEBUG:")) {
            out.debugInfo = (trimmed.substr(6) == "1");
        } else if (startsWith(trimmed, "OBJ:")) {
            out.objFile = trimmed.substr(4);
        } else if (startsWith(trimmed, "FILE:")) {
            // FILE:path:hash
            std::string rest = trimmed.substr(5);
            auto lastColon = rest.rfind(':');
            if (lastColon != std::string::npos && lastColon > 0) {
                SourceFileEntry entry;
                entry.path = rest.substr(0, lastColon);
                entry.hash = rest.substr(lastColon + 1);
                out.sources.push_back(entry);
            }
        }
    }

    return !out.objFile.empty();
}

bool BuildCache::saveManifest(const BuildCacheManifest &manifest) {
    std::ofstream file(manifestPath_);
    if (!file.is_open())
        return false;

    file << "OPT:" << manifest.optLevel << "\n";
    file << "DEBUG:" << (manifest.debugInfo ? "1" : "0") << "\n";
    file << "OBJ:" << manifest.objFile << "\n";

    for (const auto &entry : manifest.sources) {
        file << "FILE:" << normalizePath(entry.path) << ":" << entry.hash << "\n";
    }

    return file.good();
}

std::string BuildCache::objectPathForSource(const std::string &sourcePath) {
    std::string normalized = normalizePath(sourcePath);
    // Extract filename stem
    std::string stem;
    auto slashPos = normalized.find_last_of('/');
    std::string filename = (slashPos != std::string::npos)
                               ? normalized.substr(slashPos + 1)
                               : normalized;
    auto dotPos = filename.rfind('.');
    stem = (dotPos != std::string::npos) ? filename.substr(0, dotPos) : filename;

    // Hash the full normalized path for uniqueness
    uint64_t pathHash = fnv1a_64(normalized.data(), normalized.size());
    char hashBuf[9];
    std::snprintf(hashBuf, sizeof(hashBuf), "%08llx",
                  static_cast<unsigned long long>(pathHash & 0xFFFFFFFFULL));

    return stem + "_" + std::string(hashBuf) + ".o";
}

std::vector<FileCompileStatus> BuildCache::checkFilesCache(
    const std::vector<std::string> &sourceFiles, int optLevel, bool debugInfo) {

    std::vector<FileCompileStatus> result;
    result.reserve(sourceFiles.size());

    // Load per-file manifest
    std::vector<SourceFileEntry> cachedEntries;
    int cachedOpt = -1;
    bool cachedDebug = false;
    bool hasManifest = loadPerFileManifest(cachedEntries, cachedOpt, cachedDebug);

    // Build lookup map: normalized path -> SourceFileEntry
    std::unordered_map<std::string, const SourceFileEntry *> cachedMap;
    if (hasManifest) {
        for (const auto &entry : cachedEntries) {
            cachedMap[normalizePath(entry.path)] = &entry;
        }
    }

    for (const auto &path : sourceFiles) {
        FileCompileStatus status;
        status.sourcePath = path;
        status.currentHash = hashFileContent(path);
        status.needsRecompile = true;

        if (status.currentHash.empty()) {
            // Can't read file — must recompile
            result.push_back(status);
            continue;
        }

        // Check if build config changed
        if (!hasManifest || cachedOpt != optLevel || cachedDebug != debugInfo) {
            result.push_back(status);
            continue;
        }

        // Look up cached entry
        std::string normalizedPath = normalizePath(path);
        auto it = cachedMap.find(normalizedPath);
        if (it == cachedMap.end()) {
            result.push_back(status);
            continue;
        }

        const auto &cached = *it->second;
        if (cached.hash != status.currentHash || cached.objFile.empty()) {
            result.push_back(status);
            continue;
        }

        // Verify cached .o file exists
        std::string cachedObjPath = joinPath(cacheDir_, cached.objFile);
        if (!fileExists(cachedObjPath)) {
            result.push_back(status);
            continue;
        }

        // Cache hit
        status.cachedObjPath = cachedObjPath;
        status.needsRecompile = false;
        result.push_back(status);
    }

    return result;
}

bool BuildCache::storeFileObject(const std::string &sourcePath, const std::string &hash,
                                  const std::string &objPath, int optLevel, bool debugInfo) {
    if (!createDirectories(cacheDir_))
        return false;

    std::string objName = objectPathForSource(sourcePath);
    std::string destPath = joinPath(cacheDir_, objName);

    // Copy object file to cache
    {
        std::ifstream src(objPath, std::ios::binary);
        if (!src.is_open())
            return false;
        std::ofstream dst(destPath, std::ios::binary);
        if (!dst.is_open())
            return false;
        dst << src.rdbuf();
        if (!dst.good())
            return false;
    }

    // Load existing per-file manifest, update entry, save
    std::vector<SourceFileEntry> entries;
    int existingOpt = optLevel;
    bool existingDebug = debugInfo;
    loadPerFileManifest(entries, existingOpt, existingDebug);

    // If build config changed, clear old entries
    if (existingOpt != optLevel || existingDebug != debugInfo)
        entries.clear();

    // Update or add entry for this source
    std::string normalizedPath = normalizePath(sourcePath);
    bool found = false;
    for (auto &entry : entries) {
        if (normalizePath(entry.path) == normalizedPath) {
            entry.hash = hash;
            entry.objFile = objName;
            found = true;
            break;
        }
    }
    if (!found) {
        SourceFileEntry entry;
        entry.path = sourcePath;
        entry.hash = hash;
        entry.objFile = objName;
        entries.push_back(entry);
    }

    return savePerFileManifest(entries, optLevel, debugInfo);
}

bool BuildCache::loadPerFileManifest(std::vector<SourceFileEntry> &out,
                                      int &optLevel, bool &debugInfo) {
    std::ifstream file(perFileManifestPath_);
    if (!file.is_open())
        return false;

    out.clear();
    std::string line;
    while (std::getline(file, line)) {
        std::string trimmed = trim(line);
        if (trimmed.empty())
            continue;

        if (startsWith(trimmed, "OPT:")) {
            long val = strtol(trimmed.c_str() + 4, nullptr, 10);
            optLevel = static_cast<int>(val);
        } else if (startsWith(trimmed, "DEBUG:")) {
            debugInfo = (trimmed.substr(6) == "1");
        } else if (startsWith(trimmed, "FILE:")) {
            // FILE:path:hash:objfile (3 colon-separated fields after FILE:)
            std::string rest = trimmed.substr(5);
            // Find last two colons
            auto lastColon = rest.rfind(':');
            if (lastColon == std::string::npos || lastColon == 0)
                continue;
            std::string objFile = rest.substr(lastColon + 1);
            std::string remainder = rest.substr(0, lastColon);

            auto secondLastColon = remainder.rfind(':');
            if (secondLastColon == std::string::npos || secondLastColon == 0)
                continue;

            SourceFileEntry entry;
            entry.path = remainder.substr(0, secondLastColon);
            entry.hash = remainder.substr(secondLastColon + 1);
            entry.objFile = objFile;
            out.push_back(entry);
        }
    }

    return true;
}

bool BuildCache::savePerFileManifest(const std::vector<SourceFileEntry> &entries,
                                      int optLevel, bool debugInfo) {
    if (!createDirectories(cacheDir_))
        return false;

    std::ofstream file(perFileManifestPath_);
    if (!file.is_open())
        return false;

    file << "OPT:" << optLevel << "\n";
    file << "DEBUG:" << (debugInfo ? "1" : "0") << "\n";

    for (const auto &entry : entries) {
        file << "FILE:" << normalizePath(entry.path)
             << ":" << entry.hash
             << ":" << entry.objFile << "\n";
    }

    return file.good();
}

} // namespace liva
