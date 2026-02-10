#pragma once

#include "liva/AST/ASTNode.h"
#include "liva/AST/Type.h"
#include <memory>
#include <string>
#include <vector>

namespace liva {

/// Base class for all expressions
class Expr : public ASTNode {
public:
    using ASTNode::ASTNode;

    /// The resolved type of this expression (set during type checking)
    TypeRepr *getResolvedType() const { return resolvedType_.get(); }
    void setResolvedType(std::unique_ptr<TypeRepr> type) { resolvedType_ = std::move(type); }

    static bool classof(const ASTNode *node) {
        return node->getKind() >= NodeKind::IntegerLiteralExpr &&
               node->getKind() <= NodeKind::UnwrapExpr;
    }

protected:
    std::unique_ptr<TypeRepr> resolvedType_;
};

/// Integer literal: 42, 0xFF, 0b1010
class IntegerLiteralExpr : public Expr {
public:
    IntegerLiteralExpr(int64_t value, SourceRange range)
        : Expr(NodeKind::IntegerLiteralExpr, range), value_(value) {}

    int64_t getValue() const { return value_; }

    static bool classof(const ASTNode *node) {
        return node->getKind() == NodeKind::IntegerLiteralExpr;
    }

private:
    int64_t value_;
};

/// Float literal: 3.14, 1.0e10
class FloatLiteralExpr : public Expr {
public:
    FloatLiteralExpr(double value, SourceRange range)
        : Expr(NodeKind::FloatLiteralExpr, range), value_(value) {}

    double getValue() const { return value_; }

    static bool classof(const ASTNode *node) {
        return node->getKind() == NodeKind::FloatLiteralExpr;
    }

private:
    double value_;
};

/// Bool literal: true, false
class BoolLiteralExpr : public Expr {
public:
    BoolLiteralExpr(bool value, SourceRange range)
        : Expr(NodeKind::BoolLiteralExpr, range), value_(value) {}

    bool getValue() const { return value_; }

    static bool classof(const ASTNode *node) {
        return node->getKind() == NodeKind::BoolLiteralExpr;
    }

private:
    bool value_;
};

/// String literal: "hello"
class StringLiteralExpr : public Expr {
public:
    StringLiteralExpr(std::string value, SourceRange range)
        : Expr(NodeKind::StringLiteralExpr, range), value_(std::move(value)) {}

    const std::string &getValue() const { return value_; }

    static bool classof(const ASTNode *node) {
        return node->getKind() == NodeKind::StringLiteralExpr;
    }

private:
    std::string value_;
};

/// Nil literal
class NilLiteralExpr : public Expr {
public:
    explicit NilLiteralExpr(SourceRange range)
        : Expr(NodeKind::NilLiteralExpr, range) {}

    static bool classof(const ASTNode *node) {
        return node->getKind() == NodeKind::NilLiteralExpr;
    }
};

/// Identifier expression: foo, bar
class IdentifierExpr : public Expr {
public:
    IdentifierExpr(std::string name, SourceRange range)
        : Expr(NodeKind::IdentifierExpr, range), name_(std::move(name)) {}

    const std::string &getName() const { return name_; }

    static bool classof(const ASTNode *node) {
        return node->getKind() == NodeKind::IdentifierExpr;
    }

private:
    std::string name_;
};

/// Binary expression: a + b, x == y
class BinaryExpr : public Expr {
public:
    enum class Op {
        Add, Sub, Mul, Div, Mod,
        Eq, NotEq, Less, LessEq, Greater, GreaterEq,
        And, Or,
        BitAnd, BitOr, BitXor, Shl, Shr,
    };

    BinaryExpr(Op op, std::unique_ptr<Expr> lhs, std::unique_ptr<Expr> rhs, SourceRange range)
        : Expr(NodeKind::BinaryExpr, range), op_(op), lhs_(std::move(lhs)),
          rhs_(std::move(rhs)) {}

    Op getOp() const { return op_; }
    const Expr *getLHS() const { return lhs_.get(); }
    const Expr *getRHS() const { return rhs_.get(); }
    Expr *getLHS() { return lhs_.get(); }
    Expr *getRHS() { return rhs_.get(); }

    const char *getOpSpelling() const;

    static bool classof(const ASTNode *node) {
        return node->getKind() == NodeKind::BinaryExpr;
    }

private:
    Op op_;
    std::unique_ptr<Expr> lhs_;
    std::unique_ptr<Expr> rhs_;
};

/// Unary expression: -x, !b
class UnaryExpr : public Expr {
public:
    enum class Op {
        Negate,  // -
        Not,     // !
        BitNot,  // ~
    };

