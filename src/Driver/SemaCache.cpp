#include "liva/Driver/SemaCache.h"
#include "liva/AST/Decl.h"
#include "liva/Driver/BuildCache.h"
#include "liva/Driver/ProjectConfig.h"
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace liva {

// FNV-1a 64-bit hash
static uint64_t fnv1a_hash(const char *data, size_t len) {
    uint64_t hash = 0xcbf29ce484222325ULL;
    for (size_t i = 0; i < len; i++) {
        hash ^= static_cast<uint8_t>(data[i]);
        hash *= 0x100000001b3ULL;
    }
    return hash;
}

static std::string toHex(uint64_t val) {
    char buf[17];
    std::snprintf(buf, sizeof(buf), "%016llx",
                  static_cast<unsigned long long>(val));
    return std::string(buf);
}

static std::string normPath(const std::string &p) {
    std::string r = p;
    for (auto &c : r) {
        if (c == '\\')
            c = '/';
    }
    return r;
}

static std::string trimLine(const std::string &s) {
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

SemaCache::SemaCache(const std::string &projectRoot) {
    cacheDir_ = joinPath(projectRoot, ".liva-cache");
    manifestPath_ = joinPath(cacheDir_, "sema_manifest");
}

// --- computeInterfaceHash ---

std::string SemaCache::computeInterfaceHash(const TranslationUnit &tu) {
    std::string sig;

    for (const auto &node : tu.getDeclarations()) {
        auto kind = node->getKind();

        if (kind == ASTNode::NodeKind::FuncDecl) {
            auto *fd = static_cast<const FuncDecl *>(node.get());
            if (!fd->isPublic())
                continue;
            sig += "pub func ";
            sig += fd->getName();
            sig += "(";
            bool first = true;
            for (const auto &p : fd->getParams()) {
                if (p.isSelf)
                    continue;
                if (!first)
                    sig += ",";
                first = false;
                if (p.type)
                    sig += p.type->toString();
                else
                    sig += "?";
            }
            sig += ")";
            if (fd->getReturnType()) {
                sig += "->";
                sig += fd->getReturnType()->toString();
            }
            sig += "\n";

        } else if (kind == ASTNode::NodeKind::StructDecl) {
            auto *sd = static_cast<const StructDecl *>(node.get());
            if (!sd->isPublic())
                continue;
            sig += "pub struct ";
            sig += sd->getName();
            sig += "{";
            for (size_t i = 0; i < sd->getFields().size(); i++) {
                if (i > 0)
                    sig += ",";
                sig += sd->getFields()[i]->getName();
                sig += ":";
                if (sd->getFields()[i]->getType())
                    sig += sd->getFields()[i]->getType()->toString();
                else
                    sig += "?";
            }
            sig += "}\n";

        } else if (kind == ASTNode::NodeKind::EnumDecl) {
            auto *ed = static_cast<const EnumDecl *>(node.get());
            if (!ed->isPublic())
                continue;
            sig += "pub enum ";
            sig += ed->getName();
            sig += "{";
            for (size_t i = 0; i < ed->getCases().size(); i++) {
                if (i > 0)
                    sig += ",";
                sig += ed->getCases()[i]->getName();
                if (ed->getCases()[i]->hasAssociatedValues()) {
                    sig += "(";
                    const auto &types = ed->getCases()[i]->getAssociatedTypes();
                    for (size_t j = 0; j < types.size(); j++) {
                        if (j > 0)
                            sig += ",";
                        sig += types[j]->toString();
                    }
                    sig += ")";
                }
            }
            sig += "}\n";

        } else if (kind == ASTNode::NodeKind::ProtocolDecl) {
            auto *pd = static_cast<const ProtocolDecl *>(node.get());
            if (!pd->isPublic())
                continue;
            sig += "pub protocol ";
            sig += pd->getName();
            sig += "{";
            for (size_t i = 0; i < pd->getMethods().size(); i++) {
                if (i > 0)
                    sig += ",";
                sig += pd->getMethods()[i]->getName();
                sig += "(";
                bool first = true;
                for (const auto &p : pd->getMethods()[i]->getParams()) {
                    if (p.isSelf)
                        continue;
                    if (!first)
                        sig += ",";
                    first = false;
                    if (p.type)
                        sig += p.type->toString();
                    else
                        sig += "?";
                }
                sig += ")";
                if (pd->getMethods()[i]->getReturnType()) {
                    sig += "->";
                    sig += pd->getMethods()[i]->getReturnType()->toString();
                }
            }
            sig += "}\n";

        } else if (kind == ASTNode::NodeKind::TypeAliasDecl) {
            auto *td = static_cast<const TypeAliasDecl *>(node.get());
            if (!td->isPublic())
                continue;
            sig += "pub type ";
            sig += td->getName();
            sig += "=";
            if (td->getTargetType())
                sig += td->getTargetType()->toString();
            sig += "\n";
        }
    }

    if (sig.empty())
        return toHex(0);

    uint64_t h = fnv1a_hash(sig.data(), sig.size());
    return toHex(h);
}

// --- check ---

std::vector<SemaCacheStatus> SemaCache::check(
    const std::vector<std::string> &sourceFiles,
    const std::vector<FileCompileStatus> &fileStatuses) {

    loadManifest();

    std::vector<SemaCacheStatus> results;
    results.resize(sourceFiles.size());

    // Phase 1: check source hashes and dep interface snapshots
    for (size_t i = 0; i < sourceFiles.size(); i++) {
        std::string key = normPath(sourceFiles[i]);
        results[i].sourcePath = sourceFiles[i];
        results[i].needsResema = false;
        results[i].cachedInterfaceHash = "";

        auto it = entries_.find(key);
        if (it == entries_.end()) {
            // Not in manifest — never compiled
            results[i].needsResema = true;
            continue;
        }

        results[i].cachedInterfaceHash = it->second.interfaceHash;

        // Check source hash
        if (it->second.sourceHash != fileStatuses[i].currentHash) {
            results[i].needsResema = true;
            continue;
        }

        // Check dep interface snapshots
        for (const auto &dep : it->second.depInterfaceHashes) {
            auto depIt = entries_.find(dep.first);
            if (depIt == entries_.end() ||
                depIt->second.interfaceHash != dep.second) {
                results[i].needsResema = true;
                break;
            }
        }
    }

    // Phase 2: BFS cascade from needsResema files
    // Build reverse import map: file → indices of files that import it
    std::unordered_map<std::string, std::vector<size_t>> importedBy;
    for (size_t i = 0; i < sourceFiles.size(); i++) {
        std::string key = normPath(sourceFiles[i]);
        auto it = entries_.find(key);
        if (it != entries_.end()) {
            for (const auto &imp : it->second.imports) {
                importedBy[imp].push_back(i);
            }
        }
    }

    // BFS from needsResema files
    std::vector<size_t> queue;
    for (size_t i = 0; i < results.size(); i++) {
        if (results[i].needsResema)
            queue.push_back(i);
    }

    size_t head = 0;
    while (head < queue.size()) {
        size_t idx = queue[head++];
        std::string key = normPath(sourceFiles[idx]);
        auto impIt = importedBy.find(key);
        if (impIt != importedBy.end()) {
            for (size_t depIdx : impIt->second) {
                if (!results[depIdx].needsResema) {
                    results[depIdx].needsResema = true;
                    queue.push_back(depIdx);
                }
            }
        }
    }

    return results;
}

// --- store ---

void SemaCache::store(const std::string &sourcePath, const std::string &sourceHash,
                      const std::string &interfaceHash,
                      const std::vector<std::string> &imports) {
    SemaCacheEntry entry;
    entry.path = normPath(sourcePath);
    entry.sourceHash = sourceHash;
    entry.interfaceHash = interfaceHash;
    for (const auto &imp : imports)
        entry.imports.push_back(normPath(imp));

    // Snapshot dep interface hashes from current manifest entries
    for (const auto &imp : entry.imports) {
        auto it = entries_.find(imp);
        if (it != entries_.end())
            entry.depInterfaceHashes[imp] = it->second.interfaceHash;
    }

    entries_[entry.path] = std::move(entry);
}

// --- manifest I/O ---

bool SemaCache::loadManifest() {
    std::ifstream file(manifestPath_);
    if (!file.is_open())
        return false;

    entries_.clear();
    std::string line;
    while (std::getline(file, line)) {
        std::string trimmed = trimLine(line);
        if (trimmed.empty())
            continue;

        // Format: SEMA<TAB>path<TAB>sourceHash<TAB>interfaceHash<TAB>imports<TAB>depIfaceSnapshot
        // Using TAB delimiter to avoid path colon issues (e.g., C:\...)
        if (trimmed.size() < 4 || trimmed.substr(0, 4) != "SEMA")
            continue;

        // Split by tab
        std::vector<std::string> fields;
        std::istringstream ss(trimmed);
        std::string field;
        while (std::getline(ss, field, '\t'))
            fields.push_back(field);

        // SEMA, path, sourceHash, interfaceHash, imports, depSnapshot
        if (fields.size() < 4)
            continue;

        SemaCacheEntry entry;
        entry.path = fields[1];
        entry.sourceHash = fields[2];
        entry.interfaceHash = fields[3];

        // Parse imports (comma-separated)
        if (fields.size() > 4 && !fields[4].empty()) {
            std::istringstream impSS(fields[4]);
            std::string imp;
            while (std::getline(impSS, imp, ',')) {
                if (!imp.empty())
                    entry.imports.push_back(imp);
            }
        }

        // Parse dep interface snapshot (key=value pairs, semicolon-separated)
        if (fields.size() > 5 && !fields[5].empty()) {
            std::istringstream depSS(fields[5]);
            std::string pair;
            while (std::getline(depSS, pair, ';')) {
                auto eqPos = pair.find('=');
                if (eqPos != std::string::npos && eqPos > 0) {
                    std::string depPath = pair.substr(0, eqPos);
                    std::string depHash = pair.substr(eqPos + 1);
                    entry.depInterfaceHashes[depPath] = depHash;
                }
            }
        }

        entries_[entry.path] = std::move(entry);
    }

    return true;
}

bool SemaCache::saveManifest() {
    if (!createDirectories(cacheDir_))
        return false;

    std::ofstream file(manifestPath_);
    if (!file.is_open())
        return false;

    for (const auto &pair : entries_) {
        const auto &e = pair.second;
        file << "SEMA\t" << e.path << "\t" << e.sourceHash << "\t"
             << e.interfaceHash << "\t";

        // Imports (comma-separated)
        for (size_t i = 0; i < e.imports.size(); i++) {
            if (i > 0)
                file << ",";
            file << e.imports[i];
        }

        file << "\t";

        // Dep interface snapshot (semicolon-separated key=value)
        bool first = true;
        for (const auto &dep : e.depInterfaceHashes) {
            if (!first)
                file << ";";
            first = false;
            file << dep.first << "=" << dep.second;
        }

        file << "\n";
    }

    return file.good();
}

} // namespace liva
