#pragma once

#include "liva/AST/ASTNode.h"
#include "liva/AST/Expr.h"
#include <memory>
#include <vector>

namespace liva {

class Decl; // Forward declaration

/// Base class for all statements
class Stmt : public ASTNode {
public:
    using ASTNode::ASTNode;

    static bool classof(const ASTNode *node) {
        return node->getKind() >= NodeKind::ExprStmt &&
               node->getKind() <= NodeKind::WhileLetStmt;
    }
};

/// Expression statement: expr;
class ExprStmt : public Stmt {
public:
    ExprStmt(std::unique_ptr<Expr> expr, SourceRange range)
        : Stmt(NodeKind::ExprStmt, range), expr_(std::move(expr)) {}

    const Expr *getExpr() const { return expr_.get(); }
    Expr *getExpr() { return expr_.get(); }

    static bool classof(const ASTNode *node) {
        return node->getKind() == NodeKind::ExprStmt;
    }

private:
    std::unique_ptr<Expr> expr_;
};

/// Return statement: return expr;
class ReturnStmt : public Stmt {
public:
    ReturnStmt(std::unique_ptr<Expr> value, SourceRange range)
        : Stmt(NodeKind::ReturnStmt, range), value_(std::move(value)) {}

    const Expr *getValue() const { return value_.get(); }
    Expr *getValue() { return value_.get(); }
    bool hasValue() const { return value_ != nullptr; }

    static bool classof(const ASTNode *node) {
        return node->getKind() == NodeKind::ReturnStmt;
    }

private:
    std::unique_ptr<Expr> value_;
};

/// If statement: if cond { ... } else { ... }
class IfStmt : public Stmt {
public:
    IfStmt(std::unique_ptr<Expr> condition, std::unique_ptr<ASTNode> thenBody,
           std::unique_ptr<ASTNode> elseBody, SourceRange range)
        : Stmt(NodeKind::IfStmt, range), condition_(std::move(condition)),
          thenBody_(std::move(thenBody)), elseBody_(std::move(elseBody)) {}

    const Expr *getCondition() const { return condition_.get(); }
    const ASTNode *getThenBody() const { return thenBody_.get(); }
    const ASTNode *getElseBody() const { return elseBody_.get(); }
    ASTNode *getThenBody() { return thenBody_.get(); }
    ASTNode *getElseBody() { return elseBody_.get(); }
    bool hasElse() const { return elseBody_ != nullptr; }

    static bool classof(const ASTNode *node) {
        return node->getKind() == NodeKind::IfStmt;
    }

private:
    std::unique_ptr<Expr> condition_;
    std::unique_ptr<ASTNode> thenBody_;
    std::unique_ptr<ASTNode> elseBody_;
};

/// While statement: while cond { ... }
class WhileStmt : public Stmt {
public:
    WhileStmt(std::unique_ptr<Expr> condition, std::unique_ptr<ASTNode> body,
              SourceRange range)
        : Stmt(NodeKind::WhileStmt, range), condition_(std::move(condition)),
          body_(std::move(body)) {}

    const Expr *getCondition() const { return condition_.get(); }
    const ASTNode *getBody() const { return body_.get(); }

    static bool classof(const ASTNode *node) {
        return node->getKind() == NodeKind::WhileStmt;
    }

private:
    std::unique_ptr<Expr> condition_;
    std::unique_ptr<ASTNode> body_;
};

/// For-in statement: for x in range { ... } or for (k, v) in map { ... }
class ForStmt : public Stmt {
public:
    ForStmt(std::string varName, std::unique_ptr<Expr> iterable,
            std::unique_ptr<ASTNode> body, SourceRange range)
        : Stmt(NodeKind::ForStmt, range), varName_(std::move(varName)),
          iterable_(std::move(iterable)), body_(std::move(body)) {}

    ForStmt(std::string varName1, std::string varName2,
            std::unique_ptr<Expr> iterable,
            std::unique_ptr<ASTNode> body, SourceRange range)
        : Stmt(NodeKind::ForStmt, range), varName_(std::move(varName1)),
          varName2_(std::move(varName2)),
          iterable_(std::move(iterable)), body_(std::move(body)) {}

