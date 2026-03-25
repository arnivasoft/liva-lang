#include "liva/Parser/Parser.h"

namespace liva {

std::unique_ptr<TypeRepr> Parser::parseType() {
    // ref T / ref mut T
    if (check(TokenKind::kw_ref)) {
        advance();
        bool isMut = match(TokenKind::kw_mut);
        auto inner = parseType();
        return std::make_unique<ReferenceTypeRepr>(std::move(inner), isMut);
    }

    auto base = parseBaseType();
    if (!base)
        return nullptr;

    // Optional type: T?
    if (match(TokenKind::question)) {
        return std::make_unique<OptionalTypeRepr>(std::move(base));
    }

    return base;
}

std::unique_ptr<TypeRepr> Parser::parseBaseType() {
    switch (current_.getKind()) {
    // Primitive types
    case TokenKind::kw_void:
        advance();
        return makeVoidType();
    case TokenKind::kw_bool:
        advance();
        return makeBoolType();
    case TokenKind::kw_i8:
        advance();
        return makePrimitiveType(TypeRepr::Kind::I8);
    case TokenKind::kw_i16:
        advance();
        return makePrimitiveType(TypeRepr::Kind::I16);
    case TokenKind::kw_i32:
        advance();
        return makeI32Type();
    case TokenKind::kw_i64:
        advance();
        return makeI64Type();
    case TokenKind::kw_u8:
        advance();
        return makePrimitiveType(TypeRepr::Kind::U8);
    case TokenKind::kw_u16:
        advance();
        return makePrimitiveType(TypeRepr::Kind::U16);
    case TokenKind::kw_u32:
        advance();
        return makePrimitiveType(TypeRepr::Kind::U32);
    case TokenKind::kw_u64:
        advance();
        return makePrimitiveType(TypeRepr::Kind::U64);
    case TokenKind::kw_f32:
        advance();
        return makePrimitiveType(TypeRepr::Kind::F32);
    case TokenKind::kw_f64:
        advance();
        return makeF64Type();
    case TokenKind::kw_string:
        advance();
        return makeStringType();

    // Trait object type: dyn Protocol
    case TokenKind::kw_dyn: {
        advance();
        auto nameTok = expect(TokenKind::identifier);
        return std::make_unique<DynProtocolTypeRepr>(std::string(nameTok.getText()));
    }

    // Named type or generic type: Identifier<T, U>
    case TokenKind::identifier: {
        std::string name(current_.getText());
        advance();

        // Associated type reference: T.Item
        if (check(TokenKind::dot)) {
            auto nextTok = peek();
            if (nextTok.is(TokenKind::identifier)) {
                advance(); // consume '.'
                std::string assocName(current_.getText());
                advance(); // consume assoc name
                return std::make_unique<AssociatedTypeRepr>(
                    std::move(name), std::move(assocName));
            }
        }

        // Check for generic parameters: Type<T, U> or Type<T, 10>
        if (match(TokenKind::less)) {
            std::vector<std::unique_ptr<TypeRepr>> typeArgs;
            if (!check(TokenKind::greater)) {
                do {
                    // Const value arg: integer literal (e.g., Type<i32, 10>)
                    if (check(TokenKind::integer_literal)) {
                        int64_t val = current_.getIntegerValue();
                        advance();
                        typeArgs.push_back(std::make_unique<ConstValueTypeRepr>(val));
                    } else if (check(TokenKind::minus) &&
                               peek().is(TokenKind::integer_literal)) {
                        advance(); // consume -
                        int64_t val = -current_.getIntegerValue();
                        advance();
                        typeArgs.push_back(std::make_unique<ConstValueTypeRepr>(val));
                    } else {
                        typeArgs.push_back(parseType());
                    }
                } while (match(TokenKind::comma));
            }
            expect(TokenKind::greater);
            if (name == "Result" && typeArgs.size() == 2) {
                return std::make_unique<ResultTypeRepr>(
                    std::move(typeArgs[0]), std::move(typeArgs[1]));
            }
            return std::make_unique<GenericTypeRepr>(std::move(name), std::move(typeArgs));
        }

        return makeNamedType(name);
    }

    // Array type: [T] or [T; N]
    case TokenKind::l_bracket: {
        advance();
        auto elemType = parseType();

        int64_t size = -1;
        if (match(TokenKind::semicolon)) {
            if (check(TokenKind::integer_literal)) {
                size = current_.getIntegerValue();
                advance();
            }
        }

        expect(TokenKind::r_bracket);
        return std::make_unique<ArrayTypeRepr>(std::move(elemType), size);
    }

    // Function type: (T1, T2) -> T3  or  Tuple type: (T1, T2)
    case TokenKind::l_paren: {
        advance();
        std::vector<std::unique_ptr<TypeRepr>> types;
        if (!check(TokenKind::r_paren)) {
            do {
                types.push_back(parseType());
            } while (match(TokenKind::comma));
        }
        expect(TokenKind::r_paren);

        // Arrow → function type
        if (match(TokenKind::arrow)) {
            auto returnType = parseType();
            return std::make_unique<FunctionTypeRepr>(std::move(types), std::move(returnType));
        }

        // Single element → just grouping, return the inner type
        if (types.size() == 1) {
            return std::move(types[0]);
        }

        // Multiple elements → tuple type
        return std::make_unique<TupleTypeRepr>(std::move(types));
    }

    default:
        diag_.report(current_.getLocation(), DiagID::err_expected_type);
        return nullptr;
    }
}

ParamDecl Parser::parseParamDecl() {
    ParamDecl param;
    param.location = current_.getLocation();

    // Check for 'ref self', 'ref mut self', or 'self'
    if (check(TokenKind::kw_ref)) {
        param.isRef = true;
        advance();
        if (match(TokenKind::kw_mut)) {
            param.isMutRef = true;
        }
        if (check(TokenKind::kw_self)) {
            param.isSelf = true;
            param.name = "self";
            advance();
            return param;
        }
    } else if (check(TokenKind::kw_mut)) {
        advance(); // consume 'mut'
        if (check(TokenKind::kw_self)) {
            param.isSelf = true;
            param.isMutRef = true;
            param.name = "self";
            advance();
            return param;
        }
        // 'mut' not followed by 'self' — fall through to regular param
    } else if (check(TokenKind::kw_self)) {
        param.isSelf = true;
        param.name = "self";
        advance();
        return param;
    }

    // Regular parameter: name: Type
    auto nameTok = expect(TokenKind::identifier);
    param.name = std::string(nameTok.getText());

    expect(TokenKind::colon);

    // Check for ref/ref mut in type position
    if (check(TokenKind::kw_ref)) {
        param.isRef = true;
        advance();
        if (match(TokenKind::kw_mut)) {
            param.isMutRef = true;
        }
    }

    param.type = parseType();

    // Check for variadic: name: Type...
    if (match(TokenKind::ellipsis)) {
        param.isVariadic = true;
    }

    // Optional default value: name: Type = expr
    if (match(TokenKind::equal)) {
        param.defaultValue = parseExpression();
    }

    return param;
}

} // namespace liva
