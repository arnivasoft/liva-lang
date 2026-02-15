#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace liva {

/// Represents a type in the Liva type system
class TypeRepr {
public:
    enum class Kind {
        // Primitive types
        Void,
        Bool,
        I8,
        I16,
        I32,
        I64,
        U8,
        U16,
        U32,
        U64,
        F32,
        F64,
        String,

        // Composite types
        Named,       // User-defined type (struct, enum)
        Array,       // [T; N] or [T]
        Reference,   // ref T, ref mut T
        Optional,    // T?
        Result,      // Result<T, E>
        Function,    // (T1, T2) -> T3
        Tuple,       // (T1, T2)
        Generic,     // T<U, V>
        Inferred,    // Type to be inferred by the compiler
        DynProtocol,    // dyn Protocol (trait object)
        AssociatedType, // T.Item
    };

    explicit TypeRepr(Kind kind) : kind_(kind) {}
    virtual ~TypeRepr() = default;

    Kind getKind() const { return kind_; }

    virtual std::string toString() const;

    bool isVoid() const { return kind_ == Kind::Void; }
    bool isBool() const { return kind_ == Kind::Bool; }
    bool isInteger() const;
    bool isSignedInteger() const;
    bool isUnsignedInteger() const;
    bool isFloat() const;
    bool isNumeric() const { return isInteger() || isFloat(); }
    bool isPrimitive() const;
    bool isReference() const { return kind_ == Kind::Reference; }
    bool isInferred() const { return kind_ == Kind::Inferred; }
    bool isDynProtocol() const { return kind_ == Kind::DynProtocol; }

    /// Get the bit width for integer/float types
    int getBitWidth() const;

private:
    Kind kind_;
};

/// Named type reference (struct, enum, generic parameter)
class NamedTypeRepr : public TypeRepr {
public:
    explicit NamedTypeRepr(std::string name)
        : TypeRepr(Kind::Named), name_(std::move(name)) {}

    const std::string &getName() const { return name_; }
    std::string toString() const override { return name_; }

private:
    std::string name_;
};

/// Reference type: ref T or ref mut T
class ReferenceTypeRepr : public TypeRepr {
public:
    ReferenceTypeRepr(std::unique_ptr<TypeRepr> inner, bool isMutable)
        : TypeRepr(Kind::Reference), inner_(std::move(inner)), isMutable_(isMutable) {}

    const TypeRepr *getInner() const { return inner_.get(); }
    bool isMutable() const { return isMutable_; }
    std::string toString() const override;

private:
    std::unique_ptr<TypeRepr> inner_;
    bool isMutable_;
};

/// Array type: [T; N] or [T]
class ArrayTypeRepr : public TypeRepr {
public:
    ArrayTypeRepr(std::unique_ptr<TypeRepr> element, int64_t size = -1)
        : TypeRepr(Kind::Array), element_(std::move(element)), size_(size) {}

    const TypeRepr *getElement() const { return element_.get(); }
    int64_t getSize() const { return size_; }
    bool isDynamic() const { return size_ < 0; }
    std::string toString() const override;

private:
    std::unique_ptr<TypeRepr> element_;
    int64_t size_;
};

/// Function type: (T1, T2) -> T3
class FunctionTypeRepr : public TypeRepr {
public:
    FunctionTypeRepr(std::vector<std::unique_ptr<TypeRepr>> params,
                     std::unique_ptr<TypeRepr> returnType)
        : TypeRepr(Kind::Function), params_(std::move(params)),
          returnType_(std::move(returnType)) {}

    const std::vector<std::unique_ptr<TypeRepr>> &getParams() const { return params_; }
    const TypeRepr *getReturnType() const { return returnType_.get(); }
    std::string toString() const override;

private:
    std::vector<std::unique_ptr<TypeRepr>> params_;
    std::unique_ptr<TypeRepr> returnType_;
};

/// Tuple type: (T1, T2, ...)
class TupleTypeRepr : public TypeRepr {
public:
    explicit TupleTypeRepr(std::vector<std::unique_ptr<TypeRepr>> elements)
        : TypeRepr(Kind::Tuple), elements_(std::move(elements)) {}

