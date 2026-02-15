#include "liva/Driver/PackageManager.h"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>

#ifdef _WIN32
#include <direct.h>
#define popen _popen
#define pclose _pclose
#endif

namespace liva {

// ============================================================
// SHA-256 Implementation (FIPS 180-4)
// ============================================================

namespace {

static const uint32_t sha256_k[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

inline uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
inline uint32_t ch(uint32_t x, uint32_t y, uint32_t z) { return (x & y) ^ (~x & z); }
inline uint32_t maj(uint32_t x, uint32_t y, uint32_t z) { return (x & y) ^ (x & z) ^ (y & z); }
inline uint32_t sigma0(uint32_t x) { return rotr(x, 2) ^ rotr(x, 13) ^ rotr(x, 22); }
inline uint32_t sigma1(uint32_t x) { return rotr(x, 6) ^ rotr(x, 11) ^ rotr(x, 25); }
inline uint32_t gamma0(uint32_t x) { return rotr(x, 7) ^ rotr(x, 18) ^ (x >> 3); }
inline uint32_t gamma1(uint32_t x) { return rotr(x, 17) ^ rotr(x, 19) ^ (x >> 10); }

std::string sha256_compute(const uint8_t *data, size_t len) {
    uint32_t h[8] = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
    };

    // Pre-processing: pad message
    size_t bitLen = len * 8;
    size_t padLen = len + 1; // +1 for 0x80 byte
    while (padLen % 64 != 56) padLen++;
    padLen += 8; // 64-bit length

    std::vector<uint8_t> msg(padLen, 0);
    memcpy(msg.data(), data, len);
    msg[len] = 0x80;
    // Big-endian 64-bit length at end
    for (int i = 0; i < 8; i++)
        msg[padLen - 1 - i] = (uint8_t)(bitLen >> (i * 8));

    // Process 64-byte blocks
    for (size_t offset = 0; offset < padLen; offset += 64) {
        uint32_t w[64];
        for (int i = 0; i < 16; i++)
            w[i] = ((uint32_t)msg[offset + i*4] << 24) |
                    ((uint32_t)msg[offset + i*4+1] << 16) |
                    ((uint32_t)msg[offset + i*4+2] << 8) |
                    ((uint32_t)msg[offset + i*4+3]);
        for (int i = 16; i < 64; i++)
            w[i] = gamma1(w[i-2]) + w[i-7] + gamma0(w[i-15]) + w[i-16];

        uint32_t a=h[0], b=h[1], c=h[2], d=h[3],
                 e=h[4], f=h[5], g=h[6], hh=h[7];
        for (int i = 0; i < 64; i++) {
            uint32_t t1 = hh + sigma1(e) + ch(e,f,g) + sha256_k[i] + w[i];
            uint32_t t2 = sigma0(a) + maj(a,b,c);
            hh=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
        }
        h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=d;
        h[4]+=e; h[5]+=f; h[6]+=g; h[7]+=hh;
    }

    // Output hex digest
    char hex[65];
    for (int i = 0; i < 8; i++)
        snprintf(hex + i*8, 9, "%08x", h[i]);
    hex[64] = '\0';
    return std::string(hex);
}

} // anonymous namespace

std::string PackageManager::sha256(const std::string &data) {
    return sha256_compute(reinterpret_cast<const uint8_t*>(data.data()),
                          data.size());
}

std::string PackageManager::sha256File(const std::string &path) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return "";
    std::ostringstream ss;
    ss << f.rdbuf();
    return sha256(ss.str());
}

bool PackageManager::verifyChecksum(const std::string &data,
                                     const std::string &expected) {
    // expected format: "sha256:hexdigest"
    if (expected.size() < 8 || expected.substr(0, 7) != "sha256:") return false;
    std::string expectedHash = expected.substr(7);
    std::string actual = sha256(data);
    return actual == expectedHash;
}

// ============================================================
// JSON Helpers (minimal, for registry responses)
// ============================================================

