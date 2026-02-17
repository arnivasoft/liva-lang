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
