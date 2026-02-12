#include "liva/Common/Diagnostics.h"
#include "liva/Common/SourceLocation.h"
#include "liva/Lexer/Lexer.h"
#include "liva/Lexer/Token.h"
#include <gtest/gtest.h>

using namespace liva;

class LexerTest : public ::testing::Test {
protected:
    // Keep SourceManager alive so string_views in Tokens remain valid
    std::unique_ptr<SourceManager> sm_;
    DiagnosticsEngine diag_;

    std::vector<Token> lex(const std::string &source) {
        sm_ = std::make_unique<SourceManager>("test.liva", source);
        diag_.clear();
        diag_.setSourceManager(sm_.get());
        Lexer lexer(*sm_, diag_);
        return lexer.lexAll();
    }

    void expectToken(const Token &tok, TokenKind kind) {
        EXPECT_EQ(tok.getKind(), kind)
            << "Expected " << getTokenKindName(kind)
            << " but got " << getTokenKindName(tok.getKind());
    }
};

TEST_F(LexerTest, EmptySource) {
    auto tokens = lex("");
    ASSERT_EQ(tokens.size(), 1);
    expectToken(tokens[0], TokenKind::eof);
}

TEST_F(LexerTest, IntegerLiterals) {
    auto tokens = lex("42 0xFF 0b1010 0o77");
    ASSERT_GE(tokens.size(), 4);
    expectToken(tokens[0], TokenKind::integer_literal);
    EXPECT_EQ(tokens[0].getIntegerValue(), 42);
    expectToken(tokens[1], TokenKind::integer_literal);
    EXPECT_EQ(tokens[1].getIntegerValue(), 255);
    expectToken(tokens[2], TokenKind::integer_literal);
    EXPECT_EQ(tokens[2].getIntegerValue(), 10);
    expectToken(tokens[3], TokenKind::integer_literal);
    EXPECT_EQ(tokens[3].getIntegerValue(), 63);
}

TEST_F(LexerTest, FloatLiterals) {
    auto tokens = lex("3.14 1.0 2.5e10");
    ASSERT_GE(tokens.size(), 3);
    expectToken(tokens[0], TokenKind::float_literal);
    EXPECT_DOUBLE_EQ(tokens[0].getFloatValue(), 3.14);
    expectToken(tokens[1], TokenKind::float_literal);
    expectToken(tokens[2], TokenKind::float_literal);
}

TEST_F(LexerTest, StringLiterals) {
    auto tokens = lex(R"("hello" "world\n" "")");
    ASSERT_GE(tokens.size(), 3);
    expectToken(tokens[0], TokenKind::string_literal);
    EXPECT_EQ(tokens[0].getStringValue(), "hello");
    expectToken(tokens[1], TokenKind::string_literal);
    EXPECT_EQ(tokens[1].getStringValue(), "world\n");
    expectToken(tokens[2], TokenKind::string_literal);
    EXPECT_EQ(tokens[2].getStringValue(), "");
}

TEST_F(LexerTest, BoolLiterals) {
    auto tokens = lex("true false");
    ASSERT_GE(tokens.size(), 2);
    expectToken(tokens[0], TokenKind::bool_literal);
    expectToken(tokens[1], TokenKind::bool_literal);
}

TEST_F(LexerTest, Keywords) {
    auto tokens = lex("let var func return if else while for in struct");
    ASSERT_GE(tokens.size(), 10);
    expectToken(tokens[0], TokenKind::kw_let);
    expectToken(tokens[1], TokenKind::kw_var);
    expectToken(tokens[2], TokenKind::kw_func);
    expectToken(tokens[3], TokenKind::kw_return);
    expectToken(tokens[4], TokenKind::kw_if);
    expectToken(tokens[5], TokenKind::kw_else);
    expectToken(tokens[6], TokenKind::kw_while);
    expectToken(tokens[7], TokenKind::kw_for);
    expectToken(tokens[8], TokenKind::kw_in);
    expectToken(tokens[9], TokenKind::kw_struct);
}

TEST_F(LexerTest, TypeKeywords) {
    auto tokens = lex("i32 i64 f64 bool string void");
    ASSERT_GE(tokens.size(), 6);
    expectToken(tokens[0], TokenKind::kw_i32);
    expectToken(tokens[1], TokenKind::kw_i64);
    expectToken(tokens[2], TokenKind::kw_f64);
    expectToken(tokens[3], TokenKind::kw_bool);
    expectToken(tokens[4], TokenKind::kw_string);
    expectToken(tokens[5], TokenKind::kw_void);
}

