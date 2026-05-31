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

    static std::string stdlibPath() {
        return std::string(LIVA_PROJECT_ROOT) + "/stdlib";
    }

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
            loader.addSearchPath(stdlibPath());
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

// ============================================================
// Module registration tests
// ============================================================

TEST_F(UIModuleTest, ImportStdUI) {
    auto r = check("import std::ui\nfunc main() {}");
    EXPECT_TRUE(r.passed) << "import std::ui should be recognized as builtin module";
}

TEST_F(UIModuleTest, ImportStdUIUseFunctions) {
    auto r = check(
        "import std::ui\n"
        "func main() {\n"
        "    appInit()\n"
        "    let win = createWindow(800, 600, \"Hello\")\n"
        "    windowShow(win, 1)\n"
        "    appRun()\n"
        "}\n"
    );
    EXPECT_TRUE(r.passed) << "UI functions should resolve after import std::ui";
}

TEST_F(UIModuleTest, UIFunctionsNotAvailableWithoutImport) {
    auto r = check(
        "func main() {\n"
        "    appInit()\n"
        "}\n"
    );
    EXPECT_FALSE(r.passed) << "appInit should not be available without import";
}

TEST_F(UIModuleTest, UmbrellaImportIncludesUI) {
    auto r = check(
        "import std\n"
        "func main() {\n"
        "    appInit()\n"
        "    let win = createWindow(800, 600, \"Test\")\n"
        "    appQuit()\n"
        "}\n"
    );
    EXPECT_TRUE(r.passed) << "import std (umbrella) should include UI functions";
}

// ============================================================
// App lifecycle tests
// ============================================================

TEST_F(UIModuleTest, AppLifecycle) {
    auto r = check(
        "import std::ui\n"
        "func main() {\n"
        "    appInit()\n"
        "    appRun()\n"
        "    appQuit()\n"
        "}\n"
    );
    EXPECT_TRUE(r.passed) << "App lifecycle functions should resolve";
}

// ============================================================
// Window functions
// ============================================================

TEST_F(UIModuleTest, WindowFunctions) {
    auto r = check(
        "import std::ui\n"
        "func main() {\n"
        "    let win = createWindow(800, 600, \"Test\")\n"
        "    windowShow(win, 1)\n"
        "    windowSetTitle(win, \"New Title\")\n"
        "    let w = windowGetWidth(win)\n"
        "    let h = windowGetHeight(win)\n"
        "}\n"
    );
    EXPECT_TRUE(r.passed) << "Window functions should resolve";
}

// ============================================================
// Widget creation tests
// ============================================================

TEST_F(UIModuleTest, CreateBasicWidgets) {
    auto r = check(
        "import std::ui\n"
        "func main() {\n"
        "    let win = createWindow(400, 300, \"Test\")\n"
        "    let panel = createPanel(win)\n"
        "    let btn = createButton(panel, \"Click\")\n"
        "    let lbl = createLabel(panel, \"Hello\")\n"
        "    let inp = createTextInput(panel, \"\")\n"
        "    let cb = createCheckbox(panel, \"Check\")\n"
        "}\n"
    );
    EXPECT_TRUE(r.passed) << "Basic widget creation functions should resolve";
}

TEST_F(UIModuleTest, CreateSliderProgressBar) {
    auto r = check(
        "import std::ui\n"
        "func main() {\n"
        "    let panel = createPanel(0)\n"
        "    let sl = createSlider(panel, 0, 100, 50)\n"
        "    let pb = createProgressBar(panel, 100)\n"
        "}\n"
    );
    EXPECT_TRUE(r.passed) << "Slider and progress bar creation should resolve";
}

TEST_F(UIModuleTest, CreateSelectionWidgets) {
    auto r = check(
        "import std::ui\n"
        "func main() {\n"
        "    let panel = createPanel(0)\n"
        "    let rg = createRadioGroup(panel, \"A;B;C\")\n"
        "    let dd = createDropdown(panel, \"X;Y;Z\")\n"
        "    let lb = createListBox(panel)\n"
        "    let tv = createTabView(panel)\n"
        "}\n"
    );
    EXPECT_TRUE(r.passed) << "Selection widget creation should resolve";
}

