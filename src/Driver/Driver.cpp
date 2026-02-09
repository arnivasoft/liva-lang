#include "liva/Driver/Driver.h"
#include "liva/Common/Version.h"
#include "liva/Driver/CompilerInstance.h"
#include <cstring>
#include <iostream>

namespace liva {

bool Driver::parseArgs(int argc, const char **argv) {
    for (int i = 1; i < argc; ++i) {
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
            continue;
        }
        if (std::strcmp(arg, "-O1") == 0) {
            options_.optLevel = 1;
            continue;
        }
        if (std::strcmp(arg, "-O2") == 0) {
            options_.optLevel = 2;
            continue;
        }
        if (std::strcmp(arg, "-O3") == 0) {
            options_.optLevel = 3;
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

    if (options_.inputFile.empty()) {
        printVersion();
        std::cerr << "\nerror: no input file\n";
        printHelp();
        return 1;
    }

    CompilerInstance compiler;
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

void Driver::printVersion() {
    std::cout << "Liva Compiler v" << LIVA_VERSION_STRING << "\n";
}

void Driver::printHelp() {
    std::cout << "Usage: livac [options] <input.liva>\n"
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
              << "  -O0/-O1/-O2/-O3     Optimization level\n";
}

} // namespace liva
