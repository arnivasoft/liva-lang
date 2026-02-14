#pragma once

#include "liva/AST/Decl.h"
#include "liva/AST/Expr.h"
#include "liva/AST/Stmt.h"
#include "liva/AST/Type.h"
#include "liva/Common/Diagnostics.h"
#include "liva/Lexer/Lexer.h"
#include "liva/Lexer/Token.h"
#include <memory>

namespace liva {

/// Recursive descent parser with Pratt expression parsing
class Parser {
public:
    Parser(Lexer &lexer, DiagnosticsEngine &diag);

    /// Parse entire translation unit
    std::unique_ptr<TranslationUnit> parseTranslationUnit();

    // Declaration parsing (ParseDecl.cpp)
    std::unique_ptr<ASTNode> parseTopLevelDecl();
    std::unique_ptr<FuncDecl> parseFuncDecl(bool isPublic = false, bool isAsync = false);
    std::unique_ptr<VarDecl> parseVarDecl();
    std::unique_ptr<VarDecl> parseConstDecl();
    std::unique_ptr<StructDecl> parseStructDecl(bool isPublic = false);
    std::unique_ptr<EnumDecl> parseEnumDecl(bool isPublic = false);
    std::unique_ptr<ImplDecl> parseImplDecl();
    std::unique_ptr<ProtocolDecl> parseProtocolDecl(bool isPublic = false);
    std::unique_ptr<ImportDecl> parseImportDecl();
    std::unique_ptr<TypeAliasDecl> parseTypeAliasDecl(bool isPublic = false);

    // Statement parsing (ParseStmt.cpp)
    std::unique_ptr<ASTNode> parseStatement();
    std::unique_ptr<BlockStmt> parseBlock();
    std::unique_ptr<ReturnStmt> parseReturnStmt();
    std::unique_ptr<ASTNode> parseIfStmt();
    std::unique_ptr<IfLetStmt> parseIfLetStmt(SourceLocation ifLoc);
    std::unique_ptr<ASTNode> parseWhileStmt();
    std::unique_ptr<ForStmt> parseForStmt();

    // Expression parsing - Pratt parser (ParseExpr.cpp)
    std::unique_ptr<Expr> parseExpression();
    std::unique_ptr<Expr> parsePrecedenceExpr(int minPrec);
    std::unique_ptr<Expr> parsePrimaryExpr();
    std::unique_ptr<Expr> parseUnaryExpr();
    std::unique_ptr<Expr> parsePostfixExpr(std::unique_ptr<Expr> base);
    std::unique_ptr<Expr> parseCallExpr(std::unique_ptr<Expr> callee);
    std::unique_ptr<Expr> parseMemberExpr(std::unique_ptr<Expr> object,
                                             bool isOptionalChain = false);
    std::unique_ptr<Expr> parseIndexExpr(std::unique_ptr<Expr> base);
    std::unique_ptr<Expr> parseStructLiteral(const std::string &name, SourceLocation startLoc);
    std::unique_ptr<Expr> parseArrayLiteral();
    std::unique_ptr<Expr> parseMatchExpr();
    std::unique_ptr<Expr> parseClosureExpr();

    // Type parsing (ParseType.cpp)
    std::unique_ptr<TypeRepr> parseType();
    std::unique_ptr<TypeRepr> parseBaseType();
    ParamDecl parseParamDecl();

private:
    /// Get operator precedence for binary operators
    int getBinaryOpPrecedence(TokenKind kind) const;

    /// Get the BinaryExpr::Op for a token kind
    BinaryExpr::Op getBinaryOp(TokenKind kind) const;

    /// Get the AssignExpr::Op for a compound assignment token
    AssignExpr::Op getAssignOp(TokenKind kind) const;

    /// Check if token is a compound assignment operator
    bool isAssignOp(TokenKind kind) const;

    // Token management
    Token currentToken() const { return current_; }
    Token advance();
    Token peek();
    bool check(TokenKind kind) const { return current_.is(kind); }
    bool match(TokenKind kind);
    Token expect(TokenKind kind);

    /// Advance tokens until a synchronization point (statement/declaration boundary)
    void synchronize();

    /// Advance tokens until a member-declaration boundary inside a body (struct/enum/impl/protocol)
    void synchronizeBody();

    /// Create a source range from start to current position
    SourceRange rangeFrom(SourceLocation start) const;

    Lexer &lexer_;
    DiagnosticsEngine &diag_;
    Token current_;
};

} // namespace liva
