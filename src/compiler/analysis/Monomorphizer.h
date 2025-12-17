#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include "Type.h"
#include "../ast/AstNodes.h"

namespace ts {

struct Specialization {
    std::string originalName;
    std::string specializedName;
    std::vector<std::shared_ptr<Type>> argTypes;
    std::shared_ptr<Type> returnType; // Inferred return type
    ast::FunctionDeclaration* node; // Pointer to the original AST node
};

class Monomorphizer {
public:
    Monomorphizer();

    void monomorphize(ast::Program* program, const std::map<std::string, std::vector<std::vector<std::shared_ptr<Type>>>>& usages);

    const std::vector<Specialization>& getSpecializations() const { return specializations; }

private:
    std::vector<Specialization> specializations;
    
    std::string generateMangledName(const std::string& originalName, const std::vector<std::shared_ptr<Type>>& argTypes);
    ast::FunctionDeclaration* findFunction(ast::Program* program, const std::string& name);
};

} // namespace ts
