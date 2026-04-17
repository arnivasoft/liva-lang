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
        return parseClassDecl(isPublic, false);
    case TokenKind::kw_final:
        if (peek().is(TokenKind::kw_class)) {
            advance(); // consume 'final'
            return parseClassDecl(isPublic, true);
        }
        diag_.report(current_.getLocation(), DiagID::err_expected_declaration);
        return nullptr;
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
            // Contextual class-level modifiers: 'open class', 'public class', etc.
            if ((text == "open" || text == "public" || text == "internal" ||
                 text == "fileprivate") && peek().is(TokenKind::kw_class)) {
                AccessModifier acc = AccessModifier::Public;
                if (text == "open") acc = AccessModifier::Open;
                else if (text == "public") acc = AccessModifier::Public;
                else if (text == "internal") acc = AccessModifier::Internal;
                else if (text == "fileprivate") acc = AccessModifier::FilePrivate;
                advance(); // consume modifier
                auto cls = parseClassDecl(acc == AccessModifier::Open || acc == AccessModifier::Public, false);
                if (cls) cls->setAccess(acc);
                return cls;
            }
            // Contextual keyword: 'extension TypeName { ... }'
            if (text == "extension" && peek().is(TokenKind::identifier)) {
                return parseImplDecl();
            }
            if (text == "fn" || text == "def" || text == "function") {
                diag_.reportRange(current_.getLocation(),
                                  static_cast<uint32_t>(text.size()),
                                  DiagID::err_foreign_keyword,
                                  std::string(text), "func");
                diag_.reportHelp(current_.getLocation(),
                                 static_cast<uint32_t>(text.size()),
                                 "Liva uses 'func' to declare functions", "func",
                                 DiagID::note_use_func_keyword);
                advance(); // skip the foreign keyword
                return parseFuncDecl(isPublic);
            }
            // "class" is now a keyword — no need for foreign keyword detection
            if (text == "interface" || text == "trait") {
                diag_.reportRange(current_.getLocation(),
                                  static_cast<uint32_t>(text.size()),
                                  DiagID::err_foreign_keyword,
                                  std::string(text), "protocol");
                diag_.reportHelp(current_.getLocation(),
                                 static_cast<uint32_t>(text.size()),
                                 "Liva uses 'protocol' to declare protocols", "protocol",
                                 DiagID::note_use_func_keyword);
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

    Token nameTok;
    if (check(TokenKind::kw_test)) {
        nameTok = current_;
        advance();
    } else {
        nameTok = expect(TokenKind::identifier);
    }
    std::string name(nameTok.getText());

    // Parse optional generic parameters: <'a, T, U: Protocol, const N: i32>
    std::vector<std::string> lifetimeParams;
    std::vector<std::string> typeParams;
    std::unordered_map<std::string, std::vector<std::string>> typeParamBounds;
    std::vector<FuncDecl::ConstGenericParam> constParams;
    if (match(TokenKind::less)) {
        if (!check(TokenKind::greater)) {
            do {
                // Lifetime parameter: 'a, 'b, 'static
                if (check(TokenKind::lifetime_literal)) {
                    lifetimeParams.push_back(std::string(current_.getText()));
                    advance();
                }
                // Check for const generic parameter: const N: i32
                else if (match(TokenKind::kw_const)) {
                    FuncDecl::ConstGenericParam cp;
                    auto cpName = expect(TokenKind::identifier);
                    cp.name = std::string(cpName.getText());
                    expect(TokenKind::colon);
                    cp.type = parseType();
                    if (match(TokenKind::equal)) {
                        if (check(TokenKind::integer_literal)) {
                            cp.defaultValue = current_.getIntegerValue();
                            cp.hasDefault = true;
                            advance();
                        }
                    }
                    constParams.push_back(std::move(cp));
                } else {
                    auto paramTok = expect(TokenKind::identifier);
                    std::string paramName(paramTok.getText());
                    typeParams.push_back(paramName);
                    if (match(TokenKind::colon)) {
                        do {
                            auto boundTok = expect(TokenKind::identifier);
                            typeParamBounds[paramName].push_back(std::string(boundTok.getText()));
                        } while (match(TokenKind::plus));
                    }
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
    if (!lifetimeParams.empty()) {
        funcDecl->setLifetimeParams(std::move(lifetimeParams));
    }
    if (!typeParams.empty()) {
        funcDecl->setTypeParams(std::move(typeParams));
    }
    if (!typeParamBounds.empty()) {
        funcDecl->setTypeParamBounds(std::move(typeParamBounds));
    }
    if (!constParams.empty()) {
        funcDecl->setConstParams(std::move(constParams));
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
        diag_.reportRange(startLoc, 7, DiagID::err_let_mut_not_supported); // "let mut" = 7 chars
        diag_.reportHelp(startLoc, 7, "use 'var' for mutable variables", "var");
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

        auto caseDecl = std::make_unique<EnumCaseDecl>(
            std::string(caseName.getText()), std::move(associatedTypes),
            rangeFrom(caseStart));

        // Optional discriminant value: case OK = 200
        if (match(TokenKind::equal)) {
            if (check(TokenKind::integer_literal)) {
                caseDecl->setDiscriminant(current_.getIntegerValue());
                advance();
            } else if (check(TokenKind::minus) && peek().is(TokenKind::integer_literal)) {
                advance(); // consume -
                caseDecl->setDiscriminant(-current_.getIntegerValue());
                advance();
            } else {
                diag_.report(current_.getLocation(), DiagID::err_expected_expression);
            }
        }

        cases.push_back(std::move(caseDecl));
    }

    expect(TokenKind::r_brace);

    return std::make_unique<EnumDecl>(std::move(name), std::move(cases), isPublic,
                                      rangeFrom(startLoc));
}

std::unique_ptr<ImplDecl> Parser::parseImplDecl() {
    auto startLoc = current_.getLocation();
    // Accept either 'impl' or contextual 'extension' — both introduce an impl-like block
    if (check(TokenKind::identifier) && current_.getText() == "extension") {
        advance();
    } else {
        expect(TokenKind::kw_impl);
    }

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
    std::vector<ProtocolDecl::AssociatedTypeDecl> associatedTypeDecls;
    while (!check(TokenKind::r_brace) && !check(TokenKind::eof)) {
        if (diag_.hasMaxErrors()) break;
        while (match(TokenKind::semicolon)) {}
        if (check(TokenKind::r_brace) || check(TokenKind::eof)) break;

        if (check(TokenKind::kw_type)) {
            advance();  // consume 'type'
            auto typeName = expect(TokenKind::identifier);
            if (typeName.is(TokenKind::eof)) { synchronizeBody(); continue; }
            ProtocolDecl::AssociatedTypeDecl atDecl;
            atDecl.name = std::string(typeName.getText());
            // GATs: parse optional generic params <'a, T>
            if (match(TokenKind::less)) {
                if (!check(TokenKind::greater)) {
                    do {
                        if (check(TokenKind::lifetime_literal)) {
                            atDecl.lifetimeParams.push_back(std::string(current_.getText()));
                            advance();
                        } else {
                            auto pTok = expect(TokenKind::identifier);
                            atDecl.typeParams.push_back(std::string(pTok.getText()));
                        }
                    } while (match(TokenKind::comma));
                }
                expect(TokenKind::greater);
            }
            associatedTypeDecls.push_back(std::move(atDecl));
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

    // Convert AssociatedTypeDecls to simple names for constructor
    std::vector<std::string> associatedTypeNames;
    for (const auto &d : associatedTypeDecls) associatedTypeNames.push_back(d.name);
    auto proto = std::make_unique<ProtocolDecl>(std::move(name), std::move(methods),
                                                 std::move(associatedTypeNames), isPublic,
                                                 rangeFrom(startLoc));
    // Overwrite with full GATs decls if any have generic params
    bool hasGATs = false;
    for (const auto &d : associatedTypeDecls)
        if (!d.lifetimeParams.empty() || !d.typeParams.empty()) { hasGATs = true; break; }
    if (hasGATs) {
        // Replace internal decls with full versions via addAssociatedTypeDecl
        // First clear the simple ones added by constructor
        // Re-add all with full GATs info
        // (Since ProtocolDecl stores vector, we use a workaround: reconstruct)
        auto proto2 = std::make_unique<ProtocolDecl>(
            proto->getName(), std::vector<std::unique_ptr<FuncDecl>>{},
            std::vector<std::string>{}, isPublic, rangeFrom(startLoc));
        // Move methods back
        // Unfortunately we can't easily move methods out of proto, so rebuild
        // Instead, just add GATs decls to existing proto
    }
    // Directly set the GATs decls on the internal storage
    for (size_t i = 0; i < associatedTypeDecls.size(); ++i) {
        auto &src = associatedTypeDecls[i];
        auto &dst = const_cast<ProtocolDecl::AssociatedTypeDecl &>(
            proto->getAssociatedTypeDecls()[i]);
        dst.lifetimeParams = std::move(src.lifetimeParams);
        dst.typeParams = std::move(src.typeParams);
    }
    return proto;
}

std::unique_ptr<ImportDecl> Parser::parseImportDecl() {
    auto startLoc = current_.getLocation();
    expect(TokenKind::kw_import);

    // Accept identifiers and keywords in import paths (std::test, std::async)
    auto consumePathSegment = [&]() -> Token {
        if (check(TokenKind::kw_test) || check(TokenKind::kw_async)) {
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

std::unique_ptr<ClassDecl> Parser::parseClassDecl(bool isPublic, bool isFinal) {
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

        // Parse access modifier: private, pub/public, open, internal, fileprivate
        if (match(TokenKind::kw_private)) {
            member.access = AccessModifier::Private;
        } else if (match(TokenKind::kw_pub)) {
            member.access = AccessModifier::Public;
        } else if (check(TokenKind::identifier)) {
            auto txt = current_.getText();
            if (txt == "public") {
                member.access = AccessModifier::Public;
                advance();
            } else if (txt == "open") {
                member.access = AccessModifier::Open;
                advance();
            } else if (txt == "internal") {
                member.access = AccessModifier::Internal;
                advance();
            } else if (txt == "fileprivate") {
                member.access = AccessModifier::FilePrivate;
                advance();
            }
        }

        // Parse static keyword
        if (match(TokenKind::kw_static)) {
            member.isStatic = true;
        }

        // Parse final keyword
        if (match(TokenKind::kw_final)) {
            member.isFinal = true;
        }

        // Parse contextual 'convenience' (for init) and 'lazy' (for var)
        if (check(TokenKind::identifier) && current_.getText() == "convenience") {
            member.isConvenienceInit = true;
            advance();
        } else if (check(TokenKind::identifier) && current_.getText() == "lazy") {
            member.isLazy = true;
            advance();
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

            // Lazy var initializer: lazy var x: T = expr
            if (member.isLazy && match(TokenKind::equal)) {
                auto initExpr = parseExpression();
                if (initExpr) {
                    member.field->setLazyInit(std::move(initExpr));
                }
            }

            // Computed property or property observer:
            //   var name: Type { get { } set { } }
            //   var name: Type { willSet { } didSet { } }
            if (check(TokenKind::l_brace)) {
                advance(); // consume '{'
                while (!check(TokenKind::r_brace) && !check(TokenKind::eof)) {
                    if (check(TokenKind::identifier) && current_.getText() == "get") {
                        advance();
                        if (check(TokenKind::l_brace)) {
                            member.field->setGetter(parseBlock());
                        }
                    } else if (check(TokenKind::identifier) && current_.getText() == "set") {
                        advance();
                        if (check(TokenKind::l_brace)) {
                            member.field->setSetter(parseBlock());
                        }
                    } else if (check(TokenKind::identifier) && current_.getText() == "willSet") {
                        advance();
                        if (check(TokenKind::l_brace)) {
                            member.field->setWillSet(parseBlock());
                        }
                    } else if (check(TokenKind::identifier) && current_.getText() == "didSet") {
                        advance();
                        if (check(TokenKind::l_brace)) {
                            member.field->setDidSet(parseBlock());
                        }
                    } else {
                        break;
                    }
                }
                expect(TokenKind::r_brace); // closing '}'
            }

            members.push_back(std::move(member));
        }
        // init(params...) { body }  — self is implicit
        else if (check(TokenKind::kw_init)) {
            auto initStart = current_.getLocation();
            advance(); // consume 'init'

            // Failable init: init?(...)
            if (match(TokenKind::question)) {
                member.isFailableInit = true;
            }

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
        // subscript(params...) -> Type { body }  — getter only
        // subscript<T>(params...) -> Type { get { } set { } }  — generic subscript
        else if (check(TokenKind::identifier) && current_.getText() == "subscript") {
            auto subStart = current_.getLocation();
            advance(); // consume 'subscript'

            // Optional generic type params: subscript<T, U>
            std::vector<std::string> subTypeParams;
            if (match(TokenKind::less)) {
                if (!check(TokenKind::greater)) {
                    do {
                        auto paramTok = expect(TokenKind::identifier);
                        subTypeParams.push_back(std::string(paramTok.getText()));
                    } while (match(TokenKind::comma));
                }
                expect(TokenKind::greater);
            }

            expect(TokenKind::l_paren);
            std::vector<ParamDecl> params;
            if (!check(TokenKind::r_paren)) {
                do {
                    params.push_back(parseParamDecl());
                } while (match(TokenKind::comma));
            }
            expect(TokenKind::r_paren);

            // Return type
            std::unique_ptr<TypeRepr> retType = makeVoidType();
            if (match(TokenKind::arrow)) {
                retType = parseType();
            }

            std::unique_ptr<BlockStmt> getterBody;
            std::unique_ptr<BlockStmt> setterBody;

            if (check(TokenKind::l_brace)) {
                // Peek inside: if first token is 'get' or 'set', parse as get/set blocks
                advance(); // consume '{'
                bool hasGetSet = check(TokenKind::identifier) &&
                                 (current_.getText() == "get" || current_.getText() == "set");
                if (hasGetSet) {
                    while (!check(TokenKind::r_brace) && !check(TokenKind::eof)) {
                        if (check(TokenKind::identifier) && current_.getText() == "get") {
                            advance();
                            if (check(TokenKind::l_brace)) getterBody = parseBlock();
                        } else if (check(TokenKind::identifier) && current_.getText() == "set") {
                            advance();
                            if (check(TokenKind::l_brace)) setterBody = parseBlock();
                        } else {
                            break;
                        }
                    }
                    expect(TokenKind::r_brace);
                } else {
                    // Direct body = getter; re-parse as a block
                    std::vector<std::unique_ptr<ASTNode>> stmts;
                    while (!check(TokenKind::r_brace) && !check(TokenKind::eof)) {
                        auto stmt = parseStatement();
                        if (stmt) stmts.push_back(std::move(stmt));
                        else break;
                    }
                    auto bodyEnd = current_.getLocation();
                    expect(TokenKind::r_brace);
                    getterBody = std::make_unique<BlockStmt>(
                        std::move(stmts), rangeFrom(subStart));
                }
            }

            // Copy params for setter (can't move twice)
            auto cloneParams = [&]() {
                std::vector<ParamDecl> out;
                for (auto &p : params) {
                    ParamDecl np;
                    np.name = p.name;
                    np.type = cloneTypeRepr(p.type.get());
                    np.isSelf = p.isSelf;
                    np.isMutRef = p.isMutRef;
                    np.isVariadic = p.isVariadic;
                    out.push_back(std::move(np));
                }
                return out;
            };

            // Getter method
            member.method = std::make_unique<FuncDecl>(
                "subscript", cloneParams(), cloneTypeRepr(retType.get()), std::move(getterBody),
                member.access == AccessModifier::Public, rangeFrom(subStart));
            if (!subTypeParams.empty()) {
                member.method->setTypeParams(subTypeParams);
            }
            members.push_back(std::move(member));

            // Setter method (if present): subscript_set(params..., newValue: retType) → void
            if (setterBody) {
                ClassMember setMember;
                setMember.access = AccessModifier::Public;
                auto setParams = cloneParams();
                ParamDecl newValParam;
                newValParam.name = "newValue";
                newValParam.type = std::move(retType);
                setParams.push_back(std::move(newValParam));
                setMember.method = std::make_unique<FuncDecl>(
                    "subscript_set", std::move(setParams), makeVoidType(), std::move(setterBody),
                    true, rangeFrom(subStart));
                if (!subTypeParams.empty()) {
                    setMember.method->setTypeParams(subTypeParams);
                }
                members.push_back(std::move(setMember));
            }
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
        std::move(members), isPublic, rangeFrom(startLoc), isFinal);
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

    return std::make_unique<TestDecl>(std::move(name), std::move(body), rangeFrom(startLoc));
}

} // namespace liva
