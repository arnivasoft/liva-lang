// RuntimeExecTest: end-to-end harness that compiles a .liva source string to
// a native executable, runs it, and asserts on stdout + exit code.
//
// This is the foundational test layer for runtime-behavior verification — it
// exercises the FULL pipeline (Lexer -> Parser -> Sema -> IRGen -> CodeGen ->
// link with liva_runtime -> execute), not just type checking. All subsequent
// generator/yield runtime work depends on this harness.
//
// The harness uses the in-process CompilerInstance::compile() entry point —
// the same code path that `livac` uses — so we are testing real driver
// behavior, not a parallel reimplementation. The only thing we add is exec +
// stdout capture via redirected std::system + file read.
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

#ifdef _WIN32
#define EXE_SUFFIX ".exe"
#else
#include <sys/wait.h>
#define EXE_SUFFIX ""
#endif

namespace {

struct RunResult {
    // Exit code of the executed program, or -1 if compile/link/spawn failed.
    int exit_code;
    // Captured stdout (and stderr — we redirect 2>&1 so failures surface) from
    // the executed program. On compile failure this contains a short
    // diagnostic marker.
    std::string stdout_output;
};

// Compile a Liva source string to an executable, run it, capture stdout and
// the exit code. Mirrors livac's flow by calling CompilerInstance::compile()
// directly — no shortcuts, no parallel link logic.
//
// `test_name` is used to name the temporary executable so concurrent tests
// don't collide. Temp files are written under the build directory and cleaned
// up before return.
RunResult compileAndRun(const std::string &source, const std::string &test_name) {
    const std::string buildDir = LIVA_BUILD_DIR;
    const std::string exePath  = buildDir + "/_runtime_exec_" + test_name + EXE_SUFFIX;
    const std::string outPath  = buildDir + "/_runtime_exec_" + test_name + ".out.txt";
    // CompilerInstance::compile() creates a temp .o next to the exe; we clean
    // both up regardless of outcome.
    const std::string objPath  = exePath + ".o";

    auto cleanup = [&]() {
        std::remove(exePath.c_str());
        std::remove(outPath.c_str());
        std::remove(objPath.c_str());
    };

    liva::CompilerInstance compiler;
    compiler.setSource("_runtime_exec_" + test_name + ".liva", source);
#ifdef _WIN32
    compiler.setExecutablePath(buildDir + "/livac.exe");
#else
    compiler.setExecutablePath(buildDir + "/livac");
#endif

    if (!compiler.compile(exePath)) {
        cleanup();
        // Diagnostics from CompilerInstance went to stderr via the default
        // DiagnosticsEngine print callback — scroll up in gtest output to see them.
        return RunResult{-1, "<compile failed — see stderr above for diagnostics>"};
    }

    // Execute the produced binary. Redirect stderr -> stdout so any runtime
    // crash output is captured alongside println() text. Use a file rather
    // than popen so behavior is uniform across cmd.exe quoting quirks.
    std::string cmd = "\"" + exePath + "\" > \"" + outPath + "\" 2>&1";
#ifdef _WIN32
    // cmd.exe strips outer quotes when /S /C is used by std::system; wrap the
    // whole command so the inner quotes survive.
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

}  // namespace

TEST(RuntimeExecTest, HelloWorld_PrintsAndExitsZero) {
    auto r = compileAndRun(
        "func main() { println(\"hello\") }\n",
        "hello_world");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_NE(r.stdout_output.find("hello"), std::string::npos)
        << "stdout: " << r.stdout_output;
}

TEST(RuntimeExecTest, AsyncSimple_ReturnsAndPrints) {
    // Previously XFAIL'd (SEH 0xc0000005 during in-process compile) — fixed by
    // setting the PresplitCoroutine attribute on async/generator funcs (Task 6),
    // which lets CoroSplit identify and split them so coroutine intrinsics get
    // lowered before reaching CodeGen.
    auto r = compileAndRun(R"(
        async func answer() -> i32 { return 42 }
        async func main() {
            let a: i32 = await answer()
            println("got:")
            println(a)
        }
    )", "async_simple");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_NE(r.stdout_output.find("42"), std::string::npos)
        << "stdout: " << r.stdout_output;
}

TEST(RuntimeExecTest, Generator_CountToThree_Yields012) {
    // Previously XFAIL'd (codegen abort: `LLVM ERROR: Do not know how to
    // promote this operator's operand!` during DAG type legalization) — fixed
    // by setting the PresplitCoroutine attribute on async/generator funcs
    // (Task 6), which lets CoroSplit identify and split them so coroutine
    // intrinsics (coro.suspend, coro.end, coro.promise) are lowered before
    // reaching CodeGen.
    auto r = compileAndRun(R"(
        func countTo(n: i32) {
            var i: i32 = 0
            while i < n {
                yield i
                i = i + 1
            }
        }
        func main() {
            for x in countTo(3) {
                println(x)
            }
        }
    )", "gen_count_three");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_EQ(r.stdout_output, "0\n1\n2\n")
        << "stdout: " << r.stdout_output;
}

TEST(RuntimeExecTest, Generator_EmptyRange_NoOutput) {
    auto r = compileAndRun(R"(
        func gen(n: i32) {
            var i: i32 = 0
            while i < n {
                yield i
                i = i + 1
            }
        }
        func main() {
            for x in gen(0) {
                println(x)
            }
        }
    )", "gen_empty");
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_EQ(r.stdout_output, "");
}

TEST(RuntimeExecTest, Generator_BoundToVariable_Iterates) {
    auto r = compileAndRun(R"(
        func gen() {
            yield 7
            yield 8
        }
        func main() {
            let g = gen()
            for x in g {
                println(x)
            }
        }
    )", "gen_bound");
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_EQ(r.stdout_output, "7\n8\n");
}

TEST(RuntimeExecTest, Generator_NestedYieldInIf_Yields) {
    auto r = compileAndRun(R"(
        func gen(n: i32) {
            var i: i32 = 0
            while i < n {
                if i % 2 == 0 {
                    yield i
                }
                i = i + 1
            }
        }
        func main() {
            for x in gen(5) {
                println(x)
            }
        }
    )", "gen_nested");
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_EQ(r.stdout_output, "0\n2\n4\n");
}

TEST(RuntimeExecTest, Generator_VarShadowingAcrossFunctions_NoStaleType) {
    auto r = compileAndRun(R"(
        func gen() {
            yield 1
        }
        func a() {
            let g = gen()
            for x in g {
                println(x)
            }
        }
        func b() {
            let g = 42
            println(g)
        }
        func main() {
            a()
            b()
        }
    )", "gen_shadow");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_EQ(r.stdout_output, "1\n42\n") << "stdout: " << r.stdout_output;
}

#endif // LIVA_HAS_LLVM