TEST_F(LexerTest, Identifiers) {
    auto tokens = lex("foo bar _temp my_var x123");
    ASSERT_GE(tokens.size(), 5);
    for (int i = 0; i < 5; ++i) {
        expectToken(tokens[i], TokenKind::identifier);
    }
    EXPECT_EQ(tokens[0].getText(), "foo");
    EXPECT_EQ(tokens[1].getText(), "bar");
}

TEST_F(LexerTest, Operators) {
    auto tokens = lex("+ - * / % == != < <= > >= && || !");
    ASSERT_GE(tokens.size(), 14);
    expectToken(tokens[0], TokenKind::plus);
    expectToken(tokens[1], TokenKind::minus);
    expectToken(tokens[2], TokenKind::star);
    expectToken(tokens[3], TokenKind::slash);
    expectToken(tokens[4], TokenKind::percent);
    expectToken(tokens[5], TokenKind::equal_equal);
    expectToken(tokens[6], TokenKind::bang_equal);
    expectToken(tokens[7], TokenKind::less);
    expectToken(tokens[8], TokenKind::less_equal);
    expectToken(tokens[9], TokenKind::greater);
    expectToken(tokens[10], TokenKind::greater_equal);
    expectToken(tokens[11], TokenKind::amp_amp);
    expectToken(tokens[12], TokenKind::pipe_pipe);
    expectToken(tokens[13], TokenKind::bang);
}

TEST_F(LexerTest, Punctuators) {
    auto tokens = lex("( ) { } [ ] , : ; . -> =>");
    ASSERT_GE(tokens.size(), 12);
    expectToken(tokens[0], TokenKind::l_paren);
    expectToken(tokens[1], TokenKind::r_paren);
    expectToken(tokens[2], TokenKind::l_brace);
    expectToken(tokens[3], TokenKind::r_brace);
    expectToken(tokens[4], TokenKind::l_bracket);
    expectToken(tokens[5], TokenKind::r_bracket);
    expectToken(tokens[6], TokenKind::comma);
    expectToken(tokens[7], TokenKind::colon);
    expectToken(tokens[8], TokenKind::semicolon);
    expectToken(tokens[9], TokenKind::dot);
    expectToken(tokens[10], TokenKind::arrow);
    expectToken(tokens[11], TokenKind::fat_arrow);
}

TEST_F(LexerTest, CompoundAssignment) {
    auto tokens = lex("+= -= *= /= %=");
    ASSERT_GE(tokens.size(), 5);
    expectToken(tokens[0], TokenKind::plus_equal);
    expectToken(tokens[1], TokenKind::minus_equal);
    expectToken(tokens[2], TokenKind::star_equal);
    expectToken(tokens[3], TokenKind::slash_equal);
    expectToken(tokens[4], TokenKind::percent_equal);
}

TEST_F(LexerTest, Comments) {
    auto tokens = lex("foo // comment\nbar");
    ASSERT_GE(tokens.size(), 2);
    expectToken(tokens[0], TokenKind::identifier);
    EXPECT_EQ(tokens[0].getText(), "foo");
    expectToken(tokens[1], TokenKind::identifier);
    EXPECT_EQ(tokens[1].getText(), "bar");
}

TEST_F(LexerTest, BlockComments) {
    auto tokens = lex("foo /* comment */ bar");
    ASSERT_GE(tokens.size(), 2);
    expectToken(tokens[0], TokenKind::identifier);
    expectToken(tokens[1], TokenKind::identifier);
}

TEST_F(LexerTest, FunctionDeclaration) {
    auto tokens = lex("func add(a: i32, b: i32) -> i32 { return a + b }");
    ASSERT_GE(tokens.size(), 16);
    expectToken(tokens[0], TokenKind::kw_func);
    expectToken(tokens[1], TokenKind::identifier);
    expectToken(tokens[2], TokenKind::l_paren);
    expectToken(tokens[3], TokenKind::identifier);
    expectToken(tokens[4], TokenKind::colon);
    expectToken(tokens[5], TokenKind::kw_i32);
}

TEST_F(LexerTest, SourceLocations) {
    auto tokens = lex("let x = 42\nvar y = 10");
    ASSERT_GE(tokens.size(), 8);

    // First line
    EXPECT_EQ(tokens[0].getLocation().line, 1u);
    EXPECT_EQ(tokens[0].getLocation().column, 1u);

    // Second line
    EXPECT_EQ(tokens[4].getLocation().line, 2u);
}

