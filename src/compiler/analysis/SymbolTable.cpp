#include "SymbolTable.h"

namespace ts {

SymbolTable::SymbolTable() {
    // Start with a global scope
    enterScope();
}

void SymbolTable::enterScope() {
    scopes.emplace_back();
}

void SymbolTable::exitScope() {
    if (!scopes.empty()) {
        scopes.pop_back();
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

} // namespace ts
