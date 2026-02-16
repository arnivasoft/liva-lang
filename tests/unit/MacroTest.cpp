#include "liva/Common/Diagnostics.h"
#include "liva/Common/SourceLocation.h"
#include "liva/Lexer/Lexer.h"
#include "liva/Macro/MacroExpander.h"
#include "liva/Parser/Parser.h"
#include "liva/Sema/Sema.h"
#include <gtest/gtest.h>

using namespace liva;

// ============================================================
// Test fixture
// ============================================================
class MacroTest : public ::testing::Test {
protected:
    struct ParseResult {
        std::unique_ptr<SourceManager> sm;
        DiagnosticsEngine diag;
        std::unique_ptr<TranslationUnit> tu;
        bool parsed;
    };

    ParseResult parse(const std::string &source) {
        ParseResult r;
        r.sm = std::make_unique<SourceManager>("test.liva", source);
        r.diag.setSourceManager(r.sm.get());
        Lexer lexer(*r.sm, r.diag);
        Parser parser(lexer, r.diag);
        r.tu = parser.parseTranslationUnit();
        r.parsed = !r.diag.hasErrors();
        return r;
    }

    struct SemaResult {
        std::unique_ptr<SourceManager> sm;
        DiagnosticsEngine diag;
        std::unique_ptr<TranslationUnit> tu;
        bool passed;
    };

    SemaResult check(const std::string &source) {
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
        Sema sema(r.diag);
        r.passed = sema.analyze(*r.tu);
        return r;
    }

    bool hasDiag(const SemaResult &result, DiagID id) {
        for (auto &d : result.diag.getDiagnostics()) {
            if (d.id == id)
                return true;
        }
        return false;
    }
};

// ============================================================
// Parse tests
// ============================================================

TEST_F(MacroTest, MacroDeclBasic) {
    auto r = parse(R"(
macro max {
    ($a:expr, $b:expr) => {
        if $a > $b { $a } else { $b }
    }
}
)");
    ASSERT_TRUE(r.parsed);
    bool found = false;
    for (auto &d : r.tu->getDeclarations()) {
        if (d->getKind() == ASTNode::NodeKind::MacroDecl) {
            auto *md = static_cast<MacroDecl *>(d.get());
            EXPECT_EQ(md->getName(), "max");
            EXPECT_FALSE(md->getRawSource().empty());
            found = true;
        }
    }
    EXPECT_TRUE(found);
}

TEST_F(MacroTest, MacroDeclMultiArm) {
    auto r = parse(R"(
macro check {
    ($a:expr) => { if $a { println("ok") } }
    ($a:expr, $b:expr) => { if $a != $b { println("fail") } }
}
)");
    ASSERT_TRUE(r.parsed);
}

TEST_F(MacroTest, MacroDeclWithRepetition) {
    auto r = parse(R"(
macro println_all {
    ($($x:expr),+) => {
        $( println($x) )+
    }
}
)");
    ASSERT_TRUE(r.parsed);
}

TEST_F(MacroTest, MacroInvokeExpr) {
    auto r = parse(R"(
macro max {
    ($a:expr, $b:expr) => {
        if $a > $b { $a } else { $b }
    }
}
func main() {
    let m = max!(1, 2)
}
)");
    ASSERT_TRUE(r.parsed);
}

TEST_F(MacroTest, MacroInvokeNoArgs) {
    auto r = parse(R"(
macro noop {
    () => { 0 }
}
func main() {
    let x = noop!()
}
)");
    ASSERT_TRUE(r.parsed);
}

TEST_F(MacroTest, MacroInvokeNested) {
    auto r = parse(R"(
macro double {
    ($x:expr) => { $x + $x }
}
func main() {
    let x = double!(double!(2))
}
)");
    ASSERT_TRUE(r.parsed);
}

// ============================================================
// Expander unit tests
// ============================================================