namespace json {

// Skip whitespace
static size_t skipWs(const std::string &s, size_t pos) {
    while (pos < s.size() && (s[pos]==' '||s[pos]=='\t'||s[pos]=='\n'||s[pos]=='\r')) pos++;
    return pos;
}

// Skip a JSON string starting at opening quote, return pos after closing quote
static size_t skipString(const std::string &s, size_t pos) {
    if (pos >= s.size() || s[pos] != '"') return pos;
    pos++;
    while (pos < s.size() && s[pos] != '"') {
        if (s[pos] == '\\') pos++;
        pos++;
    }
    if (pos < s.size()) pos++; // closing quote
    return pos;
}

// Skip a JSON value, return pos after value
static size_t skipValue(const std::string &s, size_t pos) {
    pos = skipWs(s, pos);
    if (pos >= s.size()) return pos;
    if (s[pos] == '"') return skipString(s, pos);
    if (s[pos] == '{' || s[pos] == '[') {
        char open = s[pos], close = (open == '{') ? '}' : ']';
        int depth = 1; pos++;
        while (pos < s.size() && depth > 0) {
            if (s[pos] == '"') { pos = skipString(s, pos); continue; }
            if (s[pos] == open) depth++;
            else if (s[pos] == close) depth--;
            pos++;
        }
        return pos;
    }
    while (pos < s.size() && s[pos] != ',' && s[pos] != '}' &&
           s[pos] != ']' && s[pos] != ' ' && s[pos] != '\n') pos++;
    return pos;
}

// Find value position for key in top-level object
static size_t findKey(const std::string &json, const std::string &key, size_t &valEnd) {
    size_t pos = skipWs(json, 0);
    if (pos >= json.size() || json[pos] != '{') { valEnd = 0; return std::string::npos; }
    pos++;
    while (pos < json.size()) {
        pos = skipWs(json, pos);
        if (pos >= json.size() || json[pos] == '}') break;
        if (json[pos] == ',') { pos++; continue; }
        if (json[pos] != '"') break;
        size_t kStart = pos + 1;
        size_t kEnd = skipString(json, pos);
        std::string k = json.substr(kStart, kEnd - kStart - 1);
        pos = skipWs(json, kEnd);
        if (pos >= json.size() || json[pos] != ':') break;
        pos++;
        pos = skipWs(json, pos);
        size_t vStart = pos;
        size_t vEnd = skipValue(json, pos);
        if (k == key) { valEnd = vEnd; return vStart; }
        pos = vEnd;
    }
    valEnd = 0;
    return std::string::npos;
}

// Extract string from JSON string literal at pos
static std::string extractString(const std::string &s, size_t pos) {
    if (pos >= s.size() || s[pos] != '"') return "";
    pos++;
    std::string result;
    while (pos < s.size() && s[pos] != '"') {
        if (s[pos] == '\\') {
            pos++;
            if (pos < s.size()) {
                switch(s[pos]) {
                    case 'n': result += '\n'; break;
                    case 't': result += '\t'; break;
                    case 'r': result += '\r'; break;
                    case '\\': result += '\\'; break;
                    case '"': result += '"'; break;
                    default: result += s[pos]; break;
                }
            }
        } else {
            result += s[pos];
        }
        pos++;
    }
    return result;
}

std::string getString(const std::string &json, const std::string &key) {
    size_t valEnd;
    size_t vStart = findKey(json, key, valEnd);
    if (vStart == std::string::npos) return "";
    return extractString(json, vStart);
}

std::vector<std::string> getStringArray(const std::string &json,
                                        const std::string &key) {
    std::vector<std::string> result;
    size_t valEnd;
    size_t vStart = findKey(json, key, valEnd);
    if (vStart == std::string::npos) return result;
    size_t pos = skipWs(json, vStart);
    if (pos >= json.size() || json[pos] != '[') return result;
    pos++;
    while (pos < json.size()) {
        pos = skipWs(json, pos);
        if (pos >= json.size() || json[pos] == ']') break;
        if (json[pos] == ',') { pos++; continue; }
        if (json[pos] == '"') {
            result.push_back(extractString(json, pos));
            pos = skipString(json, pos);
        } else {
            pos = skipValue(json, pos);
        }
    }
    return result;
}

std::unordered_map<std::string, std::string> getObject(
    const std::string &json, const std::string &key) {
    std::unordered_map<std::string, std::string> result;
    size_t valEnd;
    size_t vStart = findKey(json, key, valEnd);
    if (vStart == std::string::npos) return result;
    size_t pos = skipWs(json, vStart);
    if (pos >= json.size() || json[pos] != '{') return result;
    pos++;
    while (pos < json.size()) {
        pos = skipWs(json, pos);
        if (pos >= json.size() || json[pos] == '}') break;
        if (json[pos] == ',') { pos++; continue; }
        if (json[pos] != '"') break;
        std::string k = extractString(json, pos);
        pos = skipString(json, pos);
        pos = skipWs(json, pos);
        if (pos >= json.size() || json[pos] != ':') break;
        pos++;
        pos = skipWs(json, pos);
        if (pos < json.size() && json[pos] == '"') {
            result[k] = extractString(json, pos);
            pos = skipString(json, pos);
        } else {
            size_t vEnd = skipValue(json, pos);
            result[k] = json.substr(pos, vEnd - pos);
            pos = vEnd;
        }
    }
    return result;
}

} // namespace json