    const std::string &getVarName() const { return varName_; }
    bool hasTuplePattern() const { return !varName2_.empty(); }
    const std::string &getVarName2() const { return varName2_; }
    const Expr *getIterable() const { return iterable_.get(); }
    const ASTNode *getBody() const { return body_.get(); }

    static bool classof(const ASTNode *node) {
        return node->getKind() == NodeKind::ForStmt;
    }

private:
    std::string varName_;
    std::string varName2_;
    std::unique_ptr<Expr> iterable_;
    std::unique_ptr<ASTNode> body_;
};

/// Block statement: { stmts... }
class BlockStmt : public Stmt {
public:
    BlockStmt(std::vector<std::unique_ptr<ASTNode>> statements, SourceRange range)
        : Stmt(NodeKind::BlockStmt, range), statements_(std::move(statements)) {}

    const std::vector<std::unique_ptr<ASTNode>> &getStatements() const { return statements_; }
    std::vector<std::unique_ptr<ASTNode>> &getStatements() { return statements_; }

    static bool classof(const ASTNode *node) {
        return node->getKind() == NodeKind::BlockStmt;
    }

private:
    std::vector<std::unique_ptr<ASTNode>> statements_;
};

/// Break statement
class BreakStmt : public Stmt {
public:
    explicit BreakStmt(SourceRange range) : Stmt(NodeKind::BreakStmt, range) {}

    static bool classof(const ASTNode *node) {
        return node->getKind() == NodeKind::BreakStmt;
    }
};

/// Continue statement
class ContinueStmt : public Stmt {
public:
    explicit ContinueStmt(SourceRange range) : Stmt(NodeKind::ContinueStmt, range) {}

    static bool classof(const ASTNode *node) {
        return node->getKind() == NodeKind::ContinueStmt;
    }
};

/// If-let statement: if let value = optExpr { ... } else { ... }
class IfLetStmt : public Stmt {
public:
    IfLetStmt(std::string bindingName, std::unique_ptr<Expr> optionalExpr,
              std::unique_ptr<BlockStmt> thenBody,
              std::unique_ptr<ASTNode> elseBody, SourceRange range)
        : Stmt(NodeKind::IfLetStmt, range),
          bindingName_(std::move(bindingName)),
          optionalExpr_(std::move(optionalExpr)),
          thenBody_(std::move(thenBody)),
          elseBody_(std::move(elseBody)) {}

    const std::string &getBindingName() const { return bindingName_; }
    const Expr *getOptionalExpr() const { return optionalExpr_.get(); }
    Expr *getOptionalExpr() { return optionalExpr_.get(); }
    const BlockStmt *getThenBody() const { return thenBody_.get(); }
    BlockStmt *getThenBody() { return thenBody_.get(); }
    const ASTNode *getElseBody() const { return elseBody_.get(); }
    ASTNode *getElseBody() { return elseBody_.get(); }
    bool hasElse() const { return elseBody_ != nullptr; }

    static bool classof(const ASTNode *node) {
        return node->getKind() == NodeKind::IfLetStmt;
    }

private:
    std::string bindingName_;
    std::unique_ptr<Expr> optionalExpr_;
    std::unique_ptr<BlockStmt> thenBody_;
    std::unique_ptr<ASTNode> elseBody_;
};

/// while-let statement: while let x = optional { body }
class WhileLetStmt : public Stmt {
public:
    WhileLetStmt(std::string bindingName, std::unique_ptr<Expr> optionalExpr,
                 std::unique_ptr<BlockStmt> body, SourceRange range)
        : Stmt(NodeKind::WhileLetStmt, range),
          bindingName_(std::move(bindingName)),
          optionalExpr_(std::move(optionalExpr)),
          body_(std::move(body)) {}

    const std::string &getBindingName() const { return bindingName_; }
    const Expr *getOptionalExpr() const { return optionalExpr_.get(); }
    Expr *getOptionalExpr() { return optionalExpr_.get(); }
    const BlockStmt *getBody() const { return body_.get(); }
    BlockStmt *getBody() { return body_.get(); }

    static bool classof(const ASTNode *node) {
        return node->getKind() == NodeKind::WhileLetStmt;
    }

private:
    std::string bindingName_;
    std::unique_ptr<Expr> optionalExpr_;
    std::unique_ptr<BlockStmt> body_;
};

} // namespace liva
