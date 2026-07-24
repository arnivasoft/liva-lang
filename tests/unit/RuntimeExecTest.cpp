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

// Multi-module variant of compileAndRun: writes a producer .liva module to the
// build directory, registers that directory as a module search path, then
// compiles `main_source` (which is expected to `import producer_name`) and
// runs the resulting executable. Cleans up the producer .liva file along with
// the usual exe/obj/output, even on failure.
//
// `producer_name` is the bare module name as it appears in `import` (no
// `.liva` suffix, no `::` segments). For dotted module paths a single helper
// is enough today — extend as needed.
RunResult compileAndRunWithModule(const std::string &producer_source,
                                  const std::string &producer_name,
                                  const std::string &main_source,
                                  const std::string &test_name) {
    const std::string buildDir     = LIVA_BUILD_DIR;
    const std::string exePath      = buildDir + "/_runtime_exec_" + test_name + EXE_SUFFIX;
    const std::string outPath      = buildDir + "/_runtime_exec_" + test_name + ".out.txt";
    const std::string objPath      = exePath + ".o";
    const std::string producerPath = buildDir + "/" + producer_name + ".liva";

    auto cleanup = [&]() {
        std::remove(exePath.c_str());
        std::remove(outPath.c_str());
        std::remove(objPath.c_str());
        std::remove(producerPath.c_str());
    };

    // Write producer module to the build dir so ModuleLoader can find it via
    // the search path we configure below.
    {
        std::ofstream pf(producerPath);
        if (!pf.is_open()) {
            cleanup();
            return RunResult{-1, "<failed to write producer module to " + producerPath + ">"};
        }
        pf << producer_source;
    }

    liva::CompilerInstance compiler;
    compiler.setSource("_runtime_exec_" + test_name + ".liva", main_source);
    compiler.setSearchPaths({buildDir});
#ifdef _WIN32
    compiler.setExecutablePath(buildDir + "/livac.exe");
#else
    compiler.setExecutablePath(buildDir + "/livac");
#endif

    if (!compiler.compile(exePath)) {
        cleanup();
        return RunResult{-1, "<compile failed — see stderr above for diagnostics>"};
    }

    std::string cmd = "\"" + exePath + "\" > \"" + outPath + "\" 2>&1";
#ifdef _WIN32
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
    exitCode = rawStatus;
#else
    exitCode = WIFEXITED(rawStatus) ? WEXITSTATUS(rawStatus) : -1;
#endif
    return RunResult{exitCode, captured};
}

// Counts non-overlapping occurrences of `needle` in `haystack`. Used by the
// Drop move-semantics tests to assert an EXACT drop count (double-drop RED
// shows 2, GREEN shows 1).
int countOccurrences(const std::string &haystack, const std::string &needle) {
    int count = 0;
    size_t pos = 0;
    while ((pos = haystack.find(needle, pos)) != std::string::npos) {
        ++count;
        pos += needle.size();
    }
    return count;
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
    // Regression: when `for x in g` is dispatched, the iterable's resolved
    // Sema type must reflect g's actual binding in the current function — a
    // generator in a(), a dyn-array in b() — not state leaked from elsewhere.
    // Original failure: a stale IRGen mirror map entry survived across
    // functions, causing b()'s `for x in g` to mis-dispatch as coroutine
    // iteration over an Array (silent miscompile: empty stdout, exit 0).
    // Now guarded by Sema's per-scope IdentifierExpr type resolution +
    // IRGen's type-based dispatch in visitForStmt.
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
            var g: [i32] = [10, 20, 30]
            for x in g {
                println(x)
            }
        }
        func main() {
            a()
            b()
        }
    )", "gen_shadow");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_EQ(r.stdout_output, "1\n10\n20\n30\n") << "stdout: " << r.stdout_output;
}

TEST(RuntimeExecTest, Generator_BreakEarly_DestroysSuspendedCoro) {
    // Break out of a generator loop before exhaustion — exercises
    // liva_coro_destroy on a still-suspended coroutine frame. Without
    // proper destroy, this can leak the frame or crash on Windows MSVC.
    auto r = compileAndRun(R"(
        func gen(n: i32) {
            var i: i32 = 0
            while i < n {
                yield i
                i = i + 1
            }
        }
        func main() {
            for x in gen(10) {
                if x == 3 {
                    break
                }
                println(x)
            }
        }
    )", "gen_break");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_EQ(r.stdout_output, "0\n1\n2\n") << "stdout: " << r.stdout_output;
}

TEST(RuntimeExecTest, Generator_ContinueSkips_ResumesCorrectly) {
    // Continue inside a generator loop — verifies coro.resume sequencing
    // when control jumps past `println` to the latch block.
    auto r = compileAndRun(R"(
        func gen(n: i32) {
            var i: i32 = 0
            while i < n {
                yield i
                i = i + 1
            }
        }
        func main() {
            for x in gen(5) {
                if x == 2 {
                    continue
                }
                println(x)
            }
        }
    )", "gen_continue");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_EQ(r.stdout_output, "0\n1\n3\n4\n") << "stdout: " << r.stdout_output;
}

TEST(RuntimeExecTest, Generator_BreakOnVarBoundGenerator_DestroysCoro) {
    // Same as Generator_BreakEarly but with a variable-bound generator
    // (`let g = gen(); for x in g`) — exercises the variable-bound
    // iteration path through visitForStmt's resolved-type dispatch.
    auto r = compileAndRun(R"(
        func gen(n: i32) {
            var i: i32 = 0
            while i < n {
                yield i
                i = i + 1
            }
        }
        func main() {
            let g = gen(100)
            for x in g {
                if x == 2 {
                    break
                }
                println(x)
            }
        }
    )", "gen_break_var");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_EQ(r.stdout_output, "0\n1\n") << "stdout: " << r.stdout_output;
}

TEST(RuntimeExecTest, Generator_BreakOnFirstIteration_NoOutput) {
    // Edge case: break BEFORE any println on the very first iteration.
    // Coroutine has yielded once but the consumer takes the first value
    // and immediately bails. Frame must still destroy cleanly.
    auto r = compileAndRun(R"(
        func gen() {
            yield 42
            yield 99
        }
        func main() {
            for x in gen() {
                break
            }
            println("done")
        }
    )", "gen_break_first");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_EQ(r.stdout_output, "done\n") << "stdout: " << r.stdout_output;
}

TEST(RuntimeExecTest, Generator_CrossModule_Iterates) {
    // Cross-module generator: producer module exports a `pub func` generator,
    // consumer imports it and iterates via for-in. Verifies the
    // PresplitCoroutine attribute survives the ModuleLoader IR-import path —
    // i.e. the imported coroutine is still recognized by CoroSplit and gets
    // its intrinsics lowered before reaching CodeGen. Without proper attr
    // propagation across modules this would either fail to compile or crash
    // at runtime with the same "Do not know how to promote this operator's
    // operand!" abort that single-module generators hit before Task 6.
    const char *producer = R"(
        pub func crossGen() {
            yield 100
            yield 200
            yield 300
        }
    )";
    const char *consumer = R"(
        import gen_producer
        func main() {
            for x in crossGen() {
                println(x)
            }
        }
    )";
    auto r = compileAndRunWithModule(producer, "gen_producer", consumer,
                                     "gen_cross_module");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_EQ(r.stdout_output, "100\n200\n300\n") << "stdout: " << r.stdout_output;
}

TEST(RuntimeExecTest, HashI64StableValue) {
    // Calling .hash() on the same i32 value twice must produce the same result.
    // Exercises the liva_hash_i32 runtime wrapper end-to-end.
    auto r = compileAndRun(R"(
        func main() {
            let x: i32 = 5
            let h1 = x.hash()
            let h2 = x.hash()
            if h1 == h2 {
                println("equal")
            } else {
                println("differ")
            }
        }
    )", "hash_i64_stable");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_NE(r.stdout_output.find("equal"), std::string::npos)
        << "stdout: " << r.stdout_output;
}

TEST(RuntimeExecTest, HashStringDifferentInputs) {
    // Two distinct string literals must produce different hash values
    // (collision for "foo" vs "bar" would be a very bad hash function).
    // Exercises the liva_hash_string runtime wrapper end-to-end.
    auto r = compileAndRun(R"(
        func main() {
            let h1 = "foo".hash()
            let h2 = "bar".hash()
            if h1 == h2 {
                println("collision")
            } else {
                println("different")
            }
        }
    )", "hash_string_diff");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_NE(r.stdout_output.find("different"), std::string::npos)
        << "stdout: " << r.stdout_output;
}

// ============================================================
// BTreeMap runtime tests (P1-8 alt-spec 3)
// ============================================================

TEST(RuntimeExecTest, BTreeMapI64OrderedInsertGet) {
    // Use local variables for unwrap so the Optional alloca is in scope.
    auto r = compileAndRun(R"--(
        import collections::btree
        func main() {
            var m = BTreeMapI64I64.new()
            m.insert(5, 50)
            m.insert(2, 20)
            m.insert(8, 80)
            let v5 = m.get(5)
            let v2 = m.get(2)
            let v8 = m.get(8)
            println(v5.unwrap())
            println(v2.unwrap())
            println(v8.unwrap())
            println(m.size())
            m.free()
        }
    )--", "btree_i64_ordered");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_NE(r.stdout_output.find("50"), std::string::npos)
        << "stdout: " << r.stdout_output;
    EXPECT_NE(r.stdout_output.find("20"), std::string::npos)
        << "stdout: " << r.stdout_output;
    EXPECT_NE(r.stdout_output.find("80"), std::string::npos)
        << "stdout: " << r.stdout_output;
    EXPECT_NE(r.stdout_output.find("3"), std::string::npos)
        << "stdout: " << r.stdout_output;
}

TEST(RuntimeExecTest, BTreeMapI64ContainsAndRemove) {
    auto r = compileAndRun(R"--(
        import collections::btree
        func main() {
            var m = BTreeMapI64I64.new()
            m.insert(10, 100)
            m.insert(20, 200)
            if m.contains(10) { println("has10") }
            m.remove(10)
            if m.contains(10) { println("still10") } else { println("no10") }
            m.free()
        }
    )--", "btree_i64_contains_remove");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_NE(r.stdout_output.find("has10"), std::string::npos)
        << "stdout: " << r.stdout_output;
    EXPECT_NE(r.stdout_output.find("no10"), std::string::npos)
        << "stdout: " << r.stdout_output;
}

TEST(RuntimeExecTest, BTreeMapStrLookup) {
    // Use local variables for unwrap; use contains() to check missing key.
    auto r = compileAndRun(R"--(
        import collections::btree
        func main() {
            var m = BTreeMapStrI64.new()
            m.insert("apple", 1)
            m.insert("banana", 2)
            m.insert("cherry", 3)
            let va = m.get("apple")
            let vb = m.get("banana")
            let vc = m.get("cherry")
            println(va.unwrap())
            println(vb.unwrap())
            println(vc.unwrap())
            if !m.contains("missing") { println("absent") }
            m.free()
        }
    )--", "btree_str_lookup");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_NE(r.stdout_output.find("1"), std::string::npos)
        << "stdout: " << r.stdout_output;
    EXPECT_NE(r.stdout_output.find("2"), std::string::npos)
        << "stdout: " << r.stdout_output;
    EXPECT_NE(r.stdout_output.find("3"), std::string::npos)
        << "stdout: " << r.stdout_output;
    EXPECT_NE(r.stdout_output.find("absent"), std::string::npos)
        << "stdout: " << r.stdout_output;
}

TEST(RuntimeExecTest, BTreeMapI64SizeAndIsEmpty) {
    auto r = compileAndRun(R"--(
        import collections::btree
        func main() {
            var m = BTreeMapI64I64.new()
            if m.isEmpty() { println("empty") }
            m.insert(1, 1)
            m.insert(2, 2)
            m.insert(3, 3)
            println(m.size())
            m.free()
        }
    )--", "btree_i64_size_empty");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_NE(r.stdout_output.find("empty"), std::string::npos)
        << "stdout: " << r.stdout_output;
    EXPECT_NE(r.stdout_output.find("3"), std::string::npos)
        << "stdout: " << r.stdout_output;
}

// ============================================================
// Optional method codegen (.isSome / .isNone / .unwrap)
// ============================================================

TEST(RuntimeExecTest, OptionalIsSomeUnwrap) {
    auto r = compileAndRun(R"--(
        func main() {
            let v: i32? = 7
            if v.isSome() { println("some") } else { println("none") }
            println(v.unwrap())
        }
    )--", "opt_issome_unwrap");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_NE(r.stdout_output.find("some"), std::string::npos)
        << "stdout: " << r.stdout_output;
    EXPECT_NE(r.stdout_output.find("7"), std::string::npos)
        << "stdout: " << r.stdout_output;
}

TEST(RuntimeExecTest, OptionalIsNoneOnNil) {
    auto r = compileAndRun(R"--(
        func mk(x: i32) -> i32? {
            if x == 0 { return nil }
            return x
        }
        func main() {
            let v = mk(0)
            if v.isNone() { println("isnone") } else { println("hasval") }
            let w = mk(42)
            if w.isSome() { println(w.unwrap()) }
        }
    )--", "opt_isnone_nil");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_NE(r.stdout_output.find("isnone"), std::string::npos)
        << "stdout: " << r.stdout_output;
    EXPECT_NE(r.stdout_output.find("42"), std::string::npos)
        << "stdout: " << r.stdout_output;
}

// ============================================================
// Simple (payload-less) enum lowering (Faz 4 Task 1 regression)
// ============================================================

TEST(RuntimeExecTest, SimpleEnum_AsParamAndStructField_LowersToI32) {
    // Regression guard for the Faz 4 Task 1 fix: a simple (payload-less) enum
    // used as a function/method parameter and as a class field must lower to
    // i32, not fall through to a pointer (which previously caused an LLVM type
    // mismatch). Exercises BOTH broken paths in one executable:
    //   1. enum passed as a function parameter (`rank(c: Color)`)
    //   2. enum stored/read as a class field (`Box.color`, matched in a method)
    auto r = compileAndRun(R"--(
        enum Color {
            case Red = 0
            case Green = 1
            case Blue = 2
        }

        func rank(c: Color) -> i32 {
            let r = match c {
                Color.Red => 10
                Color.Green => 20
                _ => 30
            }
            return r
        }

        class Box {
            var color: Color
            init(c: Color) {
                self.color = c
            }
            func score() -> i32 {
                let s = match self.color {
                    Color.Blue => 99
                    _ => 0
                }
                return s
            }
        }

        func main() {
            println(rank(Color.Green))
            let b = Box(Color.Blue)
            println(b.score())
        }
    )--", "simple_enum_param_field");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_NE(r.stdout_output.find("20"), std::string::npos)
        << "stdout: " << r.stdout_output;
    EXPECT_NE(r.stdout_output.find("99"), std::string::npos)
        << "stdout: " << r.stdout_output;
}

// Pattern Types (Faz B) Task 1: negative-int arm regression test. Today
// visitMatchExpr's consumption loop filters arms via `tag >= 0`, which is
// also the sentinel PatternInfo uses for "no concrete tag" (wildcard/binding
// arms) — so a `-1 =>` arm's tag (-1) collides with that sentinel and the
// arm is silently excluded from both the switch's case list and its
// numCases count, making it permanently unreachable. Exercises exactly the
// case+wildcard mix that trips this: a negative literal arm, a positive
// literal arm, and a wildcard fallback, on a plain (non-enum) i32 subject.
TEST(RuntimeExecTest, MatchNegativeIntLiteralArm) {
    auto r = compileAndRun(R"--(
        func classify(x: i32) -> i32 {
            let r = match x {
                -1 => 111
                1 => 222
                _ => 333
            }
            return r
        }
        func main() {
            println(classify(-1))
            println(classify(1))
            println(classify(5))
        }
    )--", "match_negative_int_arm");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_EQ(r.stdout_output, "111\n222\n333\n") << "stdout: " << r.stdout_output;
}

// Pattern Types (Faz B) Task 1 follow-up: a 4th structurally identical
// `tag >= 0` sentinel read survived inside emitNestedPatternMatch (line
// ~1745) — the depth>=2 nested-binding-extraction loop that recurses into a
// THIRD level of enum nesting. `Inner.Neg` has an explicit negative
// discriminant (`case Neg = -1`, legal per ParseDecl.cpp) and is carried as
// the innermost payload two levels deep (Outer.Wrap(Middle.Something(...))).
// Reaching this requires Middle itself to be a *payload* enum (so
// emitNestedPatternMatch takes its "payload enum" branch and loops over
// nested.nestedPatterns), unlike the shallower (Outer directly wrapping a
// simple enum) case already covered by MatchNegativeIntLiteralArm's sibling
// fix. Before the fix, `nested.nestedPatterns[b].tag >= 0` is false for the
// Neg leaf, so the inner check is skipped entirely (falls through to the
// "no binding name either" no-op branch) — meaning the pattern accepts ANY
// Inner value, not just Neg specifically.
TEST(RuntimeExecTest, MatchNestedNegativeDiscriminant) {
    auto r = compileAndRun(R"--(
        enum Inner {
            case Neg = -1
            case Zero = 0
        }
        enum Middle {
            case Something(Inner)
        }
        enum Outer {
            case Wrap(Middle)
            case Empty
        }
        func classify(o: Outer) -> i32 {
            let r = match o {
                Outer.Wrap(Middle.Something(Inner.Neg)) => 111
                _ => 222
            }
            return r
        }
        func main() {
            println(classify(Outer.Wrap(Middle.Something(Inner.Neg))))
            println(classify(Outer.Wrap(Middle.Something(Inner.Zero))))
        }
    )--", "match_nested_negative_discriminant");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    // Only the first call actually matches Inner.Neg; the second wraps
    // Inner.Zero and must fall through to the wildcard arm (222), not be
    // silently accepted by the Inner.Neg-specific pattern.
    EXPECT_EQ(r.stdout_output, "111\n222\n") << "stdout: " << r.stdout_output;
}

