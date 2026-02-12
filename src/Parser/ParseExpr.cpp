#include "liva/Parser/Parser.h"

namespace liva {

std::unique_ptr<Expr> Parser::parseExpression() {
    auto expr = parsePrecedenceExpr(0);
    if (!expr) return nullptr;
    // Ternary operator: condition ? thenExpr : elseExpr (lowest precedence)
    if (check(TokenKind::question)) {
        auto startLoc = expr->getStartLoc();
        advance(); // consume ?
        auto thenExpr = parseExpression();
        if (!thenExpr) return nullptr;
        expect(TokenKind::colon);
        auto elseExpr = parseExpression();
        if (!elseExpr) return nullptr;
        return std::make_unique<TernaryExpr>(std::move(expr), std::move(thenExpr),
                                              std::move(elseExpr), rangeFrom(startLoc));
    }
    return expr;
}

std::unique_ptr<Expr> Parser::parsePrecedenceExpr(int minPrec) {
    auto left = parseUnaryExpr();
    if (!left)
        return nullptr;

    // Handle 'as' cast operator
    if (check(TokenKind::kw_as)) {
        auto startLoc = left->getStartLoc();
        advance(); // consume 'as'
        auto targetType = parseType();
        left = std::make_unique<CastExpr>(std::move(left), std::move(targetType),
                                          rangeFrom(startLoc));
    }

    while (true) {
        int prec = getBinaryOpPrecedence(current_.getKind());
        if (prec < minPrec)
            break;

        auto op = getBinaryOp(current_.getKind());
        auto startLoc = left->getStartLoc();
        advance(); // consume operator

        // Right-associativity: use minPrec = prec for right-assoc ops
        // Left-associativity: use minPrec = prec + 1
        auto right = parsePrecedenceExpr(prec + 1);
        if (!right)
            return nullptr;

        left = std::make_unique<BinaryExpr>(op, std::move(left), std::move(right),
                                            rangeFrom(startLoc));
    }

    // Check for range operator (..)
    if (check(TokenKind::dotdot)) {
        auto startLoc = left->getStartLoc();
        advance(); // consume ..
        auto right = parsePrecedenceExpr(0);
        if (!right)
            return nullptr;
        return std::make_unique<RangeExpr>(std::move(left), std::move(right),
                                           rangeFrom(startLoc));
    }

    return left;
}

std::unique_ptr<Expr> Parser::parseUnaryExpr() {
    auto startLoc = current_.getLocation();

    // Unary minus
    if (match(TokenKind::minus)) {
        auto operand = parseUnaryExpr();
        if (!operand)
            return nullptr;
        return std::make_unique<UnaryExpr>(UnaryExpr::Op::Negate, std::move(operand),
                                           rangeFrom(startLoc));
    }

    // Logical not
    if (match(TokenKind::bang)) {
        auto operand = parseUnaryExpr();
        if (!operand)
            return nullptr;
        return std::make_unique<UnaryExpr>(UnaryExpr::Op::Not, std::move(operand),
                                           rangeFrom(startLoc));
    }

    // Bitwise not
    if (match(TokenKind::tilde)) {
        auto operand = parseUnaryExpr();
        if (!operand)
            return nullptr;
        return std::make_unique<UnaryExpr>(UnaryExpr::Op::BitNot, std::move(operand),
                                           rangeFrom(startLoc));
    }

    // try expression
    if (check(TokenKind::kw_try)) {
        advance();
        auto operand = parseUnaryExpr();
        if (!operand) return nullptr;
        return std::make_unique<TryExpr>(std::move(operand), rangeFrom(startLoc));
    }

    // await expression
    if (check(TokenKind::kw_await)) {
        advance();
        auto operand = parseUnaryExpr();
        if (!operand) return nullptr;
        return std::make_unique<AwaitExpr>(std::move(operand), rangeFrom(startLoc));
    }

    // ref / ref mut
    if (check(TokenKind::kw_ref)) {
        advance();
        bool isMut = match(TokenKind::kw_mut);
        auto expr = parseUnaryExpr();
        if (!expr)
            return nullptr;
        return std::make_unique<RefExpr>(std::move(expr), isMut, rangeFrom(startLoc));
    }

    auto primary = parsePrimaryExpr();
    if (!primary)
        return nullptr;

    return parsePostfixExpr(std::move(primary));
}

std::unique_ptr<Expr> Parser::parsePrimaryExpr() {
    auto startLoc = current_.getLocation();

    switch (current_.getKind()) {
    case TokenKind::integer_literal: {
        auto value = current_.getIntegerValue();
        auto range = rangeFrom(startLoc);
        advance();
        return std::make_unique<IntegerLiteralExpr>(value, range);
    }

    case TokenKind::float_literal: {
        auto value = current_.getFloatValue();
        auto range = rangeFrom(startLoc);
        advance();
        return std::make_unique<FloatLiteralExpr>(value, range);
    }

    case TokenKind::bool_literal: {
        bool value = (current_.getText() == "true");
        auto range = rangeFrom(startLoc);
        advance();
        return std::make_unique<BoolLiteralExpr>(value, range);
    }

    case TokenKind::string_literal: {
        auto value = current_.getStringValue();
        auto range = rangeFrom(startLoc);
        advance();
        return std::make_unique<StringLiteralExpr>(std::move(value), range);
    }

    case TokenKind::string_interp_begin: {
        // "hello \(name) world" → ("hello " + toString(name)) + " world"
        auto text = current_.getStringValue();
        advance(); // consume string_interp_begin

        std::unique_ptr<Expr> result;
        if (!text.empty()) {
            result = std::make_unique<StringLiteralExpr>(text, rangeFrom(startLoc));
        }

        // Parse interpolated expression
        auto expr = parseExpression();
        // Wrap in toString() call
        auto toStrCallee = std::make_unique<IdentifierExpr>("toString", rangeFrom(startLoc));
        std::vector<std::unique_ptr<Expr>> toStrArgs;
        toStrArgs.push_back(std::move(expr));
        auto toStrCall = std::make_unique<CallExpr>(
            std::move(toStrCallee), std::move(toStrArgs), rangeFrom(startLoc));

        if (result) {
            result = std::make_unique<BinaryExpr>(
                BinaryExpr::Op::Add, std::move(result), std::move(toStrCall), rangeFrom(startLoc));
        } else {
            result = std::move(toStrCall);
        }

        // Handle mid segments
        while (current_.is(TokenKind::string_interp_mid)) {
            auto midText = current_.getStringValue();
            advance();

            if (!midText.empty()) {
                auto midLit = std::make_unique<StringLiteralExpr>(midText, rangeFrom(startLoc));
                result = std::make_unique<BinaryExpr>(
                    BinaryExpr::Op::Add, std::move(result), std::move(midLit), rangeFrom(startLoc));
            }

            expr = parseExpression();
            auto midCallee = std::make_unique<IdentifierExpr>("toString", rangeFrom(startLoc));
            std::vector<std::unique_ptr<Expr>> midArgs;
            midArgs.push_back(std::move(expr));
            auto midCall = std::make_unique<CallExpr>(
                std::move(midCallee), std::move(midArgs), rangeFrom(startLoc));
            result = std::make_unique<BinaryExpr>(
                BinaryExpr::Op::Add, std::move(result), std::move(midCall), rangeFrom(startLoc));
        }

        // Handle end segment
        if (current_.is(TokenKind::string_interp_end)) {
            auto endText = current_.getStringValue();
            advance();
            if (!endText.empty()) {
                auto endLit = std::make_unique<StringLiteralExpr>(endText, rangeFrom(startLoc));
                result = std::make_unique<BinaryExpr>(
                    BinaryExpr::Op::Add, std::move(result), std::move(endLit), rangeFrom(startLoc));
            }
        }

        return result;
    }

    case TokenKind::kw_nil: {
        auto range = rangeFrom(startLoc);
        advance();
        return std::make_unique<NilLiteralExpr>(range);
    }

    case TokenKind::kw_self: {
        auto range = rangeFrom(startLoc);
        advance();
        return std::make_unique<IdentifierExpr>("self", range);
    }

    case TokenKind::identifier: {
        std::string name(current_.getText());
        advance();
        auto range = rangeFrom(startLoc);

        // Check for struct literal: Name { field: value, ... }
        // Convention: struct names start with uppercase letter
        if (check(TokenKind::l_brace) && !name.empty() &&
            name[0] >= 'A' && name[0] <= 'Z') {
            return parseStructLiteral(name, startLoc);
        }

        return std::make_unique<IdentifierExpr>(std::move(name), range);
    }

    case TokenKind::l_paren: {
        advance(); // consume (
        auto expr = parseExpression();
        if (!expr)
            return nullptr;

        // Comma → tuple literal
        if (match(TokenKind::comma)) {
            std::vector<std::unique_ptr<Expr>> elements;
            elements.push_back(std::move(expr));
            if (!check(TokenKind::r_paren)) {
                do {
                    auto elem = parseExpression();
                    if (!elem) return nullptr;
                    elements.push_back(std::move(elem));
                } while (match(TokenKind::comma));
            }
            expect(TokenKind::r_paren);
            return std::make_unique<TupleLiteralExpr>(std::move(elements), rangeFrom(startLoc));
        }

        // No comma → grouping expression
        expect(TokenKind::r_paren);
        return std::make_unique<GroupExpr>(std::move(expr), rangeFrom(startLoc));
    }

    case TokenKind::l_bracket: {
        return parseArrayLiteral();
    }

    case TokenKind::kw_match: {
        return parseMatchExpr();
    }

    case TokenKind::pipe:
    case TokenKind::pipe_pipe:
        return parseClosureExpr();

    default:
        diag_.report(current_.getLocation(), DiagID::err_expected_expression);
        return nullptr;
    }
}

std::unique_ptr<Expr> Parser::parsePostfixExpr(std::unique_ptr<Expr> base) {
    while (true) {
        if (check(TokenKind::l_paren)) {
            base = parseCallExpr(std::move(base));
            // Check for trailing closure after call: apply(5) |x| { ... }
            if (base && base->getKind() == ASTNode::NodeKind::CallExpr &&
                (check(TokenKind::pipe) || check(TokenKind::pipe_pipe))) {
                auto *call = static_cast<CallExpr *>(base.get());
                auto trailingClosure = parseClosureExpr();
                if (trailingClosure) {
                    call->addArg(std::move(trailingClosure));
                }
            }
        } else if (check(TokenKind::dot)) {
            base = parseMemberExpr(std::move(base), false);
        } else if (check(TokenKind::question_dot)) {
            base = parseMemberExpr(std::move(base), true);
        } else if (check(TokenKind::l_bracket)) {
            base = parseIndexExpr(std::move(base));
        } else if (check(TokenKind::bang)) {
            auto startLoc = base->getStartLoc();
            advance();
            base = std::make_unique<UnwrapExpr>(std::move(base), rangeFrom(startLoc));
        } else {
            break;
        }
    }
    return base;
}

std::unique_ptr<Expr> Parser::parseCallExpr(std::unique_ptr<Expr> callee) {
    auto startLoc = callee->getStartLoc();
    expect(TokenKind::l_paren);

    std::vector<std::unique_ptr<Expr>> args;
    if (!check(TokenKind::r_paren)) {
        do {
            auto arg = parseExpression();
            if (!arg)
                return nullptr;
            args.push_back(std::move(arg));
        } while (match(TokenKind::comma));
    }

    expect(TokenKind::r_paren);

    return std::make_unique<CallExpr>(std::move(callee), std::move(args),
                                      rangeFrom(startLoc));
}

std::unique_ptr<Expr> Parser::parseMemberExpr(std::unique_ptr<Expr> object,
                                               bool isOptionalChain) {
    auto startLoc = object->getStartLoc();
    if (isOptionalChain)
        expect(TokenKind::question_dot);
    else
        expect(TokenKind::dot);

    // Tuple element access: .0, .1, etc.
    if (check(TokenKind::integer_literal)) {
        auto idx = current_;
        advance();
        return std::make_unique<MemberExpr>(std::move(object),
            std::string(idx.getText()), rangeFrom(startLoc), isOptionalChain);
    }

    auto member = expect(TokenKind::identifier);

    return std::make_unique<MemberExpr>(std::move(object), std::string(member.getText()),
                                        rangeFrom(startLoc), isOptionalChain);
}

std::unique_ptr<Expr> Parser::parseIndexExpr(std::unique_ptr<Expr> base) {
    auto startLoc = base->getStartLoc();
    expect(TokenKind::l_bracket);

    auto index = parseExpression();
    if (!index)
        return nullptr;

    expect(TokenKind::r_bracket);

    return std::make_unique<IndexExpr>(std::move(base), std::move(index),
                                       rangeFrom(startLoc));
}

std::unique_ptr<Expr> Parser::parseStructLiteral(const std::string &name,
                                                  SourceLocation startLoc) {
    expect(TokenKind::l_brace);

    std::vector<StructLiteralExpr::FieldInit> fields;
    while (!check(TokenKind::r_brace) && !check(TokenKind::eof)) {
        auto fieldName = expect(TokenKind::identifier);
        expect(TokenKind::colon);
        auto value = parseExpression();
        if (!value)
            return nullptr;

        fields.push_back({std::string(fieldName.getText()), std::move(value)});

        if (!check(TokenKind::r_brace)) {
            match(TokenKind::comma); // Optional trailing comma
        }
    }

    expect(TokenKind::r_brace);

    return std::make_unique<StructLiteralExpr>(name, std::move(fields), rangeFrom(startLoc));
}

std::unique_ptr<Expr> Parser::parseArrayLiteral() {
    auto startLoc = current_.getLocation();
    expect(TokenKind::l_bracket);

    std::vector<std::unique_ptr<Expr>> elements;
    if (!check(TokenKind::r_bracket)) {
        do {
            auto elem = parseExpression();
            if (!elem)
                return nullptr;
            elements.push_back(std::move(elem));
        } while (match(TokenKind::comma));
    }

    expect(TokenKind::r_bracket);

    return std::make_unique<ArrayLiteralExpr>(std::move(elements), rangeFrom(startLoc));
}

std::unique_ptr<Expr> Parser::parseMatchExpr() {
    auto startLoc = current_.getLocation();
    expect(TokenKind::kw_match);

    auto subject = parseExpression();
    if (!subject)
        return nullptr;

    expect(TokenKind::l_brace);

    std::vector<MatchArm> arms;
    while (!check(TokenKind::r_brace) && !check(TokenKind::eof)) {
        MatchArm arm;

        // Parse pattern (simplified: just consume tokens until => or where)
        std::string pattern;
        while (!check(TokenKind::fat_arrow) && !check(TokenKind::kw_where) &&
               !check(TokenKind::r_brace) && !check(TokenKind::eof)) {
            pattern += std::string(current_.getText());
            advance();
        }
        arm.pattern = pattern;

        // Parse optional guard clause: where <expr>
        if (match(TokenKind::kw_where)) {
            arm.guard = parseExpression();
        }

        expect(TokenKind::fat_arrow);

        // Parse body expression
        arm.body = parseExpression();
        if (!arm.body)
            return nullptr;

        arms.push_back(std::move(arm));
    }

    expect(TokenKind::r_brace);

    return std::make_unique<MatchExpr>(std::move(subject), std::move(arms),
                                       rangeFrom(startLoc));
}

std::unique_ptr<Expr> Parser::parseClosureExpr() {
    auto startLoc = current_.getLocation();

    std::vector<ClosureExpr::Param> params;

    if (check(TokenKind::pipe_pipe)) {
        advance(); // consume ||
    } else {
        expect(TokenKind::pipe);
        if (!check(TokenKind::pipe)) {
            do {
                auto name = expect(TokenKind::identifier).getText();
                std::unique_ptr<TypeRepr> type;
                if (match(TokenKind::colon)) {
                    type = parseType();
                }
                ClosureExpr::Param p;
                p.name = std::string(name);
                p.type = std::move(type);
                params.push_back(std::move(p));
            } while (match(TokenKind::comma));
        }
        expect(TokenKind::pipe);
    }

    std::unique_ptr<TypeRepr> returnType;
    if (match(TokenKind::arrow)) {
        returnType = parseType();
    }

    auto body = parseBlock();

    return std::make_unique<ClosureExpr>(
        std::move(params), std::move(returnType),
        std::move(body), rangeFrom(startLoc));
}

} // namespace liva
