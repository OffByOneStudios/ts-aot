#include "SymbolTable.h"
#include <fmt/core.h>

namespace ts {

SymbolTable::SymbolTable() {
    // Start with a global scope
    enterScope();
}

SymbolTable::~SymbolTable() {
}

void SymbolTable::enterScope() {
    scopes.emplace_back();
    typeScopes.emplace_back();
}

void SymbolTable::exitScope() {
    if (!scopes.empty()) {
        scopes.pop_back();
        typeScopes.pop_back();
    }
}

bool SymbolTable::define(const std::string& name, std::shared_ptr<Type> type) {
    if (scopes.empty()) return false;

    auto& currentScope = scopes.back();
    if (currentScope.find(name) != currentScope.end()) {
        return false; // Already defined in this scope
    }

    auto symbol = std::make_shared<Symbol>();
    symbol->name = name;
    symbol->type = type;
    currentScope[name] = symbol;
    return true;
}

std::shared_ptr<Symbol> SymbolTable::lookup(const std::string& name) {
    // Search from the innermost scope outwards
    for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
        auto found = it->find(name);
        if (found != it->end()) {
            return found->second;
        }
    }
    return nullptr;
}

const std::unordered_map<std::string, std::shared_ptr<Type>>& SymbolTable::getGlobalTypes() const {
    return typeScopes.front();
}

const std::unordered_map<std::string, std::shared_ptr<Symbol>>& SymbolTable::getGlobalSymbols() const {
    return scopes.front();
}

bool SymbolTable::defineGlobalType(const std::string& name, std::shared_ptr<Type> type) {
    if (typeScopes.empty()) return false;
    fmt::print("Defining global type: {}\n", name);
    typeScopes.front()[name] = type;
    return true;
}

bool SymbolTable::defineType(const std::string& name, std::shared_ptr<Type> type) {
    if (typeScopes.empty()) return false;

    auto& currentScope = typeScopes.back();
    if (currentScope.find(name) != currentScope.end()) {
        return false; // Already defined in this scope
    }

    currentScope[name] = type;
    return true;
}

std::shared_ptr<Type> SymbolTable::lookupType(const std::string& name) const {
    // Search from the innermost scope outwards
    for (auto it = typeScopes.rbegin(); it != typeScopes.rend(); ++it) {
        auto found = it->find(name);
        if (found != it->end()) {
            return found->second;
        }
    }
    return nullptr;
}

bool SymbolTable::update(const std::string& name, std::shared_ptr<Type> type) {
    for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
        auto found = it->find(name);
        if (found != it->end()) {
            found->second->type = type;
            return true;
        }
    }
    return false;
}

} // namespace ts