// Pattern Types (Faz B) Task 2 REGRESSION test: a non-exhaustive switch-mode
// match (int subject, no wildcard arm) used as a STATEMENT is perfectly
// legal — TypeChecker only enforces exhaustiveness for enum/Result subjects,
// never for a plain int subject — and must simply fall through when the
// scrutinee matches none of the listed arms, exactly like the pre-Task-2
// behavior. An earlier version of the bool-literal-pattern PHI-verifier fix
// routed this "no wildcard" default straight to an `unreachable` block
// whenever in switch mode, which made this exact shape trap at runtime
// instead of falling through — code after the match must still run.
TEST(RuntimeExecTest, MatchNonExhaustiveStatementNoTrap) {
    auto r = compileAndRun(R"--(
        func main() {
            let x: i32 = 5
            match x {
                1 => println("a")
                2 => println("b")
            }
            println("after")
        }
    )--", "match_nonexhaustive_statement_no_trap");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_EQ(r.stdout_output, "after\n") << "stdout: " << r.stdout_output;
}

// Pattern Types (Faz B) Task 3 re-review ICE regression: a range arm on a
// subject Sema cannot classify (struct) must NOT hit an invalid ICmp / LLVM
// verify failure. The guarded fallback keeps the legacy behavior: the range
// arm's patternCond stays null, so it matches unconditionally (semantically
// wrong but Sema-legal and non-fatal — closing the Sema gap for unclassified
// subjects is tracked separately). This test pins "compiles and runs", not
// the arm choice.
TEST(RuntimeExecTest, MatchRangeOnStructSubjectNoICE) {
    auto r = compileAndRun(R"--(
        struct Point { x: i32 }
        func main() {
            let p = Point{x: 1}
            match p {
                1..10 => println("range")
                _ => println("other")
            }
            println("after")
        }
    )--", "match_range_struct_subject_no_ice");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_NE(r.stdout_output.find("after"), std::string::npos)
        << "stdout: " << r.stdout_output;
}

// Final review Critical 3: same class of ICE as MatchRangeOnStructSubjectNoICE
// above (bbc6fe0), for the FLOAT branch of emitPatternCond — a subject Sema
// cannot classify (struct) must not hit ConstantFP::get's type assert.
TEST(RuntimeExecTest, MatchFloatLiteralOnStructSubjectNoICE) {
    auto r = compileAndRun(R"--(
        struct Point { x: i32 }
        func main() {
            let p = Point{x: 1}
            match p {
                3.14 => println("float")
                _ => println("other")
            }
            println("after")
        }
    )--", "match_float_struct_subject_no_ice");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_NE(r.stdout_output.find("after"), std::string::npos)
        << "stdout: " << r.stdout_output;
}

// Final review Critical 3: STRING branch of emitPatternCond — a struct
// subject's tagVal is not a pointer, so liva_str_equal's call must not be
// built (ill-typed call -> module verify failure) against it.
TEST(RuntimeExecTest, MatchStringLiteralOnStructSubjectNoICE) {
    auto r = compileAndRun(R"--(
        struct Point { x: i32 }
        func main() {
            let p = Point{x: 1}
            match p {
                "x" => println("str")
                _ => println("other")
            }
            println("after")
        }
    )--", "match_string_struct_subject_no_ice");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_NE(r.stdout_output.find("after"), std::string::npos)
        << "stdout: " << r.stdout_output;
}

// Final review Critical 3: TUPLE PROLOGUE (visitMatchExpr's
// emitTuplePatternBindings call, NOT emitPatternCond's isTuple branch, which
// already had an isStructTy() guard) must not hit an out-of-range
// CreateExtractValue.
//
// Unlike the float/string arms above, a PLAIN struct-typed local variable
// subject (`let p = Point{x:1}; match p {(a,b) => ...}`) is actually already
// caught by Sema (checkTuplePatternType fires whenever the arm pattern
// itself is Tuple, for ANY non-null subjectType, Named included — verified:
// it correctly reports err_pattern_type_mismatch there). The genuinely
// Sema-unclassifiable case is a subject whose OWN AST node never gets a
// resolved type annotated at all — a nested match EXPRESSION used directly
// as the outer match's subject (`match (match y {...}) {...}`): TypeChecker
// never calls setResolvedType on a MatchExpr node, so
// `node->getSubject()->getResolvedType()` is null, and the loop above
// (`if (!arm.patternNode || !subjectType) continue;`) skips this arm
// entirely — the ONLY way to reach IRGen with a tuple pattern against a
// struct subject uncaught. `--check-only` confirms Sema passes; without the
// isStructTy()/element-count guard, this used to crash IRGen (assert or
// segfault depending on build config) on the 2nd element's out-of-range
// CreateExtractValue against Point's 1-field struct type.
TEST(RuntimeExecTest, MatchTuplePatternOnStructSubjectNoICE) {
    auto r = compileAndRun(R"--(
        struct Point { x: i32 }
        func main() {
            let y: i32 = 1
            match (match y { _ => Point{x: 1} }) {
                (a, b) => println("tuple")
                _ => println("other")
            }
            println("after")
        }
    )--", "match_tuple_struct_subject_no_ice");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_NE(r.stdout_output.find("after"), std::string::npos)
        << "stdout: " << r.stdout_output;
}

// === Final review Critical 2: chain-mode + payload-enum + nested enum
// pattern — wrong-arm dispatch / PHI verifier abort ===
//
// emitNestedPatternMatch was called from the arm prologue with a hardcoded
// `failBB=defaultBB` — correct ONLY under the switch-mode contract ("arm
// entered only after outer tag matched"). Here `Outer.Y | Outer.Z` (an
// or-pattern) forces the WHOLE match into if-else-chain mode, where
// `Outer.X(Inner.A(n))`'s own outer tag has NOT been verified yet by the
// time this prologue runs. For subject `Outer.Y`: arm0 misreads Y's payload
// bytes as if they were X's, and on the (near-certain) nested-tag mismatch
// jumped straight to the GLOBAL default — skipping arm1 (`Outer.Y|Outer.Z`)
// entirely and printing "3" instead of the correct "2". `Inner.B` is
// deliberately case index 0 (not `Inner.A`): Outer.Y's unused payload region
// reads back as all-zero bytes, which would otherwise coincidentally equal
// Inner.A's own tag if it were index 0 too, masking the bug (a "spurious
// match" on garbage that still resolves correctly once the REAL outer-tag
// check runs afterward). A subject is bound to a named local (`sy`/`sx`,
// not passed as a literal enum-case call argument directly) — an unrelated,
// pre-existing limitation independently rejects passing a bare no-payload
// case of a mixed payload/non-payload enum straight through as a call
// argument (out of this review's scope).
TEST(RuntimeExecTest, MatchChainModeNestedEnumDispatchOrder) {
    auto r = compileAndRun(R"--(
        enum Inner {
            case B
            case A(i32)
        }
        enum Outer {
            case X(Inner)
            case Y
            case Z
        }
        func classify(o: Outer) -> i32 {
            let r = match o {
                Outer.X(Inner.A(n)) => 1
                Outer.Y | Outer.Z => 2
                _ => 3
            }
            return r
        }
        func main() {
            let sy: Outer = Outer.Y
            println(classify(sy))
            let sx: Outer = Outer.X(Inner.A(42))
            println(classify(sx))
        }
    )--", "match_chain_nested_enum_dispatch_order");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    // Outer.Y must fall through arm0 (its own pattern doesn't match) into
    // arm1's Or-pattern (2), NOT skip straight to the wildcard (3). A real
    // Outer.X(Inner.A(...)) subject must still take arm0 (1).
    EXPECT_EQ(r.stdout_output, "2\n1\n") << "stdout: " << r.stdout_output;
}

// Same match, with the wildcard arm removed: without a wildcard, the "no
// match" default edge is mergeBB directly — before the fix, the nested
// check's fail edge landing there was untracked in `defaultEdgePreds`,
// giving mergeBB a PHI predecessor with no incoming value ("PHINode should
// have one entry for each predecessor" verifier abort). Pins "compiles and
// runs to completion", not just the arm choice.
TEST(RuntimeExecTest, MatchChainModeNestedEnumNoWildcardNoAbort) {
    auto r = compileAndRun(R"--(
        enum Inner {
            case B
            case A(i32)
        }
        enum Outer {
            case X(Inner)
            case Y
            case Z
        }
        func classify(o: Outer) -> i32 {
            let r = match o {
                Outer.X(Inner.A(n)) => 1
                Outer.Y | Outer.Z => 2
            }
            return r
        }
        func main() {
            let sy: Outer = Outer.Y
            println(classify(sy))
        }
    )--", "match_chain_nested_enum_no_wildcard_no_abort");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_EQ(r.stdout_output, "2\n") << "stdout: " << r.stdout_output;
}

// Pattern Types (Faz B) Task 2: bool/string/float literal match arms.
// Bool literal patterns reuse the existing tag/hasTag switch-case
// machinery (bool subject -> i1 tagVal, a valid CreateSwitch condition);
// this test exercises both the `true` and `false` arms so a
// switch-case-constant width bug (i1 condition vs. a hardcoded i32 case
// constant) would show up as a build/verify failure, not just wrong output.
TEST(RuntimeExecTest, MatchBoolLiteralArms) {
    auto r = compileAndRun(R"--(
        func describe(b: bool) -> i32 {
            let r = match b {
                true => 111
                false => 222
            }
            return r
        }
        func main() {
            println(describe(true))
            println(describe(false))
        }
    )--", "match_bool_literal_arms");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_EQ(r.stdout_output, "111\n222\n") << "stdout: " << r.stdout_output;
}

// String literal match arms: a string subject's tagVal is an i8* pointer
// (not an LLVM integer), so this always takes the if-else-chain path and
// exercises the liva_str_equal-based pattern comparison plus wildcard
// fallback for an unmatched value.
TEST(RuntimeExecTest, MatchStringLiteralArms) {
    auto r = compileAndRun(R"--(
        func describe(method: string) -> i32 {
            let r = match method {
                "GET" => 111
                "POST" => 222
                _ => 333
            }
            return r
        }
        func main() {
            println(describe("GET"))
            println(describe("POST"))
            println(describe("DELETE"))
        }
    )--", "match_string_literal_arms");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_EQ(r.stdout_output, "111\n222\n333\n") << "stdout: " << r.stdout_output;
}

// Float literal match arms: a float subject's tagVal is a double (not an
// LLVM integer either), exercising the fcmp-oeq-based pattern comparison
// in the same if-else-chain path.
TEST(RuntimeExecTest, MatchFloatLiteralArms) {
    auto r = compileAndRun(R"--(
        func describe(f: f64) -> i32 {
            let r = match f {
                3.14 => 111
                _ => 222
            }
            return r
        }
        func main() {
            println(describe(3.14))
            println(describe(2.71))
        }
    )--", "match_float_literal_arms");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_EQ(r.stdout_output, "111\n222\n") << "stdout: " << r.stdout_output;
}

// === Pattern Types (Faz B) Task 3: Range patterns ===
// Ranges force the if-else-chain dispatch path (see visitMatchExpr's
// `hasRangePattern` check) rather than CreateSwitch, since a range isn't a
// single discrete tag value — it needs a `sge`+`slt`/`sle` comparison pair.

// Exclusive range (`lo..hi`): lo is included, hi is NOT.
TEST(RuntimeExecTest, MatchRangePatternExclusiveBoundary) {
    auto r = compileAndRun(R"--(
        func classify(n: i32) -> i32 {
            let r = match n {
                1..5 => 1
                _ => 0
            }
            return r
        }
        func main() {
            println(classify(1))
            println(classify(4))
            println(classify(5))
        }
    )--", "match_range_exclusive_boundary");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_EQ(r.stdout_output, "1\n1\n0\n") << "stdout: " << r.stdout_output;
}

// Inclusive range (`lo..=hi`): hi IS included.
TEST(RuntimeExecTest, MatchRangePatternInclusiveBoundary) {
    auto r = compileAndRun(R"--(
        func classify(n: i32) -> i32 {
            let r = match n {
                1..=5 => 1
                _ => 0
            }
            return r
        }
        func main() {
            println(classify(5))
            println(classify(6))
        }
    )--", "match_range_inclusive_boundary");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_EQ(r.stdout_output, "1\n0\n") << "stdout: " << r.stdout_output;
}

// Negative endpoints (`-5..=5`).
TEST(RuntimeExecTest, MatchRangePatternNegativeEndpoints) {
    auto r = compileAndRun(R"--(
        func classify(n: i32) -> i32 {
            let r = match n {
                -5..=5 => 1
                _ => 0
            }
            return r
        }
        func main() {
            println(classify(-5))
            println(classify(0))
            println(classify(5))
            println(classify(-6))
            println(classify(6))
        }
    )--", "match_range_negative_endpoints");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_EQ(r.stdout_output, "1\n1\n1\n0\n0\n") << "stdout: " << r.stdout_output;
}

// A range arm alongside a plain int-literal arm and a wildcard: exercises
// the if-else-chain generalization needed so a tag-bearing (int-literal)
// arm still gets its own equality comparison once a Range arm in the same
// match forces if-else-chain mode instead of CreateSwitch.
TEST(RuntimeExecTest, MatchRangeAlongsideLiteralAndWildcard) {
    auto r = compileAndRun(R"--(
        func classify(n: i32) -> i32 {
            let r = match n {
                0 => 100
                1..10 => 200
                _ => 300
            }
            return r
        }
        func main() {
            println(classify(0))
            println(classify(5))
            println(classify(50))
        }
    )--", "match_range_alongside_literal_and_wildcard");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_EQ(r.stdout_output, "100\n200\n300\n") << "stdout: " << r.stdout_output;
}

// A range arm combined with a guard: the guard is ANDed with the range's
// own `sge`/`slt`-`sle` comparison, exactly like the float/string literal
// pattern-plus-guard combination already exercised elsewhere.
TEST(RuntimeExecTest, MatchRangeArmWithGuard) {
    auto r = compileAndRun(R"--(
        func classify(n: i32) -> i32 {
            let r = match n {
                1..100 if n % 2 == 0 => 1
                1..100 => 2
                _ => 3
            }
            return r
        }
        func main() {
            println(classify(4))
            println(classify(5))
            println(classify(200))
        }
    )--", "match_range_arm_with_guard");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_EQ(r.stdout_output, "1\n2\n3\n") << "stdout: " << r.stdout_output;
}

// Pattern Types (Faz B) Task 3 REVIEW FIX (minor b): negative-only-hi range
// (`-10..-5`, both endpoints negative) — exercises the '-'-prefixed hi
// endpoint path in maybeParseRangePattern, not just a negative lo alongside
// a non-negative hi (already covered by MatchRangePatternNegativeEndpoints'
// `-5..=5`).
TEST(RuntimeExecTest, MatchRangeNegativeOnlyHi) {
    auto r = compileAndRun(R"--(
        func classify(n: i32) -> i32 {
            let r = match n {
                -10..-5 => 1
                _ => 0
            }
            return r
        }
        func main() {
            println(classify(-10))
            println(classify(-6))
            println(classify(-5))
            println(classify(-11))
        }
    )--", "match_range_negative_only_hi");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_EQ(r.stdout_output, "1\n1\n0\n0\n") << "stdout: " << r.stdout_output;
}

// === Pattern Types (Faz B) Task 4: Or patterns ===
// Or-patterns force the if-else-chain dispatch path (see visitMatchExpr's
// `hasOrPattern` check), same rationale as Range in Task 3.

TEST(RuntimeExecTest, MatchOrPatternIntLiterals) {
    auto r = compileAndRun(R"--(
        func classify(n: i32) -> i32 {
            let r = match n {
                1|2|3 => 1
                _ => 0
            }
            return r
        }
        func main() {
            println(classify(1))
            println(classify(2))
            println(classify(3))
            println(classify(4))
        }
    )--", "match_or_int_literals");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_EQ(r.stdout_output, "1\n1\n1\n0\n") << "stdout: " << r.stdout_output;
}

TEST(RuntimeExecTest, MatchOrPatternEnumCase) {
    auto r = compileAndRun(R"--(
        enum Color {
            case Red
            case Green
            case Blue
        }

        func rank(c: Color) -> i32 {
            let r = match c {
                Color.Red|Color.Blue => 1
                Color.Green => 2
            }
            return r
        }

        func main() {
            println(rank(Color.Red))
            println(rank(Color.Blue))
            println(rank(Color.Green))
        }
    )--", "match_or_enum_case");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_EQ(r.stdout_output, "1\n1\n2\n") << "stdout: " << r.stdout_output;
}

// Mixed range+literal or-alternatives (`1..5 | 99`): a Range alternative and
// a plain IntLiteral alternative in the same or-pattern.
TEST(RuntimeExecTest, MatchOrPatternMixedRangeAndLiteral) {
    auto r = compileAndRun(R"--(
        func classify(n: i32) -> i32 {
            let r = match n {
                1..5 | 99 => 1
                _ => 0
            }
            return r
        }
        func main() {
            println(classify(1))
            println(classify(4))
            println(classify(5))
            println(classify(99))
            println(classify(50))
        }
    )--", "match_or_mixed_range_literal");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_EQ(r.stdout_output, "1\n1\n0\n1\n0\n") << "stdout: " << r.stdout_output;
}

