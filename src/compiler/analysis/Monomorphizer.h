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
    std::shared_ptr<Type> classType; // If this is a class method, the class type
    ast::Node* node; // Pointer to the original AST node (FunctionDeclaration or MethodDefinition)
};

class Analyzer; // Forward declaration

class Monomorphizer {
public:
    Monomorphizer();

    void monomorphize(ast::Program* program, Analyzer& analyzer);

    const std::vector<Specialization>& getSpecializations() const { return specializations; }

    static std::string generateMangledName(const std::string& originalName, const std::vector<std::shared_ptr<Type>>& argTypes);

private:
    std::vector<std::unique_ptr<ast::FunctionDeclaration>> syntheticFunctions;
    std::vector<Specialization> specializations;
    
    ast::FunctionDeclaration* findFunction(ast::Program* program, const std::string& name);
};

} // namespace ts
