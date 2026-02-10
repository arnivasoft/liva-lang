#include "liva/Driver/CompilerInstance.h"
#include "liva/AST/ASTPrinter.h"
#include "liva/AST/Decl.h"
#include "liva/CodeGen/CodeGen.h"
#include "liva/IR/IRGen.h"
#include "liva/Lexer/Lexer.h"
#include "liva/Parser/Parser.h"
#include "liva/Sema/ModuleLoader.h"
#include "liva/Sema/Sema.h"
#include <fstream>
#include <iostream>
#include <sstream>

namespace liva {

CompilerInstance::CompilerInstance() {
    diag_.setPrintCallback([this](const Diagnostic &d) {
        DiagnosticsEngine::printToStderr(d, sourceManager_.get());
    });
}

bool CompilerInstance::loadFile(const std::string &filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "error: cannot open file '" << filename << "'\n";
        return false;
    }

    std::stringstream ss;
    ss << file.rdbuf();
    setSource(filename, ss.str());
    return true;
}

void CompilerInstance::setSource(const std::string &filename, const std::string &source) {
    sourceManager_ = std::make_unique<SourceManager>(filename, source);
    diag_.setSourceManager(sourceManager_.get());
}

std::unique_ptr<TranslationUnit> CompilerInstance::parseSource() {
    if (!sourceManager_)
        return nullptr;

    Lexer lexer(*sourceManager_, diag_);
    Parser parser(lexer, diag_);
    return parser.parseTranslationUnit();
}

bool CompilerInstance::dumpTokens() {
    if (!sourceManager_)
        return false;

    Lexer lexer(*sourceManager_, diag_);
    auto tokens = lexer.lexAll();

    for (auto &tok : tokens) {
        std::cout << tok.toString() << "\n";
    }

    return !diag_.hasErrors();
}

bool CompilerInstance::dumpAST() {
    auto tu = parseSource();
    if (!tu || diag_.hasErrors())
        return false;

    ASTPrinter printer(std::cout);
    printer.print(*tu);

    return true;
}

bool CompilerInstance::checkOnly() {
    auto tu = parseSource();
    if (!tu || diag_.hasErrors())
        return false;

    ModuleLoader loader;
    std::string fname(sourceManager_->getFilename());
    auto pos = fname.find_last_of("/\\");
    if (pos != std::string::npos)
        loader.setBasePath(fname.substr(0, pos + 1));

    Sema sema(diag_, &loader);
    return sema.analyze(*tu);
}

bool CompilerInstance::emitIR(const std::string &outputPath) {
    auto tu = parseSource();
    if (!tu || diag_.hasErrors())
        return false;

    ModuleLoader loader;
    std::string fname(sourceManager_->getFilename());
    auto pos = fname.find_last_of("/\\");
    if (pos != std::string::npos)
        loader.setBasePath(fname.substr(0, pos + 1));

    Sema sema(diag_, &loader);
    if (!sema.analyze(*tu))
        return false;

    IRGen irgen(sourceManager_->getFilename().data(), diag_);
    irgen.setModuleLoader(&loader);
    if (!irgen.generate(*tu))
        return false;

    if (outputPath.empty()) {
        irgen.dump();
    } else {
        return irgen.writeToFile(outputPath);
    }

    return true;
}

bool CompilerInstance::compile(const std::string &outputPath) {
    auto tu = parseSource();
    if (!tu || diag_.hasErrors())
        return false;

    ModuleLoader loader;
    std::string fname(sourceManager_->getFilename());
    auto pos = fname.find_last_of("/\\");
    if (pos != std::string::npos)
        loader.setBasePath(fname.substr(0, pos + 1));

    Sema sema(diag_, &loader);
    if (!sema.analyze(*tu))
        return false;

#ifdef LIVA_HAS_LLVM
    IRGen irgen(sourceManager_->getFilename().data(), diag_);
    irgen.setModuleLoader(&loader);
    if (!irgen.generate(*tu))
        return false;

    // Write LLVM IR to temp file, then use clang to compile + link
    std::string irPath = outputPath + ".ll";
    if (!irgen.writeToFile(irPath)) {
        std::cerr << "error: failed to write IR file\n";
        return false;
    }

    // Use clang to compile IR → executable (handles runtime linking automatically)
    std::string cmd = "C:\\LLVM\\bin\\clang.exe -Wno-override-module \"" + irPath + "\" -o \"" + outputPath + "\"";
    int result = std::system(cmd.c_str());

    // Clean up temp IR file
    std::remove(irPath.c_str());

    if (result != 0) {
        std::cerr << "error: linking failed\n";
        return false;
    }

    return true;
#else
    std::cerr << "error: LLVM not available, cannot generate code\n";
    return false;
#endif
}

} // namespace liva
