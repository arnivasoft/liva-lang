#include "liva/REPL/REPL.h"
#include <gtest/gtest.h>

using namespace liva;

// ============================================================
// Input Classification Tests
// ============================================================

TEST(REPLTest, EmptyLine) {
    REPLSession session;
    auto r = session.processLine("");
    EXPECT_EQ(r.kind, REPLResult::Empty);
}

TEST(REPLTest, WhitespaceLine) {
    REPLSession session;
    auto r = session.processLine("   \t  ");
    EXPECT_EQ(r.kind, REPLResult::Empty);
}

TEST(REPLTest, FuncDeclaration) {
    REPLSession session;
    auto r = session.processLine("func foo() -> i32 { return 42 }");
    EXPECT_EQ(r.kind, REPLResult::Declaration);
}

TEST(REPLTest, StructDeclaration) {
    REPLSession session;
    auto r1 = session.processLine("struct Point {");
    EXPECT_EQ(r1.kind, REPLResult::Incomplete);
    auto r2 = session.processLine("    var x: i32");
    EXPECT_EQ(r2.kind, REPLResult::Incomplete);
    auto r3 = session.processLine("}");
    EXPECT_EQ(r3.kind, REPLResult::Declaration);
}

TEST(REPLTest, LetDeclaration) {
    REPLSession session;
    auto r = session.processLine("let x = 42");
    EXPECT_EQ(r.kind, REPLResult::Declaration);
}

TEST(REPLTest, SimpleExpression) {
    REPLSession session;
    auto r = session.processLine("1 + 2");
    EXPECT_EQ(r.kind, REPLResult::Expression);
}

// ============================================================
// Command Tests
// ============================================================

TEST(REPLTest, QuitCommand) {
    REPLSession session;
    auto r = session.processLine(":quit");
    EXPECT_EQ(r.kind, REPLResult::Quit);
    EXPECT_EQ(r.output, "Goodbye!");
}

TEST(REPLTest, QuitShortcut) {
    REPLSession session;
    auto r = session.processLine(":q");
    EXPECT_EQ(r.kind, REPLResult::Quit);
}

TEST(REPLTest, HelpCommand) {
    REPLSession session;
    auto r = session.processLine(":help");
    EXPECT_EQ(r.kind, REPLResult::Help);
    EXPECT_FALSE(r.output.empty());
}

TEST(REPLTest, HelpShortcut) {
    REPLSession session;
    auto r = session.processLine(":h");
    EXPECT_EQ(r.kind, REPLResult::Help);
}

TEST(REPLTest, ResetCommand) {
    REPLSession session;
    session.processLine("let x = 42");
    EXPECT_EQ(session.getDeclarations().size(), 1u);

    auto r = session.processLine(":reset");
    EXPECT_EQ(r.kind, REPLResult::Reset);
    EXPECT_EQ(session.getDeclarations().size(), 0u);
}

TEST(REPLTest, ResetShortcut) {
    REPLSession session;
    session.processLine("let x = 42");
    auto r = session.processLine(":r");
    EXPECT_EQ(r.kind, REPLResult::Reset);
    EXPECT_EQ(session.getDeclarations().size(), 0u);
}

TEST(REPLTest, DeclsCommand) {
    REPLSession session;
    session.processLine("let x = 42");
    auto r = session.processLine(":declarations");
    EXPECT_EQ(r.kind, REPLResult::ShowDecls);
    EXPECT_NE(r.output.find("let x = 42"), std::string::npos);
}

TEST(REPLTest, DeclsShortcut) {
    REPLSession session;
    auto r = session.processLine(":decls");
    EXPECT_EQ(r.kind, REPLResult::ShowDecls);
    EXPECT_NE(r.output.find("No declarations"), std::string::npos);
}

TEST(REPLTest, UnknownCommand) {
    REPLSession session;
    auto r = session.processLine(":foo");
    EXPECT_EQ(r.kind, REPLResult::CommandError);
    EXPECT_NE(r.output.find(":foo"), std::string::npos);
}

// ============================================================
// Declaration Accumulation Tests
// ============================================================

TEST(REPLTest, AccumulateFunc) {
    REPLSession session;
    session.processLine("func foo() -> i32 { return 42 }");
    EXPECT_EQ(session.getDeclarations().size(), 1u);
}

TEST(REPLTest, AccumulateMultiple) {
    REPLSession session;
    session.processLine("func foo() -> i32 { return 42 }");

    // Multi-line struct
    session.processLine("struct Point {");
    session.processLine("    var x: i32");
    session.processLine("}");

    EXPECT_EQ(session.getDeclarations().size(), 2u);
}

TEST(REPLTest, ResetClearsDecls) {
    REPLSession session;
    session.processLine("func foo() -> i32 { return 42 }");
    EXPECT_EQ(session.getDeclarations().size(), 1u);
    session.reset();
    EXPECT_EQ(session.getDeclarations().size(), 0u);
}

TEST(REPLTest, InvalidDeclaration) {
    REPLSession session;
    auto r = session.processLine("func {");
    // This starts as multi-line...
    EXPECT_EQ(r.kind, REPLResult::Incomplete);
    // Complete it with an invalid body
    auto r2 = session.processLine("}");
    EXPECT_EQ(r2.kind, REPLResult::Error);
    EXPECT_EQ(session.getDeclarations().size(), 0u);
}

TEST(REPLTest, VarDeclaration) {
    REPLSession session;
    auto r = session.processLine("var x: i32 = 10");
    EXPECT_EQ(r.kind, REPLResult::Declaration);
    EXPECT_EQ(session.getDeclarations().size(), 1u);
}