TEST_F(LexerTest, RefAndMut) {
    auto tokens = lex("ref mut");
    ASSERT_GE(tokens.size(), 2);
    expectToken(tokens[0], TokenKind::kw_ref);
    expectToken(tokens[1], TokenKind::kw_mut);
}

TEST_F(LexerTest, ColonColon) {
    auto tokens = lex("Foo::Bar");
    ASSERT_GE(tokens.size(), 3);
    expectToken(tokens[0], TokenKind::identifier);
    expectToken(tokens[1], TokenKind::coloncolon);
    expectToken(tokens[2], TokenKind::identifier);
}

TEST_F(LexerTest, StringInterpolationSimple) {
    auto tokens = lex(R"--("hello \(x)")--");
    ASSERT_GE(tokens.size(), 3);
    expectToken(tokens[0], TokenKind::string_interp_begin);
    EXPECT_EQ(tokens[0].getStringValue(), "hello ");
    expectToken(tokens[1], TokenKind::identifier);
    EXPECT_EQ(tokens[1].getText(), "x");
    expectToken(tokens[2], TokenKind::string_interp_end);
    EXPECT_EQ(tokens[2].getStringValue(), "");
}

TEST_F(LexerTest, StringInterpolationMulti) {
    auto tokens = lex(R"--("\(a) and \(b)")--");
    ASSERT_GE(tokens.size(), 5);
    expectToken(tokens[0], TokenKind::string_interp_begin);
    EXPECT_EQ(tokens[0].getStringValue(), "");
    expectToken(tokens[1], TokenKind::identifier);
    EXPECT_EQ(tokens[1].getText(), "a");
    expectToken(tokens[2], TokenKind::string_interp_mid);
    EXPECT_EQ(tokens[2].getStringValue(), " and ");
    expectToken(tokens[3], TokenKind::identifier);
    EXPECT_EQ(tokens[3].getText(), "b");
    expectToken(tokens[4], TokenKind::string_interp_end);
    EXPECT_EQ(tokens[4].getStringValue(), "");
}

TEST_F(LexerTest, StringInterpolationExpr) {
    auto tokens = lex(R"--("val=\(a+b)")--");
    ASSERT_GE(tokens.size(), 5);
    expectToken(tokens[0], TokenKind::string_interp_begin);
    EXPECT_EQ(tokens[0].getStringValue(), "val=");
    expectToken(tokens[1], TokenKind::identifier);
    expectToken(tokens[2], TokenKind::plus);
    expectToken(tokens[3], TokenKind::identifier);
    expectToken(tokens[4], TokenKind::string_interp_end);
}

TEST_F(LexerTest, NilCoalesceToken) {
    auto tokens = lex("x ?? 0");
    ASSERT_GE(tokens.size(), 3);
    expectToken(tokens[0], TokenKind::identifier);
    expectToken(tokens[1], TokenKind::question_question);
    expectToken(tokens[2], TokenKind::integer_literal);
}

TEST_F(LexerTest, QuestionStillWorks) {
    auto tokens = lex("i32?");
    ASSERT_GE(tokens.size(), 2);
    expectToken(tokens[0], TokenKind::kw_i32);
    expectToken(tokens[1], TokenKind::question);
}

TEST_F(LexerTest, OptionalChainToken) {
    auto tokens = lex("x?.y");
    ASSERT_GE(tokens.size(), 3);
    expectToken(tokens[0], TokenKind::identifier);
    expectToken(tokens[1], TokenKind::question_dot);
    expectToken(tokens[2], TokenKind::identifier);
}

TEST_F(LexerTest, QuestionDotVsQuestionQuestion) {
    auto tokens = lex("a ?? b ?. c");
    ASSERT_GE(tokens.size(), 5);
    expectToken(tokens[0], TokenKind::identifier);
    expectToken(tokens[1], TokenKind::question_question);
    expectToken(tokens[2], TokenKind::identifier);
    expectToken(tokens[3], TokenKind::question_dot);
    expectToken(tokens[4], TokenKind::identifier);
}