TEST_F(UIModuleTest, CreateTextWidgets) {
    auto r = check(
        "import std::ui\n"
        "func main() {\n"
        "    let panel = createPanel(0)\n"
        "    let ta = createTextArea(panel, \"multi\\nline\")\n"
        "    let sv = createScrollView(panel)\n"
        "    let div = createDivider(panel)\n"
        "}\n"
    );
    EXPECT_TRUE(r.passed) << "Text and misc widget creation should resolve";
}

TEST_F(UIModuleTest, CreateImageView) {
    auto r = check(
        "import std::ui\n"
        "func main() {\n"
        "    let panel = createPanel(0)\n"
        "    let img = createImageView(panel, \"test.png\")\n"
        "}\n"
    );
    EXPECT_TRUE(r.passed) << "ImageView creation should resolve";
}

// ============================================================
// Widget property tests
// ============================================================

TEST_F(UIModuleTest, WidgetProperties) {
    auto r = check(
        "import std::ui\n"
        "func main() {\n"
        "    let btn = createButton(0, \"Test\")\n"
        "    setText(btn, \"New Text\")\n"
        "    let t = getText(btn)\n"
        "    setValue(btn, 1)\n"
        "    let v = getValue(btn)\n"
        "    setEnabled(btn, 1)\n"
        "    setVisible(btn, 1)\n"
        "}\n"
    );
    EXPECT_TRUE(r.passed) << "Widget property functions should resolve";
}

TEST_F(UIModuleTest, WidgetSizeAndFont) {
    auto r = check(
        "import std::ui\n"
        "func main() {\n"
        "    let btn = createButton(0, \"Test\")\n"
        "    setWidgetSize(btn, 200, 50)\n"
        "    setWidgetFont(btn, 14, 1)\n"
        "}\n"
    );
    EXPECT_TRUE(r.passed) << "Widget size and font functions should resolve";
}

TEST_F(UIModuleTest, WidgetColors) {
    auto r = check(
        "import std::ui\n"
        "func main() {\n"
        "    let btn = createButton(0, \"Test\")\n"
        "    setBgColor(btn, 255, 0, 0)\n"
        "    setFgColor(btn, 255, 255, 255)\n"
        "    setTooltip(btn, \"Tooltip text\")\n"
        "}\n"
    );
    EXPECT_TRUE(r.passed) << "Widget color and tooltip functions should resolve";
}

TEST_F(UIModuleTest, DestroyWidget) {
    auto r = check(
        "import std::ui\n"
        "func main() {\n"
        "    let btn = createButton(0, \"Temp\")\n"
        "    destroyWidget(btn)\n"
        "}\n"
    );
    EXPECT_TRUE(r.passed) << "destroyWidget should resolve";
}

// ============================================================
// Layout (sizer) tests
// ============================================================

TEST_F(UIModuleTest, SizerCreation) {
    auto r = check(
        "import std::ui\n"
        "func main() {\n"
        "    let vbox = createVBoxSizer()\n"
        "    let hbox = createHBoxSizer()\n"
        "    let grid = createGridSizer(2, 3, 5, 5)\n"
        "    let flex = createFlexGridSizer(2, 3, 5, 5)\n"
        "}\n"
    );
    EXPECT_TRUE(r.passed) << "Sizer creation functions should resolve";
}

TEST_F(UIModuleTest, SizerAddAndSet) {
    auto r = check(
        "import std::ui\n"
        "func main() {\n"
        "    let panel = createPanel(0)\n"
        "    let vbox = createVBoxSizer()\n"
        "    let btn = createButton(panel, \"OK\")\n"
        "    sizerAdd(vbox, btn, 0, 0, 5)\n"
        "    setSizer(panel, vbox)\n"
        "}\n"
    );
    EXPECT_TRUE(r.passed) << "sizerAdd and setSizer should resolve";
}

