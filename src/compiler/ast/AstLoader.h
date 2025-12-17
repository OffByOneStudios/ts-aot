#pragma once
#include "AstNodes.h"
#include <string>
#include <memory>

namespace ast {
    std::unique_ptr<Program> loadAst(const std::string& jsonPath);
}
