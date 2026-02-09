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
