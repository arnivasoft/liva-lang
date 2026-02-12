#include "liva/Driver/Driver.h"
#include "liva/Common/Version.h"
#include "liva/Driver/CompilerInstance.h"
#include "liva/LSP/LSPServer.h"
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>

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
        } else if (std::strcmp(argv[1], "lsp") == 0) {
            options_.subcommand = Subcommand::Lsp;
            startIdx = 2;
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
    case Subcommand::Build: return executeBuild();
    case Subcommand::Run:   return executeRun();
    case Subcommand::Init:  return executeInit();
    case Subcommand::Lsp:   return executeLsp();
    case Subcommand::None:  return executeLegacy();
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

    CompilerInstance compiler;
    compiler.setExecutablePath(executablePath_);
    compiler.setOptLevel(options_.optLevel);
    compiler.setDebugInfo(options_.debugInfo);
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
#ifdef _WIN32
        output += ".exe";
#endif
    }

    return compiler.compile(output) ? 0 : 1;
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

    // Apply CLI overrides
    if (options_.hasOptLevelOverride)
        cfg.optLevel = options_.optLevel;
    if (options_.hasDebugOverride)
        cfg.debugInfo = options_.debugInfo;

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

    // Resolve dependencies
    if (!cfg.dependencies.empty()) {
        std::string packagesDir = joinPath(cfg.projectRoot, "packages");
        auto resolution = resolvePackages(cfg.dependencies, packagesDir);
        if (!resolution.success) {
            std::cerr << "error: " << resolution.errorMsg << "\n";
            return 1;
        }
        for (const auto &pkg : resolution.packages)
            searchPaths.push_back(pkg.srcPath);
        // Write lock file
        std::string lockPath = joinPath(cfg.projectRoot, "liva.lock");
        std::string lockContent = generateLockFile(resolution.packages);
        std::ofstream lockFile(lockPath);
        if (lockFile.is_open())
            lockFile << lockContent;
    }

    // Determine output
    std::string output = options_.outputFile;
    if (output.empty()) {
        output = joinPath(cfg.projectRoot, cfg.name);
#ifdef _WIN32
        output += ".exe";
#endif
    }

    // Compile
    CompilerInstance compiler;
    compiler.setExecutablePath(executablePath_);
    compiler.setOptLevel(cfg.optLevel);
    compiler.setDebugInfo(cfg.debugInfo);
    compiler.setSearchPaths(searchPaths);

    if (!compiler.loadFile(entryPath))
        return 1;

    if (options_.emitIR)
        return compiler.emitIR(options_.outputFile) ? 0 : 1;

    if (options_.checkOnly) {
        bool ok = compiler.checkOnly();
        if (ok)
            std::cout << "Semantic analysis passed.\n";
        return ok ? 0 : 1;
    }

    if (!compiler.compile(output))
        return 1;

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
              << "*.ll\n";
        }
    }

    std::cout << "Created project '" << name << "' in " << projectDir << "\n";
    return 0;
}

void Driver::printVersion() {
    std::cout << "Liva Compiler v" << LIVA_VERSION_STRING << "\n";
}

void Driver::printHelp() {
    std::cout << "Usage: livac [options] <input.liva>\n"
              << "       livac build [--release|--debug] [-o <file>]\n"
              << "       livac run [--release|--debug]\n"
              << "       livac init [name]\n"
              << "       livac lsp\n"
              << "\n"
              << "Subcommands:\n"
              << "  build               Build project using liva.toml\n"
              << "  run                 Build and run project\n"
              << "  init [name]         Create a new Liva project\n"
              << "  lsp                 Start Language Server Protocol server\n"
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
              << "  --release           Release build (O2, no debug info)\n";
}

} // namespace liva
