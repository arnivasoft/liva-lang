#include "liva/Driver/Driver.h"
#include "liva/Common/Version.h"
#include "liva/Driver/BuildCache.h"
#include "liva/Driver/CompilerInstance.h"
#include "liva/Driver/PackageManager.h"
#include "liva/Driver/SemaCache.h"
#include "liva/CodeGen/CodeGen.h"
#include "liva/CodeGen/TargetInfo.h"
#include "liva/Lexer/Lexer.h"
#include "liva/LSP/LSPServer.h"
#include "liva/Parser/Parser.h"
#include "liva/Plugin/PluginRegistry.h"
#include "liva/DAP/DAPServer.h"
#include "liva/REPL/REPL.h"
#ifdef LIVA_HAS_LLVM
#include "liva/JIT/JITEngine.h"
#endif
#include "liva/REPL/LineEditor.h"
#include "liva/Sema/ModuleLoader.h"
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <thread>

namespace liva {

bool Driver::parseArgs(int argc, const char **argv) {
    if (argc > 0)
        executablePath_ = argv[0];

    int startIdx = 1;

    // Check for subcommand
    if (argc > 1 && argv[1][0] != '-') {
        if (std::strcmp(argv[1], "build") == 0) {
            options_.subcommand = Subcommand::Build;
            startIdx = 2;
        } else if (std::strcmp(argv[1], "run") == 0) {
            options_.subcommand = Subcommand::Run;
            startIdx = 2;
        } else if (std::strcmp(argv[1], "init") == 0) {
            options_.subcommand = Subcommand::Init;
            startIdx = 2;
            if (argc > 2 && argv[2][0] != '-') {
                options_.initName = argv[2];
                startIdx = 3;
            }
            return true;
        } else if (std::strcmp(argv[1], "clean") == 0) {
            options_.subcommand = Subcommand::Clean;
            startIdx = 2;
            return true;
        } else if (std::strcmp(argv[1], "lsp") == 0) {
            options_.subcommand = Subcommand::Lsp;
            startIdx = 2;
            return true;
        } else if (std::strcmp(argv[1], "repl") == 0) {
            options_.subcommand = Subcommand::Repl;
            startIdx = 2;
            return true;
        } else if (std::strcmp(argv[1], "dap") == 0) {
            options_.subcommand = Subcommand::Dap;
            startIdx = 2;
            return true;
        } else if (std::strcmp(argv[1], "fmt") == 0) {
            options_.subcommand = Subcommand::Fmt;
            for (int i = 2; i < argc; ++i) {
                if (std::strcmp(argv[i], "--check") == 0)
                    options_.fmtCheck = true;
                else
                    options_.fmtFiles.push_back(argv[i]);
            }
            if (options_.fmtFiles.empty()) {
                std::cerr << "error: livac fmt requires at least one file\n";
                return false;
            }
            return true;
        } else if (std::strcmp(argv[1], "lint") == 0) {
            options_.subcommand = Subcommand::Lint;
            for (int i = 2; i < argc; ++i)
                options_.lintFiles.push_back(argv[i]);
            if (options_.lintFiles.empty()) {
                std::cerr << "error: livac lint requires at least one file\n";
                return false;
            }
            return true;
        } else if (std::strcmp(argv[1], "install") == 0) {
            options_.subcommand = Subcommand::Install;
            startIdx = 2;
            // Next non-flag arg = package name
            if (argc > 2 && argv[2][0] != '-') {
                options_.installPkgName = argv[2];
                startIdx = 3;
            }
            // Optional: next non-flag arg = version
            if (startIdx < argc && argv[startIdx][0] != '-') {
                options_.installPkgVersion = argv[startIdx];
                ++startIdx;
            }
            return true;
        } else if (std::strcmp(argv[1], "remove") == 0) {
            options_.subcommand = Subcommand::Remove;
            if (argc > 2 && argv[2][0] != '-') {
                options_.removePkgName = argv[2];
            } else {
                std::cerr << "error: livac remove requires a package name\n";
                return false;
            }
            return true;
        } else if (std::strcmp(argv[1], "bench") == 0) {
            options_.subcommand = Subcommand::Bench;
            startIdx = 2;
            if (argc > 2 && argv[2][0] != '-') {
                options_.inputFile = argv[2];
                startIdx = 3;
            }
            return true;
        } else if (std::strcmp(argv[1], "test") == 0) {
            options_.subcommand = Subcommand::Test;
            startIdx = 2;
            if (argc > 2 && argv[2][0] != '-') {
                options_.inputFile = argv[2];
                startIdx = 3;
            }
            return true;
        }
    }

    for (int i = startIdx; i < argc; ++i) {
        const char *arg = argv[i];

        if (std::strcmp(arg, "--help") == 0 || std::strcmp(arg, "-h") == 0) {
            options_.showHelp = true;
            return true;
        }
        if (std::strcmp(arg, "--version") == 0 || std::strcmp(arg, "-v") == 0) {
            options_.showVersion = true;
            return true;
        }
        if (std::strcmp(arg, "--dump-tokens") == 0) {
            options_.dumpTokens = true;
            continue;
        }
        if (std::strcmp(arg, "--dump-ast") == 0) {
            options_.dumpAST = true;
            continue;
        }
        if (std::strcmp(arg, "--check-only") == 0) {
            options_.checkOnly = true;
            continue;
        }
        if (std::strcmp(arg, "--emit-ir") == 0) {
            options_.emitIR = true;
            continue;
        }
        if (std::strcmp(arg, "--emit-obj") == 0) {
            options_.emitObj = true;
            continue;
        }
        if (std::strcmp(arg, "-o") == 0) {
            if (i + 1 < argc) {
                options_.outputFile = argv[++i];
            } else {
                std::cerr << "error: -o requires an argument\n";
                return false;
            }
            continue;
        }
        if (std::strcmp(arg, "-O0") == 0) {
            options_.optLevel = 0;
            options_.hasOptLevelOverride = true;
            continue;
        }
        if (std::strcmp(arg, "-O1") == 0) {
            options_.optLevel = 1;
            options_.hasOptLevelOverride = true;
            continue;
        }
        if (std::strcmp(arg, "-O2") == 0) {
            options_.optLevel = 2;
            options_.hasOptLevelOverride = true;
            continue;
        }
        if (std::strcmp(arg, "-O3") == 0) {
            options_.optLevel = 3;
            options_.hasOptLevelOverride = true;
            continue;
        }
        if (std::strcmp(arg, "-g") == 0) {
            options_.debugInfo = true;
            options_.hasDebugOverride = true;
            continue;
        }
        if (std::strcmp(arg, "--debug") == 0) {
            options_.optLevel = 0;
            options_.debugInfo = true;
            options_.hasOptLevelOverride = true;
            options_.hasDebugOverride = true;
            continue;
        }
        if (std::strcmp(arg, "--release") == 0) {
            options_.optLevel = 2;
            options_.debugInfo = false;
            options_.hasOptLevelOverride = true;
            options_.hasDebugOverride = true;
            continue;
        }
        if (std::strcmp(arg, "--lto") == 0 || std::strcmp(arg, "--lto=full") == 0) {
            options_.lto = "full";
            options_.hasLtoOverride = true;
            continue;
        }
        if (std::strcmp(arg, "--lto=thin") == 0) {
            options_.lto = "thin";
            options_.hasLtoOverride = true;
            continue;
        }
        if (std::strcmp(arg, "--lto=none") == 0) {
            options_.lto = "none";
            options_.hasLtoOverride = true;
            continue;
        }
        if (std::strcmp(arg, "--pgo=generate") == 0) {
            options_.pgo = "generate";
            options_.hasPgoOverride = true;
            continue;
        }
        if (std::strcmp(arg, "--pgo=use") == 0) {
            options_.pgo = "use";
            options_.hasPgoOverride = true;
            continue;
        }
        if (std::strcmp(arg, "--pgo=none") == 0) {
            options_.pgo = "none";
            options_.hasPgoOverride = true;
            continue;
        }
        if (std::strncmp(arg, "--pgo-profile=", 14) == 0) {
            options_.pgoProfile = arg + 14;
            continue;
        }

        if (std::strcmp(arg, "-j") == 0) {
            if (i + 1 < argc) {
                long val = strtol(argv[++i], nullptr, 10);
                options_.jobs = (val > 0) ? static_cast<int>(val) : 0;
                options_.hasJobsOverride = true;
            }
            continue;
        }
        // Handle -jN (no space)
        if (std::strncmp(arg, "-j", 2) == 0 && arg[2] != '\0') {
            long val = strtol(arg + 2, nullptr, 10);
            options_.jobs = (val > 0) ? static_cast<int>(val) : 0;
            options_.hasJobsOverride = true;
            continue;
        }

        if (std::strcmp(arg, "--rebuild") == 0) {
            options_.rebuild = true;
            continue;
        }

        if (std::strncmp(arg, "--target=", 9) == 0) {
            options_.targetTriple = arg + 9;
            options_.hasTargetOverride = true;
            continue;
        }
        if (std::strcmp(arg, "--target") == 0) {
            if (i + 1 < argc) {
                options_.targetTriple = argv[++i];
                options_.hasTargetOverride = true;
            } else {
                std::cerr << "error: --target requires an argument\n";
                return false;
            }
            continue;
        }

        // If starts with -, unknown flag
        if (arg[0] == '-') {
            std::cerr << "error: unknown option '" << arg << "'\n";
            return false;
        }

        // Input file
        if (options_.inputFile.empty()) {
            options_.inputFile = arg;
        } else {
            std::cerr << "error: multiple input files are not supported yet\n";
            return false;
        }
    }

    return true;
}

int Driver::execute() {
    if (options_.showHelp) {
        printHelp();
        return 0;
    }

    if (options_.showVersion) {
        printVersion();
        return 0;
    }

    switch (options_.subcommand) {
    case Subcommand::Build:  return executeBuild();
    case Subcommand::Run:    return executeRun();
    case Subcommand::Init:   return executeInit();
    case Subcommand::Clean:   return executeClean();
    case Subcommand::Lsp:     return executeLsp();
    case Subcommand::Repl:    return executeRepl();
    case Subcommand::Install: return executeInstall();
    case Subcommand::Remove:  return executeRemove();
    case Subcommand::Fmt:     return executeFmt();
    case Subcommand::Lint:    return executeLint();
    case Subcommand::Dap:     return executeDap();
    case Subcommand::Bench:   return executeBench();
    case Subcommand::Test:    return executeTest();
    case Subcommand::None:    return executeLegacy();
    }

    return 1;
}

int Driver::executeLegacy() {
    if (options_.inputFile.empty()) {
        printVersion();
        std::cerr << "\nerror: no input file\n";
        printHelp();
        return 1;
    }

    PluginRegistry pluginRegistry = PluginRegistry::createWithBuiltins();

    CompilerInstance compiler;
    compiler.setExecutablePath(executablePath_);
    compiler.setOptLevel(options_.optLevel);
    compiler.setDebugInfo(options_.debugInfo);
    compiler.setTargetTriple(options_.targetTriple);
    compiler.setPluginRegistry(&pluginRegistry);
    if (!compiler.loadFile(options_.inputFile))
        return 1;

    if (options_.dumpTokens) {
        return compiler.dumpTokens() ? 0 : 1;
    }

    if (options_.dumpAST) {
        return compiler.dumpAST() ? 0 : 1;
    }

    if (options_.checkOnly) {
        bool ok = compiler.checkOnly();
        if (ok) {
            std::cout << "Semantic analysis passed.\n";
        }
        return ok ? 0 : 1;
    }

    if (options_.emitIR) {
        return compiler.emitIR(options_.outputFile) ? 0 : 1;
    }

    // Default: full compilation
    std::string output = options_.outputFile;
    if (output.empty()) {
        // Derive output name from input
        output = options_.inputFile;
        auto dot = output.rfind('.');
        if (dot != std::string::npos)
            output = output.substr(0, dot);
        if (options_.targetTriple.find("wasm32") == 0 ||
            options_.targetTriple.find("wasm64") == 0) {
            output += ".wasm";
        }
#ifdef _WIN32
        else {
            output += ".exe";
        }
#endif
    }

    if (!compiler.compile(output))
        return 1;
    std::cout << "Built " << output << "\n";
    return 0;
}

static bool isOutputUpToDate(const std::string &outputPath,
                             const std::vector<std::string> &objPaths) {
    int64_t outMtime = getFileModTime(outputPath);
    if (outMtime == 0)
        return false;
    for (const auto &obj : objPaths) {
        int64_t objMtime = getFileModTime(obj);
        if (objMtime == 0 || objMtime > outMtime)
            return false;
    }
    return true;
}

int Driver::buildProject(bool runAfter) {
    std::string cwd = getCurrentDirectory();
    std::string tomlPath = findProjectFile(cwd);

    if (tomlPath.empty()) {
        std::cerr << "error: no liva.toml found in " << cwd
                  << " or any parent directory\n";
        return 1;
    }

    // Read TOML file
    std::ifstream file(tomlPath);
    if (!file.is_open()) {
        std::cerr << "error: cannot open " << tomlPath << "\n";
        return 1;
    }
    std::stringstream ss;
    ss << file.rdbuf();

    auto parseResult = parseTOML(ss.str());
    if (!parseResult.success) {
        std::cerr << tomlPath << ":" << parseResult.errorLine
                  << ": error: " << parseResult.errorMsg << "\n";
        return 1;
    }

    auto cfg = loadProjectConfig(parseResult.doc);
    cfg.projectRoot = getDirectoryOf(tomlPath);

    PluginRegistry pluginRegistry = PluginRegistry::createWithBuiltins();
    pluginRegistry.configureFromTOML(parseResult.doc);

    // Apply CLI overrides
    if (options_.hasOptLevelOverride)
        cfg.optLevel = options_.optLevel;
    if (options_.hasDebugOverride)
        cfg.debugInfo = options_.debugInfo;
    if (options_.hasLtoOverride)
        cfg.lto = options_.lto;
    if (options_.hasPgoOverride)
        cfg.pgo = options_.pgo;
    if (!options_.pgoProfile.empty())
        cfg.pgoProfile = options_.pgoProfile;
    if (options_.hasJobsOverride)
        cfg.jobs = options_.jobs;
    if (options_.hasTargetOverride)
        cfg.target = options_.targetTriple;

    // Resolve entry file
    std::string entryPath = joinPath(cfg.projectRoot, cfg.entry);
    if (!fileExists(entryPath)) {
        std::cerr << "error: entry file '" << entryPath << "' not found\n";
        return 1;
    }

    // Build search paths
    std::vector<std::string> searchPaths;
    for (const auto &mp : cfg.modulePaths)
        searchPaths.push_back(joinPath(cfg.projectRoot, mp));

    // Resolve dependencies (local + remote registry)
    if (!cfg.dependencies.empty()) {
        PackageManager pkgMgr(cfg.projectRoot, cfg.registryUrl);
        auto resolution = pkgMgr.resolveAndInstall(cfg.dependencies);
        if (!resolution.success) {
            std::cerr << "error: " << resolution.errorMsg << "\n";
            return 1;
        }
        // Build lock file entries with checksums
        std::vector<LockFileEntry> lockEntries;
        for (const auto &pkg : resolution.packages) {
            LockFileEntry le;
            le.name = pkg.name;
            le.version = pkg.version.toString();
            // Check if package has a checksum file
            std::string csPath = joinPath(pkg.path, ".checksum");
            std::ifstream csFile(csPath);
            if (csFile.is_open()) {
                std::string cs;
                std::getline(csFile, cs);
                le.checksum = cs;
            }
            lockEntries.push_back(le);
        }
        for (const auto &pkg : resolution.packages)
            searchPaths.push_back(pkg.srcPath);
        // Write lock file
        std::string lockPath = joinPath(cfg.projectRoot, "liva.lock");
        std::string lockContent = generateLockFile(resolution.packages, lockEntries);
        std::ofstream lockFile(lockPath);
        if (lockFile.is_open())
            lockFile << lockContent;
    }

    // Determine output directory: build/release or build/debug
    std::string buildDir;
    if (cfg.optLevel >= 2 && !cfg.debugInfo)
        buildDir = joinPath(cfg.projectRoot, joinPath("build", "release"));
    else
        buildDir = joinPath(cfg.projectRoot, joinPath("build", "debug"));

    std::string output = options_.outputFile;
    if (output.empty()) {
        createDirectories(buildDir);
        output = joinPath(buildDir, cfg.name);
        if (cfg.target.find("wasm32") == 0 || cfg.target.find("wasm64") == 0) {
            output += ".wasm";
        }
#ifdef _WIN32
        else {
            output += ".exe";
        }
#endif
    }

    // Handle emitIR and checkOnly modes (single-file, no separate compilation)
    if (options_.emitIR || options_.checkOnly) {
        CompilerInstance compiler;
        compiler.setExecutablePath(executablePath_);
        compiler.setOptLevel(cfg.optLevel);
        compiler.setDebugInfo(cfg.debugInfo);
        compiler.setLtoMode(cfg.lto);
        compiler.setPgoMode(cfg.pgo);
        compiler.setPgoProfile(cfg.pgoProfile);
        compiler.setSearchPaths(searchPaths);
        compiler.setTargetTriple(cfg.target);
        compiler.setPluginRegistry(&pluginRegistry);

        if (!compiler.loadFile(entryPath))
            return 1;

        if (options_.emitIR)
            return compiler.emitIR(options_.outputFile) ? 0 : 1;

        bool ok = compiler.checkOnly();
        if (ok)
            std::cout << "Semantic analysis passed.\n";
        return ok ? 0 : 1;
    }

    // Resolve PGO profile path for 'use' mode
    if (cfg.pgo == "use" && cfg.pgoProfile.empty())
        cfg.pgoProfile = joinPath(cfg.projectRoot, "default.profdata");

    // Scan all source file dependencies
    BuildCache cache(cfg.projectRoot);
    auto depFiles = cache.scanDependencies(entryPath, searchPaths);

    // Prune stale cache entries for deleted source files
    cache.pruneStaleEntries(depFiles);

    // Per-file incremental compilation
    auto fileStatuses = cache.checkFilesCache(depFiles, cfg.optLevel, cfg.debugInfo);

    // --rebuild: force recompile all files
    if (options_.rebuild) {
        for (auto &s : fileStatuses) {
            s.needsRecompile = true;
            s.cachedObjPath.clear();
        }
    }

    // Dependency-aware sema cache check
    SemaCache semaCache(cfg.projectRoot);
    auto semaStatuses = semaCache.check(depFiles, fileStatuses);

    // --rebuild: force re-sema all files
    if (options_.rebuild) {
        for (auto &s : semaStatuses)
            s.needsResema = true;
    }

    // Check if all files are cached (both build cache and sema cache)
    bool allCached = true;
    for (size_t i = 0; i < depFiles.size(); ++i) {
        if (fileStatuses[i].needsRecompile || semaStatuses[i].needsResema) {
            allCached = false;
            break;
        }
    }

    if (allCached) {
        // All cached — gather .o paths and link
        std::vector<std::string> objPaths;
        for (const auto &fs : fileStatuses)
            objPaths.push_back(fs.cachedObjPath);

        // Check if output binary is also up-to-date
        if (isOutputUpToDate(output, objPaths)) {
            std::cout << "Built " << cfg.name << " -> " << output << " (up-to-date)\n";
            if (runAfter) {
                std::cout << "Running " << output << "...\n";
                return std::system(output.c_str());
            }
            return 0;
        }

        int linkResult = linkObjects(objPaths, output, cfg.debugInfo, cfg.lto, cfg.pgo);
        if (linkResult == 0) {
            std::cout << "Built " << cfg.name << " -> " << output << " (cached)\n";
            if (runAfter) {
                std::cout << "Running " << output << "...\n";
                return std::system(output.c_str());
            }
            return 0;
        }
        // Link failed — fall through to recompile all
    }

    // Collect indices of files needing recompilation
    std::vector<std::string> allObjPaths(depFiles.size());
    std::vector<size_t> toCompile;
    for (size_t i = 0; i < depFiles.size(); ++i) {
        if (!fileStatuses[i].needsRecompile && !semaStatuses[i].needsResema)
            allObjPaths[i] = fileStatuses[i].cachedObjPath;
        else
            toCompile.push_back(i);
    }

    // Compute base path for module loaders
    std::string loaderBasePath;
    auto entryPos = entryPath.find_last_of("/\\");
    if (entryPos != std::string::npos)
        loaderBasePath = entryPath.substr(0, entryPos + 1);

    createDirectories(buildDir);

    bool compileFailed = false;

    // Determine thread count
    unsigned maxJobs = (cfg.jobs > 0) ? static_cast<unsigned>(cfg.jobs)
                       : std::thread::hardware_concurrency();
    if (maxJobs == 0) maxJobs = 1;
    unsigned numThreads = std::min(maxJobs, static_cast<unsigned>(toCompile.size()));
    if (numThreads == 0) numThreads = 1;

    // Progress output
    {
        size_t cachedCount = depFiles.size() - toCompile.size();
        if (numThreads <= 1 || toCompile.size() <= 1) {
            std::cout << "Compiling " << toCompile.size() << "/" << depFiles.size()
                      << " files (" << cachedCount << " cached)\n";
        } else {
            std::cout << "Compiling " << toCompile.size() << "/" << depFiles.size()
                      << " files (" << cachedCount << " cached, "
                      << numThreads << " threads)\n";
        }
    }

    if (numThreads <= 1 || toCompile.size() <= 1) {
        // Sequential path — avoids thread overhead for single files
        for (size_t wi = 0; wi < toCompile.size(); ++wi) {
            size_t fileIdx = toCompile[wi];
            const auto &sourcePath = depFiles[fileIdx];
            const auto &status = fileStatuses[fileIdx];
            bool isEntry = (sourcePath == entryPath);

            std::cout << "  [" << (wi + 1) << "/" << toCompile.size() << "] "
                      << sourcePath << "\n";

            ModuleLoader threadLoader;
            threadLoader.setBasePath(loaderBasePath);
            for (const auto &sp : searchPaths)
                threadLoader.addSearchPath(sp);

            CompilerInstance compiler;
            compiler.setExecutablePath(executablePath_);
            compiler.setOptLevel(cfg.optLevel);
            compiler.setDebugInfo(cfg.debugInfo);
            compiler.setLtoMode(cfg.lto);
            compiler.setPgoMode(cfg.pgo);
            compiler.setPgoProfile(cfg.pgoProfile);
            compiler.setSearchPaths(searchPaths);
            compiler.setTargetTriple(cfg.target);
            compiler.setPluginRegistry(&pluginRegistry);

            if (!compiler.loadFile(sourcePath)) {
                compileFailed = true;
                break;
            }

            std::string objName = cache.objectPathForSource(sourcePath);
            std::string objPath = joinPath(buildDir, objName);

            auto compileResult = compiler.compileToObjectWithMeta(objPath, isEntry, &threadLoader);
            if (!compileResult.success) {
                compileFailed = true;
                break;
            }

            cache.storeFileObject(sourcePath, status.currentHash, objPath,
                                  cfg.optLevel, cfg.debugInfo);
            semaCache.store(sourcePath, status.currentHash,
                            compileResult.interfaceHash, compileResult.imports);
            allObjPaths[fileIdx] = objPath;
        }
    } else {
        // Parallel compilation
        std::mutex resultMutex;
        std::atomic<bool> failed{false};
        std::atomic<size_t> nextWork{0};

        auto worker = [&]() {
            // Per-thread ModuleLoader (not thread-safe, so each thread gets its own)
            ModuleLoader threadLoader;
            threadLoader.setBasePath(loaderBasePath);
            for (const auto &sp : searchPaths)
                threadLoader.addSearchPath(sp);

            while (!failed.load()) {
                size_t workIdx = nextWork.fetch_add(1);
                if (workIdx >= toCompile.size()) break;
                size_t fileIdx = toCompile[workIdx];

                const auto &sourcePath = depFiles[fileIdx];
                const auto &status = fileStatuses[fileIdx];
                bool isEntry = (sourcePath == entryPath);

                CompilerInstance compiler;
                compiler.setExecutablePath(executablePath_);
                compiler.setOptLevel(cfg.optLevel);
                compiler.setDebugInfo(cfg.debugInfo);
                compiler.setLtoMode(cfg.lto);
                compiler.setPgoMode(cfg.pgo);
                compiler.setPgoProfile(cfg.pgoProfile);
                compiler.setSearchPaths(searchPaths);
                compiler.setTargetTriple(cfg.target);
                compiler.setPluginRegistry(&pluginRegistry);

                if (!compiler.loadFile(sourcePath)) {
                    failed.store(true);
                    return;
                }

                std::string objName = cache.objectPathForSource(sourcePath);
                std::string objPath = joinPath(buildDir, objName);

                auto compileResult = compiler.compileToObjectWithMeta(
                    objPath, isEntry, &threadLoader);
                if (!compileResult.success) {
                    failed.store(true);
                    return;
                }

                // Lock for shared state updates
                std::lock_guard<std::mutex> lock(resultMutex);
                cache.storeFileObject(sourcePath, status.currentHash, objPath,
                                      cfg.optLevel, cfg.debugInfo);
                semaCache.store(sourcePath, status.currentHash,
                                compileResult.interfaceHash,
                                compileResult.imports);
                allObjPaths[fileIdx] = objPath;
            }
        };

        std::vector<std::thread> threads;
        for (unsigned t = 0; t < numThreads; ++t)
            threads.emplace_back(worker);
        for (auto &t : threads)
            t.join();

        compileFailed = failed.load();
    }

    // Save sema manifest
    semaCache.saveManifest();

    if (compileFailed)
        return 1;

    // Link all .o files → executable
    int linkResult = linkObjects(allObjPaths, output, cfg.debugInfo, cfg.lto, cfg.pgo);
    if (linkResult != 0)
        return 1;

    // Clean up temp .o files in build dir
    for (const auto &objPath : allObjPaths) {
        // Don't remove cached objects (they're in .liva-cache/)
        if (objPath.find(".liva-cache") == std::string::npos)
            std::remove(objPath.c_str());
    }

    std::cout << "Built " << cfg.name << " -> " << output << "\n";

    if (runAfter) {
        std::cout << "Running " << output << "...\n";
        return std::system(output.c_str());
    }

    return 0;
}

int Driver::executeLsp() {
    LSPServer server;
    return server.run();
}

int Driver::executeDap() {
    DAPServer server;
    return server.run();
}

int Driver::executeBench() {
    // 1. Find bench files: bench/ directory or specific file
    std::vector<std::string> benchFiles;
    if (!options_.inputFile.empty()) {
        benchFiles.push_back(options_.inputFile);
    } else {
        namespace fs = std::filesystem;
        fs::path benchDir = "bench";
        if (fs::exists(benchDir) && fs::is_directory(benchDir)) {
            for (auto &entry : fs::directory_iterator(benchDir)) {
                if (entry.path().extension() == ".liva")
                    benchFiles.push_back(entry.path().string());
            }
            std::sort(benchFiles.begin(), benchFiles.end());
        }
    }
    if (benchFiles.empty()) {
        std::cerr << "error: no benchmark files found (use bench/ directory or specify file)\n";
        return 1;
    }

    // 2. For each bench file: compile → run
    std::cout << "Running " << benchFiles.size() << " benchmark file(s)...\n\n";
    int failures = 0;
    for (auto &file : benchFiles) {
        std::cout << "--- " << file << " ---\n";

        PluginRegistry pluginRegistry = PluginRegistry::createWithBuiltins();

        CompilerInstance compiler;
        compiler.setExecutablePath(executablePath_);
        compiler.setOptLevel(2);  // benchmarks always optimized
        compiler.setPluginRegistry(&pluginRegistry);
        if (!compiler.loadFile(file)) {
            ++failures;
            continue;
        }

        std::string exePath = file + ".bench.exe";
        if (!compiler.compile(exePath)) {
            ++failures;
            continue;
        }

        int ret = std::system(exePath.c_str());
        std::remove(exePath.c_str());
        if (ret != 0) ++failures;
    }
    return failures > 0 ? 1 : 0;
}

int Driver::executeTest() {
    // 1. Find test files: test/ directory or specific file
    std::vector<std::string> testFiles;
    if (!options_.inputFile.empty()) {
        testFiles.push_back(options_.inputFile);
    } else {
        namespace fs = std::filesystem;
        fs::path testDir = "test";
        if (fs::exists(testDir) && fs::is_directory(testDir)) {
            for (auto &entry : fs::directory_iterator(testDir)) {
                if (entry.path().extension() == ".liva")
                    testFiles.push_back(entry.path().string());
            }
            std::sort(testFiles.begin(), testFiles.end());
        }
    }
    if (testFiles.empty()) {
        std::cerr << "error: no test files found (use test/ directory or specify file)\n";
        return 1;
    }

    // 2. For each test file: compile → run
    std::cout << "Running " << testFiles.size() << " test file(s)...\n\n";
    int failures = 0;
    for (auto &file : testFiles) {
        std::cout << "--- " << file << " ---\n";

        PluginRegistry pluginRegistry = PluginRegistry::createWithBuiltins();

        CompilerInstance compiler;
        compiler.setExecutablePath(executablePath_);
        compiler.setOptLevel(0);
        compiler.setPluginRegistry(&pluginRegistry);
        if (!compiler.loadFile(file)) {
            ++failures;
            continue;
        }

        std::string exePath = file + ".test.exe";
        if (!compiler.compile(exePath)) {
            ++failures;
            continue;
        }

        int ret = std::system(exePath.c_str());
        std::remove(exePath.c_str());
        if (ret != 0) ++failures;
    }
    return failures > 0 ? 1 : 0;
}

int Driver::executeRepl() {
    std::cout << "Liva REPL (type :help for help, :quit to exit)\n";

#ifdef LIVA_HAS_LLVM
    auto jit = JITEngine::create();
    if (!jit)
        std::cerr << "  (JIT unavailable, using compile-to-disk fallback)\n";
#endif

    REPLSession session;
    LineEditor editor;
    editor.setCompletionCallback(
        [&](const std::string &buf, size_t pos) {
            return session.getCompletions(buf, pos);
        });

    std::string line;
    while (editor.readLine(session.getPrompt(), line)) {
        REPLResult result = session.processLine(line);

        switch (result.kind) {
        case REPLResult::Empty:
        case REPLResult::Incomplete:
            break;

        case REPLResult::Quit:
            std::cout << result.output << "\n";
            return 0;

        case REPLResult::Help:
        case REPLResult::Reset:
        case REPLResult::ShowDecls:
        case REPLResult::Declaration:
            std::cout << result.output << "\n";
            break;

        case REPLResult::CommandError:
            std::cerr << result.output << "\n";
            break;

        case REPLResult::Statement:
        case REPLResult::Expression:
            if (result.needsExecution) {
#ifdef LIVA_HAS_LLVM
                CompilerInstance compiler;
                compiler.setExecutablePath(executablePath_);
                compiler.setSource("_repl_tmp.liva", result.generatedCode);

                if (jit) {
                    // JIT path — fast in-process execution
                    auto irResult = compiler.compileToIR();
                    if (irResult) {
                        std::string errMsg;
                        int exitCode = jit->evaluate(
                            std::move(irResult->context),
                            std::move(irResult->module),
                            errMsg);
                        if (exitCode < 0)
                            std::cerr << "  JIT error: " << errMsg << "\n";
                        else if (exitCode != 0)
                            std::cerr << "  (exited with code " << exitCode << ")\n";
                    }
                } else {
                    // AOT fallback — compile to disk
                    std::string tmpExe = "_repl_tmp";
#ifdef _WIN32
                    tmpExe += ".exe";
#endif
                    if (compiler.compile(tmpExe)) {
                        int exitCode = std::system(tmpExe.c_str());
                        std::remove(tmpExe.c_str());
                        if (exitCode != 0)
                            std::cerr << "  (process exited with code " << exitCode << ")\n";
                    }
                }
#else
                std::cout << "Semantic analysis passed.\n";
#endif
            }
            break;

        case REPLResult::Error:
            std::cerr << result.output;
            break;
        }
    }

    // EOF reached
    std::cout << "\nGoodbye!\n";
    return 0;
}

int Driver::executeBuild() {
    return buildProject(false);
}

int Driver::executeRun() {
    return buildProject(true);
}

int Driver::executeInit() {
    std::string name = options_.initName;
    if (name.empty()) {
        // Use current directory name
        std::string cwd = getCurrentDirectory();
        auto pos = cwd.find_last_of("/\\");
        name = (pos != std::string::npos) ? cwd.substr(pos + 1) : cwd;
    }

    std::string projectDir;
    if (!options_.initName.empty()) {
        projectDir = joinPath(getCurrentDirectory(), name);
    } else {
        projectDir = getCurrentDirectory();
    }

    std::string srcDir = joinPath(projectDir, "src");

    // Create directories
    if (!options_.initName.empty()) {
        if (!createDirectories(projectDir)) {
            std::cerr << "error: cannot create directory '" << projectDir << "'\n";
            return 1;
        }
    }
    createDirectories(srcDir);

    // Write liva.toml
    {
        std::string tomlPath = joinPath(projectDir, "liva.toml");
        std::ofstream f(tomlPath);
        if (!f.is_open()) {
            std::cerr << "error: cannot create " << tomlPath << "\n";
            return 1;
        }
        f << "[project]\n"
          << "name = \"" << name << "\"\n"
          << "version = \"0.1.0\"\n"
          << "entry = \"src/main.liva\"\n"
          << "\n"
          << "[build]\n"
          << "opt-level = 0\n"
          << "debug-info = false\n"
          << "# lto = \"none\"  # \"none\", \"thin\", or \"full\"\n"
          << "# pgo = \"none\"  # \"none\", \"generate\", or \"use\"\n"
          << "\n"
          << "# [dependencies]\n"
          << "# mylib = \"1.0.0\"\n";
    }

    // Write src/main.liva
    {
        std::string mainPath = joinPath(srcDir, "main.liva");
        std::ofstream f(mainPath);
        if (!f.is_open()) {
            std::cerr << "error: cannot create " << mainPath << "\n";
            return 1;
        }
        f << "func main() {\n"
          << "    println(\"Hello, " << name << "!\")\n"
          << "}\n";
    }

    // Write .gitignore
    {
        std::string gitignorePath = joinPath(projectDir, ".gitignore");
        std::ofstream f(gitignorePath);
        if (f.is_open()) {
            f << "build/\n"
              << "*.exe\n"
              << "*.o\n"
              << "*.bc\n"
              << "*.ll\n"
              << "*.profraw\n"
              << "*.profdata\n";
        }
    }

    std::cout << "Created project '" << name << "' in " << projectDir << "\n";
    return 0;
}

int Driver::executeInstall() {
    if (options_.installPkgName.empty()) {
        std::cerr << "error: missing package name\n"
                  << "usage: livac install <package> [version]\n";
        return 1;
    }

    std::string cwd = getCurrentDirectory();
    std::string tomlPath = findProjectFile(cwd);

    if (tomlPath.empty()) {
        std::cerr << "error: no liva.toml found — run 'livac init' first\n";
        return 1;
    }

    // Read TOML to get registryUrl
    std::ifstream file(tomlPath);
    if (!file.is_open()) {
        std::cerr << "error: cannot open " << tomlPath << "\n";
        return 1;
    }
    std::stringstream ss;
    ss << file.rdbuf();
    file.close();

    auto parseResult = parseTOML(ss.str());
    if (!parseResult.success) {
        std::cerr << tomlPath << ":" << parseResult.errorLine
                  << ": error: " << parseResult.errorMsg << "\n";
        return 1;
    }

    auto cfg = loadProjectConfig(parseResult.doc);
    cfg.projectRoot = getDirectoryOf(tomlPath);

    // Create PackageManager and install
    PackageManager pkgMgr(cfg.projectRoot, cfg.registryUrl);
    auto result = pkgMgr.installSingle(options_.installPkgName,
                                        options_.installPkgVersion);

    if (!result.success) {
        std::cerr << "error: " << result.errorMsg << "\n";
        return 1;
    }

    // Determine version constraint string for toml
    std::string versionForToml = result.version.toString();
    if (!options_.installPkgVersion.empty())
        versionForToml = options_.installPkgVersion;

    // Add to liva.toml
    if (!addDependencyToToml(tomlPath, result.name, versionForToml)) {
        std::cerr << "warning: installed package but could not update "
                  << tomlPath << "\n";
    }

    // Update lock file
    {
        // Re-read TOML to get all deps (including the newly added one)
        std::ifstream f2(tomlPath);
        if (f2.is_open()) {
            std::stringstream ss2;
            ss2 << f2.rdbuf();
            auto pr2 = parseTOML(ss2.str());
            if (pr2.success) {
                auto allDeps = parseDependencies(pr2.doc);
                // Resolve all packages locally for lock file
                std::string packagesDir = joinPath(cfg.projectRoot, "packages");
                auto resolution = resolvePackages(allDeps, packagesDir);
                if (resolution.success) {
                    std::vector<LockFileEntry> lockEntries;
                    for (const auto &pkg : resolution.packages) {
                        LockFileEntry le;
                        le.name = pkg.name;
                        le.version = pkg.version.toString();
                        std::string csPath = joinPath(pkg.path, ".checksum");
                        std::ifstream csFile(csPath);
                        if (csFile.is_open()) {
                            std::string cs;
                            std::getline(csFile, cs);
                            le.checksum = cs;
                        }
                        lockEntries.push_back(le);
                    }
                    std::string lockPath = joinPath(cfg.projectRoot, "liva.lock");
                    std::string lockContent = generateLockFile(
                        resolution.packages, lockEntries);
                    std::ofstream lockFile(lockPath);
                    if (lockFile.is_open())
                        lockFile << lockContent;
                }
            }
        }
    }

    std::cout << "Installed " << result.name << " v"
              << result.version.toString() << "\n";
    return 0;
}

int Driver::executeRemove() {
    if (options_.removePkgName.empty()) {
        std::cerr << "error: missing package name\n"
                  << "usage: livac remove <package>\n";
        return 1;
    }

    std::string cwd = getCurrentDirectory();
    std::string tomlPath = findProjectFile(cwd);

    if (tomlPath.empty()) {
        std::cerr << "error: no liva.toml found — run 'livac init' first\n";
        return 1;
    }

    std::string projectRoot = getDirectoryOf(tomlPath);

    // Remove from liva.toml
    if (!removeDependencyFromToml(tomlPath, options_.removePkgName)) {
        std::cerr << "error: dependency '" << options_.removePkgName
                  << "' not found in " << tomlPath << "\n";
        return 1;
    }

    // Remove packages/{name} directory
    std::string pkgDir = joinPath(projectRoot, "packages/" + options_.removePkgName);
    if (fileExists(pkgDir))
        removeDirectoryRecursive(pkgDir);

    // Update lock file
    {
        std::ifstream f(tomlPath);
        if (f.is_open()) {
            std::stringstream ss;
            ss << f.rdbuf();
            auto pr = parseTOML(ss.str());
            if (pr.success) {
                auto allDeps = parseDependencies(pr.doc);
                std::string packagesDir = joinPath(projectRoot, "packages");
                auto resolution = resolvePackages(allDeps, packagesDir);
                std::string lockPath = joinPath(projectRoot, "liva.lock");
                if (resolution.success && !resolution.packages.empty()) {
                    std::vector<LockFileEntry> lockEntries;
                    for (const auto &pkg : resolution.packages) {
                        LockFileEntry le;
                        le.name = pkg.name;
                        le.version = pkg.version.toString();
                        lockEntries.push_back(le);
                    }
                    std::string lockContent = generateLockFile(
                        resolution.packages, lockEntries);
                    std::ofstream lockFile(lockPath);
                    if (lockFile.is_open())
                        lockFile << lockContent;
                } else {
                    // No more dependencies — remove lock file
                    std::remove(lockPath.c_str());
                }
            }
        }
    }

    std::cout << "Removed " << options_.removePkgName << "\n";
    return 0;
}

int Driver::executeFmt() {
    int exitCode = 0;
    for (const auto &filePath : options_.fmtFiles) {
        std::ifstream ifs(filePath);
        if (!ifs.is_open()) {
            std::cerr << "error: cannot open '" << filePath << "'\n";
            exitCode = 1;
            continue;
        }
        std::ostringstream ss;
        ss << ifs.rdbuf();
        ifs.close();
        std::string original = ss.str();

        std::string formatted = formatLivaSource(original);

        if (options_.fmtCheck) {
            if (formatted != original) {
                std::cerr << filePath << ": not formatted\n";
                exitCode = 1;
            }
        } else {
            if (formatted != original) {
                std::ofstream ofs(filePath);
                if (!ofs.is_open()) {
                    std::cerr << "error: cannot write '" << filePath << "'\n";
                    exitCode = 1;
                    continue;
                }
                ofs << formatted;
            }
        }
    }
    return exitCode;
}

int Driver::executeLint() {
    int totalWarnings = 0;
    for (const auto &filePath : options_.lintFiles) {
        std::ifstream ifs(filePath);
        if (!ifs.is_open()) {
            std::cerr << "error: cannot open '" << filePath << "'\n";
            totalWarnings = 1;
            continue;
        }
        std::ostringstream ss;
        ss << ifs.rdbuf();
        ifs.close();
        std::string source = ss.str();

        SourceManager sm(filePath, source);
        DiagnosticsEngine diag(&sm);
        diag.setPrintCallback([&sm](const Diagnostic &d) {
            DiagnosticsEngine::printToStderr(d, &sm);
        });

        Lexer lexer(sm, diag);
        Parser parser(lexer, diag);
        auto tu = parser.parseTranslationUnit();

        if (diag.hasErrors() || !tu) {
            totalWarnings += diag.getErrorCount();
            continue;
        }

        totalWarnings += lintLivaSource(*tu, diag);
    }
    return totalWarnings > 0 ? 1 : 0;
}

int Driver::executeClean() {
    std::string cwd = getCurrentDirectory();
    std::string tomlPath = findProjectFile(cwd);

    if (tomlPath.empty()) {
        std::cerr << "error: no liva.toml found in " << cwd
                  << " or any parent directory\n";
        return 1;
    }

    std::string projectRoot = getDirectoryOf(tomlPath);
    std::string buildDir = joinPath(projectRoot, "build");

    // Try to remove — will fail silently if dir doesn't exist
    removeDirectoryRecursive(buildDir);

    // Also clean build cache
    BuildCache cache(projectRoot);
    cache.clean();

    std::cout << "Cleaned build artifacts.\n";
    return 0;
}

int Driver::linkCachedObject(const std::string &objPath,
                             const std::string &outputPath, bool debugInfo,
                             const std::string &ltoMode,
                             const std::string &pgoMode) {
#ifdef LIVA_HAS_LLVM
    // Find runtime library relative to livac executable
    auto dirOfPath = [](const std::string &path) -> std::string {
        auto pos = path.find_last_of("/\\");
        return (pos != std::string::npos) ? path.substr(0, pos) : ".";
    };

    std::string exeDir = dirOfPath(executablePath_);
    std::string runtimeLib;
    std::string candidates[] = {
        exeDir + "/lib/liva_runtime.lib",
        exeDir + "/../lib/liva_runtime.lib",
        exeDir + "/lib/libliva_runtime.a",
        exeDir + "/../lib/libliva_runtime.a",
    };
    for (auto &c : candidates) {
        std::ifstream f(c);
        if (f.is_open()) { runtimeLib = c; break; }
    }
    if (runtimeLib.empty()) {
        std::cerr << "error: cannot find runtime library for cached link\n";
        return 1;
    }

    DiagnosticsEngine diag;
    TargetInfo linkTarget = options_.hasTargetOverride
        ? TargetInfo::fromTriple(options_.targetTriple)
        : TargetInfo::getHostTarget();
    CodeGen codegen(diag, linkTarget);

    std::vector<std::string> objects = { objPath, runtimeLib };
    std::vector<std::string> flags;
#ifdef _WIN32
    flags.push_back("-lwinhttp");
#endif
    if (debugInfo)
        flags.push_back("-g");

    if (!codegen.link(objects, outputPath, flags, ltoMode, pgoMode)) {
        std::cerr << "error: linking cached object failed\n";
        return 1;
    }

    return 0;
#else
    (void)objPath;
    (void)outputPath;
    (void)debugInfo;
    (void)ltoMode;
    (void)pgoMode;
    std::cerr << "error: LLVM not available, cannot link\n";
    return 1;
#endif
}

int Driver::linkObjects(const std::vector<std::string> &objPaths,
                         const std::string &outputPath, bool debugInfo,
                         const std::string &ltoMode,
                         const std::string &pgoMode) {
#ifdef LIVA_HAS_LLVM
    DiagnosticsEngine diag;
    TargetInfo linkTarget = options_.hasTargetOverride
        ? TargetInfo::fromTriple(options_.targetTriple)
        : TargetInfo::getHostTarget();
    CodeGen codegen(diag, linkTarget);

    std::vector<std::string> objects = objPaths;
    std::vector<std::string> flags;

    if (!linkTarget.isWasm()) {
        // Find runtime library relative to livac executable
        auto dirOfPath = [](const std::string &path) -> std::string {
            auto pos = path.find_last_of("/\\");
            return (pos != std::string::npos) ? path.substr(0, pos) : ".";
        };

        std::string exeDir = dirOfPath(executablePath_);
        std::string runtimeLib;
        std::string candidates[] = {
            exeDir + "/lib/liva_runtime.lib",
            exeDir + "/../lib/liva_runtime.lib",
            exeDir + "/lib/libliva_runtime.a",
            exeDir + "/../lib/libliva_runtime.a",
        };
        for (auto &c : candidates) {
            std::ifstream f(c);
            if (f.is_open()) { runtimeLib = c; break; }
        }
        if (runtimeLib.empty()) {
            std::cerr << "error: cannot find runtime library for linking\n";
            return 1;
        }
        objects.push_back(runtimeLib);
#ifdef _WIN32
        flags.push_back("-lwinhttp");
#endif
    }

    if (debugInfo)
        flags.push_back("-g");

    if (!codegen.link(objects, outputPath, flags, ltoMode, pgoMode)) {
        std::cerr << "error: linking failed for '" << outputPath << "'\n";
        return 1;
    }

    return 0;
#else
    (void)objPaths;
    (void)outputPath;
    (void)debugInfo;
    (void)ltoMode;
    (void)pgoMode;
    std::cerr << "error: LLVM not available, cannot link\n";
    return 1;
#endif
}

void Driver::printVersion() {
    std::cout << "Liva Compiler v" << LIVA_VERSION_STRING << "\n";
}

void Driver::printHelp() {
    std::cout << "Usage: livac [options] <input.liva>\n"
              << "       livac build [--release|--debug] [-o <file>]\n"
              << "       livac run [--release|--debug]\n"
              << "       livac init [name]\n"
              << "       livac install <pkg> [version]\n"
              << "       livac clean\n"
              << "       livac fmt [--check] <files...>\n"
              << "       livac lint <files...>\n"
              << "       livac lsp\n"
              << "       livac bench [file]\n"
              << "       livac test [file]\n"
              << "       livac dap\n"
              << "\n"
              << "Subcommands:\n"
              << "  build               Build project using liva.toml\n"
              << "  run                 Build and run project\n"
              << "  init [name]         Create a new Liva project\n"
              << "  install <pkg> [ver] Install a package from registry\n"
              << "  remove <pkg>        Remove a package dependency\n"
              << "  clean               Remove build artifacts\n"
              << "  fmt [--check]       Format Liva source files\n"
              << "  lint                Lint Liva source files\n"
              << "  lsp                 Start Language Server Protocol server\n"
              << "  dap                 Start Debug Adapter Protocol server\n"
              << "  bench [file]        Run benchmarks (bench/ directory or file)\n"
              << "  test [file]         Run tests (test/ directory or file)\n"
              << "  repl                Start interactive REPL\n"
              << "\n"
              << "Options:\n"
              << "  -h, --help          Show this help message\n"
              << "  -v, --version       Show version information\n"
              << "  -o <file>           Output file path\n"
              << "  --dump-tokens       Dump lexer tokens and exit\n"
              << "  --dump-ast          Dump parsed AST and exit\n"
              << "  --check-only        Run semantic analysis only\n"
              << "  --emit-ir           Emit LLVM IR\n"
              << "  --emit-obj          Emit object file\n"
              << "  -O0/-O1/-O2/-O3     Optimization level\n"
              << "  -g                  Generate debug information\n"
              << "  --debug             Debug build (O0, debug info)\n"
              << "  --release           Release build (O2, no debug info)\n"
              << "  --lto[=thin|full]   Enable link-time optimization\n"
              << "  --pgo=generate      Build with PGO instrumentation\n"
              << "  --pgo=use           Build with PGO profile data\n"
              << "  --pgo-profile=PATH  Profile data path (default: default.profdata)\n"
              << "  -j N                Number of parallel compilation jobs (default: auto)\n"
              << "  --rebuild           Force recompile all files (bypass cache)\n"
              << "  --target <triple>   Cross-compile for target triple\n";
}

} // namespace liva
