#pragma once

#include "liva/Common/Diagnostics.h"
#include "liva/Common/SourceLocation.h"
#include <memory>
#include <string>

namespace liva {

class TranslationUnit;

/// Manages a single compilation
class CompilerInstance {
public:
    CompilerInstance();

    /// Set the path of the livac executable (for finding stdlib)
    void setExecutablePath(const std::string &path) { executablePath_ = path; }

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

    /// Emit LLVM IR
    bool emitIR(const std::string &outputPath);

    DiagnosticsEngine &getDiag() { return diag_; }
    const SourceManager *getSourceManager() const { return sourceManager_.get(); }

private:
    /// Run lexing and parsing
    std::unique_ptr<TranslationUnit> parseSource();

    std::unique_ptr<SourceManager> sourceManager_;
    DiagnosticsEngine diag_;
    std::string executablePath_;
};

} // namespace liva