// ============================================================
// Registry Parsing
// ============================================================

bool PackageManager::parseRegistryEntry(const std::string &jsonStr,
                                         RegistryEntry &out) {
    out.name = json::getString(jsonStr, "name");
    std::string verStr = json::getString(jsonStr, "version");
    if (out.name.empty() || verStr.empty()) return false;

    auto verRes = parseSemVer(verStr);
    if (!verRes.success) return false;
    out.version = verRes.version;

    out.downloadUrl = json::getString(jsonStr, "url");
    out.checksum = json::getString(jsonStr, "checksum");

    // Parse dependencies object
    auto depsMap = json::getObject(jsonStr, "dependencies");
    for (const auto &kv : depsMap) {
        auto cr = parseVersionConstraint(kv.second);
        if (cr.success) {
            PackageDep dep;
            dep.name = kv.first;
            dep.constraint = cr.constraint;
            out.dependencies.push_back(dep);
        }
    }
    return true;
}

std::vector<std::string> PackageManager::parseVersionList(
    const std::string &jsonStr) {
    return json::getStringArray(jsonStr, "versions");
}

// ============================================================
// HTTP Helpers (via curl command) — hookable for testing
// ============================================================

namespace {

std::string defaultHttpGet(const std::string &url) {
    if (url.empty()) return "";
    std::string cmd = "curl -s --max-time 30 \"" + url + "\"";
    FILE *pipe = popen(cmd.c_str(), "r");
    if (!pipe) return "";
    std::string result;
    char buf[4096];
    while (fgets(buf, sizeof(buf), pipe))
        result += buf;
    pclose(pipe);
    return result;
}

bool defaultDownloadFile(const std::string &url,
                         const std::string &destPath) {
    if (url.empty() || destPath.empty()) return false;
    std::string cmd = "curl -s --max-time 120 -o \"" + destPath +
                      "\" \"" + url + "\"";
    return system(cmd.c_str()) == 0;
}

int defaultExtractTar(const std::string &tarPath, const std::string &destDir) {
    std::string cmd = "tar xzf \"" + tarPath + "\" -C \"" + destDir + "\"";
    return system(cmd.c_str());
}

} // anonymous namespace

PackageManager::HttpGetFn PackageManager::httpGetFn = defaultHttpGet;
PackageManager::DownloadFileFn PackageManager::downloadFileFn = defaultDownloadFile;
PackageManager::ExtractTarFn PackageManager::extractTarFn = defaultExtractTar;

void PackageManager::resetHttpHooks() {
    httpGetFn = defaultHttpGet;
    downloadFileFn = defaultDownloadFile;
    extractTarFn = defaultExtractTar;
}

// ============================================================
// PackageManager Core
// ============================================================

PackageManager::PackageManager(const std::string &projectRoot,
                               const std::string &registryUrl)
    : projectRoot_(projectRoot), registryUrl_(registryUrl) {
    // Normalize registry URL (strip trailing slash)
    if (!registryUrl_.empty() && registryUrl_.back() == '/')
        registryUrl_.pop_back();

    packagesDir_ = projectRoot_;
    if (!packagesDir_.empty() && packagesDir_.back() != '/' &&
        packagesDir_.back() != '\\')
        packagesDir_ += "/";
    packagesDir_ += "packages";
}

bool PackageManager::resolveLocal(const PackageDep &dep,
                                   ResolvedPackage &out) {
    std::string pkgDir = packagesDir_ + "/" + dep.name;
    std::string tomlPath = pkgDir + "/liva.toml";

    std::ifstream f(tomlPath);
    if (!f.is_open()) return false;

    std::stringstream ss;
    ss << f.rdbuf();
    std::string tomlContent = ss.str();

    // Validate package
    auto parsed = parseTOML(tomlContent);
    if (!parsed.success) return false;

    std::string pkgName = parsed.doc.getString("project", "name", "");
    if (pkgName != dep.name) return false;

    std::string verStr = parsed.doc.getString("project", "version", "");
    auto verRes = parseSemVer(verStr);
    if (!verRes.success) return false;
    if (!dep.constraint.satisfiedBy(verRes.version)) return false;

    out.name = dep.name;
    out.version = verRes.version;
    out.path = pkgDir;
    out.srcPath = pkgDir + "/src";
    return true;
}

