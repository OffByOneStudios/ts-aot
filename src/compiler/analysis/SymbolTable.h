#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <memory>
#include <optional>
#include "Type.h"

namespace ts {

enum class DeclKind { Var, Let, Const, Function, Class };

struct Symbol {
    std::string name;
    std::shared_ptr<Type> type;
    std::string modulePath; // Module path for top-level variables (empty for non-module symbols)
    DeclKind declKind = DeclKind::Var;
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

    // Define with declaration kind tracking. Returns false if redeclaration conflict.
    bool define(const std::string& name, std::shared_ptr<Type> type, DeclKind kind);

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

    // Get all symbols defined in the global scope
    const std::unordered_map<std::string, std::shared_ptr<Symbol>>& getGlobalSymbols() const;

    // Get all symbols defined in the current (innermost) scope
    const std::unordered_map<std::string, std::shared_ptr<Symbol>>& getCurrentScopeSymbols() const;

    // Get all types defined in the current (innermost) scope
    const std::unordered_map<std::string, std::shared_ptr<Type>>& getCurrentScopeTypes() const;

    // Define a type in the global scope.
    bool defineGlobalType(const std::string& name, std::shared_ptr<Type> type);

    size_t getDepth() const { return scopes.size(); }

private:
    // Stack of scopes. Each scope is a map of name -> Symbol
    std::vector<std::unordered_map<std::string, std::shared_ptr<Symbol>>> scopes;
    // Stack of type scopes. Each scope is a map of name -> Type
    std::vector<std::unordered_map<std::string, std::shared_ptr<Type>>> typeScopes;
};

} // namespace ts
