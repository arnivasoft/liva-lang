#pragma once

#include "liva/AST/Type.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace liva {

class FuncDecl;
class StructDecl;

/// Represents a symbol in the symbol table
struct Symbol {
    enum class Kind {
        Variable,
        Function,
        Parameter,
        StructType,
        EnumType,
        Field,
    };

    std::string name;
    Kind kind;
    const TypeRepr *type = nullptr;
    bool isMutable = false;
    bool isPublic = false;

    // For type checking
    const FuncDecl *funcDecl = nullptr;
    const StructDecl *structDecl = nullptr;
};

/// Lexical scope for name resolution
class Scope {
public:
    explicit Scope(Scope *parent = nullptr) : parent_(parent) {}

    /// Declare a new symbol in this scope
    bool declare(const std::string &name, Symbol symbol);

    /// Look up a symbol, searching parent scopes
    Symbol *lookup(const std::string &name);

    /// Look up only in current scope (no parent search)
    Symbol *lookupLocal(const std::string &name);

    Scope *getParent() { return parent_; }
    const Scope *getParent() const { return parent_; }

    /// Create a child scope
    std::unique_ptr<Scope> createChild();

private:
    Scope *parent_;
    std::unordered_map<std::string, Symbol> symbols_;
};

/// Manages a stack of scopes
class ScopeStack {
public:
    ScopeStack();

    void pushScope();
    void popScope();

    bool declare(const std::string &name, Symbol symbol);
    Symbol *lookup(const std::string &name);
    Symbol *lookupLocal(const std::string &name);

    Scope *currentScope() { return scopes_.back().get(); }

private:
    std::vector<std::unique_ptr<Scope>> scopes_;
};

} // namespace liva