TEST_F(MacroTest, SimpleSubstitution) {
    std::string rawSource = "($a:expr, $b:expr) => { $a + $b }";
    auto def = MacroExpander::parseMacroDef("add", false, rawSource,
                                             SourceRange::invalid());
    ASSERT_EQ(def.arms.size(), 1u);

    MacroExpander expander;
    expander.registerMacro(def);

    // Create arg tokens: "1, 2"
    SourceManager sm("test", "1, 2");
    DiagnosticsEngine diag(&sm);
    Lexer lexer(sm, diag);
    std::vector<Token> argTokens;
    while (true) {
        Token t = lexer.nextToken();
        if (t.is(TokenKind::eof)) break;
        argTokens.push_back(t);
    }

    std::string result = expander.expand("add", argTokens, diag, SourceLocation());
    EXPECT_FALSE(result.empty());
    EXPECT_NE(result.find("1"), std::string::npos);
    EXPECT_NE(result.find("2"), std::string::npos);
    EXPECT_NE(result.find("+"), std::string::npos);
}

TEST_F(MacroTest, MultipleParams) {
    std::string rawSource = "($a:expr, $b:expr, $c:expr) => { $a + $b + $c }";
    auto def = MacroExpander::parseMacroDef("add3", false, rawSource,
                                             SourceRange::invalid());
    MacroExpander expander;
    expander.registerMacro(def);

    SourceManager sm("test", "10, 20, 30");
    DiagnosticsEngine diag(&sm);
    Lexer lexer(sm, diag);
    std::vector<Token> argTokens;
    while (true) {
        Token t = lexer.nextToken();
        if (t.is(TokenKind::eof)) break;
        argTokens.push_back(t);
    }

    std::string result = expander.expand("add3", argTokens, diag, SourceLocation());
    EXPECT_FALSE(result.empty());
    EXPECT_NE(result.find("10"), std::string::npos);
    EXPECT_NE(result.find("20"), std::string::npos);
    EXPECT_NE(result.find("30"), std::string::npos);
}

TEST_F(MacroTest, PatternMatchByCount) {
    std::string rawSource =
        "($a:expr) => { $a }\n"
        "($a:expr, $b:expr) => { $a + $b }";
    auto def = MacroExpander::parseMacroDef("flex", false, rawSource,
                                             SourceRange::invalid());
    ASSERT_EQ(def.arms.size(), 2u);

    MacroExpander expander;
    expander.registerMacro(def);

    // Single arg
    {
        SourceManager sm("test", "42");
        DiagnosticsEngine diag(&sm);
        Lexer lexer(sm, diag);
        std::vector<Token> tokens;
        while (true) {
            Token t = lexer.nextToken();
            if (t.is(TokenKind::eof)) break;
            tokens.push_back(t);
        }
        std::string result = expander.expand("flex", tokens, diag, SourceLocation());
        EXPECT_NE(result.find("42"), std::string::npos);
        // Should NOT contain "+"
        EXPECT_EQ(result.find("+"), std::string::npos);
    }

    // Two args
    {
        SourceManager sm("test", "1, 2");
        DiagnosticsEngine diag(&sm);
        Lexer lexer(sm, diag);
        std::vector<Token> tokens;
        while (true) {
            Token t = lexer.nextToken();
            if (t.is(TokenKind::eof)) break;
            tokens.push_back(t);
        }
        std::string result = expander.expand("flex", tokens, diag, SourceLocation());
        EXPECT_NE(result.find("+"), std::string::npos);
    }
}

TEST_F(MacroTest, RepetitionOneOrMore) {
    std::string rawSource = "($($x:expr),+) => { $( println($x) )+ }";
    auto def = MacroExpander::parseMacroDef("pr", false, rawSource,
                                             SourceRange::invalid());
    MacroExpander expander;
    expander.registerMacro(def);

    // One arg
    {
        SourceManager sm("test", "1");
        DiagnosticsEngine diag(&sm);
        Lexer lexer(sm, diag);
        std::vector<Token> tokens;
        while (true) {
            Token t = lexer.nextToken();
            if (t.is(TokenKind::eof)) break;
            tokens.push_back(t);
        }
        std::string result = expander.expand("pr", tokens, diag, SourceLocation());
        EXPECT_FALSE(result.empty());
        EXPECT_NE(result.find("println"), std::string::npos);
    }
}

