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

void Parser::synchronize() {
    int depth = 0;
    while (current_.getKind() != TokenKind::eof) {
        if (current_.getKind() == TokenKind::l_brace) {
            ++depth;
            advance();
            continue;
        }
        if (current_.getKind() == TokenKind::r_brace) {
            if (depth == 0) return; // enclosing block's brace — stop
            --depth;
            advance();
            // After closing a nested block at depth 0, skip trailing 'else'
            if (depth == 0 && check(TokenKind::kw_else)) {
                advance(); // consume 'else'
                continue;  // loop will handle 'if' keyword or '{' brace
            }
            continue;
        }
        if (depth == 0) {
            switch (current_.getKind()) {
                case TokenKind::kw_func:
                case TokenKind::kw_struct:
                case TokenKind::kw_enum:
                case TokenKind::kw_impl:
                case TokenKind::kw_protocol:
                case TokenKind::kw_import:
                case TokenKind::kw_let:
                case TokenKind::kw_var:
                case TokenKind::kw_const:
                case TokenKind::kw_comptime:
                case TokenKind::kw_if:
                case TokenKind::kw_while:
                case TokenKind::kw_for:
                case TokenKind::kw_return:
                case TokenKind::kw_type:
                case TokenKind::kw_pub:
                case TokenKind::kw_async:
                case TokenKind::kw_extern:
                case TokenKind::kw_test:
                    return;
                default: break;
            }
        }
        advance();
    }
}

void Parser::skipBalancedBraces() {
    while (!check(TokenKind::l_brace) && !check(TokenKind::eof))
        advance();
    if (check(TokenKind::eof)) return;
    int depth = 0;
    do {
        if (check(TokenKind::l_brace)) ++depth;
        else if (check(TokenKind::r_brace)) --depth;
        advance();
    } while (depth > 0 && !check(TokenKind::eof));
}

void Parser::synchronizeBody() {
    while (current_.getKind() != TokenKind::eof) {
        if (current_.getKind() == TokenKind::r_brace)
            return;
        if (current_.getKind() == TokenKind::semicolon) {
            advance();
            continue;
        }
        switch (current_.getKind()) {
            case TokenKind::kw_var:
            case TokenKind::kw_let:
            case TokenKind::kw_func:
            case TokenKind::kw_case:
            case TokenKind::kw_type:
            case TokenKind::kw_pub:
            case TokenKind::kw_async:
            case TokenKind::kw_struct:
            case TokenKind::kw_enum:
            case TokenKind::kw_impl:
            case TokenKind::kw_protocol:
            case TokenKind::kw_mut:
                return;
            default:
                advance();
        }
    }
}

std::unique_ptr<TranslationUnit> Parser::parseTranslationUnit() {
    auto tu = std::make_unique<TranslationUnit>();

    while (!current_.is(TokenKind::eof)) {
        // Drain pending declarations (from extern blocks)
        while (!pendingDecls_.empty()) {
            tu->addDeclaration(std::move(pendingDecls_.front()));
            pendingDecls_.erase(pendingDecls_.begin());
        }

        if (diag_.hasMaxErrors()) break;  // stop after too many errors

        // Collect consecutive doc comments before a declaration
        std::string docComment;
        while (current_.is(TokenKind::doc_comment)) {
            if (!docComment.empty())
                docComment += '\n';
            docComment += std::string(current_.getText());
            advance();
        }

        if (current_.is(TokenKind::eof)) break;

        auto decl = parseTopLevelDecl();
        if (decl) {
            if (!docComment.empty()) {
                if (auto *d = dynamic_cast<Decl *>(decl.get()))
                    d->setDocComment(std::move(docComment));
            }
            tu->addDeclaration(std::move(decl));
        } else {
            // Error recovery: skip to next declaration boundary
            synchronize();
        }
    }

    // Drain any remaining pending declarations (e.g., from extern blocks at end of file)
    while (!pendingDecls_.empty()) {
        tu->addDeclaration(std::move(pendingDecls_.front()));
        pendingDecls_.erase(pendingDecls_.begin());
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
