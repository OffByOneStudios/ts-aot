#pragma once

#include <string>
#include <memory>
#include <map>
#include <vector>
#include "../ast/AstNodes.h"
#include "SymbolTable.h"

namespace ts {

struct Module {
    std::string path;
    std::shared_ptr<ast::Program> ast;
    std::shared_ptr<SymbolTable> exports;
    bool analyzed = false;
    bool isAsync = false;

    Module() : exports(std::make_shared<SymbolTable>()) {}
};

} // namespace ts
