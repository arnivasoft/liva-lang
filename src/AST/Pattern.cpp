#include "liva/AST/Pattern.h"

namespace liva {

std::string Pattern::toString() const {
    switch (kind_) {
    case Kind::Wildcard:
        return "_";

    case Kind::Identifier:
        return static_cast<const IdentifierPattern *>(this)->getName();

    case Kind::IntLiteral:
        return static_cast<const IntLiteralPattern *>(this)->getText();

    case Kind::EnumCase: {
        const auto *ec = static_cast<const EnumCasePattern *>(this);
        std::string out;
        if (!ec->getEnumName().empty()) {
            out += ec->getEnumName();
            out += ".";
        }
        out += ec->getCaseName();
        if (ec->hasParens()) {
            out += "(";
            const auto &subs = ec->getSubpatterns();
            for (size_t i = 0; i < subs.size(); ++i) {
                if (i > 0) out += ",";
                out += subs[i]->toString();
            }
            out += ")";
        }
        return out;
    }
    }
    return {};
}

std::string Pattern::getSpelling() const {
    // Today identical to toString() byte-for-byte (Pattern Types Faz B,
    // Task 1 hardening) — kept as a distinct entry point so binding-name
    // derivation call sites don't silently ride whatever toString() happens
    // to produce once a future pattern kind needs a different display form.
    return toString();
}

} // namespace liva
