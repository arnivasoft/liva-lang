// UICodegenExecTest: IRGen regression tests for the class-based UI library
// work and the underlying class-codegen fixes.
//
// Why a separate file (not RuntimeExecTest.cpp): the UI/class work runs
// concurrently with unrelated in-progress changes in RuntimeExecTest.cpp; this
// file keeps the new tests isolated, with its own small helper.
//
// VERIFICATION STRATEGY — emit-IR, not run:
//   * wxWidgets is not guaranteed here, so programs that import std::ui cannot
//     LINK; and class-based programs currently fail at the LINK step in this
//     environment (pre-existing — even examples/classes.liva does not link
//     here), though they compile through IRGen cleanly.
// So these tests drive `livac --emit-ir` (Lexer→Parser→Sema→IRGen, NO link)
// and assert that valid IR is produced. That is exactly the layer that catches
// the class-field-access codegen bug (Sema-only tests miss it).
//
// The livac invocation deliberately uses unquoted, space-free paths and no
// shell redirection: std::system on Windows mangles multi-token quoted
// commands. Diagnostics flow to the test's stderr (visible in gtest output);
// success is judged by the emitted .ll containing `define` (livac does not
// write valid IR when IRGen fails).
//
// Only built when LIVA_HAS_LLVM is defined.

#ifdef LIVA_HAS_LLVM

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <gtest/gtest.h>
#include <sstream>
#include <string>

#ifdef _WIN32
#define EXE_SUFFIX ".exe"
#else
#define EXE_SUFFIX ""
#endif

namespace {

// Emit LLVM IR for `source` via the built `livac --emit-ir`; return the .ll
// text (empty if livac failed to produce IR). LIVA_BUILD_DIR is space-free.
std::string emitIR(const std::string &source, const std::string &test_name) {
    const std::string buildDir = LIVA_BUILD_DIR;
    const std::string srcPath  = buildDir + "/_uicg_" + test_name + ".liva";
    const std::string llPath   = buildDir + "/_uicg_" + test_name + ".ll";
    const std::string livac    = buildDir + "/livac" EXE_SUFFIX;

    std::remove(llPath.c_str());
    {
        std::ofstream ofs(srcPath, std::ios::binary);
        ofs << source;
    }

    // Unquoted, space-free paths; no redirection (avoids cmd.exe quote mangling).
    std::string cmd = livac + " --emit-ir -o " + llPath + " " + srcPath;
    (void)std::system(cmd.c_str());

    std::ifstream ifs(llPath, std::ios::binary);
    std::stringstream ss;
    if (ifs.is_open()) ss << ifs.rdbuf();
    std::string ir = ss.str();

    std::remove(srcPath.c_str());
    std::remove(llPath.c_str());
    return ir;
}

// livac emits valid IR (containing function definitions) only when IRGen
// succeeds; on any Lexer/Parser/Sema/IRGen error it writes no usable .ll.
::testing::AssertionResult emitsClean(const std::string &ir) {
    if (ir.find("define") == std::string::npos)
        return ::testing::AssertionFailure()
               << "no IR emitted (see livac diagnostics on stderr above)";
    return ::testing::AssertionSuccess();
}

} // namespace

// ── Task 2a: class-typed parameter field access in IRGen ───────────────
//
// Regression for the bug where `param.field` on a class-typed parameter
// (function param, init param, method param — by value or by `ref`) failed in
// IRGen with "internal: cannot resolve member". Sema accepts these; only the
// IRGen pipeline catches the regression, hence --emit-ir.

TEST(UICodegenExec, ClassParamFieldAccessByValue) {
    auto ir = emitIR(
        "class Box {\n"
        "  var h: i32\n"
        "  init(h: i32) { self.h = h }\n"
        "}\n"
        "func grab(b: Box) -> i32 {\n"
        "  return b.h\n"
        "}\n"
        "func main() {\n"
        "  let x = Box(7)\n"
        "  println(grab(x))\n"
        "}\n",
        "class_param_byval");
    EXPECT_TRUE(emitsClean(ir));
}

TEST(UICodegenExec, ClassRefParamFieldAccessInInit) {
    // `ref` class param in an init (the widget `init(parent: ref Control)` shape).
    auto ir = emitIR(
        "class Ctrl {\n"
        "  var handle: i32\n"
        "  init(h: i32) { self.handle = h }\n"
        "}\n"
        "class Child: Ctrl {\n"
        "  init(parent: ref Ctrl, n: i32) {\n"
        "    super.init(parent.handle + n)\n"
        "  }\n"
        "}\n"
        "func main() {\n"
        "  let c = Ctrl(10)\n"
        "  let a = Child(ref c, 1)\n"
        "  let b = Child(ref c, 2)\n"
        "}\n",
        "class_ref_param_init");
    EXPECT_TRUE(emitsClean(ir));
}

TEST(UICodegenExec, ClassParamFieldAccessInMethod) {
    // non-self class param in a class-body method (the `TabView.addPage` shape).
    auto ir = emitIR(
        "class Ctrl {\n"
        "  var handle: i32\n"
        "  init(h: i32) { self.handle = h }\n"
        "}\n"
        "class Bag {\n"
        "  var sum: i32\n"
        "  init() { self.sum = 0 }\n"
        "  func add(c: ref Ctrl) {\n"
        "    self.sum = self.sum + c.handle\n"
        "  }\n"
        "}\n"
        "func main() {\n"
        "  let a = Ctrl(5)\n"
        "  let bag = Bag()\n"
        "  bag.add(ref a)\n"
        "}\n",
        "class_method_param");
    EXPECT_TRUE(emitsClean(ir));
}

#endif // LIVA_HAS_LLVM
