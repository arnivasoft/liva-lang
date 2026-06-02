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

// Compile `source` to an executable via the built livac, run it, and return
// captured stdout+stderr. Empty string on compile failure. Used for pure
// (non-UI) stdlib code — ui::types, ui::animation — which does NOT import
// std::ui and therefore links without wxWidgets. LIVA_BUILD_DIR is space-free.
std::string compileAndRun(const std::string &source, const std::string &test_name) {
    const std::string buildDir = LIVA_BUILD_DIR;
    const std::string srcPath  = buildDir + "/_uicgr_" + test_name + ".liva";
    const std::string exePath  = buildDir + "/_uicgr_" + test_name + EXE_SUFFIX;
    const std::string outPath  = buildDir + "/_uicgr_" + test_name + ".out";
    const std::string objPath  = exePath + ".o";
    const std::string livac    = buildDir + "/livac" EXE_SUFFIX;

    std::remove(exePath.c_str());
    {
        std::ofstream ofs(srcPath, std::ios::binary);
        ofs << source;
    }

    // Build (unquoted, space-free paths; no redirection — avoids quote mangling).
    std::string build = livac + " " + srcPath + " -o " + exePath;
    (void)std::system(build.c_str());

    std::string out;
    {
        std::ifstream exeCheck(exePath, std::ios::binary);
        if (exeCheck.good()) {
            exeCheck.close();
            std::string run = exePath + " > " + outPath + " 2>&1";
            (void)std::system(run.c_str());
            std::ifstream ifs(outPath, std::ios::binary);
            std::stringstream ss;
            if (ifs.is_open()) ss << ifs.rdbuf();
            out = ss.str();
        }
    }

    std::remove(srcPath.c_str());
    std::remove(exePath.c_str());
    std::remove(outPath.c_str());
    std::remove(objPath.c_str());
    return out;
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

// ── Task 2b: classes are reference (Copy) types in the ownership checker ─
//
// A class value passed by value must NOT be consumed/moved — passing it again
// is fine (the VCL pattern: add many children to one parent panel). Before the
// fix, the second use produced "use of moved value". Here Box stands in for a
// widget parent; the parent is used as a constructor arg three times.

TEST(UICodegenExec, ClassValueIsCopyNotMoved) {
    auto ir = emitIR(
        "class Ctrl {\n"
        "  var handle: i32\n"
        "  init(h: i32) { self.handle = h }\n"
        "}\n"
        "class Child {\n"
        "  var x: i32\n"
        "  init(parent: Ctrl, n: i32) { self.x = parent.handle + n }\n"
        "}\n"
        "func main() {\n"
        "  let p = Ctrl(100)\n"
        "  let a = Child(p, 1)\n"
        "  let b = Child(p, 2)\n"
        "  let c = Child(p, 3)\n"
        "}\n",
        "class_copy_no_move");
    EXPECT_TRUE(emitsClean(ir));
}

// ── Task 2c: chained field access through a class-typed field ───────────
//
// Regression for `obj.classField.field` (e.g. composite widgets storing a
// child widget and reading through it: self.input.handle). The object of the
// outer member access is itself a member expression resolving to a class type;
// IRGen previously failed with "cannot resolve member" because the class
// field-access path only handled identifier objects.

TEST(UICodegenExec, ChainedClassFieldRead) {
    auto ir = emitIR(
        "class Inner {\n"
        "  var v: i32\n"
        "  init(v: i32) { self.v = v }\n"
        "}\n"
        "class Holder {\n"
        "  var inner: Inner\n"
        "  init() { self.inner = Inner(7) }\n"
        "  func chain() -> i32 { return self.inner.v }\n"
        "}\n"
        "func main() {\n"
        "  let h = Holder()\n"
        "  println(h.inner.v)\n"
        "  println(h.chain())\n"
        "}\n",
        "chained_class_field_read");
    EXPECT_TRUE(emitsClean(ir));
}

// ── Task 2c: chained method call through a class-typed field ────────────
//
// Regression for `obj.classField.method(args)` (e.g. composite widgets
// delegating: self.input.getText()). The receiver of the call is a member
// expression resolving to a class type; the identifier-based class method
// dispatch did not handle it.

TEST(UICodegenExec, ChainedClassMethodCall) {
    auto ir = emitIR(
        "class Inner {\n"
        "  var v: i32\n"
        "  init(v: i32) { self.v = v }\n"
        "  func get() -> i32 { return self.v }\n"
        "  func setV(x: i32) { self.v = x }\n"
        "}\n"
        "class Holder {\n"
        "  var inner: Inner\n"
        "  init() { self.inner = Inner(7) }\n"
        "  func chainGet() -> i32 { return self.inner.get() }\n"
        "  func chainSet() { self.inner.setV(42) }\n"
        "}\n"
        "func main() {\n"
        "  let h = Holder()\n"
        "  println(h.inner.get())\n"
        "  println(h.chainGet())\n"
        "  h.inner.setV(9)\n"
        "  h.chainSet()\n"
        "}\n",
        "chained_class_method_call");
    EXPECT_TRUE(emitsClean(ir));
}

// ── Task 4: helper modules compile against the class widget API ─────────
//
// theme/tooltip/listview/router/layout/composite were migrated to take
// `Control` instances and store class-typed fields (exercising the chained
// access fixes above). This guards the whole helper-module surface through
// IRGen (Sema-only tests would miss codegen regressions).

TEST(UICodegenExec, HelperModulesClassApi) {
    auto ir = emitIR(
        "import ui::theme\n"
        "import ui::tooltip\n"
        "import ui::listview\n"
        "import ui::router\n"
        "import ui::layout\n"
        "import ui::composite\n"
        "import ui::widgets\n"
        "func main() {\n"
        "  appInit()\n"
        "  let win = Window(400, 300, \"T\")\n"
        "  let panel = Panel(win)\n"
        "  let th = Theme.dark()\n"
        "  th.applyToPanel(panel)\n"
        "  applyTooltip(panel, \"ip\")\n"
        "  let lv = ListView(panel)\n"
        "  lv.addItem(\"a\")\n"
        "  println(lv.getSelection())\n"
        "  let r = Router(panel)\n"
        "  r.addPage(panel, \"P1\")\n"
        "  println(r.getCurrentPage())\n"
        "  let v = VStack.new()\n"
        "  let b = Button(panel, \"x\")\n"
        "  v.add(b, 0, 4)\n"
        "  let ff = FormField(panel, \"Ad\", \"\")\n"
        "  println(ff.getText())\n"
        "  let bar = ButtonBar()\n"
        "  let b2 = bar.addButton(panel, \"ok\", |_h: i32| { })\n"
        "  b2.setText(\"done\")\n"
        "  let st = StatusText(panel, \"ok\")\n"
        "  st.setColor(255, 0, 0)\n"
        "  panel.setSizerHandle(v.handle)\n"
        "}\n",
        "helper_modules_class_api");
    EXPECT_TRUE(emitsClean(ir));
}

// ── Task 6: pure-function stdlib runs headless (no wxWidgets) ───────────
//
// ui::types and ui::animation do not import std::ui, so they compile, link,
// and run without wxWidgets. These compile-and-run tests verify the actual
// computed values, complementing the IR-only tests above.

TEST(UICodegenExec, UiTypesRectContains) {
    auto out = compileAndRun(
        "import ui::types\n"
        "func main() {\n"
        "  let rc = Rect.new(10, 10, 100, 50)\n"
        "  if rc.contains(20, 20) { println(\"inside\") } else { println(\"nope\") }\n"
        "  if rc.contains(200, 20) { println(\"wrong\") } else { println(\"outside\") }\n"
        "}\n",
        "types_rect_contains");
    EXPECT_NE(out.find("inside"), std::string::npos) << "output was: " << out;
    EXPECT_NE(out.find("outside"), std::string::npos) << "output was: " << out;
    EXPECT_EQ(out.find("wrong"), std::string::npos) << "output was: " << out;
}

TEST(UICodegenExec, UiAnimationEasing) {
    auto out = compileAndRun(
        "import ui::animation\n"
        "func main() {\n"
        "  println(easeLinear(0.5))\n"
        "  println(easeInQuad(0.5))\n"
        "}\n",
        "anim_easing");
    // easeLinear(0.5) == 0.5, easeInQuad(0.5) == 0.25
    EXPECT_NE(out.find("0.5"), std::string::npos) << "output was: " << out;
    EXPECT_NE(out.find("0.25"), std::string::npos) << "output was: " << out;
}

// ── Task 3: heap-owned callback envs for inline UI event closures ───────
//
// A closure LITERAL bound via widget.onClick(...) must pass a NON-ZERO env
// size to liva_ui_on_click so the runtime heap-copies the captured env and
// frees it on widget destroy. Otherwise a callback bound inside a helper that
// returns the widget would dangle. (The runtime side is verified by the size
// argument being non-zero; we assert on the emitted IR call.)

// True if any liva_ui_on_click CALL site passes a non-zero env size (4th arg),
// i.e. the heap-own fast path fired. Note: the Control.onClick method body also
// emits a stack-env (`, i32 0)`) call, so we scan ALL call sites rather than
// just the first.
static bool anyOnClickHeapOwns(const std::string &ir) {
    const std::string needle = "call void @liva_ui_on_click(";
    for (size_t p = ir.find(needle); p != std::string::npos;
         p = ir.find(needle, p + 1)) {
        size_t end = ir.find(')', p);
        if (end == std::string::npos) break;
        std::string call = ir.substr(p, end - p + 1);
        if (call.find(", i32 0)") == std::string::npos)
            return true; // a call whose size arg is not literally 0
    }
    return false;
}

// True if at least one liva_ui_on_click call exists.
static bool hasOnClickCall(const std::string &ir) {
    return ir.find("call void @liva_ui_on_click(") != std::string::npos;
}

TEST(UICodegenExec, InlineCallbackLiteralHeapOwnsEnv) {
    // Closure captures `count` and `btn`, bound inline → env size must be > 0.
    auto ir = emitIR(
        "import ui::widgets\n"
        "func buildCounter(panel: Control) -> Button {\n"
        "  var count = 0\n"
        "  let btn = Button(panel, \"x\")\n"
        "  btn.onClick(|_h: i32| {\n"
        "    count = count + 1\n"
        "    btn.setText(\"y\")\n"
        "  })\n"
        "  return btn\n"
        "}\n"
        "func main() {\n"
        "  appInit()\n"
        "  let win = Window(200, 100, \"T\")\n"
        "  let panel = Panel(win)\n"
        "  let b = buildCounter(panel)\n"
        "}\n",
        "inline_cb_heap");
    ASSERT_TRUE(emitsClean(ir));
    ASSERT_TRUE(hasOnClickCall(ir)) << "no liva_ui_on_click call in IR";
    // The inline closure literal captures `count`/`btn`, so the fast path must
    // emit a call with a non-zero env size (heap-own).
    EXPECT_TRUE(anyOnClickHeapOwns(ir))
        << "expected a heap-own (non-zero env size) call site";
}

TEST(UICodegenExec, NonLiteralCallbackUsesStackEnv) {
    // Closure stored in a variable first → falls through to the ordinary
    // method (stack env, size 0). Documents the Phase-1 limitation.
    auto ir = emitIR(
        "import ui::widgets\n"
        "func main() {\n"
        "  appInit()\n"
        "  let win = Window(200, 100, \"T\")\n"
        "  let b = Button(win, \"x\")\n"
        "  var n = 0\n"
        "  let cb = |_h: i32| { n = n + 1 }\n"
        "  b.onClick(cb)\n"
        "}\n",
        "nonliteral_cb_stack");
    ASSERT_TRUE(emitsClean(ir));
    // A non-literal argument does NOT take the heap-own fast path: it lowers
    // through the ordinary Control.onClick method (env size 0 — stack env).
    // (Phase-1 documented limitation.) No call site heap-owns.
    ASSERT_TRUE(hasOnClickCall(ir)) << "no liva_ui_on_click call in IR";
    EXPECT_FALSE(anyOnClickHeapOwns(ir))
        << "non-literal callback must not heap-own the env";
}

// ── Phase 2: menu system ───────────────────────────────────────────────
TEST(UICodegenExec, MenuSystemCompiles) {
    auto ir = emitIR(
        "import ui::widgets\n"
        "import ui::menu\n"
        "func main() {\n"
        "  appInit()\n"
        "  let win = Window(640, 480, \"T\")\n"
        "  let m = Menu(\"Dosya\")\n"
        "  let item = m.addItem(\"Ac\", |_h: i32| { messageBox(\"i\", \"a\", 1) })\n"
        "  item.setEnabled(false)\n"
        "  m.addSeparator()\n"
        "  m.addCheckItem(\"Kalin\", |_h: i32| { })\n"
        "  let mb = MenuBar()\n"
        "  mb.addMenu(m)\n"
        "  win.setMenuBar(mb)\n"
        "}\n",
        "menu_system");
    EXPECT_TRUE(emitsClean(ir));
}

// True if any liva_ui_menu_item_on_click call passes a non-zero env size.
static bool anyMenuItemHeapOwns(const std::string &ir) {
    const std::string needle = "call void @liva_ui_menu_item_on_click(";
    for (size_t p = ir.find(needle); p != std::string::npos;
         p = ir.find(needle, p + 1)) {
        size_t end = ir.find(')', p);
        if (end == std::string::npos) break;
        if (ir.substr(p, end - p + 1).find(", i32 0)") == std::string::npos)
            return true;
    }
    return false;
}

TEST(UICodegenExec, MenuAddItemInlineHeapOwns) {
    // The primary short-cut menu.addItem("x", |..|{..}) with a capture must
    // heap-own the env (the addItem intrinsic binds the literal directly),
    // even inside a helper that returns the item.
    auto ir = emitIR(
        "import ui::widgets\n"
        "func build() -> MenuItem {\n"
        "  var n = 0\n"
        "  let m = Menu(\"M\")\n"
        "  let item = m.addItem(\"x\", |_h: i32| { n = n + 1 })\n"
        "  return item\n"
        "}\n"
        "func main() {\n"
        "  appInit()\n"
        "  let i = build()\n"
        "}\n",
        "menu_additem_heap");
    ASSERT_TRUE(emitsClean(ir));
    EXPECT_TRUE(anyMenuItemHeapOwns(ir))
        << "menu.addItem inline closure literal must heap-own the env";
}

TEST(UICodegenExec, ContextMenuCompiles) {
    auto ir = emitIR(
        "import ui::widgets\n"
        "func main() {\n"
        "  appInit()\n"
        "  let win = Window(400, 300, \"T\")\n"
        "  let panel = Panel(win)\n"
        "  panel.onRightClick(|x: i32, y: i32| {\n"
        "    let ctx = Menu(\"\")\n"
        "    ctx.addItem(\"Kopyala\", |_h: i32| { })\n"
        "    ctx.popup(panel)\n"
        "  })\n"
        "}\n",
        "context_menu");
    EXPECT_TRUE(emitsClean(ir));
}

TEST(UICodegenExec, StatusBarAndToolbarCompile) {
    auto ir = emitIR(
        "import ui::widgets\n"
        "func main() {\n"
        "  appInit()\n"
        "  let win = Window(400, 300, \"T\")\n"
        "  let sb = win.setStatusBar(2)\n"
        "  sb.setText(0, \"Hazir\")\n"
        "  let tb = Toolbar(win)\n"
        "  let t = tb.addTool(\"Yeni\", |_h: i32| { })\n"
        "  t.setEnabled(true)\n"
        "  tb.addSeparator()\n"
        "  tb.realize()\n"
        "}\n",
        "statusbar_toolbar");
    EXPECT_TRUE(emitsClean(ir));
}

// ── Phase 3: new widgets ───────────────────────────────────────────────
TEST(UICodegenExec, SpinCtrlCompiles) {
    auto ir = emitIR(
        "import ui::widgets\n"
        "func main() {\n"
        "  appInit()\n"
        "  let win = Window(400, 300, \"T\")\n"
        "  let sp = SpinCtrl(win, 0, 100, 5)\n"
        "  sp.setValue(10)\n"
        "  println(sp.getValue())\n"
        "  sp.onChange(|_h: i32| { })\n"
        "}\n",
        "spin_ctrl");
    EXPECT_TRUE(emitsClean(ir));
}

TEST(UICodegenExec, DatePickerCompiles) {
    auto ir = emitIR(
        "import ui::widgets\n"
        "func main() {\n"
        "  appInit()\n"
        "  let win = Window(400, 300, \"T\")\n"
        "  let dp = DatePicker(win)\n"
        "  println(dp.getDate())\n"
        "  dp.onChange(|_h: i32| { })\n"
        "}\n",
        "date_picker");
    EXPECT_TRUE(emitsClean(ir));
}

TEST(UICodegenExec, ComboBoxCompiles) {
    auto ir = emitIR(
        "import ui::widgets\n"
        "func main() {\n"
        "  appInit()\n"
        "  let win = Window(400, 300, \"T\")\n"
        "  let cb = ComboBox(win, \"baslangic\")\n"
        "  cb.addItem(\"bir\")\n"
        "  cb.addItem(\"iki\")\n"
        "  println(cb.getText())\n"
        "  println(cb.getSelection())\n"
        "  cb.onSelect(|_h: i32| { })\n"
        "  cb.onChange(|_h: i32| { })\n"
        "}\n",
        "combo_box");
    EXPECT_TRUE(emitsClean(ir));
}

TEST(UICodegenExec, TreeViewCompiles) {
    auto ir = emitIR(
        "import ui::widgets\n"
        "func main() {\n"
        "  appInit()\n"
        "  let win = Window(400, 300, \"T\")\n"
        "  let tree = TreeView(win)\n"
        "  let root = tree.addRoot(\"Proje\")\n"
        "  let src = tree.addNode(root, \"src\")\n"
        "  tree.addNode(src, \"main.liva\")\n"
        "  println(tree.getSelection())\n"
        "}\n",
        "tree_view");
    EXPECT_TRUE(emitsClean(ir));
}

// True if any liva_ui_on_select CALL site passes a non-zero env size (4th arg),
// i.e. the heap-own fast path fired for a Control-subclass widget.
static bool anySelectHeapOwns(const std::string &ir) {
    const std::string needle = "call void @liva_ui_on_select(";
    for (size_t p = ir.find(needle); p != std::string::npos;
         p = ir.find(needle, p + 1)) {
        size_t end = ir.find(')', p);
        if (end == std::string::npos) break;
        if (ir.substr(p, end - p + 1).find(", i32 0)") == std::string::npos)
            return true;
    }
    return false;
}

TEST(UICodegenExec, TreeViewInlineSelectHeapOwns) {
    // Inline onSelect closure on a Control subclass (TreeView) must take the
    // generalized fast path and heap-own its env, even inside a helper that
    // returns the widget. Confirms the fast path auto-covers new widgets.
    auto ir = emitIR(
        "import ui::widgets\n"
        "func build(parent: Control) -> TreeView {\n"
        "  var n = 0\n"
        "  let tree = TreeView(parent)\n"
        "  tree.onSelect(|_h: i32| { n = n + 1 })\n"
        "  return tree\n"
        "}\n"
        "func main() {\n"
        "  appInit()\n"
        "  let win = Window(400, 300, \"T\")\n"
        "  let t = build(win)\n"
        "}\n",
        "tree_select_heap");
    ASSERT_TRUE(emitsClean(ir));
    EXPECT_TRUE(anySelectHeapOwns(ir))
        << "inline onSelect on a Control subclass must heap-own the env";
}

#endif // LIVA_HAS_LLVM
