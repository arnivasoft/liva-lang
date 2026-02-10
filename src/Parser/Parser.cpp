#include "liva/Parser/Parser.h"

namespace liva {

Parser::Parser(Lexer &lexer, DiagnosticsEngine &diag)
    : lexer_(lexer), diag_(diag), current_(lexer.nextToken()) {}

Token Parser::advance() {
    Token prev = current_;
    current_ = lexer_.nextToken();
    return prev;
}

Token Parser::peek() { return lexer_.peekToken(); }

bool Parser::match(TokenKind kind) {
    if (current_.is(kind)) {
        advance();
        return true;
    }
    return false;
}

Token Parser::expect(TokenKind kind) {
    if (current_.is(kind)) {
        return advance();
    }

    diag_.report(current_.getLocation(), DiagID::err_expected_token, getTokenSpelling(kind),
                 std::string(current_.getText()));
    return Token();
}

SourceRange Parser::rangeFrom(SourceLocation start) const {
    return {start, current_.getLocation()};
}

std::unique_ptr<TranslationUnit> Parser::parseTranslationUnit() {
    auto tu = std::make_unique<TranslationUnit>();

    while (!current_.is(TokenKind::eof)) {
        auto decl = parseTopLevelDecl();
        if (decl) {
            tu->addDeclaration(std::move(decl));
        } else {
            // Error recovery: skip to next declaration
            advance();
        }
    }

    return tu;
}

int Parser::getBinaryOpPrecedence(TokenKind kind) const {
    switch (kind) {
    case TokenKind::question_question:
        return 0;
    case TokenKind::pipe_pipe:
        return 1;
    case TokenKind::amp_amp:
        return 2;
    case TokenKind::pipe:
        return 3;
    case TokenKind::caret:
        return 4;
    case TokenKind::amp:
        return 5;
    case TokenKind::equal_equal:
    case TokenKind::bang_equal:
        return 6;
    case TokenKind::less:
    case TokenKind::less_equal:
    case TokenKind::greater:
    case TokenKind::greater_equal:
        return 7;
    case TokenKind::less_less:
    case TokenKind::greater_greater:
        return 8;
    case TokenKind::plus:
    case TokenKind::minus:
        return 9;
    case TokenKind::star:
    case TokenKind::slash:
    case TokenKind::percent:
        return 10;
    default:
        return -1;
    }
}

BinaryExpr::Op Parser::getBinaryOp(TokenKind kind) const {
    switch (kind) {
    case TokenKind::plus:
        return BinaryExpr::Op::Add;
    case TokenKind::minus:
        return BinaryExpr::Op::Sub;
    case TokenKind::star:
        return BinaryExpr::Op::Mul;
    case TokenKind::slash:
        return BinaryExpr::Op::Div;
    case TokenKind::percent:
        return BinaryExpr::Op::Mod;
    case TokenKind::equal_equal:
        return BinaryExpr::Op::Eq;
    case TokenKind::bang_equal:
        return BinaryExpr::Op::NotEq;
    case TokenKind::less:
        return BinaryExpr::Op::Less;
    case TokenKind::less_equal:
        return BinaryExpr::Op::LessEq;
    case TokenKind::greater:
        return BinaryExpr::Op::Greater;
    case TokenKind::greater_equal:
        return BinaryExpr::Op::GreaterEq;
    case TokenKind::amp_amp:
        return BinaryExpr::Op::And;
    case TokenKind::pipe_pipe:
        return BinaryExpr::Op::Or;
    case TokenKind::amp:
        return BinaryExpr::Op::BitAnd;
    case TokenKind::pipe:
        return BinaryExpr::Op::BitOr;
    case TokenKind::caret:
        return BinaryExpr::Op::BitXor;
    case TokenKind::less_less:
        return BinaryExpr::Op::Shl;
    case TokenKind::greater_greater:
        return BinaryExpr::Op::Shr;
    case TokenKind::question_question:
        return BinaryExpr::Op::NilCoalesce;
    default:
        return BinaryExpr::Op::Add; // shouldn't reach
    }
}

bool Parser::isAssignOp(TokenKind kind) const {
    switch (kind) {
    case TokenKind::equal:
    case TokenKind::plus_equal:
    case TokenKind::minus_equal:
    case TokenKind::star_equal:
    case TokenKind::slash_equal:
    case TokenKind::percent_equal:
        return true;
    default:
        return false;
    }
}

AssignExpr::Op Parser::getAssignOp(TokenKind kind) const {
    switch (kind) {
    case TokenKind::equal:
        return AssignExpr::Op::Assign;
    case TokenKind::plus_equal:
        return AssignExpr::Op::AddAssign;
    case TokenKind::minus_equal:
        return AssignExpr::Op::SubAssign;
    case TokenKind::star_equal:
        return AssignExpr::Op::MulAssign;
    case TokenKind::slash_equal:
        return AssignExpr::Op::DivAssign;
    case TokenKind::percent_equal:
        return AssignExpr::Op::ModAssign;
    default:
        return AssignExpr::Op::Assign;
    }
}

} // namespace liva
