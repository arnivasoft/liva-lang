#pragma once

#include "liva/Common/TerminalColors.h"
#include "liva/Driver/ProjectConfig.h"
#include <string>
#include <vector>

namespace liva {

enum class Subcommand { None, Build, Run, Init, Lsp, Repl, Clean, Install, Remove, Fmt, Lint, Dap, Bench, Test, Link };

/// Command-line options
struct DriverOptions {
    Subcommand subcommand = Subcommand::None;
    std::string inputFile;
    std::string outputFile;
    bool dumpTokens = false;
    bool dumpAST = false;
    bool checkOnly = false;
    bool emitIR = false;
    bool emitObj = false;
    int optLevel = 0;
    bool debugInfo = false;
    std::string lto = "none";  // "none", "thin", "full"
    bool hasLtoOverride = false;
    std::string pgo = "none";  // "none", "generate", "use"
    std::string pgoProfile;    // profile data path (for pgo=use)
    bool hasPgoOverride = false;
    bool showHelp = false;
    bool showVersion = false;
    std::string initName;
    std::string installPkgName;      // package name to install
    std::string installPkgVersion;   // optional version constraint (default: latest)
    std::string removePkgName;       // package name to remove
    std::vector<std::string> fmtFiles;
    std::vector<std::string> lintFiles;
    std::vector<std::string> objectFiles;  // for 'link' subcommand
    bool fmtCheck = false;
    bool hasOptLevelOverride = false;
    bool hasDebugOverride = false;
    int jobs = 0;  // 0 = auto (hardware_concurrency), 1 = sequential
    bool hasJobsOverride = false;
    bool rebuild = false;  // --rebuild: force recompile all, bypass cache
    std::string targetTriple;       // --target=<triple>
    bool hasTargetOverride = false;
    ColorMode colorMode = ColorMode::Auto;  // --color=auto|always|never
    bool traceMacros = false;
};

/// Parses command-line arguments and drives compilation
class Driver {
public:
    /// Parse command-line arguments
    bool parseArgs(int argc, const char **argv);

    /// Execute based on parsed options
    int execute();

    /// Print help message
    static void printHelp();

    /// Print version info
    static void printVersion();

    const DriverOptions &getOptions() const { return options_; }

private:
    int executeBuild();
    int executeRun();
    int executeInit();
    int executeLsp();
    int executeRepl();
    int executeClean();
    int executeInstall();
    int executeRemove();
    int executeFmt();
    int executeLint();
    int executeDap();
    int executeBench();
    int executeTest();
    int executeLink();
    int executeLegacy();

    int buildProject(bool runAfter);
    int linkCachedObject(const std::string &objPath,
                         const std::string &outputPath, bool debugInfo,
                         const std::string &ltoMode = "none",
                         const std::string &pgoMode = "none");
    int linkObjects(const std::vector<std::string> &objPaths,
                    const std::string &outputPath, bool debugInfo,
                    const std::string &ltoMode = "none",
                    const std::string &pgoMode = "none");

    DriverOptions options_;
    std::string executablePath_;
};

/// Format Liva source code using brace-depth indentation (4 spaces).
std::string formatLivaSource(const std::string &content);

// Forward declarations for lint
class TranslationUnit;
class DiagnosticsEngine;

/// Lint a parsed translation unit, emitting warnings via diag.
/// Returns the number of warnings emitted.
int lintLivaSource(const TranslationUnit &tu, DiagnosticsEngine &diag);

} // namespace liva