    const std::vector<std::unique_ptr<TypeRepr>> &getElements() const { return elements_; }
    size_t getArity() const { return elements_.size(); }
    std::string toString() const override;

private:
    std::vector<std::unique_ptr<TypeRepr>> elements_;
};

/// Generic type: T<U, V>
class GenericTypeRepr : public TypeRepr {
public:
    GenericTypeRepr(std::string baseName, std::vector<std::unique_ptr<TypeRepr>> typeArgs)
        : TypeRepr(Kind::Generic), baseName_(std::move(baseName)),
          typeArgs_(std::move(typeArgs)) {}

    const std::string &getBaseName() const { return baseName_; }
    const std::vector<std::unique_ptr<TypeRepr>> &getTypeArgs() const { return typeArgs_; }
    std::string toString() const override;

private:
    std::string baseName_;
    std::vector<std::unique_ptr<TypeRepr>> typeArgs_;
};

/// Optional type: T?
class OptionalTypeRepr : public TypeRepr {
public:
    explicit OptionalTypeRepr(std::unique_ptr<TypeRepr> inner)
        : TypeRepr(Kind::Optional), inner_(std::move(inner)) {}

    const TypeRepr *getInner() const { return inner_.get(); }
    std::string toString() const override;

private:
    std::unique_ptr<TypeRepr> inner_;
};

/// Result type: Result<T, E>
class ResultTypeRepr : public TypeRepr {
public:
    ResultTypeRepr(std::unique_ptr<TypeRepr> okType, std::unique_ptr<TypeRepr> errType)
        : TypeRepr(Kind::Result), okType_(std::move(okType)), errType_(std::move(errType)) {}
    const TypeRepr *getOkType() const { return okType_.get(); }
    const TypeRepr *getErrType() const { return errType_.get(); }
    std::string toString() const override;
private:
    std::unique_ptr<TypeRepr> okType_;
    std::unique_ptr<TypeRepr> errType_;
};

/// Trait object type: dyn Protocol
class DynProtocolTypeRepr : public TypeRepr {
public:
    explicit DynProtocolTypeRepr(std::string protocolName)
        : TypeRepr(Kind::DynProtocol), protocolName_(std::move(protocolName)) {}

    const std::string &getProtocolName() const { return protocolName_; }
    std::string toString() const override { return "dyn " + protocolName_; }

private:
    std::string protocolName_;
};

/// Associated type reference: T.Item
class AssociatedTypeRepr : public TypeRepr {
public:
    AssociatedTypeRepr(std::string baseName, std::string assocTypeName)
        : TypeRepr(Kind::AssociatedType), baseName_(std::move(baseName)),
          assocTypeName_(std::move(assocTypeName)) {}

    const std::string &getBaseName() const { return baseName_; }
    const std::string &getAssocTypeName() const { return assocTypeName_; }
    std::string toString() const override { return baseName_ + "." + assocTypeName_; }

private:
    std::string baseName_;
    std::string assocTypeName_;
};

/// Helper to create common types
std::unique_ptr<TypeRepr> makeVoidType();
std::unique_ptr<TypeRepr> makeBoolType();
std::unique_ptr<TypeRepr> makeI32Type();
std::unique_ptr<TypeRepr> makeI64Type();
std::unique_ptr<TypeRepr> makeF64Type();
std::unique_ptr<TypeRepr> makeStringType();
std::unique_ptr<TypeRepr> makeInferredType();
std::unique_ptr<TypeRepr> makeNamedType(const std::string &name);
std::unique_ptr<TypeRepr> makePrimitiveType(TypeRepr::Kind kind);
std::unique_ptr<TypeRepr> makeDynProtocolType(const std::string &protocolName);
std::unique_ptr<TypeRepr> makeAssociatedType(const std::string &base, const std::string &assoc);

/// Deep-clone a TypeRepr tree
std::unique_ptr<TypeRepr> cloneTypeRepr(const TypeRepr *type);

} // namespace liva
