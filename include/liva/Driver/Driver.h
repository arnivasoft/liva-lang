#pragma once

#include "liva/Driver/ProjectConfig.h"
#include <string>
#include <vector>

namespace liva {

enum class Subcommand { None, Build, Run, Init, Lsp, Repl, Clean };

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
    bool hasOptLevelOverride = false;
    bool hasDebugOverride = false;
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
    int executeLegacy();

    int buildProject(bool runAfter);
    int linkCachedObject(const std::string &objPath,
                         const std::string &outputPath, bool debugInfo,
                         const std::string &ltoMode = "none",
                         const std::string &pgoMode = "none");

    DriverOptions options_;
    std::string executablePath_;
};

} // namespace liva