// An or-arm combined with a guard: the guard applies to the WHOLE arm (ANDed
// with the OR of all alternatives), not per-alternative.
TEST(RuntimeExecTest, MatchOrPatternArmWithGuard) {
    auto r = compileAndRun(R"--(
        func classify(n: i32) -> i32 {
            let r = match n {
                1|2|3 if n % 2 == 0 => 1
                1|2|3 => 2
                _ => 3
            }
            return r
        }
        func main() {
            println(classify(2))
            println(classify(1))
            println(classify(4))
        }
    )--", "match_or_arm_with_guard");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_EQ(r.stdout_output, "1\n2\n3\n") << "stdout: " << r.stdout_output;
}

// === Pattern Types (Faz B) Task 5: `@` binding patterns ===
// `n @ pattern` binds the SUBJECT value to `n` when `pattern` matches —
// mirrors the existing whole-subject Identifier-binding materialization
// (store tagVal into an alloca named `n`), just with an extra runtime
// condition (the sub-pattern's own match check) gating whether the arm is
// taken at all.

TEST(RuntimeExecTest, MatchBindingPatternRange) {
    auto r = compileAndRun(R"--(
        func classify(x: i32) -> i32 {
            let r = match x {
                n @ 1..=9 => n
                _ => -1
            }
            return r
        }
        func main() {
            println(classify(1))
            println(classify(9))
            println(classify(5))
            println(classify(10))
            println(classify(0))
        }
    )--", "match_binding_range");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_EQ(r.stdout_output, "1\n9\n5\n-1\n-1\n") << "stdout: " << r.stdout_output;
}

TEST(RuntimeExecTest, MatchBindingPatternString) {
    auto r = compileAndRun(R"--(
        func classify(method: string) -> i32 {
            let r = match method {
                s @ "GET" => 1
                _ => 0
            }
            return r
        }
        func main() {
            println(classify("GET"))
            println(classify("POST"))
        }
    )--", "match_binding_string");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_EQ(r.stdout_output, "1\n0\n") << "stdout: " << r.stdout_output;
}

// Grammar decision: `n @ 1|2|3` binds `n` whenever ANY alternative matches
// (the `@`'s RHS is the full or-pattern, not just the first alternative).
TEST(RuntimeExecTest, MatchBindingPatternOr) {
    auto r = compileAndRun(R"--(
        func classify(x: i32) -> i32 {
            let r = match x {
                n @ 1|2|3 => n
                _ => -1
            }
            return r
        }
        func main() {
            println(classify(1))
            println(classify(2))
            println(classify(3))
            println(classify(4))
        }
    )--", "match_binding_or");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_EQ(r.stdout_output, "1\n2\n3\n-1\n") << "stdout: " << r.stdout_output;
}

// `@` arm combined with a guard: the guard is ANDed with the binding's own
// sub-pattern condition, and `n` (the bound subject) is visible in BOTH the
// guard and the body.
TEST(RuntimeExecTest, MatchBindingPatternWithGuard) {
    auto r = compileAndRun(R"--(
        func classify(x: i32) -> i32 {
            let r = match x {
                n @ 1..=9 if n != 5 => n
                n @ 1..=9 => 100
                _ => -1
            }
            return r
        }
        func main() {
            println(classify(3))
            println(classify(5))
            println(classify(10))
        }
    )--", "match_binding_with_guard");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_EQ(r.stdout_output, "3\n100\n-1\n") << "stdout: " << r.stdout_output;
}

// === Pattern Types (Faz B) Task 6: tuple patterns ===
// `(p1, p2, ...)` destructures a tuple-typed subject: matches iff EVERY
// element sub-pattern matches the corresponding element value (literal
// comparison, identifier binding, wildcard ignore).

TEST(RuntimeExecTest, MatchTuplePatternLiteralPlusBinding) {
    auto r = compileAndRun(R"--(
        func classify(t: (i32, i32)) -> i32 {
            let r = match t {
                (1, x) => x
                _ => -1
            }
            return r
        }
        func main() {
            println(classify((1, 5)))
            println(classify((2, 5)))
        }
    )--", "match_tuple_literal_binding");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_EQ(r.stdout_output, "5\n-1\n") << "stdout: " << r.stdout_output;
}

TEST(RuntimeExecTest, MatchTuplePatternFullDestructure) {
    auto r = compileAndRun(R"--(
        func addPair(t: (i32, i32)) -> i32 {
            let r = match t {
                (a, b) => a + b
            }
            return r
        }
        func main() {
            println(addPair((3, 4)))
        }
    )--", "match_tuple_full_destructure");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_EQ(r.stdout_output, "7\n") << "stdout: " << r.stdout_output;
}

TEST(RuntimeExecTest, MatchTuplePatternMixedWildcardString) {
    auto r = compileAndRun(R"--(
        func classify(t: (i32, string)) -> i32 {
            let r = match t {
                (_, "s") => 1
                _ => 0
            }
            return r
        }
        func main() {
            println(classify((1, "s")))
            println(classify((1, "x")))
        }
    )--", "match_tuple_mixed_wildcard_string");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_EQ(r.stdout_output, "1\n0\n") << "stdout: " << r.stdout_output;
}

TEST(RuntimeExecTest, MatchTuplePatternGuardUsingBoundElements) {
    auto r = compileAndRun(R"--(
        func classify(t: (i32, i32)) -> i32 {
            let r = match t {
                (a, b) if a > b => 1
                (a, b) => 0
            }
            return r
        }
        func main() {
            println(classify((5, 2)))
            println(classify((2, 5)))
        }
    )--", "match_tuple_guard");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_EQ(r.stdout_output, "1\n0\n") << "stdout: " << r.stdout_output;
}

// Nested tuple pattern: `((a,b), c)` against a `((i32,i32), i32)` subject.
TEST(RuntimeExecTest, MatchTuplePatternNested) {
    auto r = compileAndRun(R"--(
        func sum3(t: ((i32, i32), i32)) -> i32 {
            let r = match t {
                ((a, b), c) => a + b + c
            }
            return r
        }
        func main() {
            println(sum3(((1, 2), 3)))
        }
    )--", "match_tuple_nested");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_EQ(r.stdout_output, "6\n") << "stdout: " << r.stdout_output;
}

TEST(RuntimeExecTest, SqliteColumnName) {
    auto r = compileAndRun(
        "import sqlite::sqlite\n"
        "func main() {\n"
        "    if let db = SqliteDB.openMemory() {\n"
        "        var d = db\n"
        "        d.exec(\"CREATE TABLE t(alpha INTEGER, beta TEXT)\")\n"
        "        if let s = d.prepare(\"SELECT alpha, beta FROM t\") {\n"
        "            var stmt = s\n"
        "            println(stmt.columnName(0))\n"
        "            println(stmt.columnName(1))\n"
        "            stmt.finalize()\n"
        "        }\n"
        "        d.close()\n"
        "    }\n"
        "}\n",
        "sqlite_column_name");
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_EQ(r.stdout_output, "alpha\nbeta\n");
}

TEST(RuntimeExecTest, SqliteBindByName) {
    auto r = compileAndRun(
        "import sqlite::sqlite\n"
        "func main() {\n"
        "    if let db = SqliteDB.openMemory() {\n"
        "        var d = db\n"
        "        d.exec(\"CREATE TABLE t(name TEXT)\")\n"
        "        if let s = d.prepare(\"INSERT INTO t(name) VALUES (:n)\") {\n"
        "            var stmt = s\n"
        "            let ok = stmt.bindByName(\":n\", \"Ada\")\n"
        "            if ok { println(\"bound\") } else { println(\"fail\") }\n"
        "            stmt.step()\n"
        "            stmt.finalize()\n"
        "        }\n"
        "        if let v = d.queryString(\"SELECT name FROM t\") { println(v) }\n"
        "        d.close()\n"
        "    }\n"
        "}\n",
        "sqlite_bind_by_name");
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_EQ(r.stdout_output, "bound\nAda\n");
}

TEST(RuntimeExecTest, SqliteColumnTypeAndNull) {
    auto r = compileAndRun(
        "import sqlite::sqlite\n"
        "func main() {\n"
        "    if let db = SqliteDB.openMemory() {\n"
        "        var d = db\n"
        "        d.exec(\"CREATE TABLE t(a INTEGER, b TEXT)\")\n"
        "        d.exec(\"INSERT INTO t(a, b) VALUES (7, NULL)\")\n"
        "        if let s = d.prepare(\"SELECT a, b FROM t\") {\n"
        "            var stmt = s\n"
        "            if stmt.step() {\n"
        "                println(stmt.columnType(0))\n"
        "                if stmt.columnIsNull(1) { println(\"null\") } else { println(\"notnull\") }\n"
        "            }\n"
        "            stmt.finalize()\n"
        "        }\n"
        "        d.close()\n"
        "    }\n"
        "}\n",
        "sqlite_column_type");
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_EQ(r.stdout_output, "1\nnull\n");
}

TEST(RuntimeExecTest, SqliteTransactionRollback) {
    auto r = compileAndRun(
        "import sqlite::sqlite\n"
        "func main() {\n"
        "    if let db = SqliteDB.openMemory() {\n"
        "        var d = db\n"
        "        d.exec(\"CREATE TABLE t(n INTEGER)\")\n"
        "        d.begin()\n"
        "        d.exec(\"INSERT INTO t(n) VALUES (1)\")\n"
        "        d.rollback()\n"
        "        d.begin()\n"
        "        d.exec(\"INSERT INTO t(n) VALUES (2)\")\n"
        "        d.commit()\n"
        "        if let c = d.queryInt(\"SELECT COUNT(*) FROM t\") { println(c) }\n"
        "        if let v = d.queryInt(\"SELECT n FROM t\") { println(v) }\n"
        "        d.close()\n"
        "    }\n"
        "}\n",
        "sqlite_txn");
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_EQ(r.stdout_output, "1\n2\n");
}

TEST(RuntimeExecTest, SqliteBlobRoundTrip) {
    auto r = compileAndRun(
        "import sqlite::sqlite\n"
        "func main() {\n"
        "    let payload: [u8] = [104, 105, 0, 1, 255]\n"
        "    if let db = SqliteDB.openMemory() {\n"
        "        var d = db\n"
        "        d.exec(\"CREATE TABLE t(b BLOB)\")\n"
        "        if let s = d.prepare(\"INSERT INTO t(b) VALUES (?)\") {\n"
        "            var stmt = s\n"
        "            stmt.bindBlob(1, payload)\n"
        "            stmt.step()\n"
        "            stmt.finalize()\n"
        "        }\n"
        "        if let s2 = d.prepare(\"SELECT b FROM t\") {\n"
        "            var stmt2 = s2\n"
        "            if stmt2.step() {\n"
        "                let got = stmt2.columnBlob(0)\n"
        "                println(got.length)\n"
        "                println(got[2])\n"
        "                println(got[4])\n"
        "            }\n"
        "            stmt2.finalize()\n"
        "        }\n"
        "        d.close()\n"
        "    }\n"
        "}\n",
        "sqlite_blob");
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_EQ(r.stdout_output, "5\n0\n255\n");
}

TEST(RuntimeExecTest, PgNormalizeParams) {
    auto r = compileAndRun(
        "import postgres::postgres\n"
        "func main() {\n"
        "    println(pgNormalizeParams(\"SELECT * FROM t WHERE a=? AND b=?\"))\n"
        "    println(pgNormalizeParams(\"INSERT INTO t VALUES (?, '?lit?', ?)\"))\n"
        "    println(pgNormalizeParams(\"no params here\"))\n"
        "    println(pgNormalizeParams(\"SELECT ? -- ? stays\"))\n"
        "    println(pgNormalizeParams(\"a=? /* ? */ b=?\"))\n"
        "    println(pgNormalizeParams(\"x=$$a?b$$ y=?\"))\n"
        "    println(pgNormalizeParams(\"$tag$?$tag$ ?\"))\n"
        "    println(pgNormalizeParams(\"SELECT $1, ?\"))\n"
        "}\n",
        "pg_normalize");
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_EQ(r.stdout_output,
        "SELECT * FROM t WHERE a=$1 AND b=$2\n"
        "INSERT INTO t VALUES ($1, '?lit?', $2)\n"
        "no params here\n"
        "SELECT $1 -- ? stays\n"
        "a=$1 /* ? */ b=$2\n"
        "x=$$a?b$$ y=$1\n"
        "$tag$?$tag$ $1\n"
        "SELECT $1, $1\n");
}

TEST(RuntimeExecTest, PgConnectFailClosed) {
    auto r = compileAndRun(
        "import postgres::postgres\n"
        "func main() {\n"
        "    if let c = PgConn.open(\"host=256.256.256.256 dbname=nope connect_timeout=1\") {\n"
        "        var conn = c\n"
        "        println(\"connected\")\n"
        "        conn.close()\n"
        "    } else {\n"
        "        println(\"failclosed\")\n"
        "    }\n"
        "}\n",
        "pg_failclosed");
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_EQ(r.stdout_output, "failclosed\n");
}

TEST(RuntimeExecTest, PgRealRoundTrip) {
    const char *conn = std::getenv("LIVA_PG_TEST_CONN");
    if (!conn) GTEST_SKIP() << "LIVA_PG_TEST_CONN not set — skipping real PG test";
    std::string src =
        "import postgres::postgres\n"
        "func main() {\n"
        "    if let c = PgConn.open(\"" + std::string(conn) + "\") {\n"
        "        var conn = c\n"
        "        conn.exec(\"DROP TABLE IF EXISTS liva_pg_test\")\n"
        "        conn.exec(\"CREATE TABLE liva_pg_test(id INT, name TEXT)\")\n"
        "        conn.exec(\"INSERT INTO liva_pg_test VALUES (1, 'Ada')\")\n"
        "        if let res = conn.query(\"SELECT name FROM liva_pg_test WHERE id = 1\") {\n"
        "            var rs = res\n"
        "            println(rs.getText(0, 0))\n"
        "            rs.clear()\n"
        "        }\n"
        "        conn.exec(\"DROP TABLE liva_pg_test\")\n"
        "        conn.close()\n"
        "    } else { println(\"noconn\") }\n"
        "}\n";
    auto r = compileAndRun(src, "pg_real");
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_EQ(r.stdout_output, "Ada\n");
}

TEST(RuntimeExecTest, DbLayerSqliteAdapter) {
    auto r = compileAndRun(
        "import db::db\n"
        "func main() {\n"
        "    if let d = SqliteDatabase.openMemory() {\n"
        "        var db = d\n"
        "        db.exec(\"CREATE TABLE u(id INTEGER, name TEXT)\")\n"
        "        db.exec(\"INSERT INTO u VALUES (1, 'Ada')\")\n"
        "        db.exec(\"INSERT INTO u VALUES (2, 'Lin')\")\n"
        "        let rows = db.query(\"SELECT id, name FROM u WHERE id > ?\", [\"0\"])\n"
        "        println(rows.length)\n"
        "        for row in rows { println(row.getText(1)) }\n"
        "        if let r0 = rows[0].byName(\"name\") { println(r0) }\n"
        "        db.close()\n"
        "    }\n"
        "}\n",
        "db_sqlite_adapter");
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_EQ(r.stdout_output, "2\nAda\nLin\nAda\n");
}

TEST(RuntimeExecTest, DbUnifiedDemo) {
    // Exercises dynamic dispatch over the Database protocol: a SqliteDatabase
    // value is passed to dump(db: dyn Database), which calls db.query() and
    // iterates rows via getInt/getText with string interpolation (\(...)).
    // This verifies the full vtable dispatch path end-to-end on real SQLite.
    auto r = compileAndRun(
        "import db::db\n"
        "func dump(db: dyn Database) {\n"
        "    let rows = db.query(\"SELECT id, name FROM users WHERE id > ?\", [\"0\"])\n"
        "    println(\"rows: \\(rows.length)\")\n"
        "    for row in rows { println(\"\\(row.getInt(0)): \\(row.getText(1))\") }\n"
        "}\n"
        "func main() {\n"
        "    if let d = SqliteDatabase.openMemory() {\n"
        "        var db = d\n"
        "        db.exec(\"CREATE TABLE users(id INTEGER PRIMARY KEY, name TEXT)\")\n"
        "        db.exec(\"INSERT INTO users(name) VALUES ('Ada')\")\n"
        "        db.exec(\"INSERT INTO users(name) VALUES ('Linus')\")\n"
        "        dump(db)\n"
        "        db.close()\n"
        "    }\n"
        "}\n",
        "db_unified_demo");
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_EQ(r.stdout_output, "rows: 2\n1: Ada\n2: Linus\n");
}

TEST(RuntimeExecTest, ConvertToBool) {
    auto r = compileAndRun(
        "import convert::convert\n"
        "func yn(b: bool) -> String { if b { return \"Y\" } return \"N\" }\n"
        "func main() {\n"
        "    println(yn(toBool(\"1\")))\n"
        "    println(yn(toBool(\"t\")))\n"
        "    println(yn(toBool(\"TRUE\")))\n"
        "    println(yn(toBool(\" yes \")))\n"
        "    println(yn(toBool(\"0\")))\n"
        "    println(yn(toBool(\"f\")))\n"
        "    println(yn(toBool(\"\")))\n"
        "}\n",
        "convert_tobool");
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_EQ(r.stdout_output, "Y\nY\nY\nY\nN\nN\nN\n");
}

TEST(RuntimeExecTest, TimeDateAndTime) {
    auto r = compileAndRun(
        "import time::time\n"
        "func main() {\n"
        "    let d = Date.parse(\"2024-06-15\")\n"
        "    println(d.year())\n"
        "    println(d.month())\n"
        "    println(d.day())\n"
        "    let t = Time.parse(\"13:45:30\")\n"
        "    println(t.second())\n"
        "}\n",
        "time_date_time");
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_EQ(r.stdout_output, "2024\n6\n15\n30\n");
}

