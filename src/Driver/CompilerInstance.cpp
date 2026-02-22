#include "liva/Driver/CompilerInstance.h"
#include "liva/AST/ASTPrinter.h"
#include "liva/AST/Decl.h"
#include "liva/CodeGen/CodeGen.h"
#include "liva/CodeGen/TargetInfo.h"
#include "liva/Driver/SemaCache.h"
#include "liva/IR/IRGen.h"
#include "liva/Lexer/Lexer.h"
#include "liva/Parser/Parser.h"
#include "liva/Plugin/PluginRegistry.h"
#include "liva/Sema/ModuleLoader.h"
#include "liva/Macro/MacroExpander.h"
#include "liva/Sema/Sema.h"
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>

namespace liva {

static void installTraceMacros(Sema &sema) {
    sema.getTypeChecker().getMacroExpander().setTraceCallback(
        [](const MacroTraceEvent &ev) {
            fprintf(stderr, "[macro-trace] ");
            switch (ev.phase) {
            case MacroTraceEvent::Invocation:
                fprintf(stderr, "expanding %s! at %u:%u\n",
                        ev.macroName.c_str(), ev.invokeLoc.line, ev.invokeLoc.column);
                break;
            case MacroTraceEvent::ArmMatched:
                fprintf(stderr, "  arm #%zu matched", ev.armIndex);
                if (!ev.captures.empty()) {
                    fprintf(stderr, " captures:");
                    for (const auto &[k, v] : ev.captures)
                        fprintf(stderr, " $%s=%s", k.c_str(), v.c_str());
                }
                fprintf(stderr, "\n");
                break;
            case MacroTraceEvent::ArmFailed:
                fprintf(stderr, "  arm #%zu failed\n", ev.armIndex);
                break;
            case MacroTraceEvent::NoMatch:
                fprintf(stderr, "  no matching arm\n");
                break;
            case MacroTraceEvent::Completed:
                fprintf(stderr, "  => %s\n", ev.expandedSource.c_str());
                break;
            }
        });
}

static TargetInfo resolveTarget(const std::string &targetTriple) {
    if (!targetTriple.empty())
        return TargetInfo::fromTriple(targetTriple);
    return TargetInfo::getHostTarget();
}

CompilerInstance::CompilerInstance() {
    diag_.setPrintCallback([this](const Diagnostic &d) {
        bool useCol = shouldUseColor(colorMode_);
        DiagnosticsEngine::printToStderr(d, sourceManager_.get(), useCol);
    });
}

void CompilerInstance::addStdlibSearchPath(ModuleLoader &loader) {
    // Apply user-configured search paths
    for (const auto &p : searchPaths_)
        loader.addSearchPath(p);

    // Add stdlib/ relative to executable so file-based modules resolve
    if (!executablePath_.empty()) {
        auto pos = executablePath_.find_last_of("/\\");
        std::string exeDir = (pos != std::string::npos)
            ? executablePath_.substr(0, pos) : ".";
        std::string candidates[] = {
            exeDir + "/stdlib",
            exeDir + "/../stdlib",
        };
        for (const auto &c : candidates) {
            std::ifstream probe(c + "/runtime/runtime.h");
            if (probe.is_open()) {
                loader.addSearchPath(c);
                break;
            }
        }
    }
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

    if (pluginRegistry_ && !pluginRegistry_->runAfterParse(*tu, diag_))
        return false;

    ModuleLoader loader;
    std::string fname(sourceManager_->getFilename());
    auto pos = fname.find_last_of("/\\");
    if (pos != std::string::npos)
        loader.setBasePath(fname.substr(0, pos + 1));
    addStdlibSearchPath(loader);

    Sema sema(diag_, &loader);
    if (traceMacros_)
        installTraceMacros(sema);
    if (!sema.analyze(*tu))
        return false;

    if (pluginRegistry_ && !pluginRegistry_->runAfterSema(*tu, diag_))
        return false;

    return true;
}

#ifdef LIVA_HAS_LLVM
std::optional<CompilerInstance::IRResult> CompilerInstance::compileToIR() {
    auto tu = parseSource();
    if (!tu || diag_.hasErrors())
        return std::nullopt;

    if (pluginRegistry_ && !pluginRegistry_->runAfterParse(*tu, diag_))
        return std::nullopt;

    ModuleLoader loader;
    std::string fname(sourceManager_->getFilename());
    auto pos = fname.find_last_of("/\\");
    if (pos != std::string::npos)
        loader.setBasePath(fname.substr(0, pos + 1));
    addStdlibSearchPath(loader);

    Sema sema(diag_, &loader);
    if (traceMacros_)
        installTraceMacros(sema);
    if (!sema.analyze(*tu))
        return std::nullopt;

    if (pluginRegistry_ && !pluginRegistry_->runAfterSema(*tu, diag_))
        return std::nullopt;

    IRGen irgen(sourceManager_->getFilename().data(), diag_);
    irgen.setModuleLoader(&loader);
    if (!irgen.generate(*tu))
        return std::nullopt;

    return IRResult{irgen.takeContext(), irgen.takeModule()};
}
#endif

bool CompilerInstance::emitIR(const std::string &outputPath) {
    auto tu = parseSource();
    if (!tu || diag_.hasErrors())
        return false;

    if (pluginRegistry_ && !pluginRegistry_->runAfterParse(*tu, diag_))
        return false;

    ModuleLoader loader;
    std::string fname(sourceManager_->getFilename());
    auto pos = fname.find_last_of("/\\");
    if (pos != std::string::npos)
        loader.setBasePath(fname.substr(0, pos + 1));
    addStdlibSearchPath(loader);

    Sema sema(diag_, &loader);
    if (traceMacros_)
        installTraceMacros(sema);
    if (!sema.analyze(*tu))
        return false;

    if (pluginRegistry_ && !pluginRegistry_->runAfterSema(*tu, diag_))
        return false;

    IRGen irgen(sourceManager_->getFilename().data(), diag_);
    irgen.setModuleLoader(&loader);
    irgen.setDebugInfo(debugInfo_);
    irgen.setTargetTriple(targetTriple_);
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

    if (pluginRegistry_ && !pluginRegistry_->runAfterParse(*tu, diag_))
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
    }
    // Always add stdlib search path (even for shared loaders from Driver)
    addStdlibSearchPath(*loader);

