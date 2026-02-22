#include "liva/Parser/Parser.h"

namespace liva {

std::unique_ptr<ASTNode> Parser::parseStatement() {
    switch (current_.getKind()) {
    case TokenKind::kw_const: {
        advance(); // consume 'const'
        return parseConstDecl();
    }
    case TokenKind::kw_let:
    case TokenKind::kw_var:
        return parseVarDecl();

    case TokenKind::kw_return:
        return parseReturnStmt();

    case TokenKind::kw_if:
        return parseIfStmt();

    case TokenKind::kw_while:
        return parseWhileStmt();

    case TokenKind::kw_for:
        return parseForStmt();

    case TokenKind::l_brace:
        return parseBlock();

    case TokenKind::kw_break: {
        auto loc = current_.getLocation();
        advance();
        return std::make_unique<BreakStmt>(rangeFrom(loc));
    }

    case TokenKind::kw_continue: {
        auto loc = current_.getLocation();
        advance();
        return std::make_unique<ContinueStmt>(rangeFrom(loc));
    }

    default: {
        // Expression statement
        auto startLoc = current_.getLocation();
        auto expr = parseExpression();
        if (!expr)
            return nullptr;

        // Check for assignment
        if (isAssignOp(current_.getKind())) {
            auto op = getAssignOp(current_.getKind());
            advance();
            auto value = parseExpression();
            if (!value)
                return nullptr;
            auto assign = std::make_unique<AssignExpr>(op, std::move(expr), std::move(value),
                                                       rangeFrom(startLoc));
            return std::make_unique<ExprStmt>(std::move(assign), rangeFrom(startLoc));
        }

        return std::make_unique<ExprStmt>(std::move(expr), rangeFrom(startLoc));
    }
    }
}

std::unique_ptr<BlockStmt> Parser::parseBlock() {
    auto startLoc = current_.getLocation();
    expect(TokenKind::l_brace);

    std::vector<std::unique_ptr<ASTNode>> statements;
    while (!check(TokenKind::r_brace) && !check(TokenKind::eof)) {
        if (diag_.hasMaxErrors()) break;

        auto stmt = parseStatement();
        if (stmt) {
            statements.push_back(std::move(stmt));
        } else {
            // Error recovery: skip to next statement boundary
            synchronize();
        }
    }

    expect(TokenKind::r_brace);

    return std::make_unique<BlockStmt>(std::move(statements), rangeFrom(startLoc));
}

std::unique_ptr<ReturnStmt> Parser::parseReturnStmt() {
    auto startLoc = current_.getLocation();
    expect(TokenKind::kw_return);

    std::unique_ptr<Expr> value;
    // Check if there's a return value (not a closing brace or eof)
    if (!check(TokenKind::r_brace) && !check(TokenKind::eof)) {
        value = parseExpression();
    }

    return std::make_unique<ReturnStmt>(std::move(value), rangeFrom(startLoc));
}

std::unique_ptr<ASTNode> Parser::parseIfStmt() {
    auto startLoc = current_.getLocation();
    expect(TokenKind::kw_if);

    // if let pattern → optional binding
    if (check(TokenKind::kw_let)) {
        return parseIfLetStmt(startLoc);
    }

    auto condition = parseExpression();
    if (!condition) {
        skipBalancedBraces(); // skip then-block
        if (match(TokenKind::kw_else)) {
            if (check(TokenKind::kw_if)) {
                advance(); // skip 'if', let synchronize handle the rest
            }
            skipBalancedBraces(); // skip else-block
        }
        return nullptr;
    }

    auto thenBody = parseBlock();
    if (!thenBody)
        return nullptr;

    std::unique_ptr<ASTNode> elseBody;
    if (match(TokenKind::kw_else)) {
        if (check(TokenKind::kw_if)) {
            elseBody = parseIfStmt();
        } else {
            elseBody = parseBlock();
        }
    }

    return std::make_unique<IfStmt>(std::move(condition), std::move(thenBody),
                                    std::move(elseBody), rangeFrom(startLoc));
}

std::unique_ptr<IfLetStmt> Parser::parseIfLetStmt(SourceLocation ifLoc) {
    expect(TokenKind::kw_let);
    auto bindingName = expect(TokenKind::identifier);
    expect(TokenKind::equal);
    auto optionalExpr = parseExpression();
    if (!optionalExpr) {
        skipBalancedBraces();
        if (match(TokenKind::kw_else)) {
            if (check(TokenKind::kw_if)) advance();
            skipBalancedBraces();
        }
        return nullptr;
    }
    auto thenBody = parseBlock();
    if (!thenBody) return nullptr;
    std::unique_ptr<ASTNode> elseBody;
    if (match(TokenKind::kw_else)) {
        if (check(TokenKind::kw_if)) {
            elseBody = parseIfStmt();
        } else {
            elseBody = parseBlock();
        }
    }
    return std::make_unique<IfLetStmt>(
        std::string(bindingName.getText()), std::move(optionalExpr),
        std::move(thenBody), std::move(elseBody), rangeFrom(ifLoc));
}

std::unique_ptr<ASTNode> Parser::parseWhileStmt() {
    auto startLoc = current_.getLocation();
    expect(TokenKind::kw_while);

    // while let x = optional { body }
    if (current_.getKind() == TokenKind::kw_let) {
        advance(); // consume 'let'
        auto bindingName = expect(TokenKind::identifier).getText();
        expect(TokenKind::equal);
        auto optionalExpr = parseExpression();
        if (!optionalExpr) { skipBalancedBraces(); return nullptr; }
        auto body = parseBlock();
        if (!body) return nullptr;
        return std::make_unique<WhileLetStmt>(std::string(bindingName),
            std::move(optionalExpr), std::move(body), rangeFrom(startLoc));
    }

    auto condition = parseExpression();
    if (!condition) { skipBalancedBraces(); return nullptr; }

    auto body = parseBlock();
    if (!body)
        return nullptr;

    return std::make_unique<WhileStmt>(std::move(condition), std::move(body),
                                       rangeFrom(startLoc));
}

std::unique_ptr<ForStmt> Parser::parseForStmt() {
    auto startLoc = current_.getLocation();
    expect(TokenKind::kw_for);

    // Check for 'await' keyword: for await x in stream { ... }
    bool isAwait = false;
    if (current_.getKind() == TokenKind::kw_await) {
        advance();
        isAwait = true;
    }

    // Tuple pattern: for (k, v) in map { ... }
    if (current_.getKind() == TokenKind::l_paren) {
        advance(); // '('
        auto var1 = expect(TokenKind::identifier);
        expect(TokenKind::comma);
        auto var2 = expect(TokenKind::identifier);
        expect(TokenKind::r_paren);
        expect(TokenKind::kw_in);

        auto iterable = parseExpression();
        if (!iterable) { skipBalancedBraces(); return nullptr; }

        auto body = parseBlock();
        if (!body)
            return nullptr;

        auto result = std::make_unique<ForStmt>(std::string(var1.getText()),
                                         std::string(var2.getText()),
                                         std::move(iterable), std::move(body),
                                         rangeFrom(startLoc));
        result->setAwait(isAwait);
        return result;
    }

    // Single variable: for await x in stream { ... }  OR  for x in expr { ... }
    auto varName = expect(TokenKind::identifier);
    expect(TokenKind::kw_in);

    auto iterable = parseExpression();
    if (!iterable) { skipBalancedBraces(); return nullptr; }

    auto body = parseBlock();
    if (!body)
        return nullptr;

    auto result = std::make_unique<ForStmt>(std::string(varName.getText()), std::move(iterable),
                                     std::move(body), rangeFrom(startLoc));
    result->setAwait(isAwait);
    return result;
}

} // namespace liva