TEST(RuntimeExecTest, SqliteTypedColumns) {
    auto r = compileAndRun(
        "import sqlite::sqlite\n"
        "func main() {\n"
        "    if let db = SqliteDB.openMemory() {\n"
        "        var d = db\n"
        "        d.exec(\"CREATE TABLE t(price REAL, active INTEGER, dt TEXT, tm TEXT, ts TEXT)\")\n"
        "        d.exec(\"INSERT INTO t VALUES (19.99, 1, '2024-06-15', '13:45:30', '2024-06-15 13:45:30')\")\n"
        "        if let s = d.prepare(\"SELECT price, active, dt, tm, ts FROM t\") {\n"
        "            var stmt = s\n"
        "            if stmt.step() {\n"
        "                if stmt.columnDouble(0) > 19.0 { println(\"price-ok\") } else { println(\"price-bad\") }\n"
        "                if stmt.columnBool(1) { println(\"active\") } else { println(\"inactive\") }\n"
        "                println(stmt.columnDate(2).year())\n"
        "                println(stmt.columnTime(3).second())\n"
        "                println(stmt.columnDateTime(4).year())\n"
        "            }\n"
        "            stmt.finalize()\n"
        "        }\n"
        "        d.close()\n"
        "    }\n"
        "}\n",
        "sqlite_typed_columns");
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_EQ(r.stdout_output, "price-ok\nactive\n2024\n30\n2024\n");
}

TEST(RuntimeExecTest, DbRowTypedAccessors) {
    auto r = compileAndRun(
        "import db::db\n"
        "func main() {\n"
        "    if let d = SqliteDatabase.openMemory() {\n"
        "        var db = d\n"
        "        db.exec(\"CREATE TABLE t(price REAL, active INTEGER, ts TEXT)\")\n"
        "        db.exec(\"INSERT INTO t VALUES (19.99, 1, '2024-06-15 13:45:30')\")\n"
        "        let noargs: [String] = []\n"
        "        let rows = db.query(\"SELECT price, active, ts FROM t\", noargs)\n"
        "        if rows.length > (0 as i64) {\n"
        "            let row = rows[0 as i64]\n"
        "            if row.getDouble(0) > 19.0 { println(\"price-ok\") } else { println(\"price-bad\") }\n"
        "            if row.getBool(1) { println(\"active\") } else { println(\"inactive\") }\n"
        "            let dt = row.getDateTime(2)\n"
        "            println(dt.year())\n"
        "        }\n"
        "        db.close()\n"
        "    }\n"
        "}\n",
        "db_row_typed");
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_EQ(r.stdout_output, "price-ok\nactive\n2024\n");
}

TEST(RuntimeExecTest, ArrayElementChainedCall) {
    auto r = compileAndRun(
        "import db::db\n"
        "func main() {\n"
        "    if let d = SqliteDatabase.openMemory() {\n"
        "        var db = d\n"
        "        db.exec(\"CREATE TABLE t(ts TEXT)\")\n"
        "        db.exec(\"INSERT INTO t VALUES ('2024-06-15 13:45:30')\")\n"
        "        let noargs: [String] = []\n"
        "        let rows = db.query(\"SELECT ts FROM t\", noargs)\n"
        "        if rows.length > (0 as i64) {\n"
        "            let row = rows[0 as i64]\n"
        "            println(row.getDateTime(0).year())\n"
        "        }\n"
        "        db.close()\n"
        "    }\n"
        "}\n",
        "array_elem_chained");
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_EQ(r.stdout_output, "2024\n");
}

TEST(RuntimeExecTest, JsonParseObjectAndSerialize) {
    std::string src = R"LIVA(
import json::json
func main() {
    let doc = Json.parse("{\"n\":7,\"name\":\"liva\",\"ok\":true}")
    println(doc.toString())
    let pretty = doc.toStringPretty(2)
    println(pretty)
    if doc.isObject() { println("isobj") }
}
)LIVA";
    auto r = compileAndRun(src, "json_scalar");
    EXPECT_NE(r.stdout_output.find("\"n\":7"), std::string::npos) << r.stdout_output;
    EXPECT_NE(r.stdout_output.find("\"name\":\"liva\""), std::string::npos) << r.stdout_output;
    EXPECT_NE(r.stdout_output.find("isobj"), std::string::npos) << r.stdout_output;
    // Pretty output: 2-space indented key with ": " separator.
    EXPECT_NE(r.stdout_output.find("  \"n\": 7"), std::string::npos) << r.stdout_output;
}

TEST(RuntimeExecTest, JsonObjectTypedRead) {
    std::string src = R"LIVA(
import json::json
func main() {
    let doc = Json.parse("{\"name\":\"liva\",\"age\":3,\"pi\":3.5,\"ok\":true,\"addr\":{\"city\":\"izmir\"}}")
    let o = doc.object()
    println(o.getString("name"))
    println(o.getInt("age"))
    println(o.getFloat("pi"))
    if o.getBool("ok") { println("ok-true") }
    if o.has("age") { println("has-age") }
    println(o.count())
    let addr = o.getObject("addr")
    println(addr.getString("city"))
}
)LIVA";
    auto r = compileAndRun(src, "json_objread");
    EXPECT_NE(r.stdout_output.find("liva"), std::string::npos) << r.stdout_output;
    EXPECT_NE(r.stdout_output.find("ok-true"), std::string::npos) << r.stdout_output;
    EXPECT_NE(r.stdout_output.find("izmir"), std::string::npos) << r.stdout_output;
}

TEST(RuntimeExecTest, JsonArrayRead) {
    std::string src = R"LIVA(
import json::json
func main() {
    let doc = Json.parse("{\"nums\":[10,20,30],\"objs\":[{\"x\":1},{\"x\":2}]}")
    let o = doc.object()
    let nums = o.getArray("nums")
    println(nums.count())
    println(nums.getInt(0 as i64))
    println(nums.getInt(2 as i64))
    let objs = o.getArray("objs")
    println(objs.getObject(1 as i64).getInt("x"))
}
)LIVA";
    auto r = compileAndRun(src, "json_arrread");
    EXPECT_NE(r.stdout_output.find("10"), std::string::npos) << r.stdout_output;
    EXPECT_NE(r.stdout_output.find("30"), std::string::npos) << r.stdout_output;
    EXPECT_NE(r.stdout_output.find("2"), std::string::npos) << r.stdout_output;
}

TEST(RuntimeExecTest, JsonSubscriptIndexer) {
    std::string src = R"LIVA(
import json::json
func main() {
    let doc = Json.parse("{\"name\":\"liva\",\"nums\":[5,6,7]}")
    let o = doc.object()
    println(o["name"].asString())
    let arr = o.getArray("nums")
    println(arr[1 as i64].asInt())
}
)LIVA";
    auto r = compileAndRun(src, "json_subscript");
    EXPECT_NE(r.stdout_output.find("liva"), std::string::npos) << r.stdout_output;
    EXPECT_NE(r.stdout_output.find("6"), std::string::npos) << r.stdout_output;
}

TEST(RuntimeExecTest, JsonObjectKeys) {
    std::string src = R"LIVA(
import json::json
func main() {
    let doc = Json.parse("{\"a\":1,\"b\":2,\"c\":3}")
    let o = doc.object()
    let ks = o.keys()
    for k in ks { println(k) }
}
)LIVA";
    auto r = compileAndRun(src, "json_keys");
    EXPECT_NE(r.stdout_output.find("a"), std::string::npos) << r.stdout_output;
    EXPECT_NE(r.stdout_output.find("b"), std::string::npos) << r.stdout_output;
    EXPECT_NE(r.stdout_output.find("c"), std::string::npos) << r.stdout_output;
}

TEST(RuntimeExecTest, JsonTryGetters) {
    std::string src = R"LIVA(
import json::json
func main() {
    let doc = Json.parse("{\"name\":\"liva\",\"age\":3,\"pi\":2.5,\"ok\":true}")
    let o = doc.object()
    if let n = o.tryString("name") { println(n) }
    if let a = o.tryInt("age") { println("int-ok") }
    if let f = o.tryFloat("pi") { println("flt-ok") }
    if let fi = o.tryFloat("age") { println("fltint-ok") }
    if let b = o.tryBool("ok") { if b { println("bool-true") } }
    if let m = o.tryString("missing") { println(m) } else { println("none") }
    if let w = o.tryInt("name") { println(w) } else { println("wrongkind") }
    if let wb = o.tryBool("age") { println("bad") } else { println("bool-none") }
}
)LIVA";
    auto r = compileAndRun(src, "json_try");
    EXPECT_NE(r.stdout_output.find("liva"), std::string::npos) << r.stdout_output;
    EXPECT_NE(r.stdout_output.find("int-ok"), std::string::npos) << r.stdout_output;
    EXPECT_NE(r.stdout_output.find("flt-ok"), std::string::npos) << r.stdout_output;
    EXPECT_NE(r.stdout_output.find("fltint-ok"), std::string::npos) << r.stdout_output;
    EXPECT_NE(r.stdout_output.find("bool-true"), std::string::npos) << r.stdout_output;
    EXPECT_NE(r.stdout_output.find("none"), std::string::npos) << r.stdout_output;
    EXPECT_NE(r.stdout_output.find("wrongkind"), std::string::npos) << r.stdout_output;
    EXPECT_NE(r.stdout_output.find("bool-none"), std::string::npos) << r.stdout_output;
    EXPECT_EQ(r.stdout_output.find("bad"), std::string::npos) << r.stdout_output;
}

TEST(RuntimeExecTest, JsonPathRead) {
    std::string src = R"LIVA(
import json::json
func main() {
    let doc = Json.parse("{\"a\":{\"b\":[{\"c\":42}]}}")
    let o = doc.object()
    let v = o.path("a.b.0.c")
    println(v.asInt())
    let missing = o.path("a.x.y")
    if missing.isNull() { println("missing-null") }
}
)LIVA";
    auto r = compileAndRun(src, "json_path");
    EXPECT_NE(r.stdout_output.find("42"), std::string::npos) << r.stdout_output;
    EXPECT_NE(r.stdout_output.find("missing-null"), std::string::npos) << r.stdout_output;
}

TEST(RuntimeExecTest, JsonBuildRoundTrip) {
    std::string src = R"LIVA(
import json::json
func main() {
    var o = Json.object()
    o.setString("name", "liva")
    o.setInt("age", 3)
    o.setBool("ok", true)
    var arr = o.setArray("tags")
    arr.addString("a")
    arr.addString("b")
    var child = o.setObject("addr")
    child.setString("city", "izmir")
    let s = o.toString()
    println(s)
    let reparsed = Json.parse(s)
    println(reparsed.object().getString("name"))
    println(reparsed.object().getObject("addr").getString("city"))
}
)LIVA";
    auto r = compileAndRun(src, "json_build");
    EXPECT_NE(r.stdout_output.find("\"name\":\"liva\""), std::string::npos) << r.stdout_output;
    EXPECT_NE(r.stdout_output.find("izmir"), std::string::npos) << r.stdout_output;
}

TEST(RuntimeExecTest, JsonSetPathAutoViv) {
    std::string src = R"LIVA(
import json::json
func main() {
    var o = Json.object()
    o.setPathString("a.b.c", "deep")
    o.setPathInt("a.b.n", 9)
    let v = o.path("a.b.c")
    println(v.asString())
    println(o.path("a.b.n").asInt())
    println(o.toString())
}
)LIVA";
    auto r = compileAndRun(src, "json_setpath");
    EXPECT_NE(r.stdout_output.find("deep"), std::string::npos) << r.stdout_output;
    EXPECT_NE(r.stdout_output.find("9"), std::string::npos) << r.stdout_output;
}

TEST(RuntimeExecTest, IntLiteralWidensToI64InBinaryOps) {
    std::string source = R"LIVA(
func main() {
    let s = "hello"
    let n = len(s) - 1
    print(toString(n))
    if len(s) > 0 {
        print("nonempty")
    }
    let idx = s.indexOf("l")
    let after = idx + 1
    print(toString(after))
}
)LIVA";
    auto r = compileAndRun(source, "int_literal_widen");
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_NE(r.stdout_output.find("4"), std::string::npos);
    EXPECT_NE(r.stdout_output.find("nonempty"), std::string::npos);
    EXPECT_NE(r.stdout_output.find("3"), std::string::npos);
}

TEST(RuntimeExecTest, UrlComponentAccessors) {
    std::string source = R"LIVA(
func main() {
    let u = "https://api.example.com:8080/v1/users?page=2#top"
    print(urlScheme(u))
    print(urlHost(u))
    print(toString(urlPort(u)))
    print(urlPath(u))
    print(urlQuery(u))
    print(urlFragment(u))
}
)LIVA";
    auto r = compileAndRun(source, "url_accessors");
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_NE(r.stdout_output.find("https"), std::string::npos);
    EXPECT_NE(r.stdout_output.find("api.example.com"), std::string::npos);
    EXPECT_NE(r.stdout_output.find("8080"), std::string::npos);
    EXPECT_NE(r.stdout_output.find("/v1/users"), std::string::npos);
    EXPECT_NE(r.stdout_output.find("page=2"), std::string::npos);
    EXPECT_NE(r.stdout_output.find("top"), std::string::npos);
}

TEST(RuntimeExecTest, UrlParseAndBuild) {
    std::string source = R"LIVA(
import net::net
func main() {
    let u = Url.parse("https://api.example.com:8080/v1/users?page=2#top")
    print(u.scheme)
    print(u.host)
    print(toString(u.port))
    print(u.path)
    print(u.query)
    print(u.fragment)
    print(u.toString())
    let u2 = Url.parse("http://localhost/api").withQuery("q", "a b").withQuery("n", "1")
    print(u2.toString())
    print(Url.encode("a b&c"))
}
)LIVA";
    auto r = compileAndRun(source, "url_parse_build");
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_NE(r.stdout_output.find("api.example.com"), std::string::npos);
    EXPECT_NE(r.stdout_output.find("8080"), std::string::npos);
    EXPECT_NE(r.stdout_output.find("page=2"), std::string::npos);
    EXPECT_NE(r.stdout_output.find("q=a%20b"), std::string::npos);
    EXPECT_NE(r.stdout_output.find("n=1"), std::string::npos);
    EXPECT_NE(r.stdout_output.find("a%20b%26c"), std::string::npos);
}

TEST(RuntimeExecTest, HttpHeaderLookupCaseInsensitive) {
    std::string source = R"LIVA(
import std::net
func main() {
    let blob = "Content-Type: application/json\r\nX-Count: 7\r\n"
    if let ct = httpHeaderLookup(blob, "content-type") {
        print(ct)
    }
    if let missing = httpHeaderLookup(blob, "Nope") {
        print("FOUND")
    } else {
        print("absent")
    }
}
)LIVA";
    auto r = compileAndRun(source, "http_header_lookup");
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_NE(r.stdout_output.find("application/json"), std::string::npos);
    EXPECT_NE(r.stdout_output.find("absent"), std::string::npos);
}

TEST(RuntimeExecTest, HttpRequestBuilderAssemblesHeaders) {
    std::string source = R"LIVA(
import http::http
func main() {
    let req = HttpRequest.post("http://x.com")
        .header("Authorization", "Bearer t")
        .json("{}")
    print(req.headers)
    print(req.body)
    print(req.method)
}
)LIVA";
    auto r = compileAndRun(source, "http_req_headers");
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_NE(r.stdout_output.find("Authorization: Bearer t"), std::string::npos);
    EXPECT_NE(r.stdout_output.find("application/json"), std::string::npos);
    EXPECT_NE(r.stdout_output.find("POST"), std::string::npos);
}

TEST(RuntimeExecTest, HttpLiveRoundTrip) {
    if (std::getenv("LIVA_HTTP_TEST") == nullptr) {
        GTEST_SKIP() << "Set LIVA_HTTP_TEST=1 to run the live HTTP round-trip test";
    }
    std::string source = R"LIVA(
import http::http
func main() {
    let resp = HttpRequest.get("https://example.com").send()
    if resp.is2xx() {
        if len(resp.text()) > 0 {
            print("OK")
        }
    }
}
)LIVA";
    auto r = compileAndRun(source, "http_live");
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_NE(r.stdout_output.find("OK"), std::string::npos);
}

TEST(RuntimeExecTest, WsNewBuiltinsCompileAndGuard) {
    std::string source = R"LIVA(
import std::websocket
func main() {
    let h = wsConnectEx("ws://127.0.0.1:1/none", "", "", 0 as i64)
    print(toString(h))
    let bytes: [u8] = [1 as u8, 2 as u8, 3 as u8]
    let sent = wsSendBinary(h, bytes)
    if sent { print("sent") } else { print("notsent") }
    let kind = wsRecvKind(h)
    print(toString(kind))
}
)LIVA";
    auto r = compileAndRun(source, "ws_new_builtins");
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_NE(r.stdout_output.find("0"), std::string::npos);
    EXPECT_NE(r.stdout_output.find("notsent"), std::string::npos);
}

TEST(RuntimeExecTest, WsLiveEchoRoundTrip) {
    if (std::getenv("LIVA_WS_TEST") == nullptr) {
        GTEST_SKIP() << "Set LIVA_WS_TEST=1 (optionally LIVA_WS_TEST_URL=wss://...) to run the live WebSocket echo test";
    }
    const char *envUrl = std::getenv("LIVA_WS_TEST_URL");
    std::string url = envUrl ? envUrl : "wss://echo.websocket.events";
    std::string source = std::string(R"LIVA(
import websocket::websocket
func main() {
    var c = WebSocket.connect(")LIVA") + url + R"LIVA(")
    if c.isOpen() {
        if c.send("ping") {
            if let m = c.recv() {
                if len(m.text()) > 0 {
                    print("OK")
                }
            }
        }
    } else {
        print("noconnect")
    }
}
)LIVA";
    auto r = compileAndRun(source, "ws_live");
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_NE(r.stdout_output.find("OK"), std::string::npos);
}