// ============================================================
// Event callback tests
// ============================================================

TEST_F(UIModuleTest, EventCallbacks) {
    auto r = check(
        "import std::ui\n"
        "func main() {\n"
        "    let btn = createButton(0, \"Click\")\n"
        "    onClick(btn, |h: i32| { })\n"
        "}\n"
    );
    EXPECT_TRUE(r.passed) << "onClick with closure should resolve";
}

TEST_F(UIModuleTest, OnChangCallback) {
    auto r = check(
        "import std::ui\n"
        "func main() {\n"
        "    let inp = createTextInput(0, \"\")\n"
        "    onChange(inp, |h: i32| { })\n"
        "}\n"
    );
    EXPECT_TRUE(r.passed) << "onChange with closure should resolve";
}

TEST_F(UIModuleTest, OnSelectCallback) {
    auto r = check(
        "import std::ui\n"
        "func main() {\n"
        "    let dd = createDropdown(0, \"A;B;C\")\n"
        "    onSelect(dd, |h: i32| { })\n"
        "}\n"
    );
    EXPECT_TRUE(r.passed) << "onSelect with closure should resolve";
}

TEST_F(UIModuleTest, OnKeyCallback) {
    auto r = check(
        "import std::ui\n"
        "func main() {\n"
        "    let panel = createPanel(0)\n"
        "    onKey(panel, |h: i32| { })\n"
        "}\n"
    );
    EXPECT_TRUE(r.passed) << "onKey with closure should resolve";
}

TEST_F(UIModuleTest, WindowOnCloseCallback) {
    auto r = check(
        "import std::ui\n"
        "func main() {\n"
        "    let win = createWindow(400, 300, \"Test\")\n"
        "    windowOnClose(win, |h: i32| { })\n"
        "}\n"
    );
    EXPECT_TRUE(r.passed) << "windowOnClose with closure should resolve";
}

// ============================================================
// List and Tab operations
// ============================================================

TEST_F(UIModuleTest, ListOperations) {
    auto r = check(
        "import std::ui\n"
        "func main() {\n"
        "    let lb = createListBox(0)\n"
        "    listAddItem(lb, \"Item 1\")\n"
        "    listAddItem(lb, \"Item 2\")\n"
        "    let sel = listGetSelection(lb)\n"
        "    listClear(lb)\n"
        "}\n"
    );
    EXPECT_TRUE(r.passed) << "List operations should resolve";
}

TEST_F(UIModuleTest, TabOperations) {
    auto r = check(
        "import std::ui\n"
        "func main() {\n"
        "    let tv = createTabView(0)\n"
        "    let page = createPanel(tv)\n"
        "    tabAddPage(tv, page, \"Tab 1\")\n"
        "    let sel = tabGetSelection(tv)\n"
        "}\n"
    );
    EXPECT_TRUE(r.passed) << "Tab operations should resolve";
}

// ============================================================
// Dialog tests
// ============================================================

TEST_F(UIModuleTest, MessageBox) {
    auto r = check(
        "import std::ui\n"
        "func main() {\n"
        "    messageBox(\"Title\", \"Message\", 1)\n"
        "}\n"
    );
    EXPECT_TRUE(r.passed) << "messageBox should resolve";
}

TEST_F(UIModuleTest, FileDialog) {
    auto r = check(
        "import std::ui\n"
        "func main() {\n"
        "    let path = fileDialog(0, \"Open\", \"*.*\", 0)\n"
        "}\n"
    );
    EXPECT_TRUE(r.passed) << "fileDialog should resolve";
}

TEST_F(UIModuleTest, ColorDialog) {
    auto r = check(
        "import std::ui\n"
        "func main() {\n"
        "    let color = colorDialog(0)\n"
        "}\n"
    );
    EXPECT_TRUE(r.passed) << "colorDialog should resolve";
}

