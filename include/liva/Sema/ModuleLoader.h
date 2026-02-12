#pragma once

#include "liva/AST/Decl.h"
#include "liva/Common/Diagnostics.h"
#include "liva/Sema/Scope.h"
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace liva {

class ModuleLoader {
public:
    struct Module {
        std::string name;
        std::unique_ptr<SourceManager> sm;
        DiagnosticsEngine diag;
        std::unique_ptr<TranslationUnit> tu;
        std::vector<Symbol> exportedSymbols;
    };

    ModuleLoader();

    /// Set base directory for file resolution
    void setBasePath(const std::string &basePath) { basePath_ = basePath; }

    /// Add additional search path for module resolution
    void addSearchPath(const std::string &path) {
        searchPaths_.push_back(path);
    }

    /// Register in-memory module source (for unit tests)
    void registerSource(const std::string &name, const std::string &source);

    /// Load a module by import path. Returns null + reports error if not found.
    Module *loadModule(const std::vector<std::string> &importPath,
                       DiagnosticsEngine &callerDiag,
                       SourceLocation loc);

    /// Get a previously loaded module from cache (for IRGen)
    Module *getLoadedModule(const std::string &name) {
        auto it = cache_.find(name);
        return (it != cache_.end()) ? it->second.get() : nullptr;
    }

private:
    void registerBuiltinModules();
    std::unique_ptr<Module> createBuiltinModule(
        const std::string &name,
        const std::vector<std::string> &funcNames,
        const std::vector<std::string> &structNames = {});

    std::string resolveModuleName(const std::vector<std::string> &path);
    std::string resolveFilePath(const std::vector<std::string> &path);
    void collectExportedSymbols(TranslationUnit &tu, std::vector<Symbol> &out);

    std::string basePath_;
    std::vector<std::string> searchPaths_;
    std::unordered_map<std::string, std::string> testSources_;
    std::unordered_map<std::string, std::unique_ptr<Module>> cache_;
    std::set<std::string> loading_;
};

} // namespace liva