TEST_F(MacroTest, RepetitionMultiple) {
    std::string rawSource = "($($x:expr),+) => { $( println($x) )+ }";
    auto def = MacroExpander::parseMacroDef("pr", false, rawSource,
                                             SourceRange::invalid());
    MacroExpander expander;
    expander.registerMacro(def);

    // Three args
    SourceManager sm("test", "1, 2, 3");
    DiagnosticsEngine diag(&sm);
    Lexer lexer(sm, diag);
    std::vector<Token> tokens;
    while (true) {
        Token t = lexer.nextToken();
        if (t.is(TokenKind::eof)) break;
        tokens.push_back(t);
    }
    std::string result = expander.expand("pr", tokens, diag, SourceLocation());
    EXPECT_FALSE(result.empty());
    // Should have println mentioned multiple times
    size_t count = 0;
    size_t pos = 0;
    while ((pos = result.find("println", pos)) != std::string::npos) {
        ++count;
        pos += 7;
    }
    EXPECT_GE(count, 3u);
}

TEST_F(MacroTest, RepetitionZeroOrMore) {
    std::string rawSource = "($($x:expr),*) => { $( println($x) )* }";
    auto def = MacroExpander::parseMacroDef("pr0", false, rawSource,
                                             SourceRange::invalid());
    MacroExpander expander;
    expander.registerMacro(def);

    // Empty args
    SourceManager sm("test", "");
    DiagnosticsEngine diag(&sm);
    Lexer lexer(sm, diag);
    std::vector<Token> tokens;
    while (true) {
        Token t = lexer.nextToken();
        if (t.is(TokenKind::eof)) break;
        tokens.push_back(t);
    }
    std::string result = expander.expand("pr0", tokens, diag, SourceLocation());
    // Empty expansion is fine
    EXPECT_FALSE(diag.hasErrors());
}

// ============================================================
// Sema tests
// ============================================================

TEST_F(MacroTest, MacroExpandInTypeCheck) {
    auto r = check(R"(
macro double {
    ($x:expr) => { $x + $x }
}
func main() {
    let x: i32 = double!(5)
}
)");
    EXPECT_TRUE(r.passed) << "Macro expansion with type check should pass";
}

TEST_F(MacroTest, MacroWithTypeChecking) {
    auto r = check(R"(
macro add {
    ($a:expr, $b:expr) => { $a + $b }
}
func main() {
    let x: i32 = add!(1, 2)
}
)");
    EXPECT_TRUE(r.passed);
}

TEST_F(MacroTest, MacroUndefined) {
    auto r = check(R"(
func main() {
    let x = unknown_macro!(1, 2)
}
)");
    EXPECT_FALSE(r.passed);
    EXPECT_TRUE(hasDiag(r, DiagID::err_macro_undefined));
}

TEST_F(MacroTest, MacroNoMatchingArm) {
    auto r = check(R"(
macro one {
    ($a:expr) => { $a }
}
func main() {
    let x = one!(1, 2, 3)
}
)");
    EXPECT_FALSE(r.passed);
    EXPECT_TRUE(hasDiag(r, DiagID::err_macro_no_matching_arm));
}

TEST_F(MacroTest, MacroRedefinition) {
    auto r = check(R"(
macro dup {
    ($x:expr) => { $x }
}
macro dup {
    ($x:expr) => { $x + $x }
}
func main() {
    let x = dup!(1)
}
)");
    EXPECT_FALSE(r.passed);
    EXPECT_TRUE(hasDiag(r, DiagID::err_macro_redefinition));
}