// ============================================================
// Timer tests
// ============================================================

TEST_F(UIModuleTest, TimerCreateStop) {
    auto r = check(
        "import std::ui\n"
        "func main() {\n"
        "    let tmr = createTimer(100, |h: i32| { })\n"
        "    stopTimer(tmr)\n"
        "}\n"
    );
    EXPECT_TRUE(r.passed) << "Timer functions should resolve";
}

// ============================================================
// Clipboard tests
// ============================================================

TEST_F(UIModuleTest, Clipboard) {
    auto r = check(
        "import std::ui\n"
        "func main() {\n"
        "    setClipboardText(\"Hello\")\n"
        "    let text = getClipboardText()\n"
        "}\n"
    );
    EXPECT_TRUE(r.passed) << "Clipboard functions should resolve";
}

// ============================================================
// Canvas / custom drawing tests
// ============================================================

TEST_F(UIModuleTest, CanvasCreate) {
    auto r = check(
        "import std::ui\n"
        "func main() {\n"
        "    let canvas = createCanvas(0)\n"
        "    canvasOnPaint(canvas, |dc: i32| { })\n"
        "    canvasRefresh(canvas)\n"
        "}\n"
    );
    EXPECT_TRUE(r.passed) << "Canvas functions should resolve";
}

TEST_F(UIModuleTest, DcDrawFunctions) {
    auto r = check(
        "import std::ui\n"
        "func main() {\n"
        "    dcClear(0, 255, 255, 255)\n"
        "    dcDrawRect(0, 10, 10, 100, 50, 255, 0, 0)\n"
        "    dcDrawText(0, \"Hello\", 20, 20, 0, 0, 0)\n"
        "    dcDrawLine(0, 0, 0, 100, 100, 0, 255, 0)\n"
        "    dcDrawCircle(0, 50, 50, 25, 0, 0, 255)\n"
        "}\n"
    );
    EXPECT_TRUE(r.passed) << "DC drawing functions should resolve";
}

// ============================================================
// Import UI sub-modules
// ============================================================

TEST_F(UIModuleTest, ImportUITypes) {
    auto r = check(
        "import ui::types\n"
        "func main() {\n"
        "    let c = Color.rgb(255, 0, 0)\n"
        "    let r = Rect.new(10, 20, 100, 200)\n"
        "    let v = Vec2.new(5, 10)\n"
        "    let s = Size.new(800, 600)\n"
        "}\n"
    );
    EXPECT_TRUE(r.passed) << "ui::types should be importable and usable";
}

TEST_F(UIModuleTest, ImportUIWidgets) {
    auto r = check(
        "import ui::widgets\n"
        "func main() {\n"
        "    appInit()\n"
        "    let win1 = Window(800, 600, \"Test\")\n"
        "    let win2 = Window(800, 600, \"Test\")\n"
        "    let panel1 = Panel(win1)\n"
        "    let panel2 = Panel(win2)\n"
        "    let btn = Button(panel1, \"Click\")\n"
        "    let lbl = Label(panel2, \"Hello\")\n"
        "}\n"
    );
    EXPECT_TRUE(r.passed) << "ui::widgets class hierarchy should be usable";
}

TEST_F(UIModuleTest, ImportUILayout) {
    auto r = check(
        "import std::ui\n"
        "import ui::layout\n"
        "import ui::types\n"
        "func main() {\n"
        "    let vstack = VStack.new()\n"
        "    let hstack = HStack.new()\n"
        "    let grid = Grid.new(2, 2, 5, 5)\n"
        "}\n"
    );
    EXPECT_TRUE(r.passed) << "ui::layout structs should be usable";
}

TEST_F(UIModuleTest, ImportUITheme) {
    auto r = check(
        "import std::ui\n"
        "import ui::theme\n"
        "import ui::types\n"
        "func main() {\n"
        "    let dark = Theme.dark()\n"
        "    let light = Theme.light()\n"
        "}\n"
    );
    EXPECT_TRUE(r.passed) << "ui::theme should be importable and usable";
}

