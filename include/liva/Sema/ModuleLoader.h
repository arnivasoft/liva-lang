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

    /// Set base directory for file resolution
    void setBasePath(const std::string &basePath) { basePath_ = basePath; }

    /// Register in-memory module source (for unit tests)
    void registerSource(const std::string &name, const std::string &source);

    /// Load a module by import path. Returns null + reports error if not found.
    Module *loadModule(const std::vector<std::string> &importPath,
                       DiagnosticsEngine &callerDiag,
                       SourceLocation loc);

private:
    std::string resolveModuleName(const std::vector<std::string> &path);
    std::string resolveFilePath(const std::vector<std::string> &path);
    void collectExportedSymbols(TranslationUnit &tu, std::vector<Symbol> &out);

    std::string basePath_;
    std::unordered_map<std::string, std::string> testSources_;
    std::unordered_map<std::string, std::unique_ptr<Module>> cache_;
    std::set<std::string> loading_;
};

} // namespace liva