TEST(RuntimeExecTest, WsConnectFailsGracefully) {
    std::string source = R"LIVA(
import websocket::websocket
func main() {
    let ws = WebSocket.connect("ws://127.0.0.1:1/none")
    if ws.isOpen() {
        print("connected")
    } else {
        print("notopen")
    }
}
)LIVA";
    auto r = compileAndRun(source, "ws_connect_fail");
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_NE(r.stdout_output.find("notopen"), std::string::npos);
}

// === Drop/Move Tracking — Task 1: `let b = a` / `b = a` move semantics ===
//
// Drop-conforming structs get move semantics (conservative scope: ONLY Drop
// types — plain structs keep copy behavior unchanged, see
// NonDropStructCopyUnchanged below). Before the fix, `let b = a` / `b = a`
// never marked the source as moved, so BOTH `a` and `b` got dropped at scope
// exit (double-drop). The drop method prints a marker; these tests assert
// the EXACT drop count.

TEST(RuntimeExecTest, DropMoveLetNoDoubleDrop) {
    std::string source = R"LIVA(
protocol Drop {
    func drop(mut self)
}
struct Res { var id: i32 }
impl Res: Drop {
    func drop(mut self) {
        println("DROPPED")
    }
}
func main() {
    let a = Res { id: 1 }
    let b = a
    println("done")
}
)LIVA";
    auto r = compileAndRun(source, "drop_move_let");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_EQ(countOccurrences(r.stdout_output, "DROPPED"), 1)
        << "expected exactly ONE drop (b only — a was moved into b, not "
           "double-dropped); stdout: "
        << r.stdout_output;
}

TEST(RuntimeExecTest, DropMoveAssignNoDoubleDrop) {
    // `b` starts with its OWN Res value (id: 2), then `b = a` overwrites it.
    // b's ORIGINAL value (id: 2) is never dropped — that's a documented,
    // double-free-safe leak from assignment (spec point 2), not part of what
    // this test asserts. What this test asserts: after `b = a`, `a` is moved
    // into `b`, so scope exit drops ONLY the final value held by `b` (which
    // came from `a`) — not both `a` and `b`.
    std::string source = R"LIVA(
protocol Drop {
    func drop(mut self)
}
struct Res { var id: i32 }
impl Res: Drop {
    func drop(mut self) {
        println("DROPPED")
    }
}
func main() {
    let a = Res { id: 1 }
    var b = Res { id: 2 }
    b = a
    println("done")
}
)LIVA";
    auto r = compileAndRun(source, "drop_move_assign");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_EQ(countOccurrences(r.stdout_output, "DROPPED"), 1)
        << "expected exactly ONE drop (b's final value only — a was moved "
           "into b via assignment, not double-dropped; b's original value is "
           "a documented, double-free-safe leak, not counted here); stdout: "
        << r.stdout_output;
}

TEST(RuntimeExecTest, NonDropStructCopyUnchanged) {
    // Protection test: plain structs (no Drop conformance) are OUTSIDE this
    // task's scope — `let b = a` must keep copying, so `a` stays usable
    // after. Must pass BEFORE and AFTER the move-semantics fix.
    std::string source = R"LIVA(
struct Point { var x: i32 }
func main() {
    let a = Point { x: 5 }
    let b = a
    println(a.x)
    println(b.x)
}
)LIVA";
    auto r = compileAndRun(source, "nondrop_copy_unchanged");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_EQ(countOccurrences(r.stdout_output, "5"), 2)
        << "both a.x and b.x must print 5 — plain struct copy unaffected; "
           "stdout: "
        << r.stdout_output;
}

// === Drop/Move Tracking — Task 2: Optional<Drop> scope drop + if-let/
// while-let ownership transfer ===
//
// Task 0 finding: Optional<Named> variables ARE registered in varStructTypes
// (IRGenDecl.cpp:1091-1094) and emitScopeCleanup's unconditional loop called
// T_drop with the WRONG pointer (the Optional wrapper's address, not the
// payload GEP) whenever dropImplementors_ recognized the inner type — even
// when nil. These tests pin the corrected behavior: conditional
// has-value-gated payload drop, exactly once per value path.

TEST(RuntimeExecTest, OptionalDropFiresOnScopeExit) {
    std::string source = R"LIVA(
protocol Drop {
    func drop(mut self)
}
struct Res { var id: i32 }
impl Res: Drop {
    func drop(mut self) {
        println("DROPPED")
    }
}
func main() {
    let x: Res? = Res { id: 1 }
    println("done")
}
)LIVA";
    auto r = compileAndRun(source, "opt_drop_scope_exit");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_EQ(countOccurrences(r.stdout_output, "DROPPED"), 1)
        << "Optional<Res> holding a value must drop its payload exactly once "
           "at scope exit; stdout: "
        << r.stdout_output;
}

TEST(RuntimeExecTest, OptionalDropNilNoDrop) {
    std::string source = R"LIVA(
protocol Drop {
    func drop(mut self)
}
struct Res { var id: i32 }
impl Res: Drop {
    func drop(mut self) {
        println("DROPPED")
    }
}
func main() {
    let x: Res? = nil
    println("done")
}
)LIVA";
    auto r = compileAndRun(source, "opt_drop_nil");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_EQ(countOccurrences(r.stdout_output, "DROPPED"), 0)
        << "nil Optional<Res> must NOT call drop (no payload, and — pre-fix —"
           " the wrong-pointer bug fired unconditionally even for nil); "
           "stdout: "
        << r.stdout_output;
}

TEST(RuntimeExecTest, IfLetBindingSingleDrop) {
    std::string source = R"LIVA(
protocol Drop {
    func drop(mut self)
}
struct Res { var id: i32 }
impl Res: Drop {
    func drop(mut self) {
        println("DROPPED")
    }
}
func main() {
    let x: Res? = Res { id: 1 }
    if let v = x {
        println(v.id)
    }
    println("done")
}
)LIVA";
    auto r = compileAndRun(source, "iflet_binding_single_drop");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_EQ(countOccurrences(r.stdout_output, "DROPPED"), 1)
        << "if-let binding takes ownership of the payload — exactly one drop "
           "total (binding's, not the source's — no double-drop); stdout: "
        << r.stdout_output;
}

TEST(RuntimeExecTest, IfLetNilPathNoDrop) {
    std::string source = R"LIVA(
protocol Drop {
    func drop(mut self)
}
struct Res { var id: i32 }
impl Res: Drop {
    func drop(mut self) {
        println("DROPPED")
    }
}
func main() {
    let x: Res? = nil
    if let v = x {
        println(v.id)
    } else {
        println("none")
    }
    println("done")
}
)LIVA";
    auto r = compileAndRun(source, "iflet_nil_no_drop");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_EQ(countOccurrences(r.stdout_output, "DROPPED"), 0)
        << "nil source: if-body must be skipped and nothing dropped; stdout: "
        << r.stdout_output;
    EXPECT_NE(r.stdout_output.find("none"), std::string::npos)
        << "else branch must run for the nil path; stdout: " << r.stdout_output;
}

TEST(RuntimeExecTest, OptionalNonDropNoSpuriousDrop) {
    // Protection test pinning the wrong-pointer bug's removal for a
    // NON-Drop-conforming Optional<Named> payload. Box has a String field —
    // pre-fix, emitScopeCleanup's unconditional varStructTypes loop (since
    // dropImplementors_ does NOT recognize Box) fell through to
    // emitStructFieldCleanup(name, "Box"), GEP'ing into the Optional
    // wrapper's alloca ({i1 hasVal, ptr label}) AS IF it were Box's own
    // layout ({ptr label}) — field 0 of Box is the label ptr, but field 0 of
    // the wrapper is the 1-byte hasVal bool, so it read garbage bits as a
    // pointer and called free() on them. Post-fix: Optional<Named>-registered
    // names are fully excluded from that loop (no drop, no field-cleanup) —
    // matches spec point 5 (non-Drop Optionals: unchanged/no cleanup).
    std::string source = R"LIVA(
struct Box { var label: String }
func main() {
    let b: Box? = Box { label: "hi" }
    if let v = b {
        println(v.label)
    }
    println("done")
}
)LIVA";
    auto r = compileAndRun(source, "opt_nondrop_no_spurious_drop");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_NE(r.stdout_output.find("hi"), std::string::npos)
        << "stdout: " << r.stdout_output;
    EXPECT_NE(r.stdout_output.find("done"), std::string::npos)
        << "stdout: " << r.stdout_output;
}

TEST(RuntimeExecTest, WhileLetSingleDropPerIteration) {
    // Decision (#3, while-let): per-iteration ownership — the binding owns
    // the payload for the duration of its loop iteration and is dropped once
    // per pass through the loop body (symmetric with if-let's single-drop
    // rule, applied N times for N iterations). Uses a direct method-call
    // optional source (re-evaluated fresh in condBB every iteration) rather
    // than an identifier reassigned inside the loop body — a plain
    // `cur = g.next()` reassignment of an Optional<Named-struct> variable
    // hits an unrelated, pre-existing IRGen assignment bug (confirmed with a
    // plain `while` loop, no while-let/Drop involved: reassigning such a
    // variable yields a stale/shifted value on the NEXT read, independent of
    // this task). Exercising the call-expr form here sidesteps that bug
    // entirely (no assignment to an Optional<Named> var is involved) and
    // additionally requires closing a narrow, separate TypeChecker gap:
    // visitWhileLetStmt had no resolved-type fallback for non-identifier
    // optional sources (if-let already has one) — without it, member access
    // on the binding failed to resolve for `while let x = someCall()`.
    std::string source = R"LIVA(
protocol Drop {
    func drop(mut self)
}
struct Res { var id: i32 }
impl Res: Drop {
    func drop(mut self) {
        println("DROPPED")
    }
}
struct Gen { var n: i32 }
impl Gen {
    func next(mut self) -> Res? {
        if self.n <= 0 {
            return nil
        }
        self.n = self.n - 1
        return Res { id: self.n }
    }
}
func main() {
    var g = Gen { n: 3 }
    while let v = g.next() {
        println(v.id)
    }
    println("done")
}
)LIVA";
    auto r = compileAndRun(source, "whilelet_single_drop_per_iter");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_EQ(countOccurrences(r.stdout_output, "DROPPED"), 3)
        << "expected exactly 3 drops — one per loop iteration (n=3); stdout: "
        << r.stdout_output;
}

// Reviewer-caught CRITICAL: stale-map type confusion on variable-name reuse.
// vars_.varOptionalTypes/varStructTypes/movedVars are function-flat — a
// plain `if` block gives its locals NO lexical-scope save/restore (only
// closures/monomorphization/if-let/while-let snapshot vars_). After
// `if true { let x: Res? = ... }`, a later SIBLING `let x = Res{...}`
// (plain, non-Optional, same name) leaves the stale varOptionalTypes["x"]
// entry from the first declaration in place — the conditional-drop loop
// then GEPs the PLAIN Res alloca (from the second declaration) as if it
// were the {i1, Res} Optional wrapper, computing an out-of-bounds payload
// pointer and firing a stray, type-confused Res_drop call.
//
// Achievable/correct expected behavior given this codebase's existing
// architecture: cleanup (emitScopeCleanup) only runs at function-level
// events (return / natural function end), never at a plain `if` block's
// closing brace — so the FIRST (Optional) `x`, once shadowed by the SECOND
// (plain) `x`, is no longer reachable via vars_'s name-keyed maps at all.
// This exact "shadowing loses track of the shadowed variable's cleanup
// obligation" leak is PRE-EXISTING and applies uniformly to ALL
// Drop-conforming locals (verified independently with two plain, non-
// Optional shadowed `Res` locals: only ONE "DROPPED" fires, for the second
// declaration) — not a regression introduced by this fix. What the fix
// actually guarantees: no type confusion, no out-of-bounds GEP, no stray/
// misplaced drop call — exactly one CORRECT drop, for the plain `x`, at
// the right point (function end, after all of main's prints).
TEST(RuntimeExecTest, OptionalShadowingNoTypeConfusedDrop) {
    std::string source = R"LIVA(
protocol Drop {
    func drop(mut self)
}
struct Res { var id: i32 }
impl Res: Drop {
    func drop(mut self) {
        println("DROPPED")
    }
}
func main() {
    if true {
        let x: Res? = Res { id: 1 }
        println("first block")
    }
    let x = Res { id: 0 }
    println(x.id)
    println("done")
}
)LIVA";
    auto r = compileAndRun(source, "opt_shadowing_no_type_confusion");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    // Exactly one drop, and it must come AFTER "done" (function end) — not
    // interleaved/duplicated by a type-confused GEP into the wrong alloca.
    EXPECT_EQ(countOccurrences(r.stdout_output, "DROPPED"), 1)
        << "expected exactly one drop (the plain x, correctly, at function "
           "end) — no type-confused stray drop from the shadowed Optional "
           "x's stale map entry; stdout: "
        << r.stdout_output;
    std::string expected = "first block\n0\ndone\nDROPPED\n";
    EXPECT_EQ(r.stdout_output, expected)
        << "exact sequence must be: first block, 0, done, DROPPED (in that "
           "order) — stdout: "
        << r.stdout_output;
}

// === Final whole-branch review findings (fix/drop-move-tracking) ===
//
// CRITICAL 1: assigning a Drop-conforming identifier into an Optional-typed
// target (`y = a` where `y: Res?`) took the Optional-target branch in
// IRGenCall.cpp's visitAssignExpr, which `return`ed BEFORE the movedVars
// hook that marks the RHS identifier moved (that hook lived further down,
// guarding the plain-identifier-target branch only). Sema's OwnershipChecker
// DOES mark `a` moved (its gate is target-SHAPE-agnostic), so the program is
// accepted — but IRGen never suppressed `a`'s own scope-exit drop, so both
// `a` and `y` (holding a byte-copy of the same payload) dropped it.
TEST(RuntimeExecTest, OptionalTargetAssignMoveNoDoubleDrop) {
    std::string source = R"LIVA(
protocol Drop {
    func drop(mut self)
}
struct Res { var id: i32 }
impl Res: Drop {
    func drop(mut self) {
        println("DROP")
        println(self.id)
    }
}
func main() {
    let a = Res { id: 5 }
    var y: Res? = nil
    y = a
    println("done")
}
)LIVA";
    auto r = compileAndRun(source, "opt_target_assign_move");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_EQ(countOccurrences(r.stdout_output, "DROP"), 1)
        << "expected exactly ONE drop — `a` is moved into `y` via the "
           "Optional-target assignment; y's conditional scope-exit drop "
           "fires once, `a`'s own drop must be suppressed; stdout: "
        << r.stdout_output;
    EXPECT_EQ(r.stdout_output, "done\nDROP\n5\n")
        << "exact sequence: done (a moved into y silently), then y's single "
           "conditional drop at function end (id 5); stdout: "
        << r.stdout_output;
}

// CRITICAL 2: vars_.movedVars is function-flat (no lexical-scope save/
// restore for plain blocks). If-let/while-let DID snapshot/restore
// namedValues/varStructTypes/varFileTypes/varDynArrayTypes across the bound
// body, but NOT movedVars. VarDecl hygiene (IRGenDecl.cpp) erases
// movedVars[name] on every redeclaration of `name` — so a `let a = ...`
// INSIDE an if-let/while-let body that happens to reuse an outer, already-
// moved-from name `a` erases the outer suppression. Once the if-let/while-
// let body exits and namedValues/varStructTypes are restored to refer to the
// OUTER `a` again, the (erased) movedVars no longer suppresses it — the
// outer, already-moved `a` becomes drop-eligible again: resurrected,
// double-dropped alongside its rightful new owner.
TEST(RuntimeExecTest, IfLetBodyRedeclarationDoesNotResurrectOuterMovedDrop) {
    std::string source = R"LIVA(
protocol Drop {
    func drop(mut self)
}
struct Res { var id: i32 }
impl Res: Drop {
    func drop(mut self) {
        println("DROP")
        println(self.id)
    }
}

func main() {
    let a = Res { id: 1 }
    let b = a
    let opt: Res? = Res { id: 9 }
    if let v = opt {
        let a = Res { id: 2 }
        println("inner")
    }
    println("done")
}
)LIVA";
    auto r = compileAndRun(source, "iflet_redecl_no_resurrect");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    // The outer pair (a moved into b) must drop exactly once — for b, at
    // function end. (v's own payload, id 9, drops separately and correctly
    // at the if-let body's normal exit — unrelated to this bug. The inner
    // shadowed `a`, id 2, is not reachable via vars_'s name-keyed maps once
    // the if-let body's snapshot is restored — pre-existing, architecture-
    // wide "shadowing loses the shadowed variable's drop obligation" gap,
    // documented in Known Limitations; not fixed by this task.)
    EXPECT_EQ(countOccurrences(r.stdout_output, "DROP\n1"), 1)
        << "expected exactly ONE drop of id 1 (b's value, moved from a) — "
           "no resurrection/double-drop of the outer moved-from `a`; stdout: "
        << r.stdout_output;
    EXPECT_EQ(r.stdout_output, "inner\nDROP\n9\ndone\nDROP\n1\n")
        << "exact sequence: inner (body), v's own drop (id 9, if-let single-"
           "drop rule), done, b's drop (id 1, exactly once) — stdout: "
        << r.stdout_output;
}

