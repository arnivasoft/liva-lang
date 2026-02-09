#pragma once

#include "liva/Common/SourceLocation.h"
#include <cstdint>

namespace liva {

/// Base class for all AST nodes
class ASTNode {
public:
    enum class NodeKind : uint8_t {
        // Declarations
        FuncDecl,
        VarDecl,
        StructDecl,
        FieldDecl,
        EnumDecl,
        EnumCaseDecl,
        ImplDecl,
        ProtocolDecl,
        ImportDecl,

        // Statements
        ExprStmt,
        ReturnStmt,
        IfStmt,
        WhileStmt,
        ForStmt,
        BlockStmt,
        BreakStmt,
        ContinueStmt,

        // Expressions
        IntegerLiteralExpr,
        FloatLiteralExpr,
        BoolLiteralExpr,
        StringLiteralExpr,
        NilLiteralExpr,
        IdentifierExpr,
        BinaryExpr,
        UnaryExpr,
        CallExpr,
        MemberExpr,
        IndexExpr,
        AssignExpr,
        StructLiteralExpr,
        MatchExpr,
        ArrayLiteralExpr,
        CastExpr,
        RefExpr,
        GroupExpr,
        RangeExpr,
    };

    explicit ASTNode(NodeKind kind, SourceRange range = SourceRange::invalid())
        : kind_(kind), range_(range) {}
    virtual ~ASTNode() = default;

    NodeKind getKind() const { return kind_; }
    SourceRange getRange() const { return range_; }
    void setRange(SourceRange range) { range_ = range; }

    SourceLocation getStartLoc() const { return range_.start; }
    SourceLocation getEndLoc() const { return range_.end; }

private:
    NodeKind kind_;
    SourceRange range_;
};

} // namespace liva
