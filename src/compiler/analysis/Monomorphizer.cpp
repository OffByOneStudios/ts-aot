#include "Monomorphizer.h"
#include <fmt/core.h>

namespace ts {

Monomorphizer::Monomorphizer() {}

void Monomorphizer::monomorphize(ast::Program* program, const std::map<std::string, std::vector<std::vector<std::shared_ptr<Type>>>>& usages) {
    for (const auto& [name, calls] : usages) {
        ast::FunctionDeclaration* funcNode = findFunction(program, name);
        if (!funcNode) continue; // Skip if not a user-defined function (e.g. console.log)

        // Deduplicate signatures
        std::map<std::string, std::vector<std::shared_ptr<Type>>> uniqueSignatures;

        for (const auto& args : calls) {
            std::string mangled = generateMangledName(name, args);
            uniqueSignatures[mangled] = args;
        }

        for (const auto& [mangled, args] : uniqueSignatures) {
            Specialization spec;
            spec.originalName = name;
            spec.specializedName = mangled;
            spec.argTypes = args;
            spec.node = funcNode;
            // TODO: Infer return type based on body and arg types. For now, Void.
            spec.returnType = std::make_shared<Type>(TypeKind::Void); 
            
            specializations.push_back(spec);
        }
    }
}

std::string Monomorphizer::generateMangledName(const std::string& originalName, const std::vector<std::shared_ptr<Type>>& argTypes) {
    std::string name = originalName;
    for (const auto& type : argTypes) {
        name += "_";
        switch (type->kind) {
            case TypeKind::Int: name += "int"; break;
            case TypeKind::Double: name += "dbl"; break;
            case TypeKind::String: name += "str"; break;
            case TypeKind::Boolean: name += "bool"; break;
            case TypeKind::Void: name += "void"; break;
            default: name += "any"; break;
        }
    }
    return name;
}

ast::FunctionDeclaration* Monomorphizer::findFunction(ast::Program* program, const std::string& name) {
    for (const auto& stmt : program->body) {
        if (auto func = dynamic_cast<ast::FunctionDeclaration*>(stmt.get())) {
            if (func->name == name) return func;
        }
    }
    return nullptr;
}

} // namespace ts
