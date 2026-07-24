#include "liva/AST/Type.h"

namespace liva {

std::string TypeRepr::toString() const {
    switch (kind_) {
    case Kind::Void:
        return "void";
    case Kind::Bool:
        return "bool";
    case Kind::I8:
        return "i8";
    case Kind::I16:
        return "i16";
    case Kind::I32:
        return "i32";
    case Kind::I64:
        return "i64";
    case Kind::U8:
        return "u8";
    case Kind::U16:
        return "u16";
    case Kind::U32:
        return "u32";
    case Kind::U64:
        return "u64";
    case Kind::F32:
        return "f32";
    case Kind::F64:
        return "f64";
    case Kind::String:
        return "string";
    case Kind::Named:
        return "<named>";
    case Kind::Array:
        return "<array>";
    case Kind::Reference:
        return "<ref>";
    case Kind::Optional:
        return "<optional>";
    case Kind::Result:
        return "<result>";
    case Kind::Function:
        return "<function>";
    case Kind::Tuple:
        return "<tuple>";
    case Kind::Generic:
        return "<generic>";
    case Kind::Inferred:
        return "<inferred>";
    case Kind::DynProtocol:
        return "<dyn>";
    case Kind::AssociatedType:
        return "<assoc>";
    case Kind::ConstValue:
        return "const";
    }
    return "<unknown>";
}

bool TypeRepr::isInteger() const {
    return kind_ >= Kind::I8 && kind_ <= Kind::U64;
}

bool TypeRepr::isSignedInteger() const {
    return kind_ >= Kind::I8 && kind_ <= Kind::I64;
}

bool TypeRepr::isUnsignedInteger() const {
    return kind_ >= Kind::U8 && kind_ <= Kind::U64;
}

bool TypeRepr::isFloat() const {
    return kind_ == Kind::F32 || kind_ == Kind::F64;
}

bool TypeRepr::isPrimitive() const {
    return kind_ >= Kind::Void && kind_ <= Kind::String;
}

int TypeRepr::getBitWidth() const {
    switch (kind_) {
    case Kind::I8:
    case Kind::U8:
        return 8;
    case Kind::I16:
    case Kind::U16:
        return 16;
    case Kind::I32:
    case Kind::U32:
    case Kind::F32:
        return 32;
    case Kind::I64:
    case Kind::U64:
    case Kind::F64:
        return 64;
    case Kind::Bool:
        return 1;
    default:
        return 0;
    }
}

std::string ReferenceTypeRepr::toString() const {
    if (isMutable_)
        return "ref mut " + inner_->toString();
    return "ref " + inner_->toString();
}

std::string ArrayTypeRepr::toString() const {
    if (isDynamic())
        return "[" + element_->toString() + "]";
    return "[" + element_->toString() + "; " + std::to_string(size_) + "]";
}

std::string FunctionTypeRepr::toString() const {
    std::string result = "(";
    for (size_t i = 0; i < params_.size(); ++i) {
        if (i > 0)
            result += ", ";
        result += params_[i]->toString();
    }
    result += ") -> " + returnType_->toString();
    return result;
}

std::string TupleTypeRepr::toString() const {
    std::string result = "(";
    for (size_t i = 0; i < elements_.size(); ++i) {
        if (i > 0)
            result += ", ";
        result += elements_[i]->toString();
    }
    result += ")";
    return result;
}

std::string GenericTypeRepr::toString() const {
    std::string result = baseName_ + "<";
    for (size_t i = 0; i < typeArgs_.size(); ++i) {
        if (i > 0)
            result += ", ";
        result += typeArgs_[i]->toString();
    }
    result += ">";
    return result;
}

std::string OptionalTypeRepr::toString() const {
    return inner_->toString() + "?";
}

std::string ResultTypeRepr::toString() const {
    return "Result<" + okType_->toString() + ", " + errType_->toString() + ">";
}

std::unique_ptr<TypeRepr> makeVoidType() {
    return std::make_unique<TypeRepr>(TypeRepr::Kind::Void);
}

std::unique_ptr<TypeRepr> makeBoolType() {
    return std::make_unique<TypeRepr>(TypeRepr::Kind::Bool);
}

std::unique_ptr<TypeRepr> makeI32Type() {
    return std::make_unique<TypeRepr>(TypeRepr::Kind::I32);
}

std::unique_ptr<TypeRepr> makeI64Type() {
    return std::make_unique<TypeRepr>(TypeRepr::Kind::I64);
}

std::unique_ptr<TypeRepr> makeF64Type() {
    return std::make_unique<TypeRepr>(TypeRepr::Kind::F64);
}

std::unique_ptr<TypeRepr> makeStringType() {
    return std::make_unique<TypeRepr>(TypeRepr::Kind::String);
}

std::unique_ptr<TypeRepr> makeInferredType() {
    return std::make_unique<TypeRepr>(TypeRepr::Kind::Inferred);
}

std::unique_ptr<TypeRepr> makeNamedType(const std::string &name) {
    return std::make_unique<NamedTypeRepr>(name);
}

std::unique_ptr<TypeRepr> makePrimitiveType(TypeRepr::Kind kind) {
    return std::make_unique<TypeRepr>(kind);
}

std::unique_ptr<TypeRepr> makeDynProtocolType(const std::string &protocolName) {
    return std::make_unique<DynProtocolTypeRepr>(protocolName);
}

std::unique_ptr<TypeRepr> makeAssociatedType(const std::string &base, const std::string &assoc) {
    return std::make_unique<AssociatedTypeRepr>(base, assoc);
}

std::unique_ptr<TypeRepr> cloneTypeRepr(const TypeRepr *type) {
    if (!type) return nullptr;
    if (type->isPrimitive() || type->isVoid())
        return makePrimitiveType(type->getKind());
    switch (type->getKind()) {
    case TypeRepr::Kind::Named: {
        auto *n = static_cast<const NamedTypeRepr *>(type);
        return makeNamedType(n->getName());
    }
    case TypeRepr::Kind::Optional: {
        auto *o = static_cast<const OptionalTypeRepr *>(type);
        return std::make_unique<OptionalTypeRepr>(cloneTypeRepr(o->getInner()));
    }
    case TypeRepr::Kind::Array: {
        auto *a = static_cast<const ArrayTypeRepr *>(type);
        return std::make_unique<ArrayTypeRepr>(cloneTypeRepr(a->getElement()), a->getSize());
    }
    case TypeRepr::Kind::Function: {
        auto *f = static_cast<const FunctionTypeRepr *>(type);
        std::vector<std::unique_ptr<TypeRepr>> params;
        for (auto &p : f->getParams())
            params.push_back(cloneTypeRepr(p.get()));
        return std::make_unique<FunctionTypeRepr>(std::move(params), cloneTypeRepr(f->getReturnType()));
    }
    case TypeRepr::Kind::Result: {
        auto *r = static_cast<const ResultTypeRepr *>(type);
        return std::make_unique<ResultTypeRepr>(cloneTypeRepr(r->getOkType()), cloneTypeRepr(r->getErrType()));
    }
    case TypeRepr::Kind::Tuple: {
        auto *t = static_cast<const TupleTypeRepr *>(type);
        std::vector<std::unique_ptr<TypeRepr>> elems;
        for (auto &e : t->getElements())
            elems.push_back(cloneTypeRepr(e.get()));
        return std::make_unique<TupleTypeRepr>(std::move(elems));
    }
    case TypeRepr::Kind::DynProtocol: {
        auto *d = static_cast<const DynProtocolTypeRepr *>(type);
        return makeDynProtocolType(d->getProtocolName());
    }
    case TypeRepr::Kind::AssociatedType: {
        auto *a = static_cast<const AssociatedTypeRepr *>(type);
        return makeAssociatedType(a->getBaseName(), a->getAssocTypeName());
    }
    case TypeRepr::Kind::Reference: {
        auto *r = static_cast<const ReferenceTypeRepr *>(type);
        auto cloned = std::make_unique<ReferenceTypeRepr>(cloneTypeRepr(r->getInner()), r->isMutable());
        if (r->hasLifetime()) cloned->setLifetime(r->getLifetime());
        return cloned;
    }
    case TypeRepr::Kind::Generic: {
        auto *g = static_cast<const GenericTypeRepr *>(type);
        std::vector<std::unique_ptr<TypeRepr>> args;
        for (auto &a : g->getTypeArgs())
            args.push_back(cloneTypeRepr(a.get()));
        return std::make_unique<GenericTypeRepr>(g->getBaseName(), std::move(args));
    }
    default:
        return makePrimitiveType(type->getKind());
    }
}

} // namespace liva