// CRITICAL 3: the if-let/while-let "moved" mark on an identifier source is
// COMPILE-TIME ONLY — the runtime hasVal flag on the source variable is
// never cleared. Re-entering the SAME if-let/while-let code path at runtime
// over the SAME source identifier (a while loop containing an if-let, or a
// while-let whose condition re-checks the same identifier every iteration)
// re-reads and re-drops the same payload every pass, and — for a bare
// while-let over an identifier with no other exit — makes the loop
// non-terminating (hasVal never becomes false).
TEST(RuntimeExecTest, IfLetInsideLoopOverSameIdentifierSingleDrop) {
    std::string source = R"LIVA(
protocol Drop {
    func drop(mut self)
}
struct Res { var id: i32 }
impl Res: Drop {
    func drop(mut self) {
        println("DROPPED")
    }
}

func main() {
    let x: Res? = Res { id: 1 }
    var i = 0
    while i < 2 {
        if let v = x {
            println(v.id)
        }
        i = i + 1
    }
    println("done")
}
)LIVA";
    auto r = compileAndRun(source, "iflet_loop_identifier_single_drop");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_EQ(countOccurrences(r.stdout_output, "DROPPED"), 1)
        << "expected exactly ONE drop — the first loop pass takes real "
           "ownership (source's hasVal cleared at runtime), the second pass "
           "must see no value; stdout: " << r.stdout_output;
    EXPECT_EQ(r.stdout_output, "1\nDROPPED\ndone\n")
        << "exact sequence: first iteration unwraps+drops, second iteration "
           "sees hasVal=false and is skipped entirely; stdout: "
        << r.stdout_output;
}

TEST(RuntimeExecTest, WhileLetOverIdentifierTerminatesWithSingleDrop) {
    // Without the fix, `x`'s hasVal never clears, so every iteration
    // re-unwraps + re-drops the SAME payload, and the loop only terminates
    // because of the explicit `break` safety valve (n > 2) — not because
    // the optional ran out. With the fix, the first iteration consumes `x`
    // for real (hasVal cleared at runtime): the SECOND cond check sees no
    // value and exits the loop naturally, `break` is never reached.
    std::string source = R"LIVA(
protocol Drop {
    func drop(mut self)
}
struct Res { var id: i32 }
impl Res: Drop {
    func drop(mut self) {
        println("DROP")
        println(self.id)
    }
}

func main() {
    let x: Res? = Res { id: 1 }
    var n = 0
    while let v = x {
        n = n + 1
        if n > 2 {
            break
        }
    }
    println("done")
}
)LIVA";
    auto r = compileAndRun(source, "whilelet_identifier_terminates_single_drop");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_EQ(countOccurrences(r.stdout_output, "DROP"), 1)
        << "expected exactly ONE drop — the loop must terminate via the "
           "source's runtime-cleared hasVal on iteration 2, not via the "
           "n>2 break safety valve; stdout: " << r.stdout_output;
    EXPECT_EQ(r.stdout_output, "DROP\n1\ndone\n")
        << "exact sequence: one iteration (drop fires at its normal "
           "fallthrough), then the loop exits on the next cond check — "
           "stdout: " << r.stdout_output;
}

// IMPORTANT 5: the if-let/while-let binding's own fallthrough-drop guard
// (`!vars_.movedVars.count(bindingName)`) has no equivalent to VarDecl's
// redeclaration hygiene (IRGenDecl.cpp erases movedVars[name] on every
// VarDecl of `name`). If the binding's name happens to collide with an
// OUTER variable that was already moved-from, the stale movedVars entry
// wrongly suppresses the BINDING's own (unrelated) drop — a leak.
TEST(RuntimeExecTest, IfLetBindingNameReusingMovedOuterNameStillDrops) {
    std::string source = R"LIVA(
protocol Drop {
    func drop(mut self)
}
struct Res { var id: i32 }
impl Res: Drop {
    func drop(mut self) {
        println("DROP")
        println(self.id)
    }
}
func main() {
    let v = Res { id: 1 }
    let w = v
    let opt: Res? = Res { id: 2 }
    if let v = opt {
        println(v.id)
    }
    println("done")
}
)LIVA";
    auto r = compileAndRun(source, "iflet_binding_reuses_moved_outer_name");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_EQ(countOccurrences(r.stdout_output, "DROP\n2"), 1)
        << "expected the if-let binding `v` (opt's payload, id 2) to drop "
           "at its own body's normal exit — must not be suppressed by the "
           "unrelated, stale movedVars entry for the outer `v` (moved into "
           "w before the if-let); stdout: " << r.stdout_output;
    EXPECT_EQ(countOccurrences(r.stdout_output, "DROP\n1"), 1)
        << "expected the outer pair (v moved into w) to still drop exactly "
           "once, for w, at function end — the fix must not disturb this; "
           "stdout: " << r.stdout_output;
    EXPECT_EQ(r.stdout_output, "2\nDROP\n2\ndone\nDROP\n1\n")
        << "exact sequence: body prints v.id (2), binding's own drop (2), "
           "done, w's drop (1) — stdout: " << r.stdout_output;
}

// IMPORTANT 4(b) — documented, NOT fixed by this task (protection/pinning
// test): `let b = a` where `a: Res?` (Optional<Drop>) with NO type
// annotation on `b` takes IRGenDecl's untyped-init "Fallback" VarDecl branch.
// That branch registers `b` in vars_.varOptionalTypes (from the runtime
// struct shape) but NEVER in vars_.varStructTypes — because the "Register
// struct/enum type" section only matches a Sema-resolved NAMED type or an
// LLVM-type-identity match against the PLAIN struct type, and `a`'s
// resolved type is Optional<Res> (not Named), and its runtime LLVM type is
// the Optional WRAPPER struct (not %Res) — neither matches. Since
// emitScopeCleanup's conditional Optional-drop loop only considers names
// present in varStructTypes, `b` is invisible to it: `b` never drops. `a`
// (the source) IS registered in varStructTypes (from its OWN explicitly-
// annotated `let a: Res? = ...` declaration) and gets marked moved by the
// same-shape move hook — so `a`'s drop is correctly suppressed too. Net
// result: NEITHER `a` nor `b` drops — a leak (safe, not a double-free), but
// dishonest if undocumented. See docs/en/LANGUAGE-REFERENCE.md Known
// Limitations.
TEST(RuntimeExecTest, OptionalCopyWithNoAnnotationLeaksNeitherDropsDocumented) {
    std::string source = R"LIVA(
protocol Drop {
    func drop(mut self)
}
struct Res { var id: i32 }
impl Res: Drop {
    func drop(mut self) {
        println("DROPPED")
    }
}

func main() {
    let x: Res? = Res { id: 1 }
    let b = x
    println("done")
}
)LIVA";
    auto r = compileAndRun(source, "opt_copy_no_annotation_leak_documented");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_EQ(countOccurrences(r.stdout_output, "DROPPED"), 0)
        << "documented pre-existing gap (Known Limitations): an untyped "
           "`let b = a` copy of an Optional<Drop> variable currently drops "
           "NEITHER a nor b (leak, not a double-free) — this test pins that "
           "honestly-documented behavior, not a fix; stdout: "
        << r.stdout_output;
}

// ============================================================
// `??` general-LHS support (roadmap #5) — nil-coalesce-general
// ============================================================
// emitNilCoalesce's fallback path (any LHS shape that is neither a plain
// identifier nor an optional-chain MemberExpr — e.g. a CallExpr, or the
// outer node of a chained `a ?? b ?? c`) used to be a compile-time C++
// ternary (`return lhs ? lhs : visit(rhs)`), which ALWAYS took the `lhs`
// branch (an llvm::Value* is a non-null C++ pointer whenever codegen
// succeeded) and therefore never unwrapped the Optional wrapper struct nor
// evaluated the RHS at all. These tests pin the fix: LHS is evaluated once,
// detected as an Optional wrapper via pointer-identity lookup against
// optionalTypes_, and correctly branched/unwrapped through the shared
// emitOptionalCoalesce helper.

TEST(RuntimeExecTest, NilCoalesceCallLHS) {
    auto r = compileAndRun(R"--(
        func maybe(x: i32) -> i32? {
            if x > 0 { return x }
            return nil
        }
        func main() {
            let v1 = maybe(5) ?? 99
            println(v1)
            let v2 = maybe(-1) ?? 99
            println(v2)
        }
    )--", "coal_call_lhs");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_EQ(r.stdout_output, "5\n99\n") << "stdout: " << r.stdout_output;
}

TEST(RuntimeExecTest, NilCoalesceRHSLazy) {
    // RHS has a visible side effect (println). It must run ONLY on the nil
    // path, exactly once, and must NOT run on the valued path.
    auto r = compileAndRun(R"--(
        func maybe(x: i32) -> i32? {
            if x > 0 { return x }
            return nil
        }
        func loudDefault() -> i32 {
            println("SIDEEFFECT")
            return 42
        }
        func main() {
            let v1 = maybe(5) ?? loudDefault()
            println(v1)
            let v2 = maybe(-1) ?? loudDefault()
            println(v2)
        }
    )--", "coal_rhs_lazy");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_EQ(countOccurrences(r.stdout_output, "SIDEEFFECT"), 1)
        << "RHS side effect must fire exactly once (nil path only); stdout: "
        << r.stdout_output;
    EXPECT_EQ(r.stdout_output, "5\nSIDEEFFECT\n42\n") << "stdout: " << r.stdout_output;
}

TEST(RuntimeExecTest, NilCoalesceChained) {
    // a ?? b ?? c ; with right-assoc parsing this is a ?? (b ?? c), so each
    // stage's LHS stays Optional and the shared unwrap logic applies at
    // every level. Covers all three "who has the value" cases.
    auto r = compileAndRun(R"--(
        func main() {
            let a1: i32? = 1
            let b1: i32? = 2
            let v1 = a1 ?? b1 ?? 3
            println(v1)

            let a2: i32? = nil
            let b2: i32? = 2
            let v2 = a2 ?? b2 ?? 3
            println(v2)

            let a3: i32? = nil
            let b3: i32? = nil
            let v3 = a3 ?? b3 ?? 3
            println(v3)
        }
    )--", "coal_chained");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_EQ(r.stdout_output, "1\n2\n3\n") << "stdout: " << r.stdout_output;
}

TEST(RuntimeExecTest, NilCoalesceCallChained) {
    // Task 0 probe 4 shape: maybeA() ?? maybeB() ?? 77, both nil.
    auto r = compileAndRun(R"--(
        func maybeA() -> i32? { return nil }
        func maybeB() -> i32? { return nil }
        func main() {
            let v = maybeA() ?? maybeB() ?? 77
            println(v)
        }
    )--", "coal_call_chained");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_EQ(r.stdout_output, "77\n") << "stdout: " << r.stdout_output;
}

TEST(RuntimeExecTest, NilCoalesceSubscriptLHS) {
    // A struct-defined `subscript` method returning Optional<T> makes
    // `obj[i] ?? default` expressible: IndexExpr codegen (visitIndexExpr)
    // returns the raw call result (the Optional<T> wrapper struct value,
    // same shape as a CallExpr result), which lands in the SAME general
    // fallback path as NilCoalesceCallLHS above.
    auto r = compileAndRun(R"--(
        struct Bag {
            var n: i32
        }
        impl Bag {
            func subscript(ref self, key: i32) -> i32? {
                if key == 1 { return self.n }
                return nil
            }
        }
        func main() {
            let b = Bag { n: 5 }
            let v1 = b[1] ?? 99
            println(v1)
            let v2 = b[2] ?? 99
            println(v2)
        }
    )--", "coal_subscript_lhs");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_EQ(r.stdout_output, "5\n99\n") << "stdout: " << r.stdout_output;
}

TEST(RuntimeExecTest, NilCoalesceIdentifierRegression) {
    // Pre-existing correct path (plain identifier LHS) must keep working.
    auto r = compileAndRun(R"--(
        func mk(x: i32) -> i32? {
            if x == 0 { return nil }
            return x
        }
        func main() {
            let a = mk(0)
            let v1 = a ?? 10
            println(v1)
            let b = mk(7)
            let v2 = b ?? 10
            println(v2)
        }
    )--", "coal_identifier_regression");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_EQ(r.stdout_output, "10\n7\n") << "stdout: " << r.stdout_output;
}

TEST(RuntimeExecTest, NilCoalesceMemberChainRegression) {
    // Pre-existing correct path (MemberExpr optional-chain LHS) must keep
    // working: p?.x ?? 0.
    auto r = compileAndRun(R"--(
        struct Point { var x: i32 }
        func main() {
            let p: Point? = Point { x: 9 }
            let v1 = p?.x ?? 0
            println(v1)
            let q: Point? = nil
            let v2 = q?.x ?? 0
            println(v2)
        }
    )--", "coal_member_chain_regression");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_EQ(r.stdout_output, "9\n0\n") << "stdout: " << r.stdout_output;
}

// ============================================================
// Monomorphized function/method per-function return-state setup
// ============================================================
// monomorphize()/monomorphizeMethod() used to skip the per-function
// return-codegen state that visitFuncDecl establishes before visiting a
// body (currentFuncOptionalInner_, currentIsAsync_, coroutine pointers).
// Three visible failure modes, all "LLVM module verification failed":
//   1. a monomorphized `-> T?` body never wraps its return value in the
//      Optional struct (ret i32 from a function returning {i1, i32});
//   2. the flag LEAKS from an Optional-returning caller into a mono body
//      generated mid-call, wrapping a plain-T return with the caller's
//      Optional type (Invalid InsertValueInst);
//   3. an async caller's coro promise/final-block leak the same way, so
//      the mono body stores/branches into ANOTHER function.

TEST(RuntimeExecTest, GenericMethodOptionalReturn) {
    // Roadmap 2.3 minimal repro: impl<T> method returning T? (i32).
    auto r = compileAndRun(R"--(
        struct Box<T> {
            var val: T
        }
        impl<T> Box<T> {
            func make(v: T) -> Box<T> {
                return Box { val: v }
            }
            func peek(ref self, ok: bool) -> T? {
                if ok { return self.val }
                return nil
            }
        }
        func main() {
            let b = Box.make(42)
            if let v = b.peek(true) {
                println("got \(v)")
            }
            if let w = b.peek(false) {
                println("unexpected \(w)")
            } else {
                println("empty")
            }
        }
    )--", "mono_opt_return_i32");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_EQ(r.stdout_output, "got 42\nempty\n") << "stdout: " << r.stdout_output;
}

TEST(RuntimeExecTest, GenericMethodOptionalReturnString) {
    // Same shape with T = string (different Optional payload representation).
    auto r = compileAndRun(R"--(
        struct Box<T> {
            var val: T
        }
        impl<T> Box<T> {
            func make(v: T) -> Box<T> {
                return Box { val: v }
            }
            func peek(ref self) -> T? {
                return self.val
            }
        }
        func main() {
            let b = Box.make("hello")
            if let v = b.peek() {
                println(v)
            }
        }
    )--", "mono_opt_return_str");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_EQ(r.stdout_output, "hello\n") << "stdout: " << r.stdout_output;
}

TEST(RuntimeExecTest, GenericFreeFuncOptionalReturn) {
    // Free generic function returning T? goes through monomorphize(), the
    // sibling of monomorphizeMethod() with the same missing-state gap.
    auto r = compileAndRun(R"--(
        func pick<T>(v: T, ok: bool) -> T? {
            if ok { return v }
            return nil
        }
        func main() {
            let a = pick(7, true) ?? -1
            println(a)
            let b = pick(7, false) ?? -1
            println(b)
        }
    )--", "mono_opt_return_free");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_EQ(r.stdout_output, "7\n-1\n") << "stdout: " << r.stdout_output;
}

TEST(RuntimeExecTest, MonoBodyInsideOptionalCallerNoStateLeak) {
    // The mono body is generated MID-CALL while emitting `wrapper`'s body,
    // whose currentFuncOptionalInner_ is i32?. Without save/reset, both
    // Box_make (returns Box_i32) and Box_get (returns i32) get their plain
    // return values wrapped with the CALLER's Optional type.
    auto r = compileAndRun(R"--(
        struct Box<T> {
            var val: T
        }
        impl<T> Box<T> {
            func make(v: T) -> Box<T> {
                return Box { val: v }
            }
            func get(ref self) -> T {
                return self.val
            }
        }
        func wrapper(x: i32) -> i32? {
            let b = Box.make(x)
            let v = b.get()
            if v > 0 { return v }
            return nil
        }
        func main() {
            if let r = wrapper(7) {
                println("got \(r)")
            }
        }
    )--", "mono_no_opt_state_leak");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_EQ(r.stdout_output, "got 7\n") << "stdout: " << r.stdout_output;
}

TEST(RuntimeExecTest, MonoBodyInsideAsyncCallerNoStateLeak) {
    // Async caller: without save/reset the mono body stores its return
    // value into the CALLER's coro promise and branches to the caller's
    // coro.final block — cross-function references, verifier error.
    auto r = compileAndRun(R"--(
        struct Box<T> {
            var val: T
        }
        impl<T> Box<T> {
            func make(v: T) -> Box<T> {
                return Box { val: v }
            }
            func get(ref self) -> T {
                return self.val
            }
        }
        async func compute(x: i32) -> i32 {
            let b = Box.make(x)
            return b.get() + 1
        }
        async func main() {
            let r = await compute(41)
            println("got \(r)")
        }
    )--", "mono_no_async_state_leak");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_EQ(r.stdout_output, "got 42\n") << "stdout: " << r.stdout_output;
}

// ============================================================
// DynArray pop() returns the popped element
// ============================================================
// IRGen used to lower `.pop()` (both on local [T] variables and on struct
// member fields) as a void length-decrement, silently discarding the
// element — `return self.items.pop()` produced `ret void` in a value-
// returning function. The LSP (`pop() -> T`) and the DAP interpreter both
// already treated pop as value-returning; IRGen was the odd one out.

