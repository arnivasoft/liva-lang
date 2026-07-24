// ExamplesTest: compiles the curated `.liva` files under examples/ through
// the real driver pipeline (same CompilerInstance path livac.exe uses),
// runs the resulting executable, and pins stdout + exit code.
//
// This guards examples/*.liva against silent bit-rot: every stdlib/language
// change that lands must keep the example programs compiling AND producing
// the exact documented output. Adapted from RuntimeExecTest.cpp's
// compileAndRun (in-process compile, temp exe under the build dir, stdout
// captured via redirected std::system) and IntegrationTest.cpp's
// projectRoot() (resolves the repo root via LIVA_PROJECT_ROOT so tests don't
// depend on ctest's working directory).
//
// Only built when LIVA_HAS_LLVM is defined (Clang build with LLVM backend).

#ifdef LIVA_HAS_LLVM

#include "liva/Driver/CompilerInstance.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <gtest/gtest.h>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#define EXE_SUFFIX ".exe"
#else
#include <sys/wait.h>
#define EXE_SUFFIX ""
#endif

namespace {

// Resolve the repo root so `examples/<name>.liva` paths are stable
// regardless of ctest's working directory. Mirrors
// tests/unit/IntegrationTest.cpp:94-104.
static std::string projectRoot() {
#ifdef LIVA_PROJECT_ROOT
    return LIVA_PROJECT_ROOT;
#else
    // Fallback: search upward from CWD for an anchor file.
    std::vector<std::string> candidates = {".", "..", "../..", "../../.."};
    for (const auto &base : candidates) {
        std::string path = base + "/tests/integration/hello.liva";
        std::ifstream ifs(path);
        if (ifs.is_open())
            return base;
    }
    return ".";
#endif
}

static bool readFile(const std::string &path, std::string &out) {
    std::ifstream ifs(path);
    if (!ifs.is_open())
        return false;
    std::ostringstream ss;
    ss << ifs.rdbuf();
    out = ss.str();
    return true;
}

} // namespace

// Result of running a compiled example: exit code + captured stdout (stderr
// is redirected into the same stream so runtime crashes are visible too).
struct RunResult {
    // Exit code of the executed program, or -1 if the source file was
    // missing or compile/link/spawn failed.
    int exit_code;
    std::string stdout_output;
};

namespace {

// Compile examples/<baseName>.liva through the real driver
// (liva::CompilerInstance — the same class livac.exe uses), run it, and
// capture stdout + exit code. Never writes into examples/ — the temp exe/
// obj/output files all live under the build dir (LIVA_BUILD_DIR) and are
// cleaned up before returning, mirroring RuntimeExecTest.cpp:52-108.
//
// Robust to a missing example file: returns a clean RunResult (exit_code
// -1, diagnostic message) instead of crashing, so a not-yet-written example
// fails a normal EXPECT_EQ rather than aborting the test binary.
RunResult compileAndRunExampleImpl(const std::string &baseName, bool runIt) {
    const std::string srcPath = projectRoot() + "/examples/" + baseName + ".liva";
    std::string source;
    if (!readFile(srcPath, source)) {
        return RunResult{-1, "<example file not found: " + srcPath + ">"};
    }

    const std::string buildDir = LIVA_BUILD_DIR;
    const std::string exePath  = buildDir + "/_examples_test_" + baseName + EXE_SUFFIX;
    const std::string outPath  = buildDir + "/_examples_test_" + baseName + ".out.txt";
    // CompilerInstance::compile() creates a temp .o next to the exe; we
    // clean it up regardless of outcome.
    const std::string objPath  = exePath + ".o";

    auto cleanup = [&]() {
        std::remove(exePath.c_str());
        std::remove(outPath.c_str());
        std::remove(objPath.c_str());
    };

    liva::CompilerInstance compiler;
    compiler.setSource(baseName + ".liva", source);
#ifdef _WIN32
    compiler.setExecutablePath(buildDir + "/livac.exe");
#else
    compiler.setExecutablePath(buildDir + "/livac");
#endif

    if (!compiler.compile(exePath)) {
        cleanup();
        // Diagnostics went to stderr via the default DiagnosticsEngine print
        // callback — scroll up in gtest output to see them.
        return RunResult{-1, "<compile failed for " + srcPath +
                                 " — see stderr above for diagnostics>"};
    }

    if (!runIt) {
        cleanup();
        return RunResult{0, "<compile-only: not executed>"};
    }

    // Execute the produced binary. Redirect stderr -> stdout so any runtime
    // crash output is captured alongside println() text. Use a file rather
    // than popen so behavior is uniform across cmd.exe quoting quirks.
    std::string cmd = "\"" + exePath + "\" > \"" + outPath + "\" 2>&1";
#ifdef _WIN32
    // cmd.exe strips outer quotes when /S /C is used by std::system; wrap
    // the whole command so the inner quotes survive.
    cmd = "\"" + cmd + "\"";
#endif
    int rawStatus = std::system(cmd.c_str());

    std::ifstream ifs(outPath);
    std::stringstream ss;
    if (ifs.is_open())
        ss << ifs.rdbuf();
    std::string captured = ss.str();

    cleanup();

    int exitCode;
#ifdef _WIN32
    // std::system() on MSVCRT returns the program's exit code directly.
    exitCode = rawStatus;
#else
    exitCode = WIFEXITED(rawStatus) ? WEXITSTATUS(rawStatus) : -1;
#endif
    return RunResult{exitCode, captured};
}

} // namespace

