#include "liva/Driver/CompilerInstance.h"
#include "liva/AST/ASTPrinter.h"
#include "liva/AST/Decl.h"
#include "liva/CodeGen/CodeGen.h"
#include "liva/CodeGen/TargetInfo.h"
#include "liva/Driver/SemaCache.h"
#include "liva/IR/IRGen.h"
#include "liva/Lexer/Lexer.h"
#include "liva/Parser/Parser.h"
#include "liva/Sema/ModuleLoader.h"
#include "liva/Sema/Sema.h"
#include <cstdio>
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
    for (const auto &sp : searchPaths_)
        loader.addSearchPath(sp);

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
    for (const auto &sp : searchPaths_)
        loader.addSearchPath(sp);

    Sema sema(diag_, &loader);
    if (!sema.analyze(*tu))
        return false;

    IRGen irgen(sourceManager_->getFilename().data(), diag_);
    irgen.setModuleLoader(&loader);
    irgen.setDebugInfo(debugInfo_);
    if (!irgen.generate(*tu))
        return false;

    if (outputPath.empty()) {
        irgen.dump();
    } else {
        return irgen.writeToFile(outputPath);
    }

    return true;
}

bool CompilerInstance::compileToObject(const std::string &outputObjPath, bool isEntryFile,
                                        ModuleLoader *sharedLoader) {
    auto tu = parseSource();
    if (!tu || diag_.hasErrors())
        return false;

    // Use shared or local ModuleLoader
    ModuleLoader localLoader;
    ModuleLoader *loader = sharedLoader;
    if (!loader) {
        loader = &localLoader;
        std::string fname(sourceManager_->getFilename());
        auto pos = fname.find_last_of("/\\");
        if (pos != std::string::npos)
            loader->setBasePath(fname.substr(0, pos + 1));
        for (const auto &sp : searchPaths_)
            loader->addSearchPath(sp);
    }

    Sema sema(diag_, loader);
    if (!sema.analyze(*tu))
        return false;

#ifdef LIVA_HAS_LLVM
    IRGen irgen(sourceManager_->getFilename().data(), diag_);
    irgen.setModuleLoader(loader);
    irgen.setDebugInfo(debugInfo_);
    irgen.setSeparateCompilation(true);
    irgen.setRequireMain(isEntryFile);
    if (!irgen.generate(*tu))
        return false;

    auto *module = irgen.getModule();
    CodeGen codegen(diag_);
    codegen.optimize(*module, optLevel_, ltoMode_, pgoMode_, pgoProfile_);

    bool useLto = (ltoMode_ == "thin" || ltoMode_ == "full");
    bool emitOk = useLto ? codegen.emitBitcode(*module, outputObjPath)
                         : codegen.emitObjectFile(*module, outputObjPath);
    if (!emitOk) {
        std::cerr << "error: failed to emit object file '" << outputObjPath << "'\n";
        return false;
    }

    return true;
#else
    (void)outputObjPath;
    (void)isEntryFile;
    std::cerr << "error: LLVM not available, cannot generate code\n";
    return false;
#endif
}

