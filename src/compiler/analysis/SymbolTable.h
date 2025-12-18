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
    ~SymbolTable();

    // Enter a new scope (e.g. function body, block)
    void enterScope();

    // Exit the current scope
    void exitScope();

    // Define a symbol in the current scope. Returns false if already defined in this scope.
    bool define(const std::string& name, std::shared_ptr<Type> type);

    // Lookup a symbol in the current scope and parent scopes.
    std::shared_ptr<Symbol> lookup(const std::string& name);

    // Update a symbol's type in the scope where it was defined.
    bool update(const std::string& name, std::shared_ptr<Type> type);

    // Define a type in the current scope. Returns false if already defined.
    bool defineType(const std::string& name, std::shared_ptr<Type> type);

    // Lookup a type in the current scope and parent scopes.
    std::shared_ptr<Type> lookupType(const std::string& name) const;

    // Get all types defined in the global scope
    const std::unordered_map<std::string, std::shared_ptr<Type>>& getGlobalTypes() const;

    size_t getDepth() const { return scopes.size(); }

private:
    // Stack of scopes. Each scope is a map of name -> Symbol
    std::vector<std::unordered_map<std::string, std::shared_ptr<Symbol>>> scopes;
    // Stack of type scopes. Each scope is a map of name -> Type
    std::vector<std::unordered_map<std::string, std::shared_ptr<Type>>> typeScopes;
};

} // namespace ts
