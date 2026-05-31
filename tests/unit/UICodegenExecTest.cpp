// UICodegenExecTest: full-pipeline (IRGen/CodeGen) regression tests for the
// class-based UI library work and the underlying class-codegen fixes.
//
// Why a separate file (not RuntimeExecTest.cpp): the UI/class work runs
// concurrently with unrelated in-progress changes in RuntimeExecTest.cpp; this
// file keeps the new tests isolated. It carries its own small compile helpers
// (intentionally standalone) rather than sharing RuntimeExecTest's anonymous
// harness.
//
// These tests exercise the FULL pipeline via CompilerInstance — the same path
// `livac` uses — so they catch IRGen failures that Sema-only tests miss.
//
// NOTE on wxWidgets: programs that `import std::ui`/`ui::widgets` need wx to
// LINK. Where wx may be absent we use `--emit-ir` (no link) and assert on the
// emitted LLVM IR instead of running. Pure programs (class codegen, ui::types,
// ui::animation) do not import std::ui and run headless.
//
// Only built when LIVA_HAS_LLVM is defined.

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
    int exit_code;          // program exit code, or -1 on compile/link/spawn failure
    std::string stdout_output;
};

// Compile a Liva source string to an executable, run it, capture stdout+stderr
// and the exit code. For programs that do not import std::ui (no wx link).
RunResult compileAndRun(const std::string &source, const std::string &test_name) {
    const std::string buildDir = LIVA_BUILD_DIR;
    const std::string exePath  = buildDir + "/_uicg_exec_" + test_name + EXE_SUFFIX;
    const std::string outPath  = buildDir + "/_uicg_exec_" + test_name + ".out.txt";
    const std::string objPath  = exePath + ".o";

    auto cleanup = [&]() {
        std::remove(exePath.c_str());
        std::remove(outPath.c_str());
        std::remove(objPath.c_str());
    };

    liva::CompilerInstance compiler;
    compiler.setSource("_uicg_exec_" + test_name + ".liva", source);
    compiler.setExecutablePath(buildDir + "/livac" EXE_SUFFIX);

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

} // namespace

// ── Task 2a: class-typed parameter field access in IRGen ───────────────
//
// Regression for the bug where `param.field` on a class-typed parameter
// (function param, init param, method param — by value or by `ref`) failed
// with "internal: cannot resolve member". Sema accepts these; only the full
// IRGen pipeline catches the regression, hence compile-and-run.

TEST(UICodegenExec, ClassParamFieldAccessByValue) {
    auto r = compileAndRun(
        "class Box {\n"
        "  var h: i32\n"
        "  init(h: i32) { self.h = h }\n"
        "}\n"
        "func grab(b: Box) -> i32 { return b.h }\n"
        "func main() { let x = Box(7); println(grab(x)) }\n",
        "class_param_byval");
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_NE(r.stdout_output.find("7"), std::string::npos)
        << "output was: " << r.stdout_output;
}

TEST(UICodegenExec, ClassRefParamFieldAccessAndMultiUse) {
    // `ref` class param in an init, reused for multiple children (no move).
    auto r = compileAndRun(
        "class Ctrl {\n"
        "  var handle: i32\n"
        "  init(h: i32) { self.handle = h }\n"
        "}\n"
        "class Child: Ctrl {\n"
        "  init(parent: ref Ctrl, n: i32) { super.init(parent.handle + n) }\n"
        "  func id() -> i32 { return self.handle }\n"
        "}\n"
        "func main() {\n"
        "  let c = Ctrl(10)\n"
        "  let a = Child(ref c, 1)\n"
        "  let b = Child(ref c, 2)\n"
        "  println(a.id() + b.id())\n"
        "}\n",
        "class_ref_param");
    EXPECT_EQ(r.exit_code, 0);
    // a.handle = 11, b.handle = 12 -> 23
    EXPECT_NE(r.stdout_output.find("23"), std::string::npos)
        << "output was: " << r.stdout_output;
}

TEST(UICodegenExec, ClassParamFieldAccessInMethod) {
    // non-self class param in a class-body method.
    auto r = compileAndRun(
        "class Ctrl {\n"
        "  var handle: i32\n"
        "  init(h: i32) { self.handle = h }\n"
        "}\n"
        "class Bag {\n"
        "  var sum: i32\n"
        "  init() { self.sum = 0 }\n"
        "  func add(c: ref Ctrl) { self.sum = self.sum + c.handle }\n"
        "  func total() -> i32 { return self.sum }\n"
        "}\n"
        "func main() {\n"
        "  let a = Ctrl(5)\n"
        "  let b = Ctrl(8)\n"
        "  let bag = Bag()\n"
        "  bag.add(ref a)\n"
        "  bag.add(ref b)\n"
        "  println(bag.total())\n"
        "}\n",
        "class_method_param");
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_NE(r.stdout_output.find("13"), std::string::npos)
        << "output was: " << r.stdout_output;
}

#endif // LIVA_HAS_LLVM