TEST_F(UIModuleTest, ImportAnimation) {
    auto r = check(
        "import ui::animation\n"
        "func main() {\n"
        "    let v = easeLinear(0.5)\n"
        "    let t = Tween.new(0.0, 1.0, 1.0, 0)\n"
        "}\n"
    );
    EXPECT_TRUE(r.passed) << "ui::animation should be importable";
}

TEST_F(UIModuleTest, EasingFunctions) {
    auto r = check(
        "import ui::animation\n"
        "func main() {\n"
        "    let a = easeInQuad(0.5)\n"
        "    let b = easeOutQuad(0.5)\n"
        "    let c = easeInOutQuad(0.5)\n"
        "    let d = easeInCubic(0.5)\n"
        "    let e = easeOutCubic(0.5)\n"
        "    let f = easeInOutCubic(0.5)\n"
        "    let g = easeSpring(0.5)\n"
        "    let h = easeElasticOut(0.5)\n"
        "    let i = easeBounceOut(0.5)\n"
        "    let j = easeBackOut(0.5)\n"
        "}\n"
    );
    EXPECT_TRUE(r.passed) << "All easing functions should resolve";
}

TEST_F(UIModuleTest, SpringAnimator) {
    auto r = check(
        "import ui::animation\n"
        "func main() {\n"
        "    var spring = SpringAnimator.new(0.0, 200.0, 15.0)\n"
        "    spring.setTarget(100.0)\n"
        "    spring.update(0.016)\n"
        "    let v = spring.getValue()\n"
        "    let rest = spring.isAtRest()\n"
        "}\n"
    );
    EXPECT_TRUE(r.passed) << "SpringAnimator should resolve";
}

TEST_F(UIModuleTest, WidgetAnimator) {
    auto r = check(
        "import ui::animation\n"
        "func main() {\n"
        "    var wa = WidgetAnimator.new(1, 0.3)\n"
        "    wa.enter()\n"
        "    wa.update(0.016)\n"
        "    let visible = wa.isVisible()\n"
        "    let alpha = wa.getAlpha()\n"
        "}\n"
    );
    EXPECT_TRUE(r.passed) << "WidgetAnimator should resolve";
}

TEST_F(UIModuleTest, ImportFocus) {
    auto r = check(
        "import ui::focus\n"
        "func main() {\n"
        "    let k = KEY_TAB()\n"
        "    let e = KEY_ENTER()\n"
        "    let esc = KEY_ESCAPE()\n"
        "}\n"
    );
    EXPECT_TRUE(r.passed) << "ui::focus key constants should be available";
}

TEST_F(UIModuleTest, ImportComposite) {
    auto r = check(
        "import std::ui\n"
        "import ui::composite\n"
        "import ui::types\n"
        "func main() {\n"
        "    let bar = ButtonBar.new()\n"
        "}\n"
    );
    EXPECT_TRUE(r.passed) << "ui::composite should be importable";
}

TEST_F(UIModuleTest, ImportListView) {
    auto r = check(
        "import std::ui\n"
        "import ui::listview\n"
        "func main() {\n"
        "    let lv = ListView.new(0)\n"
        "    lv.addItem(\"Test\")\n"
        "}\n"
    );
    EXPECT_TRUE(r.passed) << "ui::listview should be importable";
}

TEST_F(UIModuleTest, ImportRouter) {
    auto r = check(
        "import std::ui\n"
        "import ui::router\n"
        "func main() {\n"
        "    let router = Router.new(0)\n"
        "}\n"
    );
    EXPECT_TRUE(r.passed) << "ui::router should be importable";
}

TEST_F(UIModuleTest, ImportTooltip) {
    auto r = check(
        "import std::ui\n"
        "import ui::tooltip\n"
        "func main() {\n"
        "    applyTooltip(0, \"Test\")\n"
        "}\n"
    );
    EXPECT_TRUE(r.passed) << "ui::tooltip should be importable";
}