// ============================================================
// E2E tests
// ============================================================

TEST_F(MacroTest, MaxMacro) {
    auto r = check(R"(
macro max {
    ($a:expr, $b:expr) => {
        $a + $b
    }
}
func main() {
    let x: i32 = max!(3, 5)
}
)");
    EXPECT_TRUE(r.passed);
}

TEST_F(MacroTest, AssertEqMacro) {
    auto r = check(R"(
macro assert_eq {
    ($left:expr, $right:expr) => {
        println("check")
    }
}
func main() {
    assert_eq!(1, 1)
}
)");
    EXPECT_TRUE(r.passed);
}

TEST_F(MacroTest, MacroWithArithmetic) {
    auto r = check(R"(
macro square {
    ($x:expr) => { $x * $x }
}
func main() {
    let x: i32 = square!(4)
}
)");
    EXPECT_TRUE(r.passed);
}

TEST_F(MacroTest, MacroWithTernary) {
    auto r = check(R"(
macro cond {
    ($c:expr, $t:expr, $f:expr) => {
        $c ? $t : $f
    }
}
func main() {
    let x: i32 = cond!(true, 1, 2)
}
)");
    if (!r.passed) {
        for (auto &d : r.diag.getDiagnostics()) {
            printf("  cond DIAG: %s\n", d.message.c_str());
        }
    }
    EXPECT_TRUE(r.passed);
}

TEST_F(MacroTest, MacroWithFunctionCall) {
    auto r = check(R"(
macro say {
    ($msg:expr) => { println($msg) }
}
func main() {
    say!("hello")
}
)");
    EXPECT_TRUE(r.passed);
}

TEST_F(MacroTest, MacroExpressionContext) {
    auto r = check(R"(
macro inc {
    ($x:expr) => { $x + 1 }
}
func main() {
    let x: i32 = inc!(10)
}
)");
    if (!r.passed) {
        for (auto &d : r.diag.getDiagnostics()) {
            printf("  inc DIAG: %s\n", d.message.c_str());
        }
    }
    EXPECT_TRUE(r.passed);
}

TEST_F(MacroTest, MacroInFuncBody) {
    auto r = check(R"(
macro negate {
    ($x:expr) => { -$x }
}
func foo() -> i32 {
    return negate!(5)
}
func main() {
    let x: i32 = foo()
}
)");
    EXPECT_TRUE(r.passed);
}

// ============================================================
// Fragment specifier tests
// ============================================================

TEST_F(MacroTest, ExprFragment) {
    std::string rawSource = "($x:expr) => { $x }";
    auto def = MacroExpander::parseMacroDef("id", false, rawSource,
                                             SourceRange::invalid());
    MacroExpander expander;
    expander.registerMacro(def);

    SourceManager sm("test", "1 + 2");
    DiagnosticsEngine diag(&sm);
    Lexer lexer(sm, diag);
    std::vector<Token> tokens;
    while (true) {
        Token t = lexer.nextToken();
        if (t.is(TokenKind::eof)) break;
        tokens.push_back(t);
    }
    std::string result = expander.expand("id", tokens, diag, SourceLocation());
    EXPECT_NE(result.find("1"), std::string::npos);
    EXPECT_NE(result.find("+"), std::string::npos);
    EXPECT_NE(result.find("2"), std::string::npos);
}

TEST_F(MacroTest, IdentFragment) {
    std::string rawSource = "($x:ident) => { $x }";
    auto def = MacroExpander::parseMacroDef("id", false, rawSource,
                                             SourceRange::invalid());
    MacroExpander expander;
    expander.registerMacro(def);

    SourceManager sm("test", "foo");
    DiagnosticsEngine diag(&sm);
    Lexer lexer(sm, diag);
    std::vector<Token> tokens;
    while (true) {
        Token t = lexer.nextToken();
        if (t.is(TokenKind::eof)) break;
        tokens.push_back(t);
    }
    std::string result = expander.expand("id", tokens, diag, SourceLocation());
    EXPECT_NE(result.find("foo"), std::string::npos);
}

