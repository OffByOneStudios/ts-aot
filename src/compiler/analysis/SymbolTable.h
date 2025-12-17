#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <memory>
#include <optional>
#include "Type.h"

namespace ts {

struct Symbol {
    std::string name;
    std::shared_ptr<Type> type;
};

class SymbolTable {
public:
    SymbolTable();

    // Enter a new scope (e.g. function body, block)
    void enterScope();

    // Exit the current scope
    void exitScope();

    // Define a symbol in the current scope. Returns false if already defined in this scope.
    bool define(const std::string& name, std::shared_ptr<Type> type);

    // Lookup a symbol in the current scope and parent scopes.
    std::shared_ptr<Symbol> lookup(const std::string& name);

private:
    // Stack of scopes. Each scope is a map of name -> Symbol
    std::vector<std::unordered_map<std::string, std::shared_ptr<Symbol>>> scopes;
};

} // namespace ts
