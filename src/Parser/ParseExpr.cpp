#include "liva/Parser/Parser.h"

namespace liva {

std::unique_ptr<Expr> Parser::parseExpression() {
    return parsePrecedenceExpr(0);
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
        expect(TokenKind::r_paren);
        return std::make_unique<GroupExpr>(std::move(expr), rangeFrom(startLoc));
    }

    case TokenKind::l_bracket: {
        return parseArrayLiteral();
    }

    case TokenKind::kw_match: {
        return parseMatchExpr();
    }

    default:
        diag_.report(current_.getLocation(), DiagID::err_expected_expression);
        return nullptr;
    }
}

std::unique_ptr<Expr> Parser::parsePostfixExpr(std::unique_ptr<Expr> base) {
    while (true) {
        if (check(TokenKind::l_paren)) {
            base = parseCallExpr(std::move(base));
        } else if (check(TokenKind::dot)) {
            base = parseMemberExpr(std::move(base));
        } else if (check(TokenKind::l_bracket)) {
            base = parseIndexExpr(std::move(base));
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

std::unique_ptr<Expr> Parser::parseMemberExpr(std::unique_ptr<Expr> object) {
    auto startLoc = object->getStartLoc();
    expect(TokenKind::dot);

    auto member = expect(TokenKind::identifier);

    return std::make_unique<MemberExpr>(std::move(object), std::string(member.getText()),
                                        rangeFrom(startLoc));
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

        // Parse pattern (simplified: just consume tokens until =>)
        std::string pattern;
        while (!check(TokenKind::fat_arrow) && !check(TokenKind::r_brace) &&
               !check(TokenKind::eof)) {
            pattern += std::string(current_.getText());
            advance();
        }
        arm.pattern = pattern;

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

} // namespace liva
