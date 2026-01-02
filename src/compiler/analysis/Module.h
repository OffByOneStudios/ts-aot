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
    
    // For JSON modules: the parsed JSON content
    std::optional<nlohmann::json> jsonContent;

    Module() : exports(std::make_shared<SymbolTable>()) {}
};

} // namespace ts