    Sema sema(diag_, loader);
    if (traceMacros_)
        installTraceMacros(sema);
    if (!sema.analyze(*tu))
        return false;

    if (pluginRegistry_ && !pluginRegistry_->runAfterSema(*tu, diag_))
        return false;

#ifdef LIVA_HAS_LLVM
    IRGen irgen(sourceManager_->getFilename().data(), diag_);
    irgen.setModuleLoader(loader);
    irgen.setDebugInfo(debugInfo_);
    irgen.setSeparateCompilation(true);
    irgen.setRequireMain(isEntryFile);
    irgen.setTargetTriple(targetTriple_);
    if (!irgen.generate(*tu))
        return false;

    auto *module = irgen.getModule();
    CodeGen codegen(diag_, resolveTarget(targetTriple_));
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

    if (pluginRegistry_ && !pluginRegistry_->runAfterParse(*tu, diag_))
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
    }
    // Always add stdlib search path (even for shared loaders from Driver)
    addStdlibSearchPath(*loader);

    Sema sema(diag_, loader);
    if (traceMacros_)
        installTraceMacros(sema);
    if (!sema.analyze(*tu))
        return result;

    if (pluginRegistry_ && !pluginRegistry_->runAfterSema(*tu, diag_))
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
    irgen.setTargetTriple(targetTriple_);
    if (!irgen.generate(*tu))
        return result;

    auto *module = irgen.getModule();
    CodeGen codegen(diag_, resolveTarget(targetTriple_));
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
    using Clock = std::chrono::high_resolution_clock;
    auto t0 = Clock::now();

    auto tu = parseSource();
    if (!tu || diag_.hasErrors())
        return false;

    if (pluginRegistry_ && !pluginRegistry_->runAfterParse(*tu, diag_))
        return false;

    auto t1 = Clock::now();

    ModuleLoader loader;
    std::string fname(sourceManager_->getFilename());
    auto pos = fname.find_last_of("/\\");
    if (pos != std::string::npos)
        loader.setBasePath(fname.substr(0, pos + 1));
    addStdlibSearchPath(loader);

    Sema sema(diag_, &loader);
    if (traceMacros_)
        installTraceMacros(sema);
    if (!sema.analyze(*tu))
        return false;

    if (pluginRegistry_ && !pluginRegistry_->runAfterSema(*tu, diag_))
        return false;

    auto t2 = Clock::now();

