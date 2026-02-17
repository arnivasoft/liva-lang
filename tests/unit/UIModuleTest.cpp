#include "liva/Common/Diagnostics.h"
#include "liva/Common/SourceLocation.h"
#include "liva/Lexer/Lexer.h"
#include "liva/Parser/Parser.h"
#include "liva/Sema/ModuleLoader.h"
#include "liva/Sema/Sema.h"
#include <gtest/gtest.h>

using namespace liva;

class UIModuleTest : public ::testing::Test {
protected:
    struct SemaResult {
        std::unique_ptr<SourceManager> sm;
        DiagnosticsEngine diag;
        std::unique_ptr<TranslationUnit> tu;
        bool passed;
    };

    SemaResult check(const std::string &source, bool withModuleLoader = true,
                      const std::string &extraSearchPath = "") {
        SemaResult r;
        r.sm = std::make_unique<SourceManager>("test.liva", source);
        r.diag.setSourceManager(r.sm.get());
        Lexer lexer(*r.sm, r.diag);
        Parser parser(lexer, r.diag);
        r.tu = parser.parseTranslationUnit();
        if (r.diag.hasErrors()) {
            r.passed = false;
            return r;
        }
        if (withModuleLoader) {
            ModuleLoader loader;
            if (!extraSearchPath.empty())
                loader.addSearchPath(extraSearchPath);
            Sema sema(r.diag, &loader);
            sema.analyze(*r.tu);
        } else {
            Sema sema(r.diag);
            sema.analyze(*r.tu);
        }
        r.passed = !r.diag.hasErrors();
        return r;
    }
};

// ---- Module registration tests ----

TEST_F(UIModuleTest, ImportStdUI) {
    auto r = check("import std::ui\nfunc main() {}");
    EXPECT_TRUE(r.passed) << "import std::ui should be recognized as builtin module";
}

TEST_F(UIModuleTest, ImportStdUIUseFunctions) {
    auto r = check(
        "import std::ui\n"
        "func main() {\n"
        "    initWindow(800, 600, \"Hello\")\n"
        "    closeWindow()\n"
        "}\n"
    );
    EXPECT_TRUE(r.passed) << "UI functions should resolve after import std::ui";
}

TEST_F(UIModuleTest, UIFunctionsNotAvailableWithoutImport) {
    auto r = check(
        "func main() {\n"
        "    initWindow(800, 600, \"Hello\")\n"
        "}\n"
    );
    EXPECT_FALSE(r.passed) << "initWindow should not be available without import";
}

TEST_F(UIModuleTest, UmbrellaImportIncludesUI) {
    auto r = check(
        "import std\n"
        "func main() {\n"
        "    initWindow(800, 600, \"Test\")\n"
        "    closeWindow()\n"
        "}\n"
    );
    EXPECT_TRUE(r.passed) << "import std (umbrella) should include UI functions";
}

TEST_F(UIModuleTest, DrawingFunctions) {
    auto r = check(
        "import std::ui\n"
        "func main() {\n"
        "    beginDrawing()\n"
        "    clearBackground(0, 0, 0, 255)\n"
        "    drawRect(10, 10, 100, 100, 255, 0, 0, 255)\n"
        "    drawText(\"Hello\", 50, 50, 20, 255, 255, 255, 255)\n"
        "    drawLine(0, 0, 100, 100, 255, 255, 0, 255)\n"
        "    drawCircle(200, 200, 30, 0, 255, 0, 255)\n"
        "    endDrawing()\n"
        "}\n"
    );
    EXPECT_TRUE(r.passed) << "All drawing functions should resolve";
}

TEST_F(UIModuleTest, InputFunctions) {
    auto r = check(
        "import std::ui\n"
        "func main() {\n"
        "    let pressed = isMousePressed(0)\n"
        "    let released = isMouseReleased(0)\n"
        "    let down = isMouseDown(0)\n"
        "    let mx = getMouseX()\n"
        "    let my = getMouseY()\n"
        "    let kp = isKeyPressed(32)\n"
        "    let kd = isKeyDown(32)\n"
        "    let ch = getCharPressed()\n"
        "    let key = getKeyPressed()\n"
        "}\n"
    );
    EXPECT_TRUE(r.passed) << "All input functions should resolve";
}

TEST_F(UIModuleTest, WindowAndTimeFunctions) {
    auto r = check(
        "import std::ui\n"
        "func main() {\n"
        "    let w = getScreenWidth()\n"
        "    let h = getScreenHeight()\n"
        "    let shouldClose = windowShouldClose()\n"
        "    setTargetFps(60)\n"
        "    let mt = measureText(\"test\", 20)\n"
        "}\n"
    );
    EXPECT_TRUE(r.passed) << "Window and utility functions should resolve";
}

TEST_F(UIModuleTest, ScissorFunctions) {
    auto r = check(
        "import std::ui\n"
        "func main() {\n"
        "    beginScissor(10, 10, 200, 200)\n"
        "    endScissor()\n"
        "}\n"
    );
    EXPECT_TRUE(r.passed) << "Scissor functions should resolve";
}

TEST_F(UIModuleTest, GetFrameTime) {
    auto r = check(
        "import std::ui\n"
        "func main() {\n"
        "    let dt = getFrameTime()\n"
        "}\n"
    );
    EXPECT_TRUE(r.passed) << "getFrameTime should resolve";
}

// ---- File-based module tests (ui::types) ----