TEST_F(LexerTest, AllKeywords) {
    // All 30 language keywords (true/false excluded — they lex as bool_literal)
    auto tokens = lex(
        "let var func return if else while for in struct "
        "enum match case impl protocol ref mut import pub self "
        "break continue as nil where async await const try type"
    );
    ASSERT_GE(tokens.size(), 30);
    expectToken(tokens[0],  TokenKind::kw_let);
    expectToken(tokens[1],  TokenKind::kw_var);
    expectToken(tokens[2],  TokenKind::kw_func);
    expectToken(tokens[3],  TokenKind::kw_return);
    expectToken(tokens[4],  TokenKind::kw_if);
    expectToken(tokens[5],  TokenKind::kw_else);
    expectToken(tokens[6],  TokenKind::kw_while);
    expectToken(tokens[7],  TokenKind::kw_for);
    expectToken(tokens[8],  TokenKind::kw_in);
    expectToken(tokens[9],  TokenKind::kw_struct);
    expectToken(tokens[10], TokenKind::kw_enum);
    expectToken(tokens[11], TokenKind::kw_match);
    expectToken(tokens[12], TokenKind::kw_case);
    expectToken(tokens[13], TokenKind::kw_impl);
    expectToken(tokens[14], TokenKind::kw_protocol);
    expectToken(tokens[15], TokenKind::kw_ref);
    expectToken(tokens[16], TokenKind::kw_mut);
    expectToken(tokens[17], TokenKind::kw_import);
    expectToken(tokens[18], TokenKind::kw_pub);
    expectToken(tokens[19], TokenKind::kw_self);
    expectToken(tokens[20], TokenKind::kw_break);
    expectToken(tokens[21], TokenKind::kw_continue);
    expectToken(tokens[22], TokenKind::kw_as);
    expectToken(tokens[23], TokenKind::kw_nil);
    expectToken(tokens[24], TokenKind::kw_where);
    expectToken(tokens[25], TokenKind::kw_async);
    expectToken(tokens[26], TokenKind::kw_await);
    expectToken(tokens[27], TokenKind::kw_const);
    expectToken(tokens[28], TokenKind::kw_try);
    expectToken(tokens[29], TokenKind::kw_type);
}

// ===== TD5: Error Path Tests =====

TEST_F(LexerTest, UnterminatedString) {
    auto tokens = lex("\"hello");
    EXPECT_TRUE(diag_.hasErrors());
    ASSERT_FALSE(diag_.getDiagnostics().empty());
    EXPECT_EQ(diag_.getDiagnostics()[0].id, DiagID::err_unterminated_string);
}

TEST_F(LexerTest, UnterminatedBlockComment) {
    auto tokens = lex("/* comment");
    EXPECT_TRUE(diag_.hasErrors());
    ASSERT_FALSE(diag_.getDiagnostics().empty());
    EXPECT_EQ(diag_.getDiagnostics()[0].id, DiagID::err_unterminated_block_comment);
}

TEST_F(LexerTest, InvalidEscapeSequence) {
    auto tokens = lex("\"test\\q\"");
    EXPECT_TRUE(diag_.hasErrors());
    bool found = false;
    for (auto &d : diag_.getDiagnostics())
        if (d.id == DiagID::err_invalid_escape_sequence) found = true;
    EXPECT_TRUE(found);
}

TEST_F(LexerTest, EmptyCharLiteral) {
    auto tokens = lex("''");
    EXPECT_TRUE(diag_.hasErrors());
    ASSERT_FALSE(diag_.getDiagnostics().empty());
    EXPECT_EQ(diag_.getDiagnostics()[0].id, DiagID::err_empty_char_literal);
}

TEST_F(LexerTest, UnexpectedCharacter) {
    auto tokens = lex("`");
    EXPECT_TRUE(diag_.hasErrors());
    ASSERT_FALSE(diag_.getDiagnostics().empty());
    EXPECT_EQ(diag_.getDiagnostics()[0].id, DiagID::err_unexpected_character);
}

// ===== TD5: Bitwise & Special Token Tests =====

TEST_F(LexerTest, BitwiseAndSpecialTokens) {
    auto tokens = lex("& | ^ ~ << >> @ #");
    ASSERT_GE(tokens.size(), 8u);
    expectToken(tokens[0], TokenKind::amp);
    expectToken(tokens[1], TokenKind::pipe);
    expectToken(tokens[2], TokenKind::caret);
    expectToken(tokens[3], TokenKind::tilde);
    expectToken(tokens[4], TokenKind::less_less);
    expectToken(tokens[5], TokenKind::greater_greater);
    expectToken(tokens[6], TokenKind::at);
    expectToken(tokens[7], TokenKind::hash);
}

// ===== M29: Multi-line string literals =====

