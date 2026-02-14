#pragma once

#include "liva/AST/ASTNode.h"
#include "liva/AST/Stmt.h"
#include "liva/AST/Type.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace liva {

class Expr;  // forward declaration for ParamDecl default value

/// Base class for all declarations
class Decl : public ASTNode {
public:
    using ASTNode::ASTNode;

    void setDocComment(std::string comment) { docComment_ = std::move(comment); }
    const std::string &getDocComment() const { return docComment_; }
    bool hasDocComment() const { return !docComment_.empty(); }

    static bool classof(const ASTNode *node) {
        return node->getKind() >= NodeKind::FuncDecl &&
               node->getKind() <= NodeKind::MacroDecl;
    }

private:
    std::string docComment_;
};

/// Function parameter
struct ParamDecl {
    std::string name;
    std::unique_ptr<TypeRepr> type;
    bool isRef = false;
    bool isMutRef = false;
    bool isSelf = false;
    bool isVariadic = false;
    SourceLocation location;
    std::unique_ptr<Expr> defaultValue;  // optional: func foo(x: i32 = 10)
    bool hasDefault() const { return defaultValue != nullptr; }
};

/// Function declaration: func name(params) -> ReturnType { body }
class FuncDecl : public Decl {
public:
    FuncDecl(std::string name, std::vector<ParamDecl> params,
             std::unique_ptr<TypeRepr> returnType, std::unique_ptr<BlockStmt> body,
             bool isPublic, SourceRange range, bool isAsync = false)
        : Decl(NodeKind::FuncDecl, range), name_(std::move(name)),
          params_(std::move(params)), returnType_(std::move(returnType)),
          body_(std::move(body)), isPublic_(isPublic), isAsync_(isAsync) {}

    const std::string &getName() const { return name_; }
    const std::vector<ParamDecl> &getParams() const { return params_; }
    const TypeRepr *getReturnType() const { return returnType_.get(); }
    const BlockStmt *getBody() const { return body_.get(); }
    BlockStmt *getBody() { return body_.get(); }
    bool isPublic() const { return isPublic_; }
    bool isAsync() const { return isAsync_; }
    bool hasBody() const { return body_ != nullptr; }

    /// Check if this is a method (has self parameter)
    bool isMethod() const {
        return !params_.empty() && params_[0].isSelf;
    }

    /// Generic type parameters (e.g. {"T", "U"})
    void setTypeParams(std::vector<std::string> typeParams) {
        typeParams_ = std::move(typeParams);
    }
    const std::vector<std::string> &getTypeParams() const { return typeParams_; }
    bool isGeneric() const { return !typeParams_.empty(); }

    void setTypeParamBounds(std::unordered_map<std::string, std::vector<std::string>> bounds) {
        typeParamBounds_ = std::move(bounds);
    }
    const std::unordered_map<std::string, std::vector<std::string>> &getTypeParamBounds() const {
        return typeParamBounds_;
    }

    static bool classof(const ASTNode *node) {
        return node->getKind() == NodeKind::FuncDecl;
    }

private:
    std::string name_;
    std::vector<ParamDecl> params_;
    std::unique_ptr<TypeRepr> returnType_;
    std::unique_ptr<BlockStmt> body_;
    bool isPublic_;
    bool isAsync_ = false;
    std::vector<std::string> typeParams_;
    std::unordered_map<std::string, std::vector<std::string>> typeParamBounds_;
};

/// Variable declaration: let x: i32 = 42, var y = 10
class VarDecl : public Decl {
public:
    VarDecl(std::string name, std::unique_ptr<TypeRepr> type, std::unique_ptr<Expr> init,
            bool isMutable, SourceRange range, bool isConst = false)
        : Decl(NodeKind::VarDecl, range), name_(std::move(name)), type_(std::move(type)),
          init_(std::move(init)), isMutable_(isMutable), isConst_(isConst) {}

    /// Tuple destructuring constructor: let (x, y) = expr
    VarDecl(std::vector<std::string> destructuredNames, std::unique_ptr<Expr> init,
            bool isMutable, SourceRange range)
        : Decl(NodeKind::VarDecl, range), name_(""), type_(nullptr),
          init_(std::move(init)), isMutable_(isMutable),
          destructuredNames_(std::move(destructuredNames)) {}

    const std::string &getName() const { return name_; }
    const TypeRepr *getType() const { return type_.get(); }
    const Expr *getInit() const { return init_.get(); }
    Expr *getInit() { return init_.get(); }
    bool hasInit() const { return init_ != nullptr; }
    bool isMutable() const { return isMutable_; }
    bool isConst() const { return isConst_; }
    bool hasTypeAnnotation() const { return type_ != nullptr && !type_->isInferred(); }

    bool isDestructured() const { return !destructuredNames_.empty(); }
    const std::vector<std::string> &getDestructuredNames() const { return destructuredNames_; }