// Compiles examples/<baseName>.liva, runs it, and returns {exit_code,
// stdout_output}. `baseName` has no ".liva" suffix and no directory
// component (e.g. "map_set_demo" for examples/map_set_demo.liva).
static RunResult compileAndRunExample(const std::string &baseName) {
    return compileAndRunExampleImpl(baseName, /*runIt=*/true);
}

// Compile-only check for examples that shouldn't be executed by this suite
// (e.g. network/UI examples) — used by later example-coverage tasks.
static bool compileExampleOnly(const std::string &baseName) {
    return compileAndRunExampleImpl(baseName, /*runIt=*/false).exit_code == 0;
}

TEST(ExamplesTest, MapSetDemo) {
    auto r = compileAndRunExample("map_set_demo");
    EXPECT_EQ(r.exit_code, 0) << r.stdout_output;
    EXPECT_EQ(r.stdout_output,
              "true\n90\ntrue\nfalse\nfalse\n2\nfalse\n2\n185\n2\n185\n0\ntrue\n"
              "true\ntrue\nfalse\n3\n0\ntrue\n");
}

TEST(ExamplesTest, JsonDemo) {
    auto r = compileAndRunExample("json_demo");
    EXPECT_EQ(r.exit_code, 0) << r.stdout_output;
    EXPECT_EQ(r.stdout_output,
              "liva\n3\ntrue\n3\nlang,llvm,rust-like\n"
              "{\"lang\":\"liva\",\"year\":2026,\"ok\":true}\n");
}

TEST(ExamplesTest, CsvDemo) {
    auto r = compileAndRunExample("csv_demo");
    EXPECT_EQ(r.exit_code, 0) << r.stdout_output;
    EXPECT_EQ(r.stdout_output,
              "3\nname\nAda\n36\nNew York, NY\nname,age,city\n"
              "Linus,56,\"Portland, OR\"\n");
}

TEST(ExamplesTest, TomlDemo) {
    auto r = compileAndRunExample("toml_demo");
    EXPECT_EQ(r.exit_code, 0) << r.stdout_output;
    EXPECT_EQ(r.stdout_output,
              "true\nliva\n3\ntrue\nkadir\nfalse\ntrue\nfalse\n");
}

TEST(ExamplesTest, SqliteDemo) {
    auto r = compileAndRunExample("sqlite_demo");
    EXPECT_EQ(r.exit_code, 0) << r.stdout_output;
    EXPECT_EQ(r.stdout_output,
              "1\nAda\n90\n2\nLinus\n85\n3\nGrace\n95\n3\n270\n");
}

TEST(ExamplesTest, RegexDemo) {
    auto r = compileAndRunExample("regex_demo");
    EXPECT_EQ(r.exit_code, 0) << r.stdout_output;
    EXPECT_EQ(r.stdout_output,
              "true\nfalse\n123\n3\n1\n22\n333\nxNyN\n3\nuser@host\nuser\nhost\n"
              "false\n123\n");
}

TEST(ExamplesTest, CryptoJwtDemo) {
    auto r = compileAndRunExample("crypto_jwt_demo");
    EXPECT_EQ(r.exit_code, 0) << r.stdout_output;
    EXPECT_EQ(r.stdout_output,
              "466da7edcf7a3a5d085bdce101e8c6e16cc794fc1f7e0e734d5d485de22916d0\n"
              "9c9755c4e374d0f7d765de6ae9d3ef087f696a745a031d3fa3e53f28e5fd3b94\n"
              "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9."
              "eyJzdWIiOiJhbGljZSIsInJvbGUiOiJhZG1pbiJ9."
              "eBidO_y2cBiPgKXCxhaHNruZro_NSlYOmd8bsSy_cWw\n"
              "{\"sub\":\"alice\",\"role\":\"admin\"}\n"
              "verify failed\n");
}

TEST(ExamplesTest, TimeDemo) {
    auto r = compileAndRunExample("time_demo");
    EXPECT_EQ(r.exit_code, 0) << r.stdout_output;
    EXPECT_EQ(r.stdout_output,
              "2024\n6\n15\n2024-06-15\n13\n45\n30\n13:45:30\n"
              "2024-06-15 08:30:00\n2024-06-15\n08:30:00\n2024/06/15\n"
              "2024-06-25\n15\ntrue\ntrue\n");
}

TEST(ExamplesTest, CliDemo) {
    auto r = compileAndRunExample("cli_demo");
    EXPECT_EQ(r.exit_code, 0) << r.stdout_output;
    EXPECT_EQ(r.stdout_output,
              "true\ntrue\nHi\n1\nKadir\nfalse\nunknown option: --bogus\n"
              "Usage: greet [options] <name>\n\n"
              "Greets someone by name\n\n"
              "Options:\n"
              "  -h, --help          Show this help\n"
              "  -v, --verbose       Print extra details\n"
              "  -g, --greeting <value>   The greeting word to use (default: Hello)\n\n"
              "Positionals:\n"
              "  <name>   Name of the person to greet\n\n");
}

// Network examples: compile-only (never executed — they need a live
// internet connection, which this test suite must not depend on).
TEST(ExamplesTest, HttpDemoCompiles) {
    EXPECT_TRUE(compileExampleOnly("http_demo"));
}

TEST(ExamplesTest, WebsocketDemoCompiles) {
    EXPECT_TRUE(compileExampleOnly("websocket_demo"));
}

#endif // LIVA_HAS_LLVM