TEST_F(LexerTest, MultiLineString) {
    auto tokens = lex("let s = \"\"\"\nhello\nworld\n\"\"\"");
    // let s = """
    // hello
    // world
    // """
    bool found = false;
    for (auto &tok : tokens) {
        if (tok.getKind() == TokenKind::string_literal &&
            tok.getStringValue().find("hello") != std::string::npos) {
            found = true;
            EXPECT_NE(tok.getStringValue().find("world"), std::string::npos);
        }
    }
    EXPECT_TRUE(found);
}

TEST_F(LexerTest, MultiLineStringEmpty) {
    auto tokens = lex("\"\"\"\"\"\"");
    ASSERT_GE(tokens.size(), 1);
    expectToken(tokens[0], TokenKind::string_literal);
    EXPECT_EQ(tokens[0].getStringValue(), "");
}

// === S6: Unicode Escape Sequences ===

TEST_F(LexerTest, UnicodeEscapeBasicASCII) {
    // \u{41} = 'A'
    auto tokens = lex(R"--("\u{41}")--");
    ASSERT_GE(tokens.size(), 1);
    expectToken(tokens[0], TokenKind::string_literal);
    EXPECT_EQ(tokens[0].getStringValue(), "A");
}

TEST_F(LexerTest, UnicodeEscapeTwoByte) {
    // \u{00E9} = 'é' (U+00E9, 2-byte UTF-8: 0xC3 0xA9)
    auto tokens = lex(R"--("\u{00E9}")--");
    ASSERT_GE(tokens.size(), 1);
    expectToken(tokens[0], TokenKind::string_literal);
    std::string expected;
    expected += static_cast<char>(0xC3);
    expected += static_cast<char>(0xA9);
    EXPECT_EQ(tokens[0].getStringValue(), expected);
}

TEST_F(LexerTest, UnicodeEscapeThreeByte) {
    // \u{4E16} = '世' (U+4E16, 3-byte UTF-8: 0xE4 0xB8 0x96)
    auto tokens = lex(R"--("\u{4E16}")--");
    ASSERT_GE(tokens.size(), 1);
    expectToken(tokens[0], TokenKind::string_literal);
    std::string expected;
    expected += static_cast<char>(0xE4);
    expected += static_cast<char>(0xB8);
    expected += static_cast<char>(0x96);
    EXPECT_EQ(tokens[0].getStringValue(), expected);
}

TEST_F(LexerTest, UnicodeEscapeFourByte) {
    // \u{1F600} = '😀' (U+1F600, 4-byte UTF-8)
    auto tokens = lex(R"--("\u{1F600}")--");
    ASSERT_GE(tokens.size(), 1);
    expectToken(tokens[0], TokenKind::string_literal);
    std::string expected;
    expected += static_cast<char>(0xF0);
    expected += static_cast<char>(0x9F);
    expected += static_cast<char>(0x98);
    expected += static_cast<char>(0x80);
    EXPECT_EQ(tokens[0].getStringValue(), expected);
}

TEST_F(LexerTest, UnicodeEscapeMixed) {
    // "A\u{00E9}B" = "AéB"
    auto tokens = lex(R"--("A\u{00E9}B")--");
    ASSERT_GE(tokens.size(), 1);
    expectToken(tokens[0], TokenKind::string_literal);
    std::string expected = "A";
    expected += static_cast<char>(0xC3);
    expected += static_cast<char>(0xA9);
    expected += "B";
    EXPECT_EQ(tokens[0].getStringValue(), expected);
}

TEST_F(LexerTest, UnicodeEscapeNullChar) {
    // \u{0} = null byte
    auto tokens = lex(R"--("\u{0}")--");
    ASSERT_GE(tokens.size(), 1);
    expectToken(tokens[0], TokenKind::string_literal);
    std::string expected(1, '\0');
    EXPECT_EQ(tokens[0].getStringValue(), expected);
}

TEST_F(LexerTest, UnicodeEscapeMissingBrace) {
    // \u without { should produce error
    auto tokens = lex(R"--("\uXYZ")--");
    EXPECT_TRUE(diag_.hasErrors());
    bool found = false;
    for (auto &d : diag_.getDiagnostics())
        if (d.id == DiagID::err_invalid_escape_sequence) found = true;
    EXPECT_TRUE(found);
}

// === F7: Variadic / Ellipsis Token ===

TEST_F(LexerTest, EllipsisToken) {
    auto tokens = lex(".. ...");
    ASSERT_GE(tokens.size(), 2);
    expectToken(tokens[0], TokenKind::dotdot);
    expectToken(tokens[1], TokenKind::ellipsis);
}