TEST(RuntimeExecTest, DynArrayPopReturnsElement) {
    auto r = compileAndRun(R"--(
        func main() {
            var arr: [i32] = [10, 20, 30]
            let x = arr.pop()
            println(x)
            println(arr.length)
            // statement position must keep working (value simply discarded)
            arr.pop()
            println(arr.length)
        }
    )--", "dynarray_pop_value");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_EQ(r.stdout_output, "30\n2\n1\n") << "stdout: " << r.stdout_output;
}

TEST(RuntimeExecTest, DynArrayMemberFieldPopReturnsElement) {
    // Non-generic struct: pop on a member [i32] field, returned from a
    // value-returning method (the exact shape the cookbook Stack uses,
    // minus generics).
    auto r = compileAndRun(R"--(
        struct Holder {
            var items: [i32]
        }
        impl Holder {
            func take(ref mut self) -> i32 {
                return self.items.pop()
            }
        }
        func main() {
            var h = Holder { items: [1, 2, 3] }
            println(h.take())
            println(h.take())
        }
    )--", "dynarray_member_pop_value");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_EQ(r.stdout_output, "3\n2\n") << "stdout: " << r.stdout_output;
}

TEST(RuntimeExecTest, CookbookGenericStackPop) {
    // COOKBOOK §6 Generic Container: pop() -> T? consumed by while-let.
    // Needs BOTH fixes on this branch: the mono return state above AND
    // DynArray pop() returning the popped element (it used to be void).
    // T is inferred from new(first:)'s argument — an ARGLESS static
    // `Stack.new()` never resolves T and miscompiles silently (tracked in
    // roadmap 2.3), so the cookbook example seeds the stack instead.
    auto r = compileAndRun(R"--(
        struct Stack<T> {
            var items: [T]
        }
        impl<T> Stack<T> {
            func new(first: T) -> Stack<T> {
                var s = Stack { items: [] }
                s.items.push(first)
                return s
            }
            func push(ref mut self, item: T) {
                self.items.push(item)
            }
            func pop(ref mut self) -> T? {
                if self.items.length == 0 {
                    return nil
                }
                return self.items.pop()
            }
        }
        func main() {
            var s = Stack.new(10)
            s.push(20)
            s.push(30)
            while let val = s.pop() {
                println(val)
            }
        }
    )--", "mono_cookbook_stack_pop");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_EQ(r.stdout_output, "30\n20\n10\n") << "stdout: " << r.stdout_output;
}

// ============================================================
// Built-in Map/Set: i32-literal → i64 slot coercion (roadmap #6 A1)
// ============================================================
// insert used to store an i32 literal into the i64-typed key/val temp
// alloca without widening; liva_map_insert then memcpy'd 8 bytes, the
// upper 4 undefined — get returned garbage like 140694538682369.

TEST(RuntimeExecTest, MapI64ValueLiteralInsert) {
    auto r = compileAndRun(R"--(
        func main() {
            var m: Map<string, i64>
            m.insert("a", 1)
            m.insert("b", 2)
            if let v = m.get("a") {
                println(v)
            }
            if let w = m.get("b") {
                println(w)
            }
        }
    )--", "map_i64_val_coerce");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_EQ(r.stdout_output, "1\n2\n") << "stdout: " << r.stdout_output;
}

TEST(RuntimeExecTest, MapI64KeyLiteralInsert) {
    auto r = compileAndRun(R"--(
        func main() {
            var m: Map<i64, string>
            m.insert(10, "x")
            m.insert(20, "y")
            if let s = m.get(10) {
                println(s)
            }
            println(m.contains(20))
            m.remove(20)
            println(m.contains(20))
        }
    )--", "map_i64_key_coerce");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_EQ(r.stdout_output, "x\ntrue\nfalse\n") << "stdout: " << r.stdout_output;
}

TEST(RuntimeExecTest, SetI64LiteralInsert) {
    auto r = compileAndRun(R"--(
        func main() {
            var s: Set<i64>
            s.insert(5)
            println(s.contains(5))
            s.remove(5)
            println(s.contains(5))
        }
    )--", "set_i64_coerce");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_EQ(r.stdout_output, "true\nfalse\n") << "stdout: " << r.stdout_output;
}

TEST(RuntimeExecTest, MapSizeIsEmptyClear) {
    auto r = compileAndRun(R"--(
        func main() {
            var m: Map<string, i32>
            println(m.isEmpty())
            m.insert("a", 1)
            m.insert("b", 2)
            m.insert("a", 3)
            println(m.size())
            println(m.isEmpty())
            m.clear()
            println(m.size())
            println(m.contains("a"))
            m.insert("c", 9)
            if let v = m.get("c") {
                println(v)
            }
        }
    )--", "map_size_clear");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    // insert("a",3) overwrite: size stays 2. clear() then reuse must work.
    EXPECT_EQ(r.stdout_output, "true\n2\nfalse\n0\nfalse\n9\n") << "stdout: " << r.stdout_output;
}

TEST(RuntimeExecTest, SetSizeIsEmptyClear) {
    auto r = compileAndRun(R"--(
        func main() {
            var s: Set<i64>
            println(s.isEmpty())
            s.insert(1)
            s.insert(2)
            s.insert(1)
            println(s.size())
            s.clear()
            println(s.size())
            s.insert(7)
            println(s.contains(7))
        }
    )--", "set_size_clear");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_EQ(r.stdout_output, "true\n2\n0\ntrue\n") << "stdout: " << r.stdout_output;
}

TEST(RuntimeExecTest, MapKeysValues) {
    auto r = compileAndRun(R"--(
        func main() {
            var m: Map<string, i32>
            m.insert("x", 10)
            m.insert("y", 20)
            let ks: [string] = m.keys()
            println(ks.length)
            let vs: [i32] = m.values()
            var total = 0
            for v in vs {
                total = total + v
            }
            println(total)
        }
    )--", "map_keys_values");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    // Hash order is unspecified: only length and SUM (order-independent).
    EXPECT_EQ(r.stdout_output, "2\n30\n") << "stdout: " << r.stdout_output;
}

// Pins the UNANNOTATED binding path: `let ks = m.keys()` must register the
// result as a DynArray via the call's Sema-resolved [K]/[V] type (works here
// even though unannotated strSplit binding is a known gap).
TEST(RuntimeExecTest, MapKeysValuesUnannotated) {
    auto r = compileAndRun(R"--(
        func main() {
            var m: Map<string, i32>
            m.insert("x", 10)
            m.insert("y", 20)
            let ks = m.keys()
            println(ks.length)
            let vs = m.values()
            var total = 0
            for v in vs {
                total = total + v
            }
            println(total)
        }
    )--", "map_keys_values_unannot");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_EQ(r.stdout_output, "2\n30\n") << "stdout: " << r.stdout_output;
}

TEST(RuntimeExecTest, MapKeysEmptyMap) {
    auto r = compileAndRun(R"--(
        func main() {
            var m: Map<i64, i64>
            let ks: [i64] = m.keys()
            println(ks.length)
        }
    )--", "map_keys_empty");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_EQ(r.stdout_output, "0\n") << "stdout: " << r.stdout_output;
}

// ============================================================
// cli::cli — ArgParser (roadmap #6 Faz B)
// ============================================================

TEST(RuntimeExecTest, CliFlagsOptionsPositionals) {
    auto r = compileAndRun(R"--(
        import cli::cli
        func main() {
            var p = ArgParser.new("tool", "A test tool")
            p.addFlag("verbose", "v", "Verbose output")
            p.addOption("out", "o", "default.txt", "Output file")
            p.addPositional("input", "Input file")
            let args: [string] = ["-v", "--out=result.txt", "data.csv", "extra"]
            let r = p.parse(args)
            println(r.ok)
            println(r.getFlag("verbose"))
            println(r.getOption("out"))
            println(r.positionalCount())
            println(r.getPositional(0) ?? "?")
            println(r.getPositional(1) ?? "?")
            println(r.getPositional(9) ?? "none")
        }
    )--", "cli_core");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_EQ(r.stdout_output, "true\ntrue\nresult.txt\n2\ndata.csv\nextra\nnone\n")
        << "stdout: " << r.stdout_output;
}

TEST(RuntimeExecTest, CliOptionSeparateValueAndDefault) {
    auto r = compileAndRun(R"--(
        import cli::cli
        func main() {
            var p = ArgParser.new("tool", "t")
            p.addOption("out", "o", "def.txt", "Output")
            p.addOption("mode", "m", "fast", "Mode")
            let args: [string] = ["--out", "x.txt"]
            let r = p.parse(args)
            println(r.ok)
            println(r.getOption("out"))
            println(r.getOption("mode"))
        }
    )--", "cli_opt_sep_default");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_EQ(r.stdout_output, "true\nx.txt\nfast\n") << "stdout: " << r.stdout_output;
}

TEST(RuntimeExecTest, CliErrors) {
    auto r = compileAndRun(R"--(
        import cli::cli
        func main() {
            var p = ArgParser.new("tool", "t")
            p.addOption("out", "o", "d", "Output")
            p.addPositional("input", "Input")
            let bad1: [string] = ["--unknown"]
            let r1 = p.parse(bad1)
            println(r1.ok)
            println(r1.error)
            let bad2: [string] = ["--out"]
            let r2 = p.parse(bad2)
            println(r2.ok)
            let bad3: [string] = ["--out=x"]
            let r3 = p.parse(bad3)
            println(r3.ok)
        }
    )--", "cli_errors");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    // bad1: unknown option; bad2: missing value; bad3: missing positional
    EXPECT_EQ(r.stdout_output,
        "false\nunknown option: --unknown\nfalse\nfalse\n")
        << "stdout: " << r.stdout_output;
}

TEST(RuntimeExecTest, CliDashDashAndRepeatAndHelp) {
    auto r = compileAndRun(R"--(
        import cli::cli
        func main() {
            var p = ArgParser.new("tool", "t")
            p.addFlag("verbose", "v", "V")
            p.addOption("out", "o", "d", "O")
            let args: [string] = ["--out=a", "--out=b", "--", "--verbose"]
            let r = p.parse(args)
            println(r.ok)
            println(r.getOption("out"))
            println(r.getFlag("verbose"))
            println(r.positionalCount())
            let h: [string] = ["--help"]
            let rh = p.parse(h)
            println(rh.ok)
            println(rh.helpRequested)
        }
    )--", "cli_dashdash_repeat_help");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    // repeat: last wins (b); "--" sonrası --verbose positional'dır (flag DEĞİL)
    EXPECT_EQ(r.stdout_output, "true\nb\nfalse\n1\ntrue\ntrue\n")
        << "stdout: " << r.stdout_output;
}

TEST(RuntimeExecTest, CliOptionEqualsSplitsOnFirstOnly) {
    auto r = compileAndRun(R"--(
        import cli::cli
        func main() {
            var p = ArgParser.new("tool", "t")
            p.addOption("mode", "m", "fast", "Mode")
            let args: [string] = ["--mode=a=b"]
            let r = p.parse(args)
            println(r.ok)
            println(r.getOption("mode"))
        }
    )--", "cli_opt_equals_first_only");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    // "--define=KEY=VAL" style: only the FIRST '=' separates name from value;
    // the rest of the value (including any further '=') is preserved verbatim.
    EXPECT_EQ(r.stdout_output, "true\na=b\n") << "stdout: " << r.stdout_output;
}

TEST(RuntimeExecTest, CliUsageOutput) {
    auto r = compileAndRun(R"--(
        import cli::cli
        func main() {
            var p = ArgParser.new("tool", "A test tool")
            p.addFlag("verbose", "v", "Verbose output")
            p.addOption("out", "o", "out.txt", "Output file")
            p.addPositional("input", "Input file")
            println(p.usage())
        }
    )--", "cli_usage");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_NE(r.stdout_output.find("Usage: tool [options] <input>"), std::string::npos)
        << "stdout: " << r.stdout_output;
    EXPECT_NE(r.stdout_output.find("A test tool"), std::string::npos);
    EXPECT_NE(r.stdout_output.find("-v, --verbose"), std::string::npos);
    EXPECT_NE(r.stdout_output.find("-o, --out"), std::string::npos);
    EXPECT_NE(r.stdout_output.find("out.txt"), std::string::npos)
        << "default value must appear in usage";
    EXPECT_NE(r.stdout_output.find("<input>"), std::string::npos);
}

// ============================================================
// `&&` / `||` short-circuit semantics (roadmap 2.3, 2026-07)
// ============================================================
// visitBinaryExpr used to visit BOTH operands eagerly and lower And/Or as
// bitwise CreateAnd/CreateOr — no control flow at all. Side effects on the
// RHS always ran, and the `i >= len || a[i]` bounds-guard idiom panicked
// out-of-bounds. These tests pin the short-circuit lowering.

TEST(RuntimeExecTest, LogicalOrShortCircuits) {
    auto r = compileAndRun(R"--(
        func loud(v: bool) -> bool {
            println("EVAL")
            return v
        }
        func main() {
            if true || loud(true) {
                println("t1")
            }
            if false || loud(true) {
                println("t2")
            }
            if false || loud(false) {
                println("unexpected")
            } else {
                println("t3")
            }
        }
    )--", "sc_or");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    // Case 1: LHS true → RHS must NOT run (no EVAL). Case 2/3: RHS runs once.
    EXPECT_EQ(r.stdout_output, "t1\nEVAL\nt2\nEVAL\nt3\n") << "stdout: " << r.stdout_output;
}

TEST(RuntimeExecTest, LogicalAndShortCircuits) {
    auto r = compileAndRun(R"--(
        func loud(v: bool) -> bool {
            println("EVAL")
            return v
        }
        func main() {
            if false && loud(true) {
                println("unexpected")
            } else {
                println("t1")
            }
            if true && loud(true) {
                println("t2")
            }
            if true && loud(false) {
                println("unexpected")
            } else {
                println("t3")
            }
        }
    )--", "sc_and");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    // Case 1: LHS false → RHS must NOT run. Case 2/3: RHS runs once.
    EXPECT_EQ(r.stdout_output, "t1\nEVAL\nt2\nEVAL\nt3\n") << "stdout: " << r.stdout_output;
}

TEST(RuntimeExecTest, ShortCircuitBoundsGuard) {
    // The motivating cli::cli idiom: the RHS index is only safe when the
    // LHS bounds check already decided the outcome. Used to PANIC.
    auto r = compileAndRun(R"--(
        func main() {
            let a: [i32] = [10]
            var i = 5
            if i >= 1 || a[i] > 0 {
                println("or-guard-ok")
            }
            if i < 1 && a[i] > 0 {
                println("unexpected")
            } else {
                println("and-guard-ok")
            }
        }
    )--", "sc_bounds_guard");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_EQ(r.stdout_output, "or-guard-ok\nand-guard-ok\n") << "stdout: " << r.stdout_output;
}

TEST(RuntimeExecTest, ShortCircuitChained) {
    auto r = compileAndRun(R"--(
        func loud(tag: string, v: bool) -> bool {
            println(tag)
            return v
        }
        func main() {
            if loud("a", false) || loud("b", true) || loud("c", true) {
                println("or-chain")
            }
            if loud("x", true) && loud("y", false) && loud("z", true) {
                println("unexpected")
            } else {
                println("and-chain")
            }
        }
    )--", "sc_chained");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    // a=false → b runs, b=true → c must NOT run.
    // x=true → y runs, y=false → z must NOT run.
    EXPECT_EQ(r.stdout_output, "a\nb\nor-chain\nx\ny\nand-chain\n")
        << "stdout: " << r.stdout_output;
}

TEST(RuntimeExecTest, ShortCircuitInWhileCondition) {
    // Loop conditions are re-evaluated every iteration; the short-circuit
    // blocks live inside the loop's cond region and must not break the
    // back-edge. Also the classic linear-scan idiom.
    auto r = compileAndRun(R"--(
        func main() {
            let a: [i32] = [3, 7, 9]
            var i = 0 as i64
            while i < a.length && a[i] != 7 {
                i = i + 1
            }
            println(i)
            var j = 0
            var steps = 0
            while j < 10 && steps < 3 {
                j = j + 2
                steps = steps + 1
            }
            println(j)
        }
    )--", "sc_while_cond");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_EQ(r.stdout_output, "1\n6\n") << "stdout: " << r.stdout_output;
}

// ============================================================
// Branch-declared heap containers: no cleanup of uninitialized
// storage (roadmap 2.3, 2026-07)
// ============================================================
// emitScopeCleanup walks the function-flat varDynArrayTypes/varMapTypes/
// varSetTypes maps and frees each registered alloca's data pointer on
// EVERY exit path — but the declaration's initializing store happens at
// the declaration point, which may be inside a branch that never ran.
// The entry-block alloca then still holds garbage → free(garbage) →
// STATUS_HEAP_CORRUPTION with no output at all. Fix: zero-init the
// alloca IN THE ENTRY BLOCK so untaken paths free(NULL) (a no-op).

TEST(RuntimeExecTest, BranchDeclaredDynArrayNoCorruption) {
    auto r = compileAndRun(R"--(
        func classify(arg: string) -> string {
            if strContains(arg, "=") {
                let parts: [string] = strSplit(arg, "=")
                return parts[0]
            } else {
                return "no-eq"
            }
        }
        func main() {
            println(classify("plain"))
            println(classify("k=v"))
            println("done")
        }
    )--", "branch_decl_dynarray");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_EQ(r.stdout_output, "no-eq\nk\ndone\n") << "stdout: " << r.stdout_output;
}