TEST_F(UIModuleTest, ImportUITypes) {
    // stdlib/ should contain ui/types.liva
    auto r = check(
        "import ui::types\n"
        "func main() {\n"
        "    let c = Color.red()\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "import ui::types should resolve Color struct";
}

TEST_F(UIModuleTest, UITypesRect) {
    auto r = check(
        "import ui::types\n"
        "func main() {\n"
        "    let r = Rect.new(10, 20, 100, 50)\n"
        "    let inside = r.contains(15, 25)\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "Rect.new and contains should work";
}

TEST_F(UIModuleTest, UITypesCanvas) {
    auto r = check(
        "import ui::types\n"
        "func main() {\n"
        "    let canvas = Canvas { width: 800, height: 600 }\n"
        "    let c = Color.white()\n"
        "    canvas.text(\"hello\", 10, 10, 20, c)\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "Canvas.text should work with Color param";
}

// ---- Phase 3: Widget protocol + layout tests ----

TEST_F(UIModuleTest, ImportUIWidgets) {
    auto r = check(
        "import ui::types\n"
        "import ui::widgets\n"
        "func main() {\n"
        "    let lbl = Label.new(\"Hi\", 20, Color.white())\n"
        "    let w = lbl.getWidth()\n"
        "    let h = lbl.getHeight()\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "import ui::widgets should resolve Label";
}

TEST_F(UIModuleTest, WidgetButton) {
    auto r = check(
        "import ui::widgets\n"
        "func main() {\n"
        "    let btn = Button.new(\"+\", 1)\n"
        "    let w = btn.getWidth()\n"
        "    let h = btn.getHeight()\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "Button.new and size methods should work";
}

TEST_F(UIModuleTest, WidgetSpacer) {
    auto r = check(
        "import ui::widgets\n"
        "func main() {\n"
        "    let sp = Spacer.new(10, 20)\n"
        "    let vs = Spacer.vertical(30)\n"
        "    let hs = Spacer.horizontal(40)\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "Spacer constructors should work";
}

TEST_F(UIModuleTest, WidgetPanel) {
    auto r = check(
        "import ui::types\n"
        "import ui::widgets\n"
        "func main() {\n"
        "    let p = Panel.new(Color.gray(40), 8, 200, 100)\n"
        "    let w = p.getWidth()\n"
        "    let h = p.getHeight()\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "Panel with size computation should work";
}

TEST_F(UIModuleTest, WidgetDivider) {
    auto r = check(
        "import ui::types\n"
        "import ui::widgets\n"
        "func main() {\n"
        "    let hd = Divider.horizontalLine(200, Color.gray(80))\n"
        "    let vd = Divider.verticalLine(100, Color.gray(80))\n"
        "    let w = hd.getWidth()\n"
        "    let h = vd.getHeight()\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "Divider horizontal/vertical should work";
}

TEST_F(UIModuleTest, WidgetProtocolConformance) {
    auto r = check(
        "import ui::types\n"
        "import ui::widgets\n"
        "func renderWidget(w: dyn Widget, x: i32, y: i32) {\n"
        "    w.draw(x, y)\n"
        "}\n"
        "func main() {\n"
        "    let lbl = Label.new(\"Hello\", 20, Color.white())\n"
        "    renderWidget(lbl, 10, 10)\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "Label should conform to Widget protocol via dyn dispatch";
}

TEST_F(UIModuleTest, DynWidgetArray) {
    auto r = check(
        "import ui::types\n"
        "import ui::widgets\n"
        "func main() {\n"
        "    let lbl = Label.new(\"Title\", 24, Color.white())\n"
        "    let sp = Spacer.vertical(10)\n"
        "    let btn = Button.new(\"Click\", 1)\n"
        "    let items: [dyn Widget] = [lbl, sp, btn]\n"
        "    for item in items {\n"
        "        item.draw(0, 0)\n"
        "    }\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "[dyn Widget] array with mixed widget types should work";
}

TEST_F(UIModuleTest, HitTestFunction) {
    auto r = check(
        "import ui::widgets\n"
        "func main() {\n"
        "    let inside = hitTest(50, 50, 0, 0, 100, 100)\n"
        "    let outside = hitTest(200, 200, 0, 0, 100, 100)\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "hitTest utility function should resolve";
}


TEST_F(UIModuleTest, ImportUILayoutMinimal) {
    // Test if layout module loads
    auto r = check(
        "import ui::layout\n"
        "func main() {\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "import ui::layout should load without errors";
}

TEST_F(UIModuleTest, ImportUILayout) {
    auto r = check(
        "import ui::types\n"
        "import ui::widgets\n"
        "import ui::layout\n"
        "func main() {\n"
        "    let lbl = Label.new(\"Hi\", 20, Color.white())\n"
        "    let btn = Button.new(\"OK\", 1)\n"
        "    let row: [dyn Widget] = [lbl, btn]\n"
        "    layoutHStack(row, 10, 10, 8)\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "import ui::layout should make layoutHStack available";
}

TEST_F(UIModuleTest, LayoutVStack) {
    auto r = check(
        "import ui::types\n"
        "import ui::widgets\n"
        "import ui::layout\n"
        "func main() {\n"
        "    let a = Label.new(\"Line 1\", 20, Color.white())\n"
        "    let sp = Spacer.vertical(8)\n"
        "    let b = Label.new(\"Line 2\", 16, Color.gray(200))\n"
        "    let col: [dyn Widget] = [a, sp, b]\n"
        "    layoutVStack(col, 10, 10, 4)\n"
        "    let c = Label.new(\"X\", 20, Color.white())\n"
        "    let col2: [dyn Widget] = [c]\n"
        "    let h = vstackHeight(col2, 4)\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "layoutVStack and vstackHeight should work";
}

TEST_F(UIModuleTest, LayoutCentered) {
    auto r = check(
        "import ui::types\n"
        "import ui::widgets\n"
        "import ui::layout\n"
        "func main() {\n"
        "    let title = Label.new(\"Centered\", 28, Color.white())\n"
        "    let items: [dyn Widget] = [title]\n"
        "    layoutVStackCentered(items, 100, 10)\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "layoutVStackCentered should work";
}

TEST_F(UIModuleTest, ButtonStyled) {
    auto r = check(
        "import ui::types\n"
        "import ui::widgets\n"
        "func main() {\n"
        "    let btn = Button.styled(\"Go\", 1,\n"
        "        Color { r: 60, g: 160, b: 60, a: 255 },\n"
        "        Color.white())\n"
        "    let w = btn.getWidth()\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "Button.styled factory should work";
}

// ---- Phase 4: Interactive widgets ----

TEST_F(UIModuleTest, NewBuiltinGetMouseWheel) {
    auto r = check(
        "import std::ui\n"
        "func main() {\n"
        "    let w = getMouseWheel()\n"
        "}\n");
    EXPECT_TRUE(r.passed) << "getMouseWheel should resolve";
}

TEST_F(UIModuleTest, NewBuiltinDrawRectLines) {
    auto r = check(
        "import std::ui\n"
        "func main() {\n"
        "    drawRectLines(10, 20, 100, 50, 255, 255, 255, 255)\n"
        "}\n");
    EXPECT_TRUE(r.passed) << "drawRectLines should resolve";
}

TEST_F(UIModuleTest, CharToStringBuiltin) {
    auto r = check(
        "import std::convert\n"
        "func main() {\n"
        "    let s = charToString(65)\n"
        "}\n");
    EXPECT_TRUE(r.passed) << "charToString should resolve";
}

TEST_F(UIModuleTest, WidgetTextInput) {
    auto r = check(
        "import ui::types\n"
        "import ui::widgets\n"
        "func main() {\n"
        "    var ti = TextInput.new(\"Enter name...\", 200)\n"
        "    let w = ti.getWidth()\n"
        "    let h = ti.getHeight()\n"
        "    let txt = ti.getText()\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "TextInput construction and methods should work";
}

TEST_F(UIModuleTest, WidgetTextInputUpdate) {
    auto r = check(
        "import ui::types\n"
        "import ui::widgets\n"
        "func main() {\n"
        "    var ti = TextInput.new(\"Type here\", 250)\n"
        "    ti.update(10, 10)\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "TextInput.update should type-check";
}

TEST_F(UIModuleTest, WidgetCheckbox) {
    auto r = check(
        "import ui::types\n"
        "import ui::widgets\n"
        "func main() {\n"
        "    var cb = Checkbox.new(\"Enable\", false)\n"
        "    cb.update(10, 10)\n"
        "    let checked = cb.isChecked()\n"
        "    let w = cb.getWidth()\n"
        "    let h = cb.getHeight()\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "Checkbox construction and methods should work";
}

TEST_F(UIModuleTest, WidgetSlider) {
    auto r = check(
        "import ui::types\n"
        "import ui::widgets\n"
        "func main() {\n"
        "    var sl = Slider.new(0, 100, 200)\n"
        "    sl.update(10, 10)\n"
        "    let v = sl.getValue()\n"
        "    let w = sl.getWidth()\n"
        "    let h = sl.getHeight()\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "Slider construction and methods should work";
}

TEST_F(UIModuleTest, WidgetScrollView) {
    auto r = check(
        "import ui::types\n"
        "import ui::widgets\n"
        "func main() {\n"
        "    var sv = ScrollView.new(300, 400, 800)\n"
        "    sv.update(10, 10)\n"
        "    sv.beginDraw(10, 10)\n"
        "    sv.endDraw()\n"
        "    let sy = sv.getScrollY()\n"
        "    let w = sv.getWidth()\n"
        "    let h = sv.getHeight()\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "ScrollView construction and methods should work";
}

TEST_F(UIModuleTest, WidgetProgressBar) {
    auto r = check(
        "import ui::types\n"
        "import ui::widgets\n"
        "func main() {\n"
        "    var pb = ProgressBar.new(200, 20)\n"
        "    pb.setValue(50)\n"
        "    let w = pb.getWidth()\n"
        "    let h = pb.getHeight()\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "ProgressBar construction and methods should work";
}

TEST_F(UIModuleTest, InteractiveWidgetProtocolConformance) {
    auto r = check(
        "import ui::types\n"
        "import ui::widgets\n"
        "func renderWidget(w: dyn Widget, x: i32, y: i32) {\n"
        "    w.draw(x, y)\n"
        "}\n"
        "func main() {\n"
        "    let ti = TextInput.new(\"Name\", 200)\n"
        "    let cb = Checkbox.new(\"On\", true)\n"
        "    let sl = Slider.new(0, 100, 200)\n"
        "    let pb = ProgressBar.new(200, 20)\n"
        "    renderWidget(ti, 10, 10)\n"
        "    renderWidget(cb, 10, 50)\n"
        "    renderWidget(sl, 10, 90)\n"
        "    renderWidget(pb, 10, 130)\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "Interactive widgets should conform to Widget protocol";
}

TEST_F(UIModuleTest, DynWidgetArrayWithInteractive) {
    auto r = check(
        "import ui::types\n"
        "import ui::widgets\n"
        "func main() {\n"
        "    let lbl = Label.new(\"Form\", 24, Color.white())\n"
        "    let ti = TextInput.new(\"Enter...\", 200)\n"
        "    let cb = Checkbox.new(\"Accept\", false)\n"
        "    let sl = Slider.new(0, 100, 200)\n"
        "    let pb = ProgressBar.new(200, 10)\n"
        "    let items: [dyn Widget] = [lbl, ti, cb, sl, pb]\n"
        "    for item in items {\n"
        "        item.draw(0, 0)\n"
        "    }\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "[dyn Widget] array with interactive widgets should work";
}

// ---- Phase 5: Theme + Styling tests ----

TEST_F(UIModuleTest, ImportUITheme) {
    auto r = check(
        "import ui::theme\n"
        "func main() {\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "import ui::theme should load without errors";
}

TEST_F(UIModuleTest, ThemeDarkFactory) {
    auto r = check(
        "import ui::types\n"
        "import ui::theme\n"
        "func main() {\n"
        "    let t = Theme.dark()\n"
        "    let bg = t.background\n"
        "    let fs = t.fontSize\n"
        "    let sp = t.spacing\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "Theme.dark() should create theme with accessible fields";
}

TEST_F(UIModuleTest, ThemeLightFactory) {
    auto r = check(
        "import ui::types\n"
        "import ui::theme\n"
        "func main() {\n"
        "    let t = Theme.light()\n"
        "    let prim = t.primary\n"
        "    let txt = t.text\n"
        "    let pad = t.padding\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "Theme.light() should create theme with accessible fields";
}

TEST_F(UIModuleTest, ThemeCustomStruct) {
    auto r = check(
        "import ui::types\n"
        "import ui::theme\n"
        "func main() {\n"
        "    let t = Theme {\n"
        "        background: Color.black(),\n"
        "        surface: Color.gray(40),\n"
        "        surfaceAlt: Color.gray(30),\n"
        "        primary: Color.blue(),\n"
        "        primaryHover: Color.blue(),\n"
        "        onPrimary: Color.white(),\n"
        "        text: Color.white(),\n"
        "        textSecondary: Color.gray(120),\n"
        "        success: Color.green(),\n"
        "        error: Color.red(),\n"
        "        border: Color.gray(100),\n"
        "        borderFocused: Color.blue(),\n"
        "        divider: Color.gray(80),\n"
        "        fontSize: 18,\n"
        "        padding: 6,\n"
        "        spacing: 10,\n"
        "        scrollSpeed: 25\n"
        "    }\n"
        "    let bg = t.background\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "Custom Theme struct literal should work";
}

TEST_F(UIModuleTest, ButtonThemed) {
    auto r = check(
        "import ui::types\n"
        "import ui::theme\n"
        "import ui::widgets\n"
        "func main() {\n"
        "    let t = Theme.dark()\n"
        "    let btn = Button.themed(\"OK\", 1, t)\n"
        "    let w = btn.getWidth()\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "Button.themed() factory should work";
}

TEST_F(UIModuleTest, TextInputThemed) {
    auto r = check(
        "import ui::types\n"
        "import ui::theme\n"
        "import ui::widgets\n"
        "func main() {\n"
        "    let t = Theme.light()\n"
        "    var ti = TextInput.themed(\"Name\", 200, t)\n"
        "    let w = ti.getWidth()\n"
        "    let txt = ti.getText()\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "TextInput.themed() factory should work";
}

TEST_F(UIModuleTest, CheckboxThemed) {
    auto r = check(
        "import ui::types\n"
        "import ui::theme\n"
        "import ui::widgets\n"
        "func main() {\n"
        "    let t = Theme.dark()\n"
        "    var cb = Checkbox.themed(\"Enable\", false, t)\n"
        "    let checked = cb.isChecked()\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "Checkbox.themed() factory should work";
}

TEST_F(UIModuleTest, SliderThemed) {
    auto r = check(
        "import ui::types\n"
        "import ui::theme\n"
        "import ui::widgets\n"
        "func main() {\n"
        "    let t = Theme.dark()\n"
        "    var sl = Slider.themed(0, 100, 200, t)\n"
        "    let v = sl.getValue()\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "Slider.themed() factory should work";
}

TEST_F(UIModuleTest, ScrollViewThemed) {
    auto r = check(
        "import ui::types\n"
        "import ui::theme\n"
        "import ui::widgets\n"
        "func main() {\n"
        "    let t = Theme.dark()\n"
        "    var sv = ScrollView.themed(300, 400, 800, t)\n"
        "    let sy = sv.getScrollY()\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "ScrollView.themed() factory should work";
}

TEST_F(UIModuleTest, ProgressBarThemed) {
    auto r = check(
        "import ui::types\n"
        "import ui::theme\n"
        "import ui::widgets\n"
        "func main() {\n"
        "    let t = Theme.light()\n"
        "    var pb = ProgressBar.themed(200, 20, t)\n"
        "    pb.setValue(75)\n"
        "    let w = pb.getWidth()\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "ProgressBar.themed() factory should work";
}

TEST_F(UIModuleTest, PanelThemed) {
    auto r = check(
        "import ui::types\n"
        "import ui::theme\n"
        "import ui::widgets\n"
        "func main() {\n"
        "    let t = Theme.dark()\n"
        "    let p = Panel.themed(t, 12, 200, 100)\n"
        "    let w = p.getWidth()\n"
        "    let h = p.getHeight()\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "Panel.themed() factory should work";
}

TEST_F(UIModuleTest, BackwardCompatNoTheme) {
    auto r = check(
        "import ui::types\n"
        "import ui::widgets\n"
        "func main() {\n"
        "    let btn = Button.new(\"Go\", 1)\n"
        "    var ti = TextInput.new(\"Enter\", 200)\n"
        "    var cb = Checkbox.new(\"On\", true)\n"
        "    var sl = Slider.new(0, 50, 150)\n"
        "    var sv = ScrollView.new(300, 200, 600)\n"
        "    var pb = ProgressBar.new(180, 16)\n"
        "    let p = Panel.new(Color.gray(40), 8, 200, 100)\n"
        "    let d = Divider.horizontalLine(200, Color.gray(80))\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "Old API (new()) should still work without theme import";
}

// ---- Callback tests ----

TEST_F(UIModuleTest, ButtonWithCallback) {
    auto r = check(
        "import std::ui\n"
        "import ui::types\n"
        "import ui::theme\n"
        "import ui::widgets\n"
        "func main() {\n"
        "    let dark = Theme.dark()\n"
        "    let btn = Button.withClick(\"OK\", 1, dark) |id: i32| {\n"
        "        println(id)\n"
        "    }\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "Button.withClick with trailing closure should pass sema";
}

TEST_F(UIModuleTest, CheckboxWithCallback) {
    auto r = check(
        "import std::ui\n"
        "import ui::types\n"
        "import ui::theme\n"
        "import ui::widgets\n"
        "func main() {\n"
        "    let dark = Theme.dark()\n"
        "    let cb = Checkbox.withToggle(\"Enable\", false, dark) |checked: bool| {\n"
        "        println(checked)\n"
        "    }\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "Checkbox.withToggle with trailing closure should pass sema";
}

TEST_F(UIModuleTest, SliderWithCallback) {
    auto r = check(
        "import std::ui\n"
        "import ui::types\n"
        "import ui::theme\n"
        "import ui::widgets\n"
        "func main() {\n"
        "    let dark = Theme.dark()\n"
        "    let sl = Slider.withChange(0, 100, 200, dark) |val: i32| {\n"
        "        println(val)\n"
        "    }\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "Slider.withChange with trailing closure should pass sema";
}

TEST_F(UIModuleTest, BackwardCompatNoCallback) {
    auto r = check(
        "import std::ui\n"
        "import ui::types\n"
        "import ui::widgets\n"
        "func main() {\n"
        "    let btn = Button.new(\"Go\", 1)\n"
        "    var cb = Checkbox.new(\"On\", true)\n"
        "    var sl = Slider.new(0, 50, 150)\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "Old API (new()) with closure fields should still work";
}

// ---- Phase 7: RadioGroup, TabView, Dropdown, Dialog ----

TEST_F(UIModuleTest, RadioGroupOf3) {
    auto r = check(
        "import std::ui\n"
        "import ui::types\n"
        "import ui::widgets\n"
        "func main() {\n"
        "    var rg = RadioGroup.of3(\"Red\", \"Green\", \"Blue\")\n"
        "    let sel = rg.getSelected()\n"
        "    let opt = rg.getOption(0)\n"
        "    let w = rg.getWidth()\n"
        "    let h = rg.getHeight()\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "RadioGroup.of3 construction and methods should work";
}

TEST_F(UIModuleTest, RadioGroupThemed) {
    auto r = check(
        "import std::ui\n"
        "import ui::types\n"
        "import ui::theme\n"
        "import ui::widgets\n"
        "func main() {\n"
        "    let t = Theme.dark()\n"
        "    var rg = RadioGroup.themed3(\"Small\", \"Medium\", \"Large\", t)\n"
        "    rg.update(10, 10)\n"
        "    let sel = rg.getSelected()\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "RadioGroup.themed3 with theme should work";
}

TEST_F(UIModuleTest, DropdownOf3) {
    auto r = check(
        "import std::ui\n"
        "import ui::types\n"
        "import ui::widgets\n"
        "func main() {\n"
        "    var dd = Dropdown.of3(\"Apple\", \"Banana\", \"Cherry\", 200)\n"
        "    let sel = dd.getSelected()\n"
        "    let open = dd.isOpen()\n"
        "    let opt = dd.getOption(1)\n"
        "    let w = dd.getWidth()\n"
        "    let h = dd.getHeight()\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "Dropdown.of3 construction and methods should work";
}

TEST_F(UIModuleTest, DropdownThemed) {
    auto r = check(
        "import std::ui\n"
        "import ui::types\n"
        "import ui::theme\n"
        "import ui::widgets\n"
        "func main() {\n"
        "    let t = Theme.dark()\n"
        "    var dd = Dropdown.themed3(\"One\", \"Two\", \"Three\", 180, t)\n"
        "    dd.update(10, 10)\n"
        "    let sel = dd.getSelected()\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "Dropdown.themed3 with theme should work";
}

TEST_F(UIModuleTest, TabViewOf3) {
    auto r = check(
        "import std::ui\n"
        "import ui::types\n"
        "import ui::widgets\n"
        "func main() {\n"
        "    var tv = TabView.of3(\"Home\", \"Settings\", \"About\")\n"
        "    let sel = tv.getSelected()\n"
        "    let tw = tv.getTabWidth(0)\n"
        "    let w = tv.getWidth()\n"
        "    let h = tv.getHeight()\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "TabView.of3 construction and methods should work";
}

TEST_F(UIModuleTest, TabViewThemed) {
    auto r = check(
        "import std::ui\n"
        "import ui::types\n"
        "import ui::theme\n"
        "import ui::widgets\n"
        "func main() {\n"
        "    let t = Theme.dark()\n"
        "    var tv = TabView.themed3(\"Tab1\", \"Tab2\", \"Tab3\", t)\n"
        "    tv.update(10, 10)\n"
        "    let sel = tv.getSelected()\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "TabView.themed3 with theme should work";
}

TEST_F(UIModuleTest, DialogAlert) {
    auto r = check(
        "import std::ui\n"
        "import ui::types\n"
        "import ui::widgets\n"
        "func main() {\n"
        "    var dlg = Dialog.alert(\"Info\", \"Operation completed.\", \"OK\")\n"
        "    let vis = dlg.isVisible()\n"
        "    dlg.show()\n"
        "    let w = dlg.getWidth()\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "Dialog.alert construction and methods should work";
}

TEST_F(UIModuleTest, DialogConfirmThemed) {
    auto r = check(
        "import std::ui\n"
        "import ui::types\n"
        "import ui::theme\n"
        "import ui::widgets\n"
        "func main() {\n"
        "    let t = Theme.dark()\n"
        "    var dlg = Dialog.confirmThemed(\"Delete?\", \"This cannot be undone.\", \"Delete\", \"Cancel\", t)\n"
        "    dlg.setOnConfirm(|_id: i32| { })\n"
        "    dlg.setOnCancel(|_id: i32| { })\n"
        "    dlg.show()\n"
        "    dlg.update()\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "Dialog.confirmThemed with callbacks should work";
}

// ---- Phase 8: Advanced Layout System ----

TEST_F(UIModuleTest, GridLayout) {
    auto r = check(
        "import ui::types\n"
        "import ui::widgets\n"
        "import ui::layout\n"
        "func main() {\n"
        "    let a = Label.new(\"A\", 20, Color.white())\n"
        "    let b = Label.new(\"B\", 20, Color.white())\n"
        "    let c = Label.new(\"C\", 20, Color.white())\n"
        "    let d = Label.new(\"D\", 20, Color.white())\n"
        "    let items: [dyn Widget] = [a, b, c, d]\n"
        "    layoutGrid(items, 10, 10, 2, 100, 40, 8, 8)\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "layoutGrid with 4 labels in 2x2 should work";
}

TEST_F(UIModuleTest, GridAlignedLayout) {
    auto r = check(
        "import ui::types\n"
        "import ui::widgets\n"
        "import ui::layout\n"
        "func main() {\n"
        "    let a = Label.new(\"A\", 16, Color.white())\n"
        "    let b = Label.new(\"BB\", 20, Color.white())\n"
        "    let c = Label.new(\"CCC\", 14, Color.white())\n"
        "    let d = Label.new(\"D\", 18, Color.white())\n"
        "    let items: [dyn Widget] = [a, b, c, d]\n"
        "    layoutGridAligned(items, 10, 10, 2, 120, 50, 8, 8, 1, 1)\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "layoutGridAligned with center alignment should work";
}

TEST_F(UIModuleTest, GridSizeHelpers) {
    auto r = check(
        "import ui::types\n"
        "import ui::widgets\n"
        "import ui::layout\n"
        "func main() {\n"
        "    let a = Label.new(\"X\", 20, Color.white())\n"
        "    let b = Label.new(\"Y\", 20, Color.white())\n"
        "    let c = Label.new(\"Z\", 20, Color.white())\n"
        "    let items: [dyn Widget] = [a, b, c]\n"
        "    let rows = gridRows(items, 2)\n"
        "    let gw = gridWidth(2, 100, 8)\n"
        "    let gh = gridHeight(rows, 40, 8)\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "gridRows/gridWidth/gridHeight should work";
}

TEST_F(UIModuleTest, VStackAligned) {
    auto r = check(
        "import ui::types\n"
        "import ui::widgets\n"
        "import ui::layout\n"
        "func main() {\n"
        "    let a = Label.new(\"Short\", 20, Color.white())\n"
        "    let b = Label.new(\"Longer text\", 20, Color.white())\n"
        "    let items: [dyn Widget] = [a, b]\n"
        "    layoutVStackAligned(items, 10, 10, 8, 300, 2)\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "layoutVStackAligned with right alignment should work";
}

TEST_F(UIModuleTest, HStackAligned) {
    auto r = check(
        "import ui::types\n"
        "import ui::widgets\n"
        "import ui::layout\n"
        "func main() {\n"
        "    let a = Label.new(\"Top\", 16, Color.white())\n"
        "    let b = Button.new(\"OK\", 1)\n"
        "    let items: [dyn Widget] = [a, b]\n"
        "    layoutHStackAligned(items, 10, 10, 8, 60, 1)\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "layoutHStackAligned with center alignment should work";
}

TEST_F(UIModuleTest, AlignedStacksThemed) {
    auto r = check(
        "import ui::types\n"
        "import ui::theme\n"
        "import ui::widgets\n"
        "import ui::layout\n"
        "func main() {\n"
        "    let t = Theme.dark()\n"
        "    let a = Label.new(\"A\", 20, Color.white())\n"
        "    let b = Label.new(\"B\", 20, Color.white())\n"
        "    let items: [dyn Widget] = [a, b]\n"
        "    layoutVStackAlignedThemed(items, 10, 10, 300, 1, t)\n"
        "    let c = Label.new(\"C\", 20, Color.white())\n"
        "    let d = Label.new(\"D\", 20, Color.white())\n"
        "    let items2: [dyn Widget] = [c, d]\n"
        "    layoutHStackAlignedThemed(items2, 10, 100, 60, 1, t)\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "Themed aligned stack variants should work";
}

TEST_F(UIModuleTest, HStackSpaced) {
    auto r = check(
        "import ui::types\n"
        "import ui::widgets\n"
        "import ui::layout\n"
        "func main() {\n"
        "    let a = Button.new(\"A\", 1)\n"
        "    let b = Button.new(\"B\", 2)\n"
        "    let c = Button.new(\"C\", 3)\n"
        "    let items: [dyn Widget] = [a, b, c]\n"
        "    layoutHStackSpaced(items, 10, 10, 500)\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "layoutHStackSpaced (space-between) should work";
}

TEST_F(UIModuleTest, VStackSpaced) {
    auto r = check(
        "import ui::types\n"
        "import ui::widgets\n"
        "import ui::layout\n"
        "func main() {\n"
        "    let a = Label.new(\"Top\", 20, Color.white())\n"
        "    let b = Label.new(\"Mid\", 20, Color.white())\n"
        "    let c = Label.new(\"Bot\", 20, Color.white())\n"
        "    let items: [dyn Widget] = [a, b, c]\n"
        "    layoutVStackSpaced(items, 10, 10, 400)\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "layoutVStackSpaced (space-between) should work";
}

TEST_F(UIModuleTest, CenteredLayouts) {
    auto r = check(
        "import ui::types\n"
        "import ui::widgets\n"
        "import ui::layout\n"
        "func main() {\n"
        "    let a = Button.new(\"OK\", 1)\n"
        "    let b = Button.new(\"Cancel\", 2)\n"
        "    let items: [dyn Widget] = [a, b]\n"
        "    layoutHStackCentered(items, 0, 10, 800, 8)\n"
        "    let c = Label.new(\"Title\", 24, Color.white())\n"
        "    let d = Button.new(\"Go\", 3)\n"
        "    let items2: [dyn Widget] = [c, d]\n"
        "    layoutVStackCenteredInRect(items2, 0, 0, 800, 600, 10)\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "layoutHStackCentered and layoutVStackCenteredInRect should work";
}

TEST_F(UIModuleTest, PaddedContainers) {
    auto r = check(
        "import ui::types\n"
        "import ui::widgets\n"
        "import ui::layout\n"
        "func main() {\n"
        "    let a = Label.new(\"Line 1\", 20, Color.white())\n"
        "    let b = Label.new(\"Line 2\", 20, Color.white())\n"
        "    let items: [dyn Widget] = [a, b]\n"
        "    layoutVStackPadded(items, 10, 10, 8, 16, 12)\n"
        "    let c = Label.new(\"Line 3\", 20, Color.white())\n"
        "    let d = Label.new(\"Line 4\", 20, Color.white())\n"
        "    let items2: [dyn Widget] = [c, d]\n"
        "    layoutHStackPadded(items2, 10, 200, 8, 16, 12)\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "Padded VStack/HStack should work";
}

// ---- Phase 9: Font & TextArea tests ----

TEST_F(UIModuleTest, FontLoadBuiltin) {
    auto r = check(
        "import std::ui\n"
        "func main() {\n"
        "    let handle = loadFont(\"test.ttf\", 20)\n"
        "    unloadFont(handle)\n"
        "}\n");
    EXPECT_TRUE(r.passed) << "loadFont/unloadFont should pass sema";
}

TEST_F(UIModuleTest, FontDrawBuiltin) {
    auto r = check(
        "import std::ui\n"
        "func main() {\n"
        "    drawTextFont(1, \"Hi\", 10, 10, 20, 255, 255, 255, 255)\n"
        "}\n");
    EXPECT_TRUE(r.passed) << "drawTextFont should pass sema";
}

TEST_F(UIModuleTest, FontMeasureBuiltin) {
    auto r = check(
        "import std::ui\n"
        "func main() {\n"
        "    let w = measureTextFont(1, \"Hello\", 20)\n"
        "}\n");
    EXPECT_TRUE(r.passed) << "measureTextFont should pass sema";
}

TEST_F(UIModuleTest, TextWrappedBuiltin) {
    auto r = check(
        "import std::ui\n"
        "func main() {\n"
        "    let h = drawTextWrapped(\"Hello world\", 10, 10, 20, 200, 255, 255, 255, 255)\n"
        "    let h2 = measureTextWrapped(\"Hello world\", 20, 200)\n"
        "}\n");
    EXPECT_TRUE(r.passed) << "drawTextWrapped/measureTextWrapped should pass sema";
}

TEST_F(UIModuleTest, TextAreaNew) {
    auto r = check(
        "import ui::types\n"
        "import ui::widgets\n"
        "func main() {\n"
        "    var ta = TextArea.new(\"Enter text...\", 400, 200)\n"
        "    let text = ta.getText()\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "TextArea.new and getText should work";
}

TEST_F(UIModuleTest, TextAreaThemed) {
    auto r = check(
        "import ui::types\n"
        "import ui::theme\n"
        "import ui::widgets\n"
        "func main() {\n"
        "    let theme = Theme.dark()\n"
        "    var ta = TextArea.themed(\"Notes\", 400, 300, theme)\n"
        "    let text = ta.getText()\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "TextArea.themed should work";
}

TEST_F(UIModuleTest, TextAreaUpdate) {
    auto r = check(
        "import ui::types\n"
        "import ui::widgets\n"
        "func main() {\n"
        "    var ta = TextArea.new(\"Type here\", 300, 150)\n"
        "    ta.update(10, 10)\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "TextArea.update should pass sema";
}

TEST_F(UIModuleTest, TextAreaGetLineCount) {
    auto r = check(
        "import ui::types\n"
        "import ui::widgets\n"
        "func main() {\n"
        "    var ta = TextArea.new(\"Editor\", 400, 200)\n"
        "    let lines = ta.getLineCount()\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "TextArea.getLineCount should pass sema";
}

TEST_F(UIModuleTest, TextAreaWidgetConformance) {
    auto r = check(
        "import ui::types\n"
        "import ui::widgets\n"
        "func main() {\n"
        "    var ta = TextArea.new(\"Code\", 400, 300)\n"
        "    let w = ta.getWidth()\n"
        "    let h = ta.getHeight()\n"
        "    ta.draw(0, 0)\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "TextArea Widget conformance should work";
}

TEST_F(UIModuleTest, TextAreaInWidgetArray) {
    auto r = check(
        "import ui::types\n"
        "import ui::widgets\n"
        "func main() {\n"
        "    let lbl = Label.new(\"Title\", 24, Color.white())\n"
        "    let ta = TextArea.new(\"Notes\", 400, 200)\n"
        "    let items: [dyn Widget] = [lbl, ta]\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "TextArea in [dyn Widget] array should work";
}

// ---- Phase 10: Animation & Transitions ----

TEST_F(UIModuleTest, ImportAnimation) {
    auto r = check(
        "import ui::animation\n"
        "func main() {\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "import ui::animation should load without errors";
}

TEST_F(UIModuleTest, EasingFunctions) {
    auto r = check(
        "import ui::animation\n"
        "func main() {\n"
        "    let a = easeLinear(0.5)\n"
        "    let b = easeInQuad(0.5)\n"
        "    let c = easeOutQuad(0.5)\n"
        "    let d = easeInOutQuad(0.5)\n"
        "    let e = easeInCubic(0.5)\n"
        "    let f = easeOutCubic(0.5)\n"
        "    let g = easeInOutCubic(0.5)\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "All 7 easing functions should resolve";
}

TEST_F(UIModuleTest, ClampAndApplyEasing) {
    auto r = check(
        "import ui::animation\n"
        "func main() {\n"
        "    let clamped = clampF64(1.5, 0.0, 1.0)\n"
        "    let eased = applyEasing(0.5, 2)\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "clampF64 and applyEasing should resolve";
}

TEST_F(UIModuleTest, LerpFunctions) {
    auto r = check(
        "import ui::types\n"
        "import ui::animation\n"
        "func main() {\n"
        "    let f = lerpF64(0.0, 100.0, 0.5)\n"
        "    let i = lerpI32(0, 255, 0.5)\n"
        "    let c = lerpColor(Color.black(), Color.white(), 0.5)\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "lerpF64, lerpI32, lerpColor should resolve";
}

TEST_F(UIModuleTest, TweenNew) {
    auto r = check(
        "import ui::animation\n"
        "func main() {\n"
        "    let tw = Tween.new(0.0, 100.0, 1.0, 0)\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "Tween.new construction should work";
}

TEST_F(UIModuleTest, TweenLifecycle) {
    auto r = check(
        "import ui::animation\n"
        "func main() {\n"
        "    var tw = Tween.new(0.0, 100.0, 1.0, 2)\n"
        "    tw.start()\n"
        "    tw.update(0.5)\n"
        "    let p = tw.progress()\n"
        "    let v = tw.getValue()\n"
        "    let done = tw.isComplete()\n"
        "    tw.reset()\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "Tween start/update/progress/getValue/isComplete/reset should work";
}

TEST_F(UIModuleTest, ColorTransitionNew) {
    auto r = check(
        "import ui::types\n"
        "import ui::animation\n"
        "func main() {\n"
        "    let ct = ColorTransition.new(Color.black(), Color.white(), 0.5, 1)\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "ColorTransition.new construction should work";
}

TEST_F(UIModuleTest, ColorTransitionLifecycle) {
    auto r = check(
        "import ui::types\n"
        "import ui::animation\n"
        "func main() {\n"
        "    var ct = ColorTransition.new(Color.red(), Color.blue(), 1.0, 0)\n"
        "    ct.start()\n"
        "    ct.update(0.3)\n"
        "    let p = ct.progress()\n"
        "    let c = ct.getValue()\n"
        "    let done = ct.isComplete()\n"
        "    ct.reset()\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "ColorTransition start/update/getValue/isComplete/reset should work";
}

TEST_F(UIModuleTest, HoverAnimatorNew) {
    auto r = check(
        "import ui::types\n"
        "import ui::animation\n"
        "func main() {\n"
        "    var ha = HoverAnimator.new(5.0)\n"
        "    ha.update(true, 0.016)\n"
        "    let p = ha.getProgress()\n"
        "    let c = ha.getColor(Color.gray(40), Color.gray(60))\n"
        "    let v = ha.getValue(0.0, 1.0)\n"
        "    let vi = ha.getValueI32(10, 20)\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "HoverAnimator construction and methods should work";
}

TEST_F(UIModuleTest, FocusAnimatorNew) {
    auto r = check(
        "import ui::types\n"
        "import ui::animation\n"
        "func main() {\n"
        "    var fa = FocusAnimator.new(5.0)\n"
        "    fa.update(true, 0.016)\n"
        "    let p = fa.getProgress()\n"
        "    let c = fa.getColor(Color.gray(60), Color.blue())\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "FocusAnimator construction and methods should work";
}
