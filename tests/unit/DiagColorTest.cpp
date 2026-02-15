#include <gtest/gtest.h>
#include "liva/Common/Diagnostics.h"
#include "liva/Common/TerminalColors.h"
#include <sstream>

using namespace liva;

// Helper: capture stderr output during a callback
static std::string captureStderr(std::function<void()> fn) {
    std::stringstream buf;
    auto *oldBuf = std::cerr.rdbuf(buf.rdbuf());
    fn();
    std::cerr.rdbuf(oldBuf);
    return buf.str();
}

// ---- Rust-style layout tests (no color) ----

TEST(DiagColorTest, RustStyleLayoutNoColor) {
    SourceManager sm("test.liva", "let x = foo + 1\n");
    Diagnostic diag;
    diag.id = DiagID::err_undeclared_identifier;
    diag.level = DiagLevel::Error;
    diag.location = {1, 9};
    diag.message = "use of undeclared identifier 'foo'";

    auto output = captureStderr([&]() {
        DiagnosticsEngine::printToStderr(diag, &sm, false);
    });

    // Check severity line
    EXPECT_NE(output.find("error: use of undeclared identifier 'foo'"), std::string::npos);
    // Check --> location arrow
    EXPECT_NE(output.find("--> test.liva:1:9"), std::string::npos);
    // Check source line with gutter
    EXPECT_NE(output.find("1 | let x = foo + 1"), std::string::npos);
    // Check caret line
    EXPECT_NE(output.find("| "), std::string::npos);
    EXPECT_NE(output.find("^"), std::string::npos);
}

TEST(DiagColorTest, RustStyleLayoutWithColor) {
    SourceManager sm("test.liva", "let x = foo + 1\n");
    Diagnostic diag;
    diag.id = DiagID::err_undeclared_identifier;
    diag.level = DiagLevel::Error;
    diag.location = {1, 9};
    diag.message = "use of undeclared identifier 'foo'";

    auto output = captureStderr([&]() {
        DiagnosticsEngine::printToStderr(diag, &sm, true);
    });

    // ANSI escape sequences should be present
    EXPECT_NE(output.find("\033["), std::string::npos);
    // Bold red for error
    EXPECT_NE(output.find("\033[1;31m"), std::string::npos);
    // Bold blue for gutter
    EXPECT_NE(output.find("\033[1;34m"), std::string::npos);
    // Reset sequence
    EXPECT_NE(output.find("\033[0m"), std::string::npos);
}

TEST(DiagColorTest, WarningUsesYellow) {
    SourceManager sm("test.liva", "var x: i32 = 42\n");
    Diagnostic diag;
    diag.id = DiagID::warn_unused_variable;
    diag.level = DiagLevel::Warning;
    diag.location = {1, 5};
    diag.message = "unused variable 'x'";

    auto output = captureStderr([&]() {
        DiagnosticsEngine::printToStderr(diag, &sm, true);
    });

    // Bold yellow for warning
    EXPECT_NE(output.find("\033[1;33m"), std::string::npos);
    EXPECT_NE(output.find("warning:"), std::string::npos);
}

TEST(DiagColorTest, NoSourceManager) {
    Diagnostic diag;
    diag.id = DiagID::err_too_many_errors;
    diag.level = DiagLevel::Error;
    diag.location = {0, 0};
    diag.message = "too many errors emitted";

    auto output = captureStderr([&]() {
        DiagnosticsEngine::printToStderr(diag, nullptr, false);
    });

    // Should have severity + message but no source snippet
    EXPECT_NE(output.find("error: too many errors emitted"), std::string::npos);
    // Should NOT have --> or source gutter
    EXPECT_EQ(output.find("-->"), std::string::npos);
    EXPECT_EQ(output.find(" | "), std::string::npos);
}

TEST(DiagColorTest, FormatDiagnosticUnchanged) {
    SourceManager sm("test.liva", "let x = 1\n");
    DiagnosticsEngine engine(&sm);

    Diagnostic diag;
    diag.id = DiagID::err_undeclared_identifier;
    diag.level = DiagLevel::Error;
    diag.location = {1, 5};
    diag.message = "use of undeclared identifier 'x'";

    std::string formatted = engine.formatDiagnostic(diag);
    // formatDiagnostic still uses the old format
    EXPECT_NE(formatted.find("test.liva:1:5: error:"), std::string::npos);
}

TEST(DiagColorTest, ShouldUseColorNever) {
    EXPECT_FALSE(shouldUseColor(ColorMode::Never));
}

TEST(DiagColorTest, ShouldUseColorAlways) {
    EXPECT_TRUE(shouldUseColor(ColorMode::Always));
}
