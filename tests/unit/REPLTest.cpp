#include "liva/REPL/REPL.h"
#include <algorithm>
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

// ============================================================
// Statement Execution Tests
// ============================================================

TEST(REPLTest, IfStatement) {
    REPLSession session;
    auto r1 = session.processLine("if true {");
    EXPECT_EQ(r1.kind, REPLResult::Incomplete);
    auto r2 = session.processLine("    println(42)");
    EXPECT_EQ(r2.kind, REPLResult::Incomplete);
    auto r3 = session.processLine("}");
    EXPECT_EQ(r3.kind, REPLResult::Statement);
    EXPECT_TRUE(r3.needsExecution);
    EXPECT_NE(r3.generatedCode.find("if true"), std::string::npos);
}

TEST(REPLTest, WhileStatement) {
    REPLSession session;
    auto r1 = session.processLine("while false {");
    EXPECT_EQ(r1.kind, REPLResult::Incomplete);
    auto r2 = session.processLine("}");
    EXPECT_EQ(r2.kind, REPLResult::Statement);
    EXPECT_TRUE(r2.needsExecution);
}

TEST(REPLTest, ForStatement) {
    REPLSession session;
    auto r1 = session.processLine("for i in [1, 2, 3] {");
    EXPECT_EQ(r1.kind, REPLResult::Incomplete);
    auto r2 = session.processLine("    println(i)");
    EXPECT_EQ(r2.kind, REPLResult::Incomplete);
    auto r3 = session.processLine("}");
    EXPECT_EQ(r3.kind, REPLResult::Statement);
    EXPECT_TRUE(r3.needsExecution);
}

TEST(REPLTest, StatementInMain) {
    REPLSession session;
    auto r1 = session.processLine("if true {");
    auto r2 = session.processLine("    println(1)");
    auto r3 = session.processLine("}");
    EXPECT_NE(r3.generatedCode.find("func main()"), std::string::npos);
}

// ============================================================
// Import Support Tests
// ============================================================

TEST(REPLTest, ImportDeclaration) {
    REPLSession session;
    auto r = session.processLine("import std::math");
    EXPECT_EQ(r.kind, REPLResult::Declaration);
    EXPECT_EQ(r.output, "Import added.");
    EXPECT_EQ(session.getDeclarations().size(), 1u);
    EXPECT_EQ(session.getDeclarations()[0], "import std::math");
}

TEST(REPLTest, ImportIncludedInGenerated) {
    REPLSession session;
    session.processLine("import std::math");
    auto r = session.processLine("42");
    EXPECT_EQ(r.kind, REPLResult::Expression);
    EXPECT_NE(r.generatedCode.find("import std::math"), std::string::npos);
}

TEST(REPLTest, MultipleImports) {
    REPLSession session;
    session.processLine("import std::math");
    session.processLine("import std::io");
    EXPECT_EQ(session.getDeclarations().size(), 2u);
}

// ============================================================
// Tab Completion Tests
// ============================================================

TEST(REPLTest, CompletionKeywordPrefix) {
    REPLSession session;
    auto results = session.getCompletions("fu", 2);
    EXPECT_FALSE(results.empty());
    EXPECT_NE(std::find(results.begin(), results.end(), "func"),
              results.end());
}

TEST(REPLTest, CompletionBuiltinPrefix) {
    REPLSession session;
    auto results = session.getCompletions("pri", 3);
    EXPECT_NE(std::find(results.begin(), results.end(), "print"),
              results.end());
    EXPECT_NE(std::find(results.begin(), results.end(), "println"),
              results.end());
}

TEST(REPLTest, CompletionCommandPrefix) {
    REPLSession session;
    auto results = session.getCompletions(":h", 2);
    EXPECT_NE(std::find(results.begin(), results.end(), ":h"),
              results.end());
    EXPECT_NE(std::find(results.begin(), results.end(), ":help"),
              results.end());
}

TEST(REPLTest, CompletionCommandColon) {
    REPLSession session;
    auto results = session.getCompletions(":", 1);
    // Should return all REPL commands
    EXPECT_GE(results.size(), 8u);
    EXPECT_NE(std::find(results.begin(), results.end(), ":quit"),
              results.end());
    EXPECT_NE(std::find(results.begin(), results.end(), ":help"),
              results.end());
    EXPECT_NE(std::find(results.begin(), results.end(), ":reset"),
              results.end());
    EXPECT_NE(std::find(results.begin(), results.end(), ":declarations"),
              results.end());
}

TEST(REPLTest, CompletionDeclaredFunc) {
    REPLSession session;
    session.processLine("func add(a: i32, b: i32) -> i32 { return a + b }");
    auto results = session.getCompletions("ad", 2);
    EXPECT_NE(std::find(results.begin(), results.end(), "add"),
              results.end());
}

TEST(REPLTest, CompletionDeclaredStruct) {
    REPLSession session;
    session.processLine("struct Point {");
    session.processLine("    var x: i32");
    session.processLine("}");
    auto results = session.getCompletions("Po", 2);
    EXPECT_NE(std::find(results.begin(), results.end(), "Point"),
              results.end());
}

TEST(REPLTest, CompletionNoMatch) {
    REPLSession session;
    auto results = session.getCompletions("xyz123", 6);
    EXPECT_TRUE(results.empty());
}

TEST(REPLTest, CompletionEmptyPrefix) {
    REPLSession session;
    auto results = session.getCompletions("", 0);
    // Should return all keywords + builtins + primitives
    EXPECT_GT(results.size(), 30u);
}

TEST(REPLTest, CompletionCaseSensitive) {
    REPLSession session;
    auto results = session.getCompletions("Fu", 2);
    // Keywords are lowercase, "Fu" should not match "func"
    EXPECT_EQ(std::find(results.begin(), results.end(), "func"),
              results.end());
}

TEST(REPLTest, CompletionAfterReset) {
    REPLSession session;
    session.processLine("func myFunc() -> i32 { return 1 }");
    auto r1 = session.getCompletions("my", 2);
    EXPECT_NE(std::find(r1.begin(), r1.end(), "myFunc"), r1.end());

    session.processLine(":reset");
    auto r2 = session.getCompletions("my", 2);
    EXPECT_EQ(std::find(r2.begin(), r2.end(), "myFunc"), r2.end());
}

TEST(REPLTest, CompletionMidLine) {
    REPLSession session;
    auto results = session.getCompletions("let x = pri", 11);
    EXPECT_NE(std::find(results.begin(), results.end(), "print"),
              results.end());
    EXPECT_NE(std::find(results.begin(), results.end(), "println"),
              results.end());
}

TEST(REPLTest, CompletionPrimitiveTypes) {
    REPLSession session;
    auto results = session.getCompletions("i3", 2);
    EXPECT_NE(std::find(results.begin(), results.end(), "i32"),
              results.end());
}

TEST(REPLTest, ExtractCurrentWord) {
    // Test via getCompletions — prefix extraction is internal
    REPLSession session;
    // "let x = pr" at cursor=10 → prefix "pr"
    auto r1 = session.getCompletions("let x = pr", 10);
    EXPECT_NE(std::find(r1.begin(), r1.end(), "print"), r1.end());

    // ":he" at cursor=3 → prefix ":he"
    auto r2 = session.getCompletions(":he", 3);
    EXPECT_NE(std::find(r2.begin(), r2.end(), ":help"), r2.end());

    // Empty at cursor=0 → all candidates
    auto r3 = session.getCompletions("", 0);
    EXPECT_GT(r3.size(), 0u);
}
