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
        "}\n",
        "pg_normalize");
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_EQ(r.stdout_output,
        "SELECT * FROM t WHERE a=$1 AND b=$2\n"
        "INSERT INTO t VALUES ($1, '?lit?', $2)\n"
        "no params here\n");
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

#endif // LIVA_HAS_LLVM