TEST(REPLTest, ConstDeclaration) {
    REPLSession session;
    auto r = session.processLine("const PI = 3");
    EXPECT_EQ(r.kind, REPLResult::Declaration);
    EXPECT_EQ(session.getDeclarations().size(), 1u);
}

// ============================================================
// Multi-line Input Tests
// ============================================================

TEST(REPLTest, UnclosedBrace) {
    REPLSession session;
    auto r = session.processLine("func foo() -> i32 {");
    EXPECT_EQ(r.kind, REPLResult::Incomplete);
    EXPECT_TRUE(session.isIncomplete());
}

TEST(REPLTest, CompletedBrace) {
    REPLSession session;
    auto r1 = session.processLine("func foo() -> i32 {");
    EXPECT_EQ(r1.kind, REPLResult::Incomplete);

    auto r2 = session.processLine("    return 42");
    EXPECT_EQ(r2.kind, REPLResult::Incomplete);

    auto r3 = session.processLine("}");
    EXPECT_EQ(r3.kind, REPLResult::Declaration);
}

TEST(REPLTest, UnclosedParen) {
    REPLSession session;
    auto r = session.processLine("println(");
    EXPECT_EQ(r.kind, REPLResult::Incomplete);
    EXPECT_TRUE(session.isIncomplete());
}

TEST(REPLTest, NestedBraces) {
    REPLSession session;
    auto r1 = session.processLine("func foo() -> i32 {");
    EXPECT_EQ(r1.kind, REPLResult::Incomplete);
    auto r2 = session.processLine("    if true {");
    EXPECT_EQ(r2.kind, REPLResult::Incomplete);
    auto r3 = session.processLine("        return 1");
    EXPECT_EQ(r3.kind, REPLResult::Incomplete);
    auto r4 = session.processLine("    }");
    EXPECT_EQ(r4.kind, REPLResult::Incomplete);
    auto r5 = session.processLine("    return 0");
    EXPECT_EQ(r5.kind, REPLResult::Incomplete);
    auto r6 = session.processLine("}");
    EXPECT_EQ(r6.kind, REPLResult::Declaration);
}

TEST(REPLTest, IsIncomplete) {
    REPLSession session;
    EXPECT_FALSE(session.isIncomplete());
    session.processLine("func foo() {");
    EXPECT_TRUE(session.isIncomplete());
}

// ============================================================
// Expression Wrapping Tests
// ============================================================

TEST(REPLTest, ExprWrapped) {
    REPLSession session;
    auto r = session.processLine("1 + 2");
    EXPECT_EQ(r.kind, REPLResult::Expression);
    EXPECT_NE(r.generatedCode.find("println(1 + 2)"), std::string::npos);
}

TEST(REPLTest, PrintlnNotWrapped) {
    REPLSession session;
    auto r = session.processLine("println(42)");
    EXPECT_EQ(r.kind, REPLResult::Expression);
    // Should not contain "println(println(42))"
    EXPECT_EQ(r.generatedCode.find("println(println("), std::string::npos);
    // But should contain "println(42)"
    EXPECT_NE(r.generatedCode.find("println(42)"), std::string::npos);
}

TEST(REPLTest, PrintNotWrapped) {
    REPLSession session;
    auto r = session.processLine("print(42)");
    EXPECT_EQ(r.kind, REPLResult::Expression);
    EXPECT_EQ(r.generatedCode.find("println(print("), std::string::npos);
}

TEST(REPLTest, ExprNeedsExecution) {
    REPLSession session;
    auto r = session.processLine("1 + 2");
    EXPECT_TRUE(r.needsExecution);
}

TEST(REPLTest, GeneratedCodeHasMain) {
    REPLSession session;
    auto r = session.processLine("42");
    EXPECT_NE(r.generatedCode.find("func main()"), std::string::npos);
}

TEST(REPLTest, GeneratedCodeIncludesDeclarations) {
    REPLSession session;
    session.processLine("func foo() -> i32 { return 42 }");
    auto r = session.processLine("foo()");
    EXPECT_EQ(r.kind, REPLResult::Expression);
    EXPECT_NE(r.generatedCode.find("func foo()"), std::string::npos);
    EXPECT_NE(r.generatedCode.find("func main()"), std::string::npos);
}

// ============================================================
// Error Handling Tests
// ============================================================

TEST(REPLTest, ParseError) {
    REPLSession session;
    // An expression that won't parse correctly in Liva
    auto r = session.processLine("@#$%");
    EXPECT_EQ(r.kind, REPLResult::Error);
    EXPECT_FALSE(r.output.empty());
}

TEST(REPLTest, SessionContinuesAfterError) {
    REPLSession session;
    auto r1 = session.processLine("@#$%");
    EXPECT_EQ(r1.kind, REPLResult::Error);

    // Should still work after error
    auto r2 = session.processLine("1 + 2");
    EXPECT_EQ(r2.kind, REPLResult::Expression);
}

// ============================================================
// Prompt Tests
// ============================================================

TEST(REPLTest, NormalPrompt) {
    REPLSession session;
    EXPECT_EQ(session.getPrompt(), ">>> ");
}

TEST(REPLTest, ContinuationPrompt) {
    REPLSession session;
    session.processLine("func foo() {");
    EXPECT_EQ(session.getPrompt(), "... ");
}

TEST(REPLTest, PromptRestoredAfterComplete) {
    REPLSession session;
    session.processLine("func foo() -> i32 {");
    EXPECT_EQ(session.getPrompt(), "... ");
    session.processLine("    return 42");
    session.processLine("}");
    EXPECT_EQ(session.getPrompt(), ">>> ");
}
