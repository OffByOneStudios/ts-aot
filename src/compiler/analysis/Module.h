#pragma once

#include <string>
#include <memory>
#include <map>
#include <vector>
#include <optional>
#include <set>
#include <map>
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

    // ES ResolveExport link-error validation (dynamic import() must REJECT
    // with SyntaxError instead of resolving). Empty linkError = healthy.
    // The re* fields record the module's export entries in spec form so
    // circular indirect exports and ambiguous star-exports are detectable
    // (the eager symbol-copy in visitExportDeclaration loses that shape).
    std::string linkError;
    std::set<std::string> reDirectExports;                 // locally declared exports
    std::map<std::string, std::pair<std::string, std::string>> reNamedIndirect; // name -> (srcPath, srcName)
    std::vector<std::string> reStarSources;                // `export * from` source paths

    Module() : exports(std::make_shared<SymbolTable>()), moduleSymbols(std::make_shared<SymbolTable>()) {}
};

} // namespace ts