    static bool classof(const ASTNode *node) {
        return node->getKind() == NodeKind::VarDecl;
    }

private:
    std::string name_;
    std::unique_ptr<TypeRepr> type_;
    std::unique_ptr<Expr> init_;
    bool isMutable_;
    bool isConst_ = false;
    std::vector<std::string> destructuredNames_;
};

/// Struct field declaration
class FieldDecl : public Decl {
public:
    FieldDecl(std::string name, std::unique_ptr<TypeRepr> type, bool isMutable,
              SourceRange range)
        : Decl(NodeKind::FieldDecl, range), name_(std::move(name)),
          type_(std::move(type)), isMutable_(isMutable) {}

    const std::string &getName() const { return name_; }
    const TypeRepr *getType() const { return type_.get(); }
    bool isMutable() const { return isMutable_; }

    static bool classof(const ASTNode *node) {
        return node->getKind() == NodeKind::FieldDecl;
    }

private:
    std::string name_;
    std::unique_ptr<TypeRepr> type_;
    bool isMutable_;
};

/// Struct declaration
class StructDecl : public Decl {
public:
    StructDecl(std::string name, std::vector<std::unique_ptr<FieldDecl>> fields,
               bool isPublic, SourceRange range)
        : Decl(NodeKind::StructDecl, range), name_(std::move(name)),
          fields_(std::move(fields)), isPublic_(isPublic) {}

    const std::string &getName() const { return name_; }
    const std::vector<std::unique_ptr<FieldDecl>> &getFields() const { return fields_; }
    bool isPublic() const { return isPublic_; }

    /// Generic type parameters (e.g. {"T", "U"})
    void setTypeParams(std::vector<std::string> typeParams) {
        typeParams_ = std::move(typeParams);
    }
    const std::vector<std::string> &getTypeParams() const { return typeParams_; }
    bool isGeneric() const { return !typeParams_.empty(); }

    void setTypeParamBounds(std::unordered_map<std::string, std::vector<std::string>> bounds) {
        typeParamBounds_ = std::move(bounds);
    }
    const std::unordered_map<std::string, std::vector<std::string>> &getTypeParamBounds() const {
        return typeParamBounds_;
    }

    static bool classof(const ASTNode *node) {
        return node->getKind() == NodeKind::StructDecl;
    }

private:
    std::string name_;
    std::vector<std::unique_ptr<FieldDecl>> fields_;
    bool isPublic_;
    std::vector<std::string> typeParams_;
    std::unordered_map<std::string, std::vector<std::string>> typeParamBounds_;
};

/// Enum case declaration
class EnumCaseDecl : public Decl {
public:
    EnumCaseDecl(std::string name, std::vector<std::unique_ptr<TypeRepr>> associatedTypes,
                 SourceRange range)
        : Decl(NodeKind::EnumCaseDecl, range), name_(std::move(name)),
          associatedTypes_(std::move(associatedTypes)) {}

    const std::string &getName() const { return name_; }
    const std::vector<std::unique_ptr<TypeRepr>> &getAssociatedTypes() const {
        return associatedTypes_;
    }
    bool hasAssociatedValues() const { return !associatedTypes_.empty(); }

    static bool classof(const ASTNode *node) {
        return node->getKind() == NodeKind::EnumCaseDecl;
    }

private:
    std::string name_;
    std::vector<std::unique_ptr<TypeRepr>> associatedTypes_;
};

/// Enum declaration
class EnumDecl : public Decl {
public:
    EnumDecl(std::string name, std::vector<std::unique_ptr<EnumCaseDecl>> cases,
             bool isPublic, SourceRange range)
        : Decl(NodeKind::EnumDecl, range), name_(std::move(name)),
          cases_(std::move(cases)), isPublic_(isPublic) {}

    const std::string &getName() const { return name_; }
    const std::vector<std::unique_ptr<EnumCaseDecl>> &getCases() const { return cases_; }
    bool isPublic() const { return isPublic_; }

    static bool classof(const ASTNode *node) {
        return node->getKind() == NodeKind::EnumDecl;
    }

private:
    std::string name_;
    std::vector<std::unique_ptr<EnumCaseDecl>> cases_;
    bool isPublic_;
};

/// Impl block declaration
class ImplDecl : public Decl {
public:
    ImplDecl(std::string typeName, std::string protocolName,
             std::vector<std::unique_ptr<FuncDecl>> methods, SourceRange range)
        : Decl(NodeKind::ImplDecl, range), typeName_(std::move(typeName)),
          protocolName_(std::move(protocolName)), methods_(std::move(methods)) {}

    const std::string &getTypeName() const { return typeName_; }
    const std::string &getProtocolName() const { return protocolName_; }
    bool hasProtocol() const { return !protocolName_.empty(); }
    const std::vector<std::unique_ptr<FuncDecl>> &getMethods() const { return methods_; }

