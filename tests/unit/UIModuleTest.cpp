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
