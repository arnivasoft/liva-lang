#pragma once

#include "liva/Common/Diagnostics.h"
#include "liva/Common/SourceLocation.h"
#include <memory>
#include <optional>
#include <string>
#include <vector>

#ifdef LIVA_HAS_LLVM
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#endif

namespace liva {

class TranslationUnit;
class ModuleLoader;
class PluginRegistry;

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

    /// Set cross-compilation target triple
    void setTargetTriple(const std::string &triple) { targetTriple_ = triple; }

    /// Set color mode for diagnostic output
    void setColorMode(ColorMode mode) { colorMode_ = mode; }

    /// Enable macro expansion tracing to stderr
    void setTraceMacros(bool v) { traceMacros_ = v; }

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

    /// Compile to object, returning sema metadata for caching
    struct CompileResult {
        bool success;
        std::string interfaceHash;
        std::vector<std::string> imports;  // resolved import file paths
    };
    CompileResult compileToObjectWithMeta(const std::string &outputObjPath, bool isEntryFile,
                                          ModuleLoader *sharedLoader = nullptr);

#ifdef LIVA_HAS_LLVM
    /// Compile to IR module (for JIT). Returns {context, module} pair.
    struct IRResult {
        std::unique_ptr<llvm::LLVMContext> context;
        std::unique_ptr<llvm::Module> module;
    };
    std::optional<IRResult> compileToIR();
#endif

    /// Emit LLVM IR
    bool emitIR(const std::string &outputPath);

    /// Keep object file after linking (for build cache)
    void setKeepObjectFile(bool keep) { keepObjectFile_ = keep; }
    const std::string &getObjectFilePath() const { return lastObjPath_; }

    void setPluginRegistry(PluginRegistry *registry) { pluginRegistry_ = registry; }

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
    std::string targetTriple_;
    ColorMode colorMode_ = ColorMode::Auto;
    bool traceMacros_ = false;
    PluginRegistry *pluginRegistry_ = nullptr;
};

} // namespace liva