TEST_F(MacroTest, LiteralFragment) {
    std::string rawSource = "($x:literal) => { $x }";
    auto def = MacroExpander::parseMacroDef("lit", false, rawSource,
                                             SourceRange::invalid());
    MacroExpander expander;
    expander.registerMacro(def);

    SourceManager sm("test", "42");
    DiagnosticsEngine diag(&sm);
    Lexer lexer(sm, diag);
    std::vector<Token> tokens;
    while (true) {
        Token t = lexer.nextToken();
        if (t.is(TokenKind::eof)) break;
        tokens.push_back(t);
    }
    std::string result = expander.expand("lit", tokens, diag, SourceLocation());
    EXPECT_NE(result.find("42"), std::string::npos);
}

TEST_F(MacroTest, BlockFragment) {
    std::string rawSource = "($x:block) => { $x }";
    auto def = MacroExpander::parseMacroDef("blk", false, rawSource,
                                             SourceRange::invalid());
    MacroExpander expander;
    expander.registerMacro(def);

    SourceManager sm("test", "{ 1 + 2 }");
    DiagnosticsEngine diag(&sm);
    Lexer lexer(sm, diag);
    std::vector<Token> tokens;
    while (true) {
        Token t = lexer.nextToken();
        if (t.is(TokenKind::eof)) break;
        tokens.push_back(t);
    }
    std::string result = expander.expand("blk", tokens, diag, SourceLocation());
    EXPECT_NE(result.find("{"), std::string::npos);
    EXPECT_NE(result.find("}"), std::string::npos);
}

// ============================================================
// Lexer tests for $ token
// ============================================================

TEST_F(MacroTest, DollarToken) {
    SourceManager sm("test", "$ $a");
    DiagnosticsEngine diag(&sm);
    Lexer lexer(sm, diag);

    Token t1 = lexer.nextToken();
    EXPECT_EQ(t1.getKind(), TokenKind::dollar);

    Token t2 = lexer.nextToken();
    EXPECT_EQ(t2.getKind(), TokenKind::dollar);

    Token t3 = lexer.nextToken();
    EXPECT_EQ(t3.getKind(), TokenKind::identifier);
    EXPECT_EQ(t3.getText(), "a");
}

TEST_F(MacroTest, MacroKeyword) {
    SourceManager sm("test", "macro");
    DiagnosticsEngine diag(&sm);
    Lexer lexer(sm, diag);
    Token t = lexer.nextToken();
    EXPECT_EQ(t.getKind(), TokenKind::kw_macro);
}

// ============================================================
// Trace callback tests
// ============================================================

TEST_F(MacroTest, MacroTraceCallbackBasic) {
    MacroExpander expander;
    auto def = MacroExpander::parseMacroDef("add1", false,
        "($x:expr) => { $x + 1 }", SourceRange());
    expander.registerMacro(def);

    std::vector<MacroTraceEvent> events;
    expander.setTraceCallback([&](const MacroTraceEvent &ev) {
        events.push_back(ev);
    });

    SourceManager sm("test", "add1!(42)");
    DiagnosticsEngine diag(&sm);
    std::string result = expander.expand("add1", {}, diag, SourceLocation{1, 1});
    // Even with empty args, callback is invoked
    ASSERT_GE(events.size(), 1u);
    EXPECT_EQ(events[0].phase, MacroTraceEvent::Invocation);
    EXPECT_EQ(events[0].macroName, "add1");
}