TEST(RuntimeExecTest, BranchDeclaredArrayLiteralNoCorruption) {
    // Array-literal declaration flavor (different visitVarDecl path than
    // the call-result flavor above).
    auto r = compileAndRun(R"--(
        func pick(flag: bool) -> i64 {
            if flag {
                let xs: [i32] = [1, 2, 3]
                return xs.length
            }
            return 0 as i64
        }
        func main() {
            println(pick(false))
            println(pick(true))
            println("done")
        }
    )--", "branch_decl_arrlit");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_EQ(r.stdout_output, "0\n3\ndone\n") << "stdout: " << r.stdout_output;
}

TEST(RuntimeExecTest, BranchDeclaredMapSetNoCorruption) {
    auto r = compileAndRun(R"--(
        func mapSide(flag: bool) -> i64 {
            if flag {
                var m: Map<string, i32>
                m.insert("a", 1)
                return m.size()
            }
            return -1 as i64
        }
        func setSide(flag: bool) -> i64 {
            if flag {
                var s: Set<i64>
                s.insert(7)
                return s.size()
            }
            return -1 as i64
        }
        func main() {
            println(mapSide(false))
            println(mapSide(true))
            println(setSide(false))
            println(setSide(true))
            println("done")
        }
    )--", "branch_decl_mapset");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_EQ(r.stdout_output, "-1\n1\n-1\n1\ndone\n") << "stdout: " << r.stdout_output;
}

// ============================================================
// regex::regex wrapper — argument-order fix pinning tests
//
// stdlib/regex/regex.liva's isMatch/find/findAll/replace/groups methods
// used to pass (pattern, text) to the underlying regexMatch/regexFind/...
// builtins, which actually expect (text, pattern) — an inverted-argument
// bug. These tests pin the CORRECT (pattern held by Regex.new, text passed
// to the method) semantics; they fail against the pre-fix wrapper.
// ============================================================

TEST(RuntimeExecTest, RegexWrapperIsMatchTrueFalse) {
    auto r = compileAndRun(
        "import regex::regex\n"
        "func main() {\n"
        "    let re = Regex.new(\"^[a-z]+$\")\n"
        "    println(re.isMatch(\"abc\"))\n"
        "    println(re.isMatch(\"abc123\"))\n"
        "}\n",
        "regex_wrapper_ismatch");
    EXPECT_EQ(r.exit_code, 0) << r.stdout_output;
    EXPECT_EQ(r.stdout_output, "true\nfalse\n") << r.stdout_output;
}

TEST(RuntimeExecTest, RegexWrapperFindAllCount) {
    auto r = compileAndRun(
        "import regex::regex\n"
        "func main() {\n"
        "    let re = Regex.new(\"[0-9]+\")\n"
        "    let all: [String] = re.findAll(\"a1b22c333\")\n"
        "    println(all.length)\n"
        "    for m in all { println(m) }\n"
        "}\n",
        "regex_wrapper_findall");
    EXPECT_EQ(r.exit_code, 0) << r.stdout_output;
    EXPECT_EQ(r.stdout_output, "3\n1\n22\n333\n") << r.stdout_output;
}

TEST(RuntimeExecTest, RegexWrapperReplace) {
    auto r = compileAndRun(
        "import regex::regex\n"
        "func main() {\n"
        "    let re = Regex.new(\"[0-9]+\")\n"
        "    println(re.replace(\"x9y8\", \"N\"))\n"
        "}\n",
        "regex_wrapper_replace");
    EXPECT_EQ(r.exit_code, 0) << r.stdout_output;
    EXPECT_EQ(r.stdout_output, "xNyN\n") << r.stdout_output;
}

TEST(RuntimeExecTest, RegexWrapperGroups) {
    auto r = compileAndRun(
        "import regex::regex\n"
        "func main() {\n"
        "    let re = Regex.new(\"(\\\\w+)@(\\\\w+)\")\n"
        "    let gs: [String] = re.groups(\"user@host\")\n"
        "    println(gs.length)\n"
        "    for g in gs { println(g) }\n"
        "}\n",
        "regex_wrapper_groups");
    EXPECT_EQ(r.exit_code, 0) << r.stdout_output;
    EXPECT_EQ(r.stdout_output, "3\nuser@host\nuser\nhost\n") << r.stdout_output;
}

// ============================================================
// Top-level var: clean compile error, not a compiler segfault
// ============================================================
// End-to-end pin: before the Sema diagnostic existed, this source
// SEGFAULTED the in-process compiler (null insert-block deref in
// IRGen::visitVarDecl) — which would crash this whole test binary.
// After the fix it must fail compilation CLEANLY.

TEST(RuntimeExecTest, TopLevelVarCleanCompileError) {
    auto r = compileAndRun(R"--(
        var counter = 0

        func main() {
            println(counter)
        }
    )--", "toplevel_var_clean_error");
    EXPECT_NE(r.exit_code, 0) << "top-level var must be a compile error";
    EXPECT_NE(r.stdout_output.find("compile failed"), std::string::npos)
        << "expected a clean compile failure, got: " << r.stdout_output;
}

// ============================================================
// [string] element storage ownership family (roadmap 2.3 — the
// intermittent Cli ctest flake's root cause)
// ============================================================
// Storing a string into a DynArray slot must give the ARRAY its own copy
// (liva_str_dup), like local `arr.push(x)` already did. Three broken
// siblings found while isolating the ~5%-per-process Cli flake:
//   F1: local `arr[i] = localHeapStr` only did removeFromTempStrings —
//       a NAMED local's buffer is freed at function exit → dangling
//       element → use-after-free (recycled block showed "true").
//   F2: member-field `self.arr.push(x)` stored the raw pointer with no
//       dup at all (asymmetric with the local push path).
//   F3: member-field `self.arr[i] = x` / `h.arr[i] = x` was a SILENT
//       NO-OP — the IndexExpr-assign path only handled IdentifierExpr
//       bases and fell through, returning without storing.

TEST(RuntimeExecTest, StringElemAssignFromLocalOwnsCopy) {
    // F1 — mirrors cli::cli parse(): build a local [string], overwrite an
    // element with a substring-derived NAMED local, return the array; the
    // caller churns the heap and re-reads. Pre-fix: 3-4% of iterations
    // read recycled memory (nonzero bad count on every run).
    auto r = compileAndRun(R"--(
        func build(src: string) -> [string] {
            var a: [string] = ["fast"]
            let v: string = src[0..3]
            a[0] = v
            return a
        }
        func main() {
            var bad = 0
            var i = 0
            while i < 200 {
                let a: [string] = build("abcdef")
                let junk: string = strToUpper("recycle-me-please")
                let got: string = a[0]
                if got != "abc" {
                    bad = bad + 1
                }
                i = i + 1
            }
            println(bad)
        }
    )--", "str_elem_assign_local_owns");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_EQ(r.stdout_output, "0\n") << "stdout: " << r.stdout_output;
}

TEST(RuntimeExecTest, StringMemberPushOwnsCopy) {
    // F2 — pushing a scope-local heap string onto a struct-field array.
    auto r = compileAndRun(R"--(
        struct Holder {
            var vals: [string]
        }
        impl Holder {
            func add(ref mut self, s: string) {
                let piece: string = s[0..3]
                self.vals.push(piece)
            }
        }
        func main() {
            var bad = 0
            var i = 0
            while i < 200 {
                var h = Holder { vals: [] }
                h.add("abcdef")
                let junk: string = strToUpper("recycle-me-please")
                let got: string = h.vals[0]
                if got != "abc" {
                    bad = bad + 1
                }
                i = i + 1
            }
            println(bad)
        }
    )--", "str_member_push_owns");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_EQ(r.stdout_output, "0\n") << "stdout: " << r.stdout_output;
}

TEST(RuntimeExecTest, StringMemberElemAssignActuallyStores) {
    // F3 — deterministic silent no-op: the write must happen at all, both
    // via `self.vals[i]` inside an impl and via `h.vals[i]` from outside.
    auto r = compileAndRun(R"--(
        struct Holder {
            var vals: [string]
        }
        impl Holder {
            func seed(ref mut self) {
                self.vals.push("seed")
            }
            func overwrite(ref mut self, s: string) {
                let piece: string = s[0..3]
                self.vals[0] = piece
            }
        }
        func main() {
            var h = Holder { vals: [] }
            h.seed()
            h.overwrite("xyzdef")
            println(h.vals[0])
            h.vals[0] = "direct"
            println(h.vals[0])
        }
    )--", "str_member_elem_assign_stores");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_EQ(r.stdout_output, "xyz\ndirect\n") << "stdout: " << r.stdout_output;
}

// ============================================================
// Nested dynamic arrays [[T]] — core (roadmap 2.3)
// ============================================================
// Elements of a nested dynamic array are INLINE %DynArray structs (24B).
// Outer cleanup frees only the outer buffer; inner buffers intentionally
// leak (same profile as string elements). Copies are SHALLOW.

TEST(RuntimeExecTest, NestedArrayLiteralAndIndexRead) {
    auto r = compileAndRun(R"--(
        func main() {
            var rows: [[i32]] = [[1, 2], [30, 40, 50]]
            println(rows.length)
            let first: [i32] = rows[0]
            println(first.length)
            println(first[1])
            let second: [i32] = rows[1]
            println(second.length)
            println(second[2])
        }
    )--", "nested_literal_index");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_EQ(r.stdout_output, "2\n2\n2\n3\n50\n") << "stdout: " << r.stdout_output;
}

TEST(RuntimeExecTest, NestedArrayInnerReadViaBinding) {
    // Inner array read through a binding is fully functional (push works
    // on the SHALLOW-shared copy; documented semantics).
    auto r = compileAndRun(R"--(
        func main() {
            var rows: [[i32]] = [[7]]
            let inner: [i32] = rows[0]
            var total = 0
            for x in inner {
                total = total + x
            }
            println(total)
        }
    )--", "nested_inner_via_binding");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_EQ(r.stdout_output, "7\n") << "stdout: " << r.stdout_output;
}

TEST(RuntimeExecTest, NestedArrayPushLocalAndMember) {
    auto r = compileAndRun(R"--(
        struct Grid {
            var rows: [[i32]]
        }
        impl Grid {
            func addRow(ref mut self, row: [i32]) {
                self.rows.push(row)
            }
        }
        func main() {
            var rows: [[i32]] = []
            let a: [i32] = [1, 2, 3]
            rows.push(a)
            println(rows.length)
            let back: [i32] = rows[0]
            println(back[2])

            var g = Grid { rows: [] }
            let b: [i32] = [9, 8]
            g.addRow(b)
            println(g.rows.length)
            let gr: [i32] = g.rows[0]
            println(gr[0])
        }
    )--", "nested_push_local_member");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_EQ(r.stdout_output, "1\n3\n1\n9\n") << "stdout: " << r.stdout_output;
}

TEST(RuntimeExecTest, NestedArrayForInAndElemAssign) {
    auto r = compileAndRun(R"--(
        func main() {
            var rows: [[i32]] = [[1, 2], [3, 4]]
            var total = 0
            for row in rows {
                for x in row {
                    total = total + x
                }
            }
            println(total)
            let repl: [i32] = [100]
            rows[0] = repl
            let got: [i32] = rows[0]
            println(got[0])
            println(got.length)
        }
    )--", "nested_forin_elemassign");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_EQ(r.stdout_output, "10\n100\n1\n") << "stdout: " << r.stdout_output;
}

TEST(RuntimeExecTest, NestedArrayChurnNoCorruption) {
    // UAF-family pattern: 200 iterations with heap churn; any layout-
    // dependent corruption shows as a nonzero bad count.
    auto r = compileAndRun(R"--(
        func fill() -> [[i32]] {
            var rows: [[i32]] = []
            let a: [i32] = [1, 2]
            rows.push(a)
            let b: [i32] = [3]
            rows.push(b)
            return rows
        }
        func main() {
            var bad = 0
            var i = 0
            while i < 200 {
                let rows: [[i32]] = fill()
                let junk: string = strToUpper("recycle-me-please")
                let x: [i32] = rows[0]
                let y: [i32] = rows[1]
                if x.length != 2 { bad = bad + 1 }
                if y[0] != 3 { bad = bad + 1 }
                i = i + 1
            }
            println(bad)
        }
    )--", "nested_churn");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_EQ(r.stdout_output, "0\n") << "stdout: " << r.stdout_output;
}

// ============================================================
// Nested dynamic arrays [[T]] — for-in loop-var borrow fix
// ============================================================
// The for-in loop variable (`for row in rows`) is a BORROWED view into the
// outer array's own storage, not a copy — it must be marked in movedVars
// so emitScopeCleanup doesn't treat it as an owner and free the shared
// buffer out from under the array that still needs it (same idiom as the
// if-let DynArray binding: "Borrowed... skip cleanup").

TEST(RuntimeExecTest, NestedArrayForInEmptyOuter) {
    // Empty outer array: the loop body never runs, but the loop-var alloca
    // is still created (uninitialized) and, pre-fix, still registered as an
    // "owner" — emitScopeCleanup then frees garbage from the never-stored
    // alloca.
    auto r = compileAndRun(R"--(
        func main() {
            var rows: [[i32]] = []
            for row in rows {
                println(row.length)
            }
            println(rows.length)
        }
    )--", "nested_forin_empty_outer");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_EQ(r.stdout_output, "0\n") << "stdout: " << r.stdout_output;
}

TEST(RuntimeExecTest, NestedArrayReadAfterForIn) {
    // After a full for-in pass, the loop var's last value aliases rows[1]'s
    // buffer. Pre-fix, that alias is (wrongly) freed at scope exit,
    // dangling the buffer that the subsequent `rows[1]` read still needs.
    auto r = compileAndRun(R"--(
        func main() {
            var rows: [[i32]] = [[1, 2], [3, 4]]
            for row in rows {
                println(row.length)
            }
            let z: [i32] = rows[1]
            println(z[0])
        }
    )--", "nested_read_after_forin");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_EQ(r.stdout_output, "2\n2\n3\n") << "stdout: " << r.stdout_output;
}

TEST(RuntimeExecTest, NestedArrayForInThenReturn) {
    // fill() iterates its own local `rows` with a for-in BEFORE returning
    // it. Pre-fix, the loop var's last alias (rows[1]'s buffer) is freed at
    // fill()'s own scope exit — the returned struct's last row is left
    // dangling. 200-iteration churn (recycled heap blocks) makes any
    // dangling-pointer corruption reliably visible as a nonzero bad count.
    auto r = compileAndRun(R"--(
        func fill() -> [[i32]] {
            var rows: [[i32]] = []
            let a: [i32] = [1, 2]
            rows.push(a)
            let b: [i32] = [3]
            rows.push(b)
            var count = 0
            for row in rows {
                count = count + 1
            }
            return rows
        }
        func main() {
            var bad = 0
            var i = 0
            while i < 200 {
                let rows: [[i32]] = fill()
                let junk: string = strToUpper("recycle-me-please")
                let x: [i32] = rows[0]
                let y: [i32] = rows[1]
                if x.length != 2 { bad = bad + 1 }
                if y[0] != 3 { bad = bad + 1 }
                i = i + 1
            }
            println(bad)
        }
    )--", "nested_forin_then_return");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_EQ(r.stdout_output, "0\n") << "stdout: " << r.stdout_output;
}

TEST(RuntimeExecTest, NestedArrayMemberForInCallerIntact) {
    // A method iterates self.rows with a for-in. Pre-fix, the loop var's
    // last alias (self.rows[last]'s buffer) is freed at the method's own
    // scope exit — the caller's subsequent read of that same row crashes.
    auto r = compileAndRun(R"--(
        struct Grid {
            var rows: [[i32]]
        }
        impl Grid {
            func addRow(ref mut self, row: [i32]) {
                self.rows.push(row)
            }
            func sumAll(ref self) -> i32 {
                var total = 0
                for row in self.rows {
                    total = total + 1
                }
                return total
            }
        }
        func main() {
            var g = Grid { rows: [] }
            let a: [i32] = [1, 2]
            g.addRow(a)
            let b: [i32] = [3, 4, 5]
            g.addRow(b)
            let t = g.sumAll()
            println(t)
            let last: [i32] = g.rows[1]
            println(last.length)
        }
    )--", "nested_member_forin_caller_intact");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_EQ(r.stdout_output, "2\n3\n") << "stdout: " << r.stdout_output;
}

// ============================================================
// Variadic function EXECUTION (roadmap 2.3)
// ============================================================
// The variadic parameter is packed by the CALLER into a stack-allocated
// element array and passed as a DynArray struct by value. The callee's
// registration lacked the movedVars borrow mark, so emitScopeCleanup
// called free() on the caller's STACK pointer at every variadic-function
// return -> STATUS_HEAP_CORRUPTION. The existing VariadicFunction test
// only ran parse+sema, so execution was never covered.

TEST(RuntimeExecTest, VariadicExecutionSumAndEmpty) {
    auto r = compileAndRun(R"--(
        func sumAll(values: i32...) -> i32 {
            var total = 0
            for v in values {
                total = total + v
            }
            return total
        }
        func main() {
            println(sumAll(1, 2, 3, 4, 5))
            println(sumAll(10))
            println(sumAll())
            println(sumAll(7, 7))
        }
    )--", "variadic_exec_sum");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_EQ(r.stdout_output, "15\n10\n0\n14\n") << "stdout: " << r.stdout_output;
}

TEST(RuntimeExecTest, VariadicExecutionWithLeadingFixedParam) {
    auto r = compileAndRun(R"--(
        func scaleSum(factor: i32, values: i32...) -> i32 {
            var total = 0
            for v in values {
                total = total + v * factor
            }
            return total
        }
        func main() {
            println(scaleSum(2, 1, 2, 3))
            println(scaleSum(10, 5))
        }
    )--", "variadic_exec_fixed");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_EQ(r.stdout_output, "12\n50\n") << "stdout: " << r.stdout_output;
}

#endif // LIVA_HAS_LLVM