    UnaryExpr(Op op, std::unique_ptr<Expr> operand, SourceRange range)
        : Expr(NodeKind::UnaryExpr, range), op_(op), operand_(std::move(operand)) {}

    Op getOp() const { return op_; }
    const Expr *getOperand() const { return operand_.get(); }
    Expr *getOperand() { return operand_.get(); }

    const char *getOpSpelling() const;

    static bool classof(const ASTNode *node) {
        return node->getKind() == NodeKind::UnaryExpr;
    }

private:
    Op op_;
    std::unique_ptr<Expr> operand_;
};

/// Function call: foo(a, b)
class CallExpr : public Expr {
public:
    CallExpr(std::unique_ptr<Expr> callee, std::vector<std::unique_ptr<Expr>> args,
             SourceRange range)
        : Expr(NodeKind::CallExpr, range), callee_(std::move(callee)),
          args_(std::move(args)) {}

    const Expr *getCallee() const { return callee_.get(); }
    Expr *getCallee() { return callee_.get(); }
    const std::vector<std::unique_ptr<Expr>> &getArgs() const { return args_; }

    static bool classof(const ASTNode *node) {
        return node->getKind() == NodeKind::CallExpr;
    }

private:
    std::unique_ptr<Expr> callee_;
    std::vector<std::unique_ptr<Expr>> args_;
};

/// Member access: obj.field, obj.method()
class MemberExpr : public Expr {
public:
    MemberExpr(std::unique_ptr<Expr> object, std::string member, SourceRange range)
        : Expr(NodeKind::MemberExpr, range), object_(std::move(object)),
          member_(std::move(member)) {}

    const Expr *getObject() const { return object_.get(); }
    Expr *getObject() { return object_.get(); }
    const std::string &getMember() const { return member_; }

    static bool classof(const ASTNode *node) {
        return node->getKind() == NodeKind::MemberExpr;
    }

private:
    std::unique_ptr<Expr> object_;
    std::string member_;
};

/// Index expression: arr[i]
class IndexExpr : public Expr {
public:
    IndexExpr(std::unique_ptr<Expr> base, std::unique_ptr<Expr> index, SourceRange range)
        : Expr(NodeKind::IndexExpr, range), base_(std::move(base)),
          index_(std::move(index)) {}

    const Expr *getBase() const { return base_.get(); }
    const Expr *getIndex() const { return index_.get(); }

    static bool classof(const ASTNode *node) {
        return node->getKind() == NodeKind::IndexExpr;
    }

private:
    std::unique_ptr<Expr> base_;
    std::unique_ptr<Expr> index_;
};

/// Assignment: x = expr, x += expr
class AssignExpr : public Expr {
public:
    enum class Op {
        Assign,    // =
        AddAssign, // +=
        SubAssign, // -=
        MulAssign, // *=
        DivAssign, // /=
        ModAssign, // %=
    };

    AssignExpr(Op op, std::unique_ptr<Expr> target, std::unique_ptr<Expr> value,
               SourceRange range)
        : Expr(NodeKind::AssignExpr, range), op_(op), target_(std::move(target)),
          value_(std::move(value)) {}

    Op getOp() const { return op_; }
    const Expr *getTarget() const { return target_.get(); }
    const Expr *getValue() const { return value_.get(); }
    Expr *getTarget() { return target_.get(); }
    Expr *getValue() { return value_.get(); }

    static bool classof(const ASTNode *node) {
        return node->getKind() == NodeKind::AssignExpr;
    }

private:
    Op op_;
    std::unique_ptr<Expr> target_;
    std::unique_ptr<Expr> value_;
};

/// Struct literal: Point { x: 1.0, y: 2.0 }
class StructLiteralExpr : public Expr {
public:
    struct FieldInit {
        std::string name;
        std::unique_ptr<Expr> value;
    };

    StructLiteralExpr(std::string typeName, std::vector<FieldInit> fields, SourceRange range)
        : Expr(NodeKind::StructLiteralExpr, range), typeName_(std::move(typeName)),
          fields_(std::move(fields)) {}

    const std::string &getTypeName() const { return typeName_; }
    const std::vector<FieldInit> &getFields() const { return fields_; }