TEST_F(MacroTest, MacroTraceCallbackCaptures) {
    MacroExpander expander;
    auto def = MacroExpander::parseMacroDef("double", false,
        "($x:expr) => { $x + $x }", SourceRange());
    expander.registerMacro(def);

    std::vector<MacroTraceEvent> events;
    expander.setTraceCallback([&](const MacroTraceEvent &ev) {
        events.push_back(ev);
    });

    // Create a token for "42"
    SourceManager sm("test", "42");
    DiagnosticsEngine diag(&sm);
    Lexer lexer(sm, diag);
    std::vector<Token> args;
    args.push_back(lexer.nextToken());

    std::string result = expander.expand("double", args, diag, SourceLocation{1, 1});
    EXPECT_FALSE(result.empty());

    // Find ArmMatched event
    bool foundMatch = false;
    for (const auto &ev : events) {
        if (ev.phase == MacroTraceEvent::ArmMatched) {
            foundMatch = true;
            EXPECT_EQ(ev.armIndex, 0u);
            EXPECT_EQ(ev.captures.count("x"), 1u);
            EXPECT_EQ(ev.captures.at("x"), "42");
        }
    }
    EXPECT_TRUE(foundMatch);

    // Find Completed event
    bool foundComplete = false;
    for (const auto &ev : events) {
        if (ev.phase == MacroTraceEvent::Completed) {
            foundComplete = true;
            EXPECT_FALSE(ev.expandedSource.empty());
        }
    }
    EXPECT_TRUE(foundComplete);
}

TEST_F(MacroTest, MacroTraceCallbackNoMatch) {
    MacroExpander expander;
    // Macro expects an ident, but we'll pass a literal
    auto def = MacroExpander::parseMacroDef("id_only", false,
        "($x:ident) => { $x }", SourceRange());
    expander.registerMacro(def);

    std::vector<MacroTraceEvent> events;
    expander.setTraceCallback([&](const MacroTraceEvent &ev) {
        events.push_back(ev);
    });

    // Pass a string literal token — won't match ident fragment
    SourceManager sm("test", "\"hello\"");
    DiagnosticsEngine diag(&sm);
    Lexer lexer(sm, diag);
    std::vector<Token> args;
    args.push_back(lexer.nextToken());

    std::string result = expander.expand("id_only", args, diag, SourceLocation{1, 1});
    EXPECT_TRUE(result.empty());

    // Should have ArmFailed + NoMatch
    bool foundFailed = false, foundNoMatch = false;
    for (const auto &ev : events) {
        if (ev.phase == MacroTraceEvent::ArmFailed) foundFailed = true;
        if (ev.phase == MacroTraceEvent::NoMatch) foundNoMatch = true;
    }
    EXPECT_TRUE(foundFailed);
    EXPECT_TRUE(foundNoMatch);
}

TEST_F(MacroTest, MacroTraceCallbackMultiArm) {
    MacroExpander expander;
    auto def = MacroExpander::parseMacroDef("overload", false,
        "($a:ident, $b:ident) => { $a + $b },\n"
        "($x:expr) => { $x }", SourceRange());
    expander.registerMacro(def);

    std::vector<MacroTraceEvent> events;
    expander.setTraceCallback([&](const MacroTraceEvent &ev) {
        events.push_back(ev);
    });

    // Pass single expr — first arm should fail, second should match
    SourceManager sm("test", "42");
    DiagnosticsEngine diag(&sm);
    Lexer lexer(sm, diag);
    std::vector<Token> args;
    args.push_back(lexer.nextToken());

    std::string result = expander.expand("overload", args, diag, SourceLocation{1, 1});
    EXPECT_FALSE(result.empty());

    // Verify: Invocation, ArmFailed(0), ArmMatched(1), Completed
    ASSERT_GE(events.size(), 4u);
    EXPECT_EQ(events[0].phase, MacroTraceEvent::Invocation);
    EXPECT_EQ(events[1].phase, MacroTraceEvent::ArmFailed);
    EXPECT_EQ(events[1].armIndex, 0u);
    EXPECT_EQ(events[2].phase, MacroTraceEvent::ArmMatched);
    EXPECT_EQ(events[2].armIndex, 1u);
    EXPECT_EQ(events[3].phase, MacroTraceEvent::Completed);
}
