#pragma once

#include "liva/Common/Diagnostics.h"
#include "liva/Common/SourceLocation.h"
#include <memory>
#include <string>
#include <vector>

namespace liva {

class TranslationUnit;
class ModuleLoader;

/// Manages a single compilation
class CompilerInstance {
public:
    CompilerInstance();

    /// Set the path of the livac executable (for finding stdlib)
    void setExecutablePath(const std::string &path) { executablePath_ = path; }

    /// Set optimization level (0-3)
    void setOptLevel(int level) { optLevel_ = level; }

    /// Set debug info generation
    void setDebugInfo(bool enable) { debugInfo_ = enable; }

    /// Set LTO mode: "none", "thin", "full"
    void setLtoMode(const std::string &mode) { ltoMode_ = mode; }

    /// Set PGO mode: "none", "generate", "use"
    void setPgoMode(const std::string &mode) { pgoMode_ = mode; }

    /// Set profile data path for PGO use mode
    void setPgoProfile(const std::string &path) { pgoProfile_ = path; }

    /// Set additional module search paths
    void setSearchPaths(const std::vector<std::string> &paths) {
        searchPaths_ = paths;
    }

    /// Set source from file
    bool loadFile(const std::string &filename);

    /// Set source from string (for testing)
    void setSource(const std::string &filename, const std::string &source);

    /// Run the lexer and dump tokens
    bool dumpTokens();

    /// Run the parser and dump AST
    bool dumpAST();

    /// Run semantic analysis only
    bool checkOnly();

    /// Full compilation pipeline
    bool compile(const std::string &outputPath);

    /// Compile a single file to an object file (for separate compilation)
    /// isEntryFile: if true, requires main(); if false, skips main check
    /// sharedLoader: optional shared ModuleLoader for cross-file parse cache
    bool compileToObject(const std::string &outputObjPath, bool isEntryFile,
                         ModuleLoader *sharedLoader = nullptr);

    /// Emit LLVM IR
    bool emitIR(const std::string &outputPath);

    /// Keep object file after linking (for build cache)
    void setKeepObjectFile(bool keep) { keepObjectFile_ = keep; }
    const std::string &getObjectFilePath() const { return lastObjPath_; }

    DiagnosticsEngine &getDiag() { return diag_; }
    const SourceManager *getSourceManager() const { return sourceManager_.get(); }

private:
    /// Run lexing and parsing
    std::unique_ptr<TranslationUnit> parseSource();

    std::unique_ptr<SourceManager> sourceManager_;
    DiagnosticsEngine diag_;
    std::string executablePath_;
    int optLevel_ = 0;
    bool debugInfo_ = false;
    std::string ltoMode_ = "none";
    std::string pgoMode_ = "none";
    std::string pgoProfile_;
    bool keepObjectFile_ = false;
    std::string lastObjPath_;
    std::vector<std::string> searchPaths_;
};

} // namespace liva
