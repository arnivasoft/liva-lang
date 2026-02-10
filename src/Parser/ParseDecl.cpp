#include "liva/Parser/Parser.h"

namespace liva {

std::unique_ptr<ASTNode> Parser::parseTopLevelDecl() {
    bool isPublic = false;
    if (match(TokenKind::kw_pub)) {
        isPublic = true;
    }

    switch (current_.getKind()) {
    case TokenKind::kw_func:
        return parseFuncDecl(isPublic);
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
    default:
        diag_.report(current_.getLocation(), DiagID::err_expected_declaration);
        return nullptr;
    }
}

std::unique_ptr<FuncDecl> Parser::parseFuncDecl(bool isPublic) {
    auto startLoc = current_.getLocation();
    expect(TokenKind::kw_func);

    auto nameTok = expect(TokenKind::identifier);
    std::string name(nameTok.getText());

    // Parse optional generic type parameters: <T, U>
    std::vector<std::string> typeParams;
    if (match(TokenKind::less)) {
        if (!check(TokenKind::greater)) {
            do {
                auto paramTok = expect(TokenKind::identifier);
                typeParams.push_back(std::string(paramTok.getText()));
            } while (match(TokenKind::comma));
        }
        expect(TokenKind::greater);
    }

    // Parse parameter list
    expect(TokenKind::l_paren);
    std::vector<ParamDecl> params;
    if (!check(TokenKind::r_paren)) {
        do {
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

    // Parse body (optional for protocol declarations)
    std::unique_ptr<BlockStmt> body;
    if (check(TokenKind::l_brace)) {
        body = parseBlock();
    }

    auto funcDecl = std::make_unique<FuncDecl>(std::move(name), std::move(params),
                                                std::move(returnType), std::move(body),
                                                isPublic, rangeFrom(startLoc));
    if (!typeParams.empty()) {
        funcDecl->setTypeParams(std::move(typeParams));
    }
    return funcDecl;
}

std::unique_ptr<VarDecl> Parser::parseVarDecl() {
    auto startLoc = current_.getLocation();
    bool isMutable = current_.is(TokenKind::kw_var);
    advance(); // consume let/var

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

    // Parse optional generic type parameters: <T, U>
    std::vector<std::string> typeParams;
    if (match(TokenKind::less)) {
        if (!check(TokenKind::greater)) {
            do {
                auto paramTok = expect(TokenKind::identifier);
                typeParams.push_back(std::string(paramTok.getText()));
            } while (match(TokenKind::comma));
        }
        expect(TokenKind::greater);
    }

    expect(TokenKind::l_brace);

    std::vector<std::unique_ptr<FieldDecl>> fields;
    while (!check(TokenKind::r_brace) && !check(TokenKind::eof)) {
        auto fieldStart = current_.getLocation();
        bool fieldMutable = false;

        if (check(TokenKind::kw_var)) {
            fieldMutable = true;
            advance();
        } else if (check(TokenKind::kw_let)) {
            advance();
        }

        auto fieldName = expect(TokenKind::identifier);
        expect(TokenKind::colon);
        auto fieldType = parseType();

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
        expect(TokenKind::kw_case);
        auto caseStart = current_.getLocation();
        auto caseName = expect(TokenKind::identifier);

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

    // Optional protocol conformance
    std::string protocolName;
    if (match(TokenKind::colon)) {
        auto protoTok = expect(TokenKind::identifier);
        protocolName = std::string(protoTok.getText());
    }

    expect(TokenKind::l_brace);

    std::vector<std::unique_ptr<FuncDecl>> methods;
    while (!check(TokenKind::r_brace) && !check(TokenKind::eof)) {
        bool isPublic = false;
        if (match(TokenKind::kw_pub)) {
            isPublic = true;
        }
        methods.push_back(parseFuncDecl(isPublic));
    }

    expect(TokenKind::r_brace);

    return std::make_unique<ImplDecl>(std::string(typeName.getText()), std::move(protocolName),
                                      std::move(methods), rangeFrom(startLoc));
}

std::unique_ptr<ProtocolDecl> Parser::parseProtocolDecl(bool isPublic) {
    auto startLoc = current_.getLocation();
    expect(TokenKind::kw_protocol);

    auto nameTok = expect(TokenKind::identifier);
    std::string name(nameTok.getText());

    expect(TokenKind::l_brace);

    std::vector<std::unique_ptr<FuncDecl>> methods;
    while (!check(TokenKind::r_brace) && !check(TokenKind::eof)) {
        methods.push_back(parseFuncDecl(false));
    }

    expect(TokenKind::r_brace);

    return std::make_unique<ProtocolDecl>(std::move(name), std::move(methods), isPublic,
                                          rangeFrom(startLoc));
}

std::unique_ptr<ImportDecl> Parser::parseImportDecl() {
    auto startLoc = current_.getLocation();
    expect(TokenKind::kw_import);

    std::vector<std::string> path;
    auto first = expect(TokenKind::identifier);
    path.push_back(std::string(first.getText()));

    while (match(TokenKind::coloncolon)) {
        auto next = expect(TokenKind::identifier);
        path.push_back(std::string(next.getText()));
    }

    return std::make_unique<ImportDecl>(std::move(path), rangeFrom(startLoc));
}

} // namespace liva
