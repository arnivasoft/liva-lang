#include "liva/Sema/Scope.h"

namespace liva {

bool Scope::declare(const std::string &name, Symbol symbol) {
    auto result = symbols_.emplace(name, std::move(symbol));
    return result.second; // false if already exists
}

Symbol *Scope::lookup(const std::string &name) {
    auto it = symbols_.find(name);
    if (it != symbols_.end())
        return &it->second;
    if (parent_)
        return parent_->lookup(name);
    return nullptr;
}

Symbol *Scope::lookupLocal(const std::string &name) {
    auto it = symbols_.find(name);
    if (it != symbols_.end())
        return &it->second;
    return nullptr;
}

std::unique_ptr<Scope> Scope::createChild() {
    return std::make_unique<Scope>(this);
}

// === ScopeStack ===

ScopeStack::ScopeStack() {
    // Start with a global scope
    scopes_.push_back(std::make_unique<Scope>(nullptr));
}

void ScopeStack::pushScope() {
    auto child = std::make_unique<Scope>(scopes_.back().get());
    scopes_.push_back(std::move(child));
}

void ScopeStack::popScope() {
    if (scopes_.size() > 1)
        scopes_.pop_back();
}

bool ScopeStack::declare(const std::string &name, Symbol symbol) {
    return scopes_.back()->declare(name, std::move(symbol));
}

Symbol *ScopeStack::lookup(const std::string &name) {
    return scopes_.back()->lookup(name);
}

Symbol *ScopeStack::lookupLocal(const std::string &name) {
    return scopes_.back()->lookupLocal(name);
}

} // namespace liva
