#include "liva/AST/Expr.h"
#include "liva/AST/Stmt.h"

namespace liva {

const char *BinaryExpr::getOpSpelling() const {
    switch (op_) {
    case Op::Add:
        return "+";
    case Op::Sub:
        return "-";
    case Op::Mul:
        return "*";
    case Op::Div:
        return "/";
    case Op::Mod:
        return "%";
    case Op::Eq:
        return "==";
    case Op::NotEq:
        return "!=";
    case Op::Less:
        return "<";
    case Op::LessEq:
        return "<=";
    case Op::Greater:
        return ">";
    case Op::GreaterEq:
        return ">=";
    case Op::And:
        return "&&";
    case Op::Or:
        return "||";
    case Op::BitAnd:
        return "&";
    case Op::BitOr:
        return "|";
    case Op::BitXor:
        return "^";
    case Op::Shl:
        return "<<";
    case Op::Shr:
        return ">>";
    }
    return "<unknown>";
}

const char *UnaryExpr::getOpSpelling() const {
    switch (op_) {
    case Op::Negate:
        return "-";
    case Op::Not:
        return "!";
    case Op::BitNot:
        return "~";
    }
    return "<unknown>";
}

ClosureExpr::~ClosureExpr() = default;

} // namespace liva