#ifdef LIVA_HAS_LLVM
    IRGen irgen(sourceManager_->getFilename().data(), diag_);
    irgen.setModuleLoader(&loader);
    irgen.setDebugInfo(debugInfo_);
    if (!irgen.generate(*tu))
        return false;

    auto t3 = Clock::now();

    // Use CodeGen API: optimize → emit object/bitcode → link
    auto *module = irgen.getModule();
    CodeGen codegen(diag_, resolveTarget(targetTriple_));
    codegen.optimize(*module, optLevel_, ltoMode_, pgoMode_, pgoProfile_);

    auto t4 = Clock::now();

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

    auto t5 = Clock::now();

    TargetInfo target = resolveTarget(targetTriple_);

    std::vector<std::string> objects = { objPath };
    std::vector<std::string> flags;

    if (!target.isWasm()) {
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
        objects.push_back(runtimeLib);

        // Check if std::ui is imported — link UI runtime + raylib
        bool needsUI = false;
        for (auto &decl : tu->getDeclarations()) {
            if (auto *imp = dynamic_cast<ImportDecl *>(decl.get())) {
                auto path = imp->getPathString();
                if (path == "std::ui" || path == "std") {
                    needsUI = true;
                    break;
                }
            }
        }
        if (needsUI) {
            std::string uiLibCandidates[] = {
                exeDir + "/lib/liva_ui.lib",
                exeDir + "/../lib/liva_ui.lib",
                exeDir + "/lib/libliva_ui.a",
                exeDir + "/../lib/libliva_ui.a",
            };
            for (auto &c : uiLibCandidates) {
                if (fileExists(c)) { objects.push_back(c); break; }
            }
            std::string raylibCandidates[] = {
                exeDir + "/lib/raylib.lib",
                exeDir + "/../lib/raylib.lib",
                exeDir + "/lib/libraylib.a",
                exeDir + "/../lib/libraylib.a",
            };
            for (auto &c : raylibCandidates) {
                if (fileExists(c)) { objects.push_back(c); break; }
            }
#ifdef _WIN32
            flags.push_back("-lgdi32");
            flags.push_back("-lwinmm");
            flags.push_back("-luser32");
            flags.push_back("-lshell32");
#endif
        }

#ifdef _WIN32
        flags.push_back("-lwinhttp");
#endif
    }

    if (debugInfo_)
        flags.push_back("-g");

    bool linkOk = codegen.link(objects, outputPath, flags, ltoMode_, pgoMode_);

    auto t6 = Clock::now();

    // Clean up temp object file (unless kept for caching)
    if (!keepObjectFile_)
        std::remove(objPath.c_str());

    if (!linkOk) {
        std::cerr << "error: linking failed for '" << outputPath << "'\n";
        std::cerr << "  objects:";
        for (const auto &o : objects)
            std::cerr << " " << o;
        std::cerr << "\n";
        return false;
    }

    if (dumpTimings_) {
        auto ms = [](std::chrono::high_resolution_clock::duration d) {
            return std::chrono::duration<double, std::milli>(d).count();
        };
        double parseMs    = ms(t1 - t0);
        double semaMs     = ms(t2 - t1);
        double irgenMs    = ms(t3 - t2);
        double optimizeMs = ms(t4 - t3);
        double emitMs     = ms(t5 - t4);
        double linkMs     = ms(t6 - t5);
        double totalMs    = ms(t6 - t0);

        fprintf(stderr, "\n=== Compilation Timings ===\n");
        fprintf(stderr, "  Parse:       %8.2f ms\n", parseMs);
        fprintf(stderr, "  Sema:        %8.2f ms\n", semaMs);
        fprintf(stderr, "  IRGen:       %8.2f ms\n", irgenMs);
        fprintf(stderr, "  Optimize:    %8.2f ms\n", optimizeMs);
        fprintf(stderr, "  Emit:        %8.2f ms\n", emitMs);
        fprintf(stderr, "  Link:        %8.2f ms\n", linkMs);
        fprintf(stderr, "  ─────────────────────────\n");
        fprintf(stderr, "  Total:       %8.2f ms\n", totalMs);

        const auto &mono = irgen.getMonoStats();
        if (mono.funcCount || mono.methodCount || mono.structCount) {
            fprintf(stderr, "\n  Monomorphization:\n");
            fprintf(stderr, "    Functions:  %u  (cache hits: %u)\n",
                    mono.funcCount, mono.funcCacheHits);
            fprintf(stderr, "    Methods:    %u  (cache hits: %u)\n",
                    mono.methodCount, mono.methodCacheHits);
            fprintf(stderr, "    Structs:    %u  (cache hits: %u)\n",
                    mono.structCount, mono.structCacheHits);
        }
    }

    return true;
#else
    std::cerr << "error: LLVM not available, cannot generate code\n";
    return false;
#endif
}

} // namespace liva