CompilerInstance::CompileResult CompilerInstance::compileToObjectWithMeta(
    const std::string &outputObjPath, bool isEntryFile,
    ModuleLoader *sharedLoader) {

    CompileResult result;
    result.success = false;

    auto tu = parseSource();
    if (!tu || diag_.hasErrors())
        return result;

    // Use shared or local ModuleLoader
    ModuleLoader localLoader;
    ModuleLoader *loader = sharedLoader;
    if (!loader) {
        loader = &localLoader;
        std::string fname(sourceManager_->getFilename());
        auto pos = fname.find_last_of("/\\");
        if (pos != std::string::npos)
            loader->setBasePath(fname.substr(0, pos + 1));
        for (const auto &sp : searchPaths_)
            loader->addSearchPath(sp);
    }

    Sema sema(diag_, loader);
    if (!sema.analyze(*tu))
        return result;

    // Compute interface hash from parsed+checked AST
    result.interfaceHash = SemaCache::computeInterfaceHash(*tu);

    // Extract import file paths from ImportDecl nodes
    for (const auto &decl : tu->getDeclarations()) {
        if (decl->getKind() == ASTNode::NodeKind::ImportDecl) {
            auto *importDecl = static_cast<const ImportDecl *>(decl.get());
            result.imports.push_back(importDecl->getPathString());
        }
    }

#ifdef LIVA_HAS_LLVM
    IRGen irgen(sourceManager_->getFilename().data(), diag_);
    irgen.setModuleLoader(loader);
    irgen.setDebugInfo(debugInfo_);
    irgen.setSeparateCompilation(true);
    irgen.setRequireMain(isEntryFile);
    if (!irgen.generate(*tu))
        return result;

    auto *module = irgen.getModule();
    CodeGen codegen(diag_);
    codegen.optimize(*module, optLevel_, ltoMode_, pgoMode_, pgoProfile_);

    bool useLto = (ltoMode_ == "thin" || ltoMode_ == "full");
    bool emitOk = useLto ? codegen.emitBitcode(*module, outputObjPath)
                         : codegen.emitObjectFile(*module, outputObjPath);
    if (!emitOk) {
        std::cerr << "error: failed to emit object file '" << outputObjPath << "'\n";
        return result;
    }

    result.success = true;
    return result;
#else
    (void)outputObjPath;
    (void)isEntryFile;
    std::cerr << "error: LLVM not available, cannot generate code\n";
    return result;
#endif
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
    for (const auto &sp : searchPaths_)
        loader.addSearchPath(sp);

    Sema sema(diag_, &loader);
    if (!sema.analyze(*tu))
        return false;

#ifdef LIVA_HAS_LLVM
    IRGen irgen(sourceManager_->getFilename().data(), diag_);
    irgen.setModuleLoader(&loader);
    irgen.setDebugInfo(debugInfo_);
    if (!irgen.generate(*tu))
        return false;

    // Use CodeGen API: optimize → emit object/bitcode → link
    auto *module = irgen.getModule();
    CodeGen codegen(diag_);
    codegen.optimize(*module, optLevel_, ltoMode_, pgoMode_, pgoProfile_);

    bool useLto = (ltoMode_ == "thin" || ltoMode_ == "full");
    std::string objPath = outputPath + (useLto ? ".bc" : ".o");
    lastObjPath_ = objPath;

    bool emitOk = useLto ? codegen.emitBitcode(*module, objPath)
                         : codegen.emitObjectFile(*module, objPath);
    if (!emitOk) {
        std::cerr << "error: failed to emit "
                  << (useLto ? "bitcode" : "object") << " file '"
                  << objPath << "'\n";
        return false;
    }

    // Find pre-built runtime library relative to livac executable
    auto fileExists = [](const std::string &path) {
        std::ifstream f(path);
        return f.is_open();
    };
    auto dirOfPath = [](const std::string &path) -> std::string {
        auto pos = path.find_last_of("/\\");
        return (pos != std::string::npos) ? path.substr(0, pos) : ".";
    };

    std::string exeDir = dirOfPath(executablePath_);
    std::string runtimeLib;
    // Search for both .lib (Windows) and .a (Linux/macOS) variants
    std::string candidates[] = {
        exeDir + "/lib/liva_runtime.lib",
        exeDir + "/../lib/liva_runtime.lib",
        exeDir + "/lib/libliva_runtime.a",
        exeDir + "/../lib/libliva_runtime.a",
    };
    for (auto &c : candidates) {
        if (fileExists(c)) { runtimeLib = c; break; }
    }
    if (runtimeLib.empty()) {
        std::cerr << "error: cannot find runtime library\n";
        std::cerr << "  searched paths:\n";
        for (auto &c : candidates)
            std::cerr << "    " << c << "\n";
        std::remove(objPath.c_str());
        return false;
    }

    // Link object file + runtime → executable
    std::vector<std::string> objects = { objPath, runtimeLib };
    std::vector<std::string> flags;
#ifdef _WIN32
    flags.push_back("-lwinhttp");
#endif
    if (debugInfo_)
        flags.push_back("-g");

    bool linkOk = codegen.link(objects, outputPath, flags, ltoMode_, pgoMode_);

    // Clean up temp object file (unless kept for caching)
    if (!keepObjectFile_)
        std::remove(objPath.c_str());

    if (!linkOk) {
        std::cerr << "error: linking failed for '" << outputPath << "'\n";
        std::cerr << "  objects: " << objPath << " " << runtimeLib << "\n";
        return false;
    }

    return true;
#else
    std::cerr << "error: LLVM not available, cannot generate code\n";
    return false;
#endif
}

} // namespace liva
