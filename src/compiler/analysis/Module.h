#pragma once

#include <string>
#include <memory>
#include <map>
#include <vector>
#include <optional>
#include "../ast/AstNodes.h"
#include "SymbolTable.h"
#include "ModuleResolver.h"
#include <nlohmann/json.hpp>

namespace ts {

struct Module {
    std::string path;
    ModuleType type = ModuleType::TypeScript;
    std::shared_ptr<ast::Program> ast;
    std::shared_ptr<SymbolTable> exports;
    bool analyzed = false;
    bool isAsync = false;
    bool isJsonModule = false;
    bool isESM = false;

    // For JSON modules: the parsed JSON content
    std::optional<nlohmann::json> jsonContent;

    // All module-level symbols (including non-exported ones).
    // Used to restore scope when re-analyzing function bodies during monomorphization.
    std::shared_ptr<SymbolTable> moduleSymbols;

    Module() : exports(std::make_shared<SymbolTable>()), moduleSymbols(std::make_shared<SymbolTable>()) {}
};

} // namespace ts
