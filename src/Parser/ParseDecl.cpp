#include "liva/Parser/Parser.h"
#include <unordered_map>

namespace liva {

std::unique_ptr<ASTNode> Parser::parseTopLevelDecl() {
    bool isPublic = false;
    if (match(TokenKind::kw_pub)) {
        isPublic = true;
    }

    switch (current_.getKind()) {
    case TokenKind::kw_async:
        advance(); // consume 'async'
        return parseFuncDecl(isPublic, /*isAsync=*/true);
    case TokenKind::kw_func:
        return parseFuncDecl(isPublic);
    case TokenKind::kw_const:
        advance(); // consume 'const'
        return parseConstDecl();
    case TokenKind::kw_let:
    case TokenKind::kw_var:
        return parseVarDecl();
    case TokenKind::kw_struct:
        return parseStructDecl(isPublic);
    case TokenKind::kw_enum:
        return parseEnumDecl(isPublic);
    case TokenKind::kw_impl:
        return parseImplDecl();
    case TokenKind::kw_protocol:
        return parseProtocolDecl(isPublic);
    case TokenKind::kw_import:
        return parseImportDecl();
    case TokenKind::kw_type:
        return parseTypeAliasDecl(isPublic);
    case TokenKind::kw_macro:
        return parseMacroDecl(isPublic);
    case TokenKind::kw_class:
        return parseClassDecl(isPublic);
    case TokenKind::kw_test:
        if (isPublic) {
            diag_.report(current_.getLocation(), DiagID::err_expected_declaration);
        }
        return parseTestDecl();
    case TokenKind::kw_extern: {
        auto funcs = parseExternBlock(isPublic);
        if (funcs.empty()) return nullptr;
        for (size_t i = 1; i < funcs.size(); ++i) {
            pendingDecls_.push_back(std::move(funcs[i]));
        }
        return std::move(funcs[0]);
    }
    default: {
        // Detect foreign language keywords used as identifiers
        if (current_.getKind() == TokenKind::identifier) {
            auto text = current_.getText();
            if (text == "fn" || text == "def" || text == "function") {
                diag_.report(current_.getLocation(), DiagID::err_foreign_keyword,
                             std::string(text), "func");
                diag_.report(current_.getLocation(), DiagID::note_use_func_keyword);
                advance(); // skip the foreign keyword
                return parseFuncDecl(isPublic);
            }
            // "class" is now a keyword — no need for foreign keyword detection
            if (text == "interface" || text == "trait") {
                diag_.report(current_.getLocation(), DiagID::err_foreign_keyword,
                             std::string(text), "protocol");
                advance();
                return parseProtocolDecl(isPublic);
            }
        }
        diag_.report(current_.getLocation(), DiagID::err_expected_declaration);
        return nullptr;
    }
    }
}

std::unique_ptr<FuncDecl> Parser::parseFuncDecl(bool isPublic, bool isAsync) {
    auto startLoc = current_.getLocation();
    expect(TokenKind::kw_func);

    auto nameTok = expect(TokenKind::identifier);
    std::string name(nameTok.getText());

    // Parse optional generic type parameters: <T, U> or <T: Protocol> or <T: A + B>
    std::vector<std::string> typeParams;
    std::unordered_map<std::string, std::vector<std::string>> typeParamBounds;
    if (match(TokenKind::less)) {
        if (!check(TokenKind::greater)) {
            do {
                auto paramTok = expect(TokenKind::identifier);
                std::string paramName(paramTok.getText());
                typeParams.push_back(paramName);
                if (match(TokenKind::colon)) {
                    do {
                        auto boundTok = expect(TokenKind::identifier);
                        typeParamBounds[paramName].push_back(std::string(boundTok.getText()));
                    } while (match(TokenKind::plus));
                }
            } while (match(TokenKind::comma));
        }
        expect(TokenKind::greater);
    }

    // Parse parameter list
    expect(TokenKind::l_paren);
    std::vector<ParamDecl> params;
    bool hasCVarargs = false;
    if (!check(TokenKind::r_paren)) {
        do {
            // Standalone ... → C varargs
            if (check(TokenKind::ellipsis)) {
                hasCVarargs = true;
                advance();
                break;
            }
            params.push_back(parseParamDecl());
        } while (match(TokenKind::comma));
    }
    expect(TokenKind::r_paren);

    // Parse return type
    std::unique_ptr<TypeRepr> returnType;
    if (match(TokenKind::arrow)) {
        returnType = parseType();
    } else {
        returnType = makeVoidType();
    }

    // Parse optional where clause
    std::vector<WhereConstraint> whereConstraints;
    if (match(TokenKind::kw_where)) {
        do {
            auto paramTok = expect(TokenKind::identifier);
            std::string paramName(paramTok.getText());

            if (match(TokenKind::dot)) {
                // T.Item == Type  or  T.Item: Protocol
                auto assocTok = expect(TokenKind::identifier);
                std::string assocName(assocTok.getText());

                if (match(TokenKind::equal_equal)) {
                    // T.Item == ConcreteType
                    auto targetType = parseType();
                    WhereConstraint c;
                    c.kind = WhereConstraint::Kind::AssociatedTypeEqual;
                    c.paramName = paramName;
                    c.assocTypeName = assocName;
                    c.equalTypeName = targetType ? targetType->toString() : "";
                    whereConstraints.push_back(std::move(c));
                } else {
                    // T.Item: Protocol [+ Protocol2]
                    expect(TokenKind::colon);
                    WhereConstraint c;
                    c.kind = WhereConstraint::Kind::AssociatedTypeBound;
                    c.paramName = paramName;
                    c.assocTypeName = assocName;
                    do {
                        auto boundTok = expect(TokenKind::identifier);
                        c.protocolNames.push_back(std::string(boundTok.getText()));
                    } while (match(TokenKind::plus));
                    whereConstraints.push_back(std::move(c));
                }
            } else {
                // Existing: T: Protocol [+ Protocol2]
                expect(TokenKind::colon);
                do {
                    auto boundTok = expect(TokenKind::identifier);
                    typeParamBounds[paramName].push_back(std::string(boundTok.getText()));
                } while (match(TokenKind::plus));
            }
        } while (match(TokenKind::comma));
    }

    // Parse body (optional for protocol declarations)
    std::unique_ptr<BlockStmt> body;
    if (check(TokenKind::l_brace)) {
        body = parseBlock();
    }

    auto funcDecl = std::make_unique<FuncDecl>(std::move(name), std::move(params),
                                                std::move(returnType), std::move(body),
                                                isPublic, rangeFrom(startLoc), isAsync);
    if (hasCVarargs) {
        funcDecl->setCVarargs(true);
    }
    if (!typeParams.empty()) {
        funcDecl->setTypeParams(std::move(typeParams));
    }
    if (!typeParamBounds.empty()) {
        funcDecl->setTypeParamBounds(std::move(typeParamBounds));
    }
    if (!whereConstraints.empty()) {
        funcDecl->setWhereConstraints(std::move(whereConstraints));
    }
    return funcDecl;
}

std::unique_ptr<VarDecl> Parser::parseVarDecl() {
    auto startLoc = current_.getLocation();
    bool isMutable = current_.is(TokenKind::kw_var);
    advance(); // consume let/var

    // Detect Rust-style "let mut" and suggest "var"
    if (!isMutable && current_.is(TokenKind::kw_mut)) {
        diag_.report(startLoc, DiagID::err_let_mut_not_supported);
        advance(); // consume 'mut', continue parsing as mutable variable
        isMutable = true;
    }

    // Tuple destructuring: let (x, y) = expr
    if (check(TokenKind::l_paren)) {
        advance(); // consume (
        std::vector<std::string> names;
        do {
            auto name = expect(TokenKind::identifier);
            names.push_back(std::string(name.getText()));
        } while (match(TokenKind::comma));
        expect(TokenKind::r_paren);
        expect(TokenKind::equal);
        auto init = parseExpression();
        return std::make_unique<VarDecl>(std::move(names), std::move(init),
                                          isMutable, rangeFrom(startLoc));
    }

    auto nameTok = expect(TokenKind::identifier);
    std::string name(nameTok.getText());

    // Optional type annotation
    std::unique_ptr<TypeRepr> type;
    if (match(TokenKind::colon)) {
        type = parseType();
    } else {
        type = makeInferredType();
    }

    // Optional initializer
    std::unique_ptr<Expr> init;
    if (match(TokenKind::equal)) {
        init = parseExpression();
    }

    return std::make_unique<VarDecl>(std::move(name), std::move(type), std::move(init),
                                     isMutable, rangeFrom(startLoc));
}

std::unique_ptr<StructDecl> Parser::parseStructDecl(bool isPublic) {
    auto startLoc = current_.getLocation();
    expect(TokenKind::kw_struct);

    auto nameTok = expect(TokenKind::identifier);
    std::string name(nameTok.getText());

    // Parse optional generic type parameters: <T, U> or <T: Protocol> or <T: A + B>
    std::vector<std::string> typeParams;
    std::unordered_map<std::string, std::vector<std::string>> typeParamBounds;
    if (match(TokenKind::less)) {
        if (!check(TokenKind::greater)) {
            do {
                auto paramTok = expect(TokenKind::identifier);
                std::string paramName(paramTok.getText());
                typeParams.push_back(paramName);
                if (match(TokenKind::colon)) {
                    do {
                        auto boundTok = expect(TokenKind::identifier);
                        typeParamBounds[paramName].push_back(std::string(boundTok.getText()));
                    } while (match(TokenKind::plus));
                }
            } while (match(TokenKind::comma));
        }
        expect(TokenKind::greater);
    }

    // Parse optional where clause
    if (match(TokenKind::kw_where)) {
        do {
            auto paramTok = expect(TokenKind::identifier);
            std::string paramName(paramTok.getText());

            if (match(TokenKind::dot)) {
                auto assocTok = expect(TokenKind::identifier);
                std::string assocName(assocTok.getText());
                if (match(TokenKind::equal_equal)) {
                    parseType(); // consume the target type (struct doesn't use constraints yet)
                } else {
                    expect(TokenKind::colon);
                    do {
                        expect(TokenKind::identifier);
                    } while (match(TokenKind::plus));
                }
            } else {
                expect(TokenKind::colon);
                do {
                    auto boundTok = expect(TokenKind::identifier);
                    typeParamBounds[paramName].push_back(std::string(boundTok.getText()));
                } while (match(TokenKind::plus));
            }
        } while (match(TokenKind::comma));
    }

    expect(TokenKind::l_brace);

    std::vector<std::unique_ptr<FieldDecl>> fields;
    while (!check(TokenKind::r_brace) && !check(TokenKind::eof)) {
        if (diag_.hasMaxErrors()) break;

        // Skip stray semicolons between fields
        while (match(TokenKind::semicolon)) {}
        if (check(TokenKind::r_brace) || check(TokenKind::eof)) break;

        auto fieldStart = current_.getLocation();
        bool fieldMutable = false;

        if (check(TokenKind::kw_var)) {
            fieldMutable = true;
            advance();
        } else if (check(TokenKind::kw_let)) {
            advance();
        }

        auto fieldName = expect(TokenKind::identifier);
        if (fieldName.is(TokenKind::eof)) {
            synchronizeBody();
            continue;
        }
        if (!match(TokenKind::colon)) {
            diag_.report(current_.getLocation(), DiagID::err_expected_token,
                         ":", std::string(current_.getText()));
            synchronizeBody();
            continue;
        }
        auto fieldType = parseType();
        if (!fieldType) {
            synchronizeBody();
            continue;
        }

        fields.push_back(std::make_unique<FieldDecl>(
            std::string(fieldName.getText()), std::move(fieldType), fieldMutable,
            rangeFrom(fieldStart)));
    }

    expect(TokenKind::r_brace);

    auto structDecl = std::make_unique<StructDecl>(std::move(name), std::move(fields), isPublic,
                                                    rangeFrom(startLoc));
    if (!typeParams.empty()) {
        structDecl->setTypeParams(std::move(typeParams));
    }
    if (!typeParamBounds.empty()) {
        structDecl->setTypeParamBounds(std::move(typeParamBounds));
    }
    return structDecl;
}

std::unique_ptr<EnumDecl> Parser::parseEnumDecl(bool isPublic) {
    auto startLoc = current_.getLocation();
    expect(TokenKind::kw_enum);

    auto nameTok = expect(TokenKind::identifier);
    std::string name(nameTok.getText());

    expect(TokenKind::l_brace);

    std::vector<std::unique_ptr<EnumCaseDecl>> cases;
    while (!check(TokenKind::r_brace) && !check(TokenKind::eof)) {
        if (diag_.hasMaxErrors()) break;
        while (match(TokenKind::semicolon)) {}
        if (check(TokenKind::r_brace) || check(TokenKind::eof)) break;

        if (!match(TokenKind::kw_case)) {
            diag_.report(current_.getLocation(), DiagID::err_expected_token,
                         "case", std::string(current_.getText()));
            synchronizeBody();
            continue;
        }
        auto caseStart = current_.getLocation();
        auto caseName = expect(TokenKind::identifier);
        if (caseName.is(TokenKind::eof)) { synchronizeBody(); continue; }

        std::vector<std::unique_ptr<TypeRepr>> associatedTypes;
        if (match(TokenKind::l_paren)) {
            if (!check(TokenKind::r_paren)) {
                do {
                    associatedTypes.push_back(parseType());
                } while (match(TokenKind::comma));
            }
            expect(TokenKind::r_paren);
        }

        cases.push_back(std::make_unique<EnumCaseDecl>(
            std::string(caseName.getText()), std::move(associatedTypes),
            rangeFrom(caseStart)));
    }

    expect(TokenKind::r_brace);

    return std::make_unique<EnumDecl>(std::move(name), std::move(cases), isPublic,
                                      rangeFrom(startLoc));
}

std::unique_ptr<ImplDecl> Parser::parseImplDecl() {
    auto startLoc = current_.getLocation();
    expect(TokenKind::kw_impl);

    auto typeName = expect(TokenKind::identifier);

    // Parse optional generic type parameters: <T, U> or <T: Protocol> or <T: A + B>
    std::vector<std::string> typeParams;
    std::unordered_map<std::string, std::vector<std::string>> typeParamBounds;
    if (match(TokenKind::less)) {
        if (!check(TokenKind::greater)) {
            do {
                auto paramTok = expect(TokenKind::identifier);
                std::string paramName(paramTok.getText());
                typeParams.push_back(paramName);
                if (match(TokenKind::colon)) {
                    do {
                        auto boundTok = expect(TokenKind::identifier);
                        typeParamBounds[paramName].push_back(std::string(boundTok.getText()));
                    } while (match(TokenKind::plus));
                }
            } while (match(TokenKind::comma));
        }
        expect(TokenKind::greater);
    }

    // Optional protocol conformance
    std::string protocolName;
    if (match(TokenKind::colon)) {
        auto protoTok = expect(TokenKind::identifier);
        protocolName = std::string(protoTok.getText());
    }

    // Parse optional where clause
    std::vector<WhereConstraint> implWhereConstraints;
    if (match(TokenKind::kw_where)) {
        do {
            auto paramTok = expect(TokenKind::identifier);
            std::string paramName(paramTok.getText());

            if (match(TokenKind::dot)) {
                auto assocTok = expect(TokenKind::identifier);
                std::string assocName(assocTok.getText());

                if (match(TokenKind::equal_equal)) {
                    auto targetType = parseType();
                    WhereConstraint c;
                    c.kind = WhereConstraint::Kind::AssociatedTypeEqual;
                    c.paramName = paramName;
                    c.assocTypeName = assocName;
                    c.equalTypeName = targetType ? targetType->toString() : "";
                    implWhereConstraints.push_back(std::move(c));
                } else {
                    expect(TokenKind::colon);
                    WhereConstraint c;
                    c.kind = WhereConstraint::Kind::AssociatedTypeBound;
                    c.paramName = paramName;
                    c.assocTypeName = assocName;
                    do {
                        auto boundTok = expect(TokenKind::identifier);
                        c.protocolNames.push_back(std::string(boundTok.getText()));
                    } while (match(TokenKind::plus));
                    implWhereConstraints.push_back(std::move(c));
                }
            } else {
                expect(TokenKind::colon);
                do {
                    auto boundTok = expect(TokenKind::identifier);
                    typeParamBounds[paramName].push_back(std::string(boundTok.getText()));
                } while (match(TokenKind::plus));
            }
        } while (match(TokenKind::comma));
    }

    expect(TokenKind::l_brace);

    std::vector<std::unique_ptr<FuncDecl>> methods;
    std::unordered_map<std::string, std::string> associatedTypes;
    while (!check(TokenKind::r_brace) && !check(TokenKind::eof)) {
        if (diag_.hasMaxErrors()) break;
        while (match(TokenKind::semicolon)) {}
        if (check(TokenKind::r_brace) || check(TokenKind::eof)) break;

        if (check(TokenKind::kw_type)) {
            advance();  // consume 'type'
            auto assocName = expect(TokenKind::identifier);
            if (assocName.is(TokenKind::eof)) { synchronizeBody(); continue; }
            expect(TokenKind::equal);
            auto targetType = parseType();
            associatedTypes[std::string(assocName.getText())] = targetType->toString();
        } else {
            bool isPublic = false;
            if (match(TokenKind::kw_pub)) {
                isPublic = true;
            }
            bool isAsync = false;
            if (match(TokenKind::kw_async)) {
                isAsync = true;
            }
            if (!check(TokenKind::kw_func)) {
                diag_.report(current_.getLocation(), DiagID::err_expected_token,
                             "func", std::string(current_.getText()));
                synchronizeBody();
                continue;
            }
            methods.push_back(parseFuncDecl(isPublic, isAsync));
        }
    }

    expect(TokenKind::r_brace);

    auto implDecl = std::make_unique<ImplDecl>(std::string(typeName.getText()), std::move(protocolName),
                                      std::move(methods), rangeFrom(startLoc));
    if (!typeParams.empty())
        implDecl->setTypeParams(std::move(typeParams));
    if (!typeParamBounds.empty())
        implDecl->setTypeParamBounds(std::move(typeParamBounds));
    if (!associatedTypes.empty())
        implDecl->setAssociatedTypes(std::move(associatedTypes));
    if (!implWhereConstraints.empty())
        implDecl->setWhereConstraints(std::move(implWhereConstraints));
    return implDecl;
}

std::unique_ptr<ProtocolDecl> Parser::parseProtocolDecl(bool isPublic) {
    auto startLoc = current_.getLocation();
    expect(TokenKind::kw_protocol);

    auto nameTok = expect(TokenKind::identifier);
    std::string name(nameTok.getText());

    expect(TokenKind::l_brace);

    std::vector<std::unique_ptr<FuncDecl>> methods;
    std::vector<std::string> associatedTypes;
    while (!check(TokenKind::r_brace) && !check(TokenKind::eof)) {
        if (diag_.hasMaxErrors()) break;
        while (match(TokenKind::semicolon)) {}
        if (check(TokenKind::r_brace) || check(TokenKind::eof)) break;

        if (check(TokenKind::kw_type)) {
            advance();  // consume 'type'
            auto typeName = expect(TokenKind::identifier);
            if (typeName.is(TokenKind::eof)) { synchronizeBody(); continue; }
            associatedTypes.push_back(std::string(typeName.getText()));
        } else if (check(TokenKind::kw_func) || check(TokenKind::kw_async)) {
            methods.push_back(parseFuncDecl(false));
        } else {
            diag_.report(current_.getLocation(), DiagID::err_expected_token,
                         "func", std::string(current_.getText()));
            synchronizeBody();
            continue;
        }
    }

    expect(TokenKind::r_brace);

    return std::make_unique<ProtocolDecl>(std::move(name), std::move(methods),
                                          std::move(associatedTypes), isPublic,
                                          rangeFrom(startLoc));
}

std::unique_ptr<ImportDecl> Parser::parseImportDecl() {
    auto startLoc = current_.getLocation();
    expect(TokenKind::kw_import);

    // Accept identifiers and 'test' keyword in import paths (std::test)
    auto consumePathSegment = [&]() -> Token {
        if (check(TokenKind::kw_test)) {
            return advance();
        }
        return expect(TokenKind::identifier);
    };

    std::vector<std::string> path;
    auto first = consumePathSegment();
    path.push_back(std::string(first.getText()));

    while (match(TokenKind::coloncolon)) {
        auto next = consumePathSegment();
        path.push_back(std::string(next.getText()));
    }

    return std::make_unique<ImportDecl>(std::move(path), rangeFrom(startLoc));
}

std::unique_ptr<TypeAliasDecl> Parser::parseTypeAliasDecl(bool isPublic) {
    auto startLoc = current_.getLocation();
    expect(TokenKind::kw_type);

    auto nameTok = expect(TokenKind::identifier);
    std::string name(nameTok.getText());

    expect(TokenKind::equal);

    auto targetType = parseType();

    return std::make_unique<TypeAliasDecl>(std::move(name), std::move(targetType),
                                            isPublic, rangeFrom(startLoc));
}

std::unique_ptr<VarDecl> Parser::parseConstDecl() {
    auto startLoc = current_.getLocation();
    // 'const' already consumed by caller

    auto nameTok = expect(TokenKind::identifier);
    std::string name(nameTok.getText());

    // Optional type annotation
    std::unique_ptr<TypeRepr> type;
    if (match(TokenKind::colon)) {
        type = parseType();
    } else {
        type = makeInferredType();
    }

    // Initializer (required semantically, optional syntactically for better error recovery)
    std::unique_ptr<Expr> init;
    if (match(TokenKind::equal)) {
        init = parseExpression();
    }

    return std::make_unique<VarDecl>(std::move(name), std::move(type),
                                      std::move(init), /*isMutable=*/false,
                                      rangeFrom(startLoc), /*isConst=*/true);
}

std::unique_ptr<MacroDecl> Parser::parseMacroDecl(bool isPublic) {
    auto startLoc = current_.getLocation();
    expect(TokenKind::kw_macro);

    auto nameTok = expect(TokenKind::identifier);
    std::string name(nameTok.getText());

    expect(TokenKind::l_brace);

    // Collect raw source between { and matching }
    std::string rawSource;
    int braceDepth = 1;
    while (!check(TokenKind::eof) && braceDepth > 0) {
        if (check(TokenKind::l_brace))
            ++braceDepth;
        else if (check(TokenKind::r_brace)) {
            --braceDepth;
            if (braceDepth == 0)
                break;
        }
        // Reconstruct source text from tokens
        auto tok = current_;
        auto k = tok.getKind();
        if (k == TokenKind::string_literal) {
            rawSource += "\"";
            rawSource += std::string(tok.getStringValue());
            rawSource += "\"";
        } else {
            rawSource += std::string(tok.getText());
        }
        rawSource += " ";
        advance();
    }

    expect(TokenKind::r_brace);

    auto decl = std::make_unique<MacroDecl>(std::move(name), isPublic, rangeFrom(startLoc));
    decl->setRawSource(std::move(rawSource));
    return decl;
}

std::unique_ptr<ClassDecl> Parser::parseClassDecl(bool isPublic) {
    auto startLoc = current_.getLocation();
    expect(TokenKind::kw_class);

    auto nameTok = expect(TokenKind::identifier);
    std::string name(nameTok.getText());

    // Parse optional generic type parameters: <T, U> or <T: Protocol>
    std::vector<std::string> typeParams;
    std::unordered_map<std::string, std::vector<std::string>> typeParamBounds;
    if (match(TokenKind::less)) {
        if (!check(TokenKind::greater)) {
            do {
                auto paramTok = expect(TokenKind::identifier);
                std::string paramName(paramTok.getText());
                typeParams.push_back(paramName);
                if (match(TokenKind::colon)) {
                    do {
                        auto boundTok = expect(TokenKind::identifier);
                        typeParamBounds[paramName].push_back(std::string(boundTok.getText()));
                    } while (match(TokenKind::plus));
                }
            } while (match(TokenKind::comma));
        }
        expect(TokenKind::greater);
    }

    // Parse optional inheritance/protocol list: : Parent, Protocol1, Protocol2
    std::string parentClass;
    std::vector<std::string> protocols;
    if (match(TokenKind::colon)) {
        // First name could be parent class or protocol — resolved in TypeChecker
        auto firstTok = expect(TokenKind::identifier);
        std::string firstName(firstTok.getText());
        // Treat the first as parent class; if it's actually a protocol, TypeChecker will handle it
        parentClass = firstName;
        while (match(TokenKind::comma)) {
            auto protoTok = expect(TokenKind::identifier);
            protocols.push_back(std::string(protoTok.getText()));
        }
    }

    expect(TokenKind::l_brace);

    // Parse class members: fields, methods, init, deinit
    std::vector<ClassMember> members;
    while (!check(TokenKind::r_brace) && !check(TokenKind::eof)) {
        if (diag_.hasMaxErrors()) break;

        // Skip stray semicolons
        while (match(TokenKind::semicolon)) {}
        if (check(TokenKind::r_brace) || check(TokenKind::eof)) break;

        ClassMember member;

        // Parse access modifier: private
        if (match(TokenKind::kw_private)) {
            member.access = AccessModifier::Private;
        } else if (match(TokenKind::kw_pub)) {
            member.access = AccessModifier::Public;
        }

        // Parse override keyword
        if (match(TokenKind::kw_override)) {
            member.isOverride = true;
        }

        // Field: var name: Type or let name: Type
        if (check(TokenKind::kw_var) || check(TokenKind::kw_let)) {
            bool fieldMutable = current_.is(TokenKind::kw_var);
            advance();

            auto fieldName = expect(TokenKind::identifier);
            if (fieldName.is(TokenKind::eof)) { synchronizeBody(); continue; }
            if (!match(TokenKind::colon)) {
                diag_.report(current_.getLocation(), DiagID::err_expected_token,
                             ":", std::string(current_.getText()));
                synchronizeBody();
                continue;
            }
            auto fieldType = parseType();
            if (!fieldType) { synchronizeBody(); continue; }

            member.field = std::make_unique<FieldDecl>(
                std::string(fieldName.getText()), std::move(fieldType),
                fieldMutable, rangeFrom(fieldName.getLocation()));
            members.push_back(std::move(member));
        }
        // init(params...) { body }  — self is implicit
        else if (check(TokenKind::kw_init)) {
            auto initStart = current_.getLocation();
            advance(); // consume 'init'

            expect(TokenKind::l_paren);
            std::vector<ParamDecl> params;
            if (!check(TokenKind::r_paren)) {
                do {
                    params.push_back(parseParamDecl());
                } while (match(TokenKind::comma));
            }
            expect(TokenKind::r_paren);

            std::unique_ptr<BlockStmt> body;
            if (check(TokenKind::l_brace)) {
                body = parseBlock();
            }

            member.method = std::make_unique<FuncDecl>(
                "init", std::move(params), makeVoidType(), std::move(body),
                member.access == AccessModifier::Public, rangeFrom(initStart));
            members.push_back(std::move(member));
        }
        // deinit { body }  — self is implicit, parens optional
        else if (check(TokenKind::kw_deinit)) {
            auto deinitStart = current_.getLocation();
            advance(); // consume 'deinit'

            // Optional parens: deinit, deinit(), or deinit(params...) — params rejected by sema
            std::vector<ParamDecl> params;
            if (match(TokenKind::l_paren)) {
                if (!check(TokenKind::r_paren)) {
                    do {
                        params.push_back(parseParamDecl());
                    } while (match(TokenKind::comma));
                }
                expect(TokenKind::r_paren);
            }

            std::unique_ptr<BlockStmt> body;
            if (check(TokenKind::l_brace)) {
                body = parseBlock();
            }

            member.method = std::make_unique<FuncDecl>(
                "deinit", std::move(params), makeVoidType(), std::move(body),
                false, rangeFrom(deinitStart));
            members.push_back(std::move(member));
        }
        // func method(params...) — self is implicit
        else if (check(TokenKind::kw_func) || check(TokenKind::kw_async)) {
            bool isAsync = false;
            if (match(TokenKind::kw_async)) {
                isAsync = true;
            }
            inClassBody_ = true;
            member.method = parseFuncDecl(member.access == AccessModifier::Public, isAsync);
            inClassBody_ = false;
            members.push_back(std::move(member));
        }
        else {
            diag_.report(current_.getLocation(), DiagID::err_expected_token,
                         "var/let/func/init/deinit", std::string(current_.getText()));
            synchronizeBody();
            continue;
        }
    }

    expect(TokenKind::r_brace);

    auto classDecl = std::make_unique<ClassDecl>(
        std::move(name), std::move(parentClass), std::move(protocols),
        std::move(members), isPublic, rangeFrom(startLoc));
    if (!typeParams.empty()) {
        classDecl->setTypeParams(std::move(typeParams));
    }
    if (!typeParamBounds.empty()) {
        classDecl->setTypeParamBounds(std::move(typeParamBounds));
    }
    return classDecl;
}

std::vector<std::unique_ptr<FuncDecl>> Parser::parseExternBlock(bool isPublic) {
    advance(); // consume 'extern'

    // Expect "C" string literal
    auto strTok = expect(TokenKind::string_literal);
    if (strTok.getStringValue() != "C") {
        diag_.report(strTok.getLocation(), DiagID::err_extern_unsupported_abi,
                     strTok.getStringValue());
        return {};
    }

    std::vector<std::unique_ptr<FuncDecl>> result;

    if (match(TokenKind::l_brace)) {
        // Block form: extern "C" { func ...; func ...; }
        while (!check(TokenKind::r_brace) && !check(TokenKind::eof)) {
            if (diag_.hasMaxErrors()) break;
            while (match(TokenKind::semicolon)) {}
            if (check(TokenKind::r_brace) || check(TokenKind::eof)) break;

            bool isAsync = false;
            if (match(TokenKind::kw_async)) {
                isAsync = true;
            }
            if (!check(TokenKind::kw_func)) {
                diag_.report(current_.getLocation(), DiagID::err_extern_only_func);
                synchronizeBody();
                continue;
            }
            auto func = parseFuncDecl(isPublic, isAsync);
            if (func) {
                func->setExtern(true);
                result.push_back(std::move(func));
            }
        }
        expect(TokenKind::r_brace);
    } else if (check(TokenKind::kw_func) || check(TokenKind::kw_async)) {
        // Single form: extern "C" func ...
        bool isAsync = false;
        if (match(TokenKind::kw_async)) {
            isAsync = true;
        }
        auto func = parseFuncDecl(isPublic, isAsync);
        if (func) {
            func->setExtern(true);
            result.push_back(std::move(func));
        }
    } else {
        diag_.report(current_.getLocation(), DiagID::err_extern_only_func);
    }

    return result;
}

std::unique_ptr<TestDecl> Parser::parseTestDecl() {
    auto startLoc = current_.getLocation();
    expect(TokenKind::kw_test);

    auto nameTok = expect(TokenKind::string_literal);
    std::string name(nameTok.getStringValue());

    auto body = parseBlock();
    if (!body) return nullptr;

    return std::make_unique<TestDecl>(std::move(name), std::move(body), rangeFrom(startLoc));
}

} // namespace liva
