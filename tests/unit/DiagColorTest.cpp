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

// ---- Rich diagnostic tests ----

TEST(DiagColorTest, UnderlineSpan) {
    SourceManager sm("test.liva", "let x = foo + 1\n");
    Diagnostic diag;
    diag.id = DiagID::err_undeclared_identifier;
    diag.level = DiagLevel::Error;
    diag.location = {1, 9};
    diag.message = "use of undeclared identifier 'foo'";
    diag.highlightLength = 3;

    auto output = captureStderr([&]() {
        DiagnosticsEngine::printToStderr(diag, &sm, false);
    });

    // Should have 3 carets instead of 1
    EXPECT_NE(output.find("^^^"), std::string::npos);
}

TEST(DiagColorTest, HelpSuggestion) {
    SourceManager sm("test.liva", "let x = foo + 1\n");
    Diagnostic diag;
    diag.id = DiagID::NUM_DIAGNOSTICS;
    diag.level = DiagLevel::Help;
    diag.location = {1, 9};
    diag.message = "did you mean 'for'?";
    diag.highlightLength = 3;
    diag.suggestion = "for";

    auto output = captureStderr([&]() {
        DiagnosticsEngine::printToStderr(diag, &sm, false);
    });

    // Should have help: label
    EXPECT_NE(output.find("help: did you mean 'for'?"), std::string::npos);
    // Should show modified source line with suggestion applied
    EXPECT_NE(output.find("let x = for + 1"), std::string::npos);
    // Should have tildes under the suggestion
    EXPECT_NE(output.find("~~~"), std::string::npos);
    // Should NOT have --> arrow for help diagnostics
    EXPECT_EQ(output.find("-->"), std::string::npos);
}

TEST(DiagColorTest, HelpUsesGreenColor) {
    SourceManager sm("test.liva", "let x = foo + 1\n");
    Diagnostic diag;
    diag.id = DiagID::NUM_DIAGNOSTICS;
    diag.level = DiagLevel::Help;
    diag.location = {1, 9};
    diag.message = "did you mean 'for'?";
    diag.highlightLength = 3;
    diag.suggestion = "for";

    auto output = captureStderr([&]() {
        DiagnosticsEngine::printToStderr(diag, &sm, true);
    });

    // Bold green for help
    EXPECT_NE(output.find("\033[1;32m"), std::string::npos);
    EXPECT_NE(output.find("help:"), std::string::npos);
}

TEST(DiagColorTest, InlineLabel) {
    SourceManager sm("test.liva", "let x = foo + 1\n");
    Diagnostic diag;
    diag.id = DiagID::err_undeclared_identifier;
    diag.level = DiagLevel::Error;
    diag.location = {1, 9};
    diag.message = "use of undeclared identifier 'foo'";
    diag.highlightLength = 3;
    diag.inlineLabel = "not found in this scope";

    auto output = captureStderr([&]() {
        DiagnosticsEngine::printToStderr(diag, &sm, false);
    });

    // Should have inline label after carets
    EXPECT_NE(output.find("^^^ not found in this scope"), std::string::npos);
}

TEST(DiagColorTest, DefaultHighlightLengthOne) {
    SourceManager sm("test.liva", "let x = 1\n");
    Diagnostic diag;
    diag.id = DiagID::err_undeclared_identifier;
    diag.level = DiagLevel::Error;
    diag.location = {1, 5};
    diag.message = "some error";
    // highlightLength defaults to 1

    auto output = captureStderr([&]() {
        DiagnosticsEngine::printToStderr(diag, &sm, false);
    });

    // Should have exactly one caret (not multiple)
    auto pos = output.find("^");
    ASSERT_NE(pos, std::string::npos);
    // The character after the single ^ should NOT be another ^
    EXPECT_NE(output[pos + 1], '^');
}