bool PackageManager::queryRegistry(const PackageDep &dep,
                                    RegistryEntry &out) {
    if (registryUrl_.empty()) return false;

    // 1. Get version list
    std::string listUrl = registryUrl_ + "/packages/" + dep.name;
    std::string listJson = httpGetFn(listUrl);
    if (listJson.empty()) return false;

    auto versions = parseVersionList(listJson);
    if (versions.empty()) return false;

    // 2. Find best version satisfying constraint (highest matching)
    SemVer bestVer{-1, 0, 0};
    std::string bestVerStr;
    for (const auto &vs : versions) {
        auto vr = parseSemVer(vs);
        if (vr.success && dep.constraint.satisfiedBy(vr.version)) {
            if (bestVer.major < 0 || vr.version > bestVer) {
                bestVer = vr.version;
                bestVerStr = vs;
            }
        }
    }
    if (bestVerStr.empty()) return false;

    // 3. Get package info for selected version
    std::string infoUrl = registryUrl_ + "/packages/" + dep.name +
                          "/" + bestVerStr;
    std::string infoJson = httpGetFn(infoUrl);
    if (infoJson.empty()) return false;

    return parseRegistryEntry(infoJson, out);
}

bool PackageManager::installPackage(const RegistryEntry &entry,
                                     std::string &errorMsg) {
    if (entry.downloadUrl.empty()) {
        errorMsg = "no download URL for package '" + entry.name + "'";
        return false;
    }

    // Create packages/ dir if needed
#ifdef _WIN32
    _mkdir(packagesDir_.c_str());
#else
    mkdir(packagesDir_.c_str(), 0755);
#endif

    // Download to temp file
    std::string tempFile = packagesDir_ + "/" + entry.name + "-" +
                           entry.version.toString() + ".tar.gz";
    if (!downloadFileFn(entry.downloadUrl, tempFile)) {
        errorMsg = "failed to download package '" + entry.name + "'";
        return false;
    }

    // Verify checksum if provided
    if (!entry.checksum.empty()) {
        std::string fileHash = sha256File(tempFile);
        std::string expected = entry.checksum;
        if (expected.substr(0, 7) == "sha256:") expected = expected.substr(7);
        if (fileHash != expected) {
            std::remove(tempFile.c_str());
            errorMsg = "checksum mismatch for package '" + entry.name +
                       "': expected " + entry.checksum +
                       ", got sha256:" + fileHash;
            return false;
        }
    }

    // Extract to packages/{name}/
    std::string pkgDir = packagesDir_ + "/" + entry.name;
#ifdef _WIN32
    _mkdir(pkgDir.c_str());
#else
    mkdir(pkgDir.c_str(), 0755);
#endif
    int rc = extractTarFn(tempFile, pkgDir);
    std::remove(tempFile.c_str());

    if (rc != 0) {
        errorMsg = "failed to extract package '" + entry.name + "'";
        return false;
    }

    return true;
}

bool PackageManager::resolveDependencyTree(
    const std::vector<PackageDep> &deps,
    std::vector<ResolvedPackage> &resolved,
    std::unordered_set<std::string> &resolving,
    std::string &errorMsg) {

    for (const auto &dep : deps) {
        // Skip already resolved
        bool alreadyResolved = false;
        for (const auto &r : resolved) {
            if (r.name == dep.name) { alreadyResolved = true; break; }
        }
        if (alreadyResolved) continue;

        // Circular dependency check
        if (resolving.count(dep.name)) {
            errorMsg = "circular dependency detected: '" + dep.name + "'";
            return false;
        }
        resolving.insert(dep.name);

        // Try local first
        ResolvedPackage pkg;
        RegistryEntry regEntry;

        if (resolveLocal(dep, pkg)) {
            resolved.push_back(pkg);

            // Check if local package has its own dependencies
            std::string tomlPath = pkg.path + "/liva.toml";
            std::ifstream f(tomlPath);
            if (f.is_open()) {
                std::stringstream ss;
                ss << f.rdbuf();
                auto parsed = parseTOML(ss.str());
                if (parsed.success) {
                    auto subDeps = parseDependencies(parsed.doc);
                    if (!subDeps.empty()) {
                        if (!resolveDependencyTree(subDeps, resolved,
                                                    resolving, errorMsg)) {
                            resolving.erase(dep.name);
                            return false;
                        }
                    }
                }
            }
        } else if (!registryUrl_.empty() && queryRegistry(dep, regEntry)) {
            // Download and install
            if (!installPackage(regEntry, errorMsg)) {
                resolving.erase(dep.name);
                return false;
            }
            // Now resolve locally (should succeed after install)
            if (!resolveLocal(dep, pkg)) {
                errorMsg = "package '" + dep.name +
                           "' installed but still cannot resolve";
                resolving.erase(dep.name);
                return false;
            }
            resolved.push_back(pkg);

            // Resolve transitive dependencies from registry metadata
            if (!regEntry.dependencies.empty()) {
                if (!resolveDependencyTree(regEntry.dependencies, resolved,
                                            resolving, errorMsg)) {
                    resolving.erase(dep.name);
                    return false;
                }
            }
        } else {
            errorMsg = "package '" + dep.name + "' not found";
            if (registryUrl_.empty())
                errorMsg += " (no registry configured)";
            resolving.erase(dep.name);
            return false;
        }

        resolving.erase(dep.name);
    }
    return true;
}