    void setTypeParams(std::vector<std::string> params) { typeParams_ = std::move(params); }
    const std::vector<std::string> &getTypeParams() const { return typeParams_; }
    bool isGeneric() const { return !typeParams_.empty(); }

    void setTypeParamBounds(std::unordered_map<std::string, std::vector<std::string>> bounds) {
        typeParamBounds_ = std::move(bounds);
    }
    const std::unordered_map<std::string, std::vector<std::string>> &getTypeParamBounds() const {
        return typeParamBounds_;
    }

    void setAssociatedTypes(std::unordered_map<std::string, std::string> types) {
        associatedTypes_ = std::move(types);
    }
    const std::unordered_map<std::string, std::string> &getAssociatedTypes() const {
        return associatedTypes_;
    }

    static bool classof(const ASTNode *node) {
        return node->getKind() == NodeKind::ImplDecl;
    }

private:
    std::string typeName_;
    std::string protocolName_;
    std::vector<std::unique_ptr<FuncDecl>> methods_;
    std::vector<std::string> typeParams_;
    std::unordered_map<std::string, std::vector<std::string>> typeParamBounds_;
    std::unordered_map<std::string, std::string> associatedTypes_;
};

/// Protocol (trait) declaration
class ProtocolDecl : public Decl {
public:
    ProtocolDecl(std::string name, std::vector<std::unique_ptr<FuncDecl>> methods,
                 std::vector<std::string> associatedTypes,
                 bool isPublic, SourceRange range)
        : Decl(NodeKind::ProtocolDecl, range), name_(std::move(name)),
          methods_(std::move(methods)), associatedTypes_(std::move(associatedTypes)),
          isPublic_(isPublic) {}

    const std::string &getName() const { return name_; }
    const std::vector<std::unique_ptr<FuncDecl>> &getMethods() const { return methods_; }
    const std::vector<std::string> &getAssociatedTypes() const { return associatedTypes_; }
    bool isPublic() const { return isPublic_; }

    static bool classof(const ASTNode *node) {
        return node->getKind() == NodeKind::ProtocolDecl;
    }

private:
    std::string name_;
    std::vector<std::unique_ptr<FuncDecl>> methods_;
    std::vector<std::string> associatedTypes_;
    bool isPublic_;
};

/// Import declaration
class ImportDecl : public Decl {
public:
    ImportDecl(std::vector<std::string> path, SourceRange range)
        : Decl(NodeKind::ImportDecl, range), path_(std::move(path)) {}

    const std::vector<std::string> &getPath() const { return path_; }

    std::string getPathString() const {
        std::string result;
        for (size_t i = 0; i < path_.size(); ++i) {
            if (i > 0)
                result += "::";
            result += path_[i];
        }
        return result;
    }

    static bool classof(const ASTNode *node) {
        return node->getKind() == NodeKind::ImportDecl;
    }

private:
    std::vector<std::string> path_;
};

/// Type alias declaration: type Name = TargetType
class TypeAliasDecl : public Decl {
public:
    TypeAliasDecl(std::string name, std::unique_ptr<TypeRepr> targetType,
                  bool isPublic, SourceRange range)
        : Decl(NodeKind::TypeAliasDecl, range), name_(std::move(name)),
          targetType_(std::move(targetType)), isPublic_(isPublic) {}

    const std::string &getName() const { return name_; }
    const TypeRepr *getTargetType() const { return targetType_.get(); }
    bool isPublic() const { return isPublic_; }

    static bool classof(const ASTNode *node) {
        return node->getKind() == NodeKind::TypeAliasDecl;
    }

private:
    std::string name_;
    std::unique_ptr<TypeRepr> targetType_;
    bool isPublic_;
};

/// Macro declaration: macro name { (pattern) => { expansion } }
class MacroDecl : public Decl {
public:
    MacroDecl(std::string name, bool isPublic, SourceRange range)
        : Decl(NodeKind::MacroDecl, range), name_(std::move(name)), isPublic_(isPublic) {}

    const std::string &getName() const { return name_; }
    bool isPublic() const { return isPublic_; }

    void setRawSource(std::string src) { rawSource_ = std::move(src); }
    const std::string &getRawSource() const { return rawSource_; }

    static bool classof(const ASTNode *node) {
        return node->getKind() == NodeKind::MacroDecl;
    }

private:
    std::string name_;
    bool isPublic_;
    std::string rawSource_;
};

/// Translation unit - the top-level node representing an entire file
class TranslationUnit {
public:
    void addDeclaration(std::unique_ptr<ASTNode> decl) {
        declarations_.push_back(std::move(decl));
    }

    const std::vector<std::unique_ptr<ASTNode>> &getDeclarations() const {
        return declarations_;
    }

    std::vector<std::unique_ptr<ASTNode>> &getDeclarations() { return declarations_; }

private:
    std::vector<std::unique_ptr<ASTNode>> declarations_;
};

} // namespace liva