TEST_F(UIModuleTest, ImportDragDrop) {
    auto r = check(
        "import std::ui\n"
        "import ui::dragdrop\n"
        "func main() {\n"
        "    var ds = DragState.new()\n"
        "    let active = ds.isActive()\n"
        "}\n"
    );
    EXPECT_TRUE(r.passed) << "ui::dragdrop should be importable";
}

TEST_F(UIModuleTest, AllUIModulesImport) {
    auto r = check(
        "import std::ui\n"
        "import ui::types\n"
        "import ui::widgets\n"
        "import ui::layout\n"
        "import ui::theme\n"
        "import ui::animation\n"
        "import ui::focus\n"
        "import ui::composite\n"
        "import ui::listview\n"
        "import ui::router\n"
        "import ui::tooltip\n"
        "import ui::dragdrop\n"
        "func main() {\n"
        "    let c = Color.rgb(255, 0, 0)\n"
        "    let dark = Theme.dark()\n"
        "}\n"
    );
    EXPECT_TRUE(r.passed) << "All UI modules should import without conflict";
}

// ============================================================
// Widget struct method tests
// ============================================================

TEST_F(UIModuleTest, WindowStruct) {
    auto r = check(
        "import ui::widgets\n"
        "func main() {\n"
        "    let win = Window(800, 600, \"App\")\n"
        "    win.show()\n"
        "    win.setTitle(\"Updated\")\n"
        "    let w = win.getWidth()\n"
        "    let h = win.getHeight()\n"
        "}\n"
    );
    EXPECT_TRUE(r.passed) << "Window class methods should resolve";
}

TEST_F(UIModuleTest, ButtonStruct) {
    auto r = check(
        "import ui::widgets\n"
        "func main() {\n"
        "    let win = Window(400, 300, \"App\")\n"
        "    let panel = Panel(win)\n"
        "    let btn = Button(panel, \"Click\")\n"
        "    btn.setText(\"Updated\")\n"
        "    btn.setEnabled(true)\n"
        "    btn.setTooltip(\"Help\")\n"
        "    btn.onClick(|h: i32| { })\n"
        "}\n"
    );
    EXPECT_TRUE(r.passed) << "Button class methods should resolve";
}

TEST_F(UIModuleTest, TextInputStruct) {
    auto r = check(
        "import ui::widgets\n"
        "func main() {\n"
        "    let win = Window(400, 300, \"App\")\n"
        "    let panel = Panel(win)\n"
        "    let inp = TextInput(panel, \"initial\")\n"
        "    let text = inp.getText()\n"
        "    inp.setText(\"new\")\n"
        "    inp.onChange(|h: i32| { })\n"
        "}\n"
    );
    EXPECT_TRUE(r.passed) << "TextInput class methods should resolve";
}

TEST_F(UIModuleTest, CheckboxStruct) {
    auto r = check(
        "import ui::widgets\n"
        "func main() {\n"
        "    let win = Window(400, 300, \"App\")\n"
        "    let panel = Panel(win)\n"
        "    let cb = Checkbox(panel, \"Check\")\n"
        "    let v = cb.getValue()\n"
        "    cb.setValue(1)\n"
        "    cb.onChange(|h: i32| { })\n"
        "}\n"
    );
    EXPECT_TRUE(r.passed) << "Checkbox class methods should resolve";
}

TEST_F(UIModuleTest, SliderStruct) {
    auto r = check(
        "import ui::widgets\n"
        "func main() {\n"
        "    let win = Window(400, 300, \"App\")\n"
        "    let panel = Panel(win)\n"
        "    let sl = Slider(panel, 0, 100, 50)\n"
        "    let v = sl.getValue()\n"
        "    sl.setValue(75)\n"
        "    sl.onChange(|h: i32| { })\n"
        "}\n"
    );
    EXPECT_TRUE(r.passed) << "Slider class methods should resolve";
}