InstallResult PackageManager::installSingle(const std::string &pkgName,
                                             const std::string &versionStr) {
    InstallResult result;
    result.name = pkgName;

    // Build version constraint
    VersionConstraint constraint;
    if (versionStr.empty()) {
        // Default: accept any version (latest)
        constraint.kind = VersionConstraint::Minimum;
        constraint.min = SemVer{0, 0, 0};
    } else {
        auto cr = parseVersionConstraint(versionStr);
        if (!cr.success) {
            result.errorMsg = "invalid version constraint: " + cr.errorMsg;
            return result;
        }
        constraint = cr.constraint;
    }

    PackageDep dep;
    dep.name = pkgName;
    dep.constraint = constraint;

    // 1. Try local resolution first
    ResolvedPackage pkg;
    if (resolveLocal(dep, pkg)) {
        result.success = true;
        result.version = pkg.version;
        // Read checksum if available
        std::string csPath = pkg.path + "/.checksum";
        std::ifstream csFile(csPath);
        if (csFile.is_open()) {
            std::string cs;
            std::getline(csFile, cs);
            result.checksum = cs;
        }
        return result;
    }

    // 2. Query registry
    if (registryUrl_.empty()) {
        result.errorMsg = "package '" + pkgName +
            "' not found locally and no registry configured"
            " — set [registry] url in liva.toml or LIVA_REGISTRY_URL env";
        return result;
    }

    RegistryEntry regEntry;
    if (!queryRegistry(dep, regEntry)) {
        result.errorMsg = "package '" + pkgName + "' not found in registry";
        return result;
    }

    // 3. Download and install
    std::string installError;
    if (!installPackage(regEntry, installError)) {
        result.errorMsg = installError;
        return result;
    }

    // 4. Write checksum file
    if (!regEntry.checksum.empty()) {
        std::string csPath = packagesDir_ + "/" + pkgName + "/.checksum";
        std::ofstream csFile(csPath);
        if (csFile.is_open())
            csFile << regEntry.checksum;
    }

    // 5. Verify installation via local resolve
    if (!resolveLocal(dep, pkg)) {
        result.errorMsg = "package '" + pkgName +
                          "' installed but cannot resolve locally";
        return result;
    }

    // 6. Resolve transitive dependencies
    if (!regEntry.dependencies.empty()) {
        std::vector<ResolvedPackage> resolved;
        resolved.push_back(pkg);
        std::unordered_set<std::string> resolving;
        resolving.insert(pkgName);
        std::string treeError;
        if (!resolveDependencyTree(regEntry.dependencies, resolved,
                                    resolving, treeError)) {
            result.errorMsg = "transitive dependency error: " + treeError;
            return result;
        }
    }

    result.success = true;
    result.version = pkg.version;
    result.checksum = regEntry.checksum;
    return result;
}

PackageResolutionResult PackageManager::resolveAndInstall(
    const std::vector<PackageDep> &deps) {

    PackageResolutionResult result;
    std::unordered_set<std::string> resolving;
    std::string errorMsg;

    if (resolveDependencyTree(deps, result.packages, resolving, errorMsg)) {
        result.success = true;
    } else {
        result.errorMsg = errorMsg;
    }
    return result;
}

} // namespace liva