    static bool classof(const ASTNode *node) {
        return node->getKind() == NodeKind::StructLiteralExpr;
    }

private:
    std::string typeName_;
    std::vector<FieldInit> fields_;
};

/// Match arm for pattern matching
struct MatchArm {
    std::string pattern;                        // Pattern string (simplified for now)
    std::vector<std::string> bindings;          // Bound variables in pattern
    std::unique_ptr<Expr> body;                 // Body expression
};

/// Match expression: match expr { arms }
class MatchExpr : public Expr {
public:
    MatchExpr(std::unique_ptr<Expr> subject, std::vector<MatchArm> arms, SourceRange range)
        : Expr(NodeKind::MatchExpr, range), subject_(std::move(subject)),
          arms_(std::move(arms)) {}

    const Expr *getSubject() const { return subject_.get(); }
    const std::vector<MatchArm> &getArms() const { return arms_; }

    static bool classof(const ASTNode *node) {
        return node->getKind() == NodeKind::MatchExpr;
    }

private:
    std::unique_ptr<Expr> subject_;
    std::vector<MatchArm> arms_;
};

/// Array literal: [1, 2, 3]
class ArrayLiteralExpr : public Expr {
public:
    ArrayLiteralExpr(std::vector<std::unique_ptr<Expr>> elements, SourceRange range)
        : Expr(NodeKind::ArrayLiteralExpr, range), elements_(std::move(elements)) {}

    const std::vector<std::unique_ptr<Expr>> &getElements() const { return elements_; }

    static bool classof(const ASTNode *node) {
        return node->getKind() == NodeKind::ArrayLiteralExpr;
    }

private:
    std::vector<std::unique_ptr<Expr>> elements_;
};

/// Cast expression: expr as Type
class CastExpr : public Expr {
public:
    CastExpr(std::unique_ptr<Expr> expr, std::unique_ptr<TypeRepr> targetType,
             SourceRange range)
        : Expr(NodeKind::CastExpr, range), expr_(std::move(expr)),
          targetType_(std::move(targetType)) {}

    const Expr *getExpr() const { return expr_.get(); }
    const TypeRepr *getTargetType() const { return targetType_.get(); }

    static bool classof(const ASTNode *node) {
        return node->getKind() == NodeKind::CastExpr;
    }

private:
    std::unique_ptr<Expr> expr_;
    std::unique_ptr<TypeRepr> targetType_;
};

/// Reference expression: ref x, ref mut x
class RefExpr : public Expr {
public:
    RefExpr(std::unique_ptr<Expr> expr, bool isMutable, SourceRange range)
        : Expr(NodeKind::RefExpr, range), expr_(std::move(expr)), isMutable_(isMutable) {}

    const Expr *getExpr() const { return expr_.get(); }
    bool isMutable() const { return isMutable_; }

    static bool classof(const ASTNode *node) {
        return node->getKind() == NodeKind::RefExpr;
    }

private:
    std::unique_ptr<Expr> expr_;
    bool isMutable_;
};

/// Grouped/parenthesized expression: (expr)
class GroupExpr : public Expr {
public:
    GroupExpr(std::unique_ptr<Expr> expr, SourceRange range)
        : Expr(NodeKind::GroupExpr, range), expr_(std::move(expr)) {}

    const Expr *getExpr() const { return expr_.get(); }
    Expr *getExpr() { return expr_.get(); }

    static bool classof(const ASTNode *node) {
        return node->getKind() == NodeKind::GroupExpr;
    }

private:
    std::unique_ptr<Expr> expr_;
};

/// Range expression: start..end
class RangeExpr : public Expr {
public:
    RangeExpr(std::unique_ptr<Expr> start, std::unique_ptr<Expr> end, SourceRange range)
        : Expr(NodeKind::RangeExpr, range), start_(std::move(start)),
          end_(std::move(end)) {}

    const Expr *getStart() const { return start_.get(); }
    const Expr *getEnd() const { return end_.get(); }
    Expr *getStart() { return start_.get(); }
    Expr *getEnd() { return end_.get(); }

    static bool classof(const ASTNode *node) {
        return node->getKind() == NodeKind::RangeExpr;
    }

private:
    std::unique_ptr<Expr> start_;
    std::unique_ptr<Expr> end_;
};

/// Force unwrap expression: x!
class UnwrapExpr : public Expr {
public:
    UnwrapExpr(std::unique_ptr<Expr> operand, SourceRange range)
        : Expr(NodeKind::UnwrapExpr, range), operand_(std::move(operand)) {}

    const Expr *getOperand() const { return operand_.get(); }
    Expr *getOperand() { return operand_.get(); }

    static bool classof(const ASTNode *node) {
        return node->getKind() == NodeKind::UnwrapExpr;
    }

private:
    std::unique_ptr<Expr> operand_;
};

} // namespace liva