TEST_F(UIModuleTest, SizerStruct) {
    auto r = check(
        "import std::ui\n"
        "func main() {\n"
        "    let sizer = createVBoxSizer()\n"
        "    let btn = createButton(0, \"OK\")\n"
        "    sizerAdd(sizer, btn, 0, 0, 5)\n"
        "}\n"
    );
    EXPECT_TRUE(r.passed) << "Sizer raw functions should resolve";
}

TEST_F(UIModuleTest, CanvasStruct) {
    auto r = check(
        "import ui::widgets\n"
        "func main() {\n"
        "    let win = Window(400, 300, \"App\")\n"
        "    let panel = Panel(win)\n"
        "    let canvas = Canvas(panel)\n"
        "    canvas.onPaint(|dc: i32| { })\n"
        "    canvas.refresh()\n"
        "}\n"
    );
    EXPECT_TRUE(r.passed) << "Canvas class methods should resolve";
}

TEST_F(UIModuleTest, ListBoxStruct) {
    auto r = check(
        "import ui::widgets\n"
        "func main() {\n"
        "    let win = Window(400, 300, \"App\")\n"
        "    let panel = Panel(win)\n"
        "    let lb = ListBox(panel)\n"
        "    lb.addItem(\"Item 1\")\n"
        "    lb.addItem(\"Item 2\")\n"
        "    lb.clear()\n"
        "    let sel = lb.getSelection()\n"
        "}\n"
    );
    EXPECT_TRUE(r.passed) << "ListBox class methods should resolve";
}

TEST_F(UIModuleTest, TabViewStruct) {
    auto r = check(
        "import ui::widgets\n"
        "func main() {\n"
        "    let win1 = Window(400, 300, \"App\")\n"
        "    let win2 = Window(400, 300, \"App\")\n"
        "    let panel = Panel(win1)\n"
        "    let tv = TabView(panel)\n"
        "    let page = Panel(win2)\n"
        "    tv.addPage(page, \"Tab 1\")\n"
        "    let sel = tv.getSelection()\n"
        "}\n"
    );
    EXPECT_TRUE(r.passed) << "TabView class methods should resolve";
}

// ============================================================
// Layout struct tests
// ============================================================

TEST_F(UIModuleTest, VStackLayout) {
    auto r = check(
        "import std::ui\n"
        "import ui::layout\n"
        "import ui::types\n"
        "func main() {\n"
        "    var vs = VStack.new()\n"
        "    let btn = createButton(0, \"OK\")\n"
        "    vs.add(btn, 0, 5)\n"
        "}\n"
    );
    EXPECT_TRUE(r.passed) << "VStack layout should resolve";
}

TEST_F(UIModuleTest, HStackLayout) {
    auto r = check(
        "import std::ui\n"
        "import ui::layout\n"
        "import ui::types\n"
        "func main() {\n"
        "    var hs = HStack.new()\n"
        "    let btn = createButton(0, \"OK\")\n"
        "    hs.add(btn, 0, 5)\n"
        "}\n"
    );
    EXPECT_TRUE(r.passed) << "HStack layout should resolve";
}

TEST_F(UIModuleTest, GridLayout) {
    auto r = check(
        "import std::ui\n"
        "import ui::layout\n"
        "import ui::types\n"
        "func main() {\n"
        "    var g = Grid.new(2, 2, 5, 5)\n"
        "    let btn = createButton(0, \"OK\")\n"
        "    g.add(btn, 0, 5)\n"
        "}\n"
    );
    EXPECT_TRUE(r.passed) << "Grid layout should resolve";
}

// ============================================================
// Theme tests
// ============================================================

TEST_F(UIModuleTest, ThemeDark) {
    auto r = check(
        "import std::ui\n"
        "import ui::theme\n"
        "import ui::types\n"
        "func main() {\n"
        "    var theme = Theme.dark()\n"
        "    let panel = createPanel(0)\n"
        "    theme.applyToPanel(panel)\n"
        "}\n"
    );
    EXPECT_TRUE(r.passed) << "Theme.dark() and applyToPanel should resolve";
}

TEST_F(UIModuleTest, ThemeLight) {
    auto r = check(
        "import std::ui\n"
        "import ui::theme\n"
        "import ui::types\n"
        "func main() {\n"
        "    var theme = Theme.light()\n"
        "    let btn = createButton(0, \"Test\")\n"
        "    theme.applyToWidget(btn)\n"
        "}\n"
    );
    EXPECT_TRUE(r.passed) << "Theme.light() and applyToWidget should resolve";
}

// ============================================================
// Composite widget tests
// ============================================================

TEST_F(UIModuleTest, FormFieldComposite) {
    auto r = check(
        "import std::ui\n"
        "import ui::composite\n"
        "import ui::types\n"
        "func main() {\n"
        "    let ff = FormField.new(0, \"Name:\", \"Enter name\")\n"
        "    let text = ff.getText()\n"
        "    ff.setText(\"John\")\n"
        "}\n"
    );
    EXPECT_TRUE(r.passed) << "FormField composite should resolve";
}

TEST_F(UIModuleTest, ButtonBarComposite) {
    auto r = check(
        "import std::ui\n"
        "import ui::composite\n"
        "import ui::types\n"
        "func main() {\n"
        "    var bar = ButtonBar.new()\n"
        "    let btn = bar.addButton(0, \"OK\", |h: i32| { })\n"
        "}\n"
    );
    EXPECT_TRUE(r.passed) << "ButtonBar composite should resolve";
}

TEST_F(UIModuleTest, StatusTextComposite) {
    auto r = check(
        "import std::ui\n"
        "import ui::composite\n"
        "import ui::types\n"
        "func main() {\n"
        "    var st = StatusText.new(0, \"Ready\")\n"
        "    st.setText(\"Loading...\")\n"
        "    st.setColor(0, 255, 0)\n"
        "}\n"
    );
    EXPECT_TRUE(r.passed) << "StatusText composite should resolve";
}

// ============================================================
// Full application pattern test
// ============================================================

TEST_F(UIModuleTest, FullAppPattern) {
    auto r = check(
        "import ui::widgets\n"
        "import ui::layout\n"
        "import ui::types\n"
        "func main() {\n"
        "    appInit()\n"
        "    let win = Window(400, 300, \"My App\")\n"
        "    let win2 = Window(400, 300, \"My App\")\n"
        "    let win3 = Window(400, 300, \"My App\")\n"
        "    let panelMain = Panel(win)\n"
        "    let panelLbl = Panel(win2)\n"
        "    let panelBtn = Panel(win3)\n"
        "    let vbox = createVBoxSizer()\n"
        "    let lbl = Label(panelLbl, \"Hello!\")\n"
        "    let btn = Button(panelBtn, \"Click Me\")\n"
        "    btn.onClick(|h: i32| {\n"
        "        println(\"Clicked!\")\n"
        "    })\n"
        "    sizerAdd(vbox, lbl.handle, 0, 0, 5)\n"
        "    sizerAdd(vbox, btn.handle, 0, 0, 5)\n"
        "    panelMain.setSizerHandle(vbox)\n"
        "    appRun()\n"
        "}\n"
    );
    EXPECT_TRUE(r.passed) << "Full VCL-style app pattern should resolve";
}

TEST_F(UIModuleTest, ControlClassHierarchy) {
    auto r = check(
        "import ui::widgets\n"
        "func main() {\n"
        "  appInit()\n"
        "  let win = Window(400, 300, \"T\")\n"
        "  let panel = Panel(win)\n"
        "  let btn = Button(panel, \"Tikla\")\n"
        "  btn.setEnabled(true)\n"
        "  btn.setBounds(0, 0, 100, 30)\n"
        "  let kids: [dyn Control] = [btn, Label(panel, \"x\")]\n"
        "}\n");
    EXPECT_TRUE(r.passed) << "Control-based class hierarchy should type-check";
}
