#include "Monomorphizer.h"
#include "Analyzer.h"
#include <fmt/core.h>

namespace ts {

Monomorphizer::Monomorphizer() {}

void Monomorphizer::monomorphize(ast::Program* program, Analyzer& analyzer) {
    const auto& usages = analyzer.getFunctionUsages();
    
    // Extract top-level code into user_main
    auto userMain = std::make_unique<ast::FunctionDeclaration>();
    userMain->name = "user_main";
    
    std::vector<std::unique_ptr<ast::Statement>> newProgramBody;
    bool hasTopLevel = false;
    
    for (auto& stmt : program->body) {
        if (stmt->getKind() == "FunctionDeclaration" || 
            stmt->getKind() == "ClassDeclaration" ||
            stmt->getKind() == "InterfaceDeclaration") {
            newProgramBody.push_back(std::move(stmt));
        } else {
            userMain->body.push_back(std::move(stmt));
            hasTopLevel = true;
        }
    }
    program->body = std::move(newProgramBody);
    
    if (hasTopLevel) {
        Specialization spec;
        spec.originalName = "user_main";
        spec.specializedName = "user_main";
        spec.argTypes = {};
        spec.returnType = std::make_shared<Type>(TypeKind::Void);
        spec.node = userMain.get();
        
        specializations.push_back(spec);
        syntheticFunctions.push_back(std::move(userMain));
    }

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
            
            // Infer return type
            spec.returnType = analyzer.analyzeFunctionBody(funcNode, args);
            
            specializations.push_back(spec);
        }
    }

    // Always ensure main is monomorphized
    ast::FunctionDeclaration* mainFunc = findFunction(program, "main");
    if (mainFunc) {
        bool processed = false;
        for (const auto& spec : specializations) {
            if (spec.originalName == "main") {
                processed = true;
                break;
            }
        }

        if (!processed) {
            Specialization spec;
            spec.originalName = "main";
            spec.specializedName = "user_main";
            spec.argTypes = {};
            spec.node = mainFunc;
            
            spec.returnType = analyzer.analyzeFunctionBody(mainFunc, {});
            
            // Force main to return Void if it was inferred as Any (default from parser)
            // This ensures we generate a ret void instruction.
            if (spec.returnType->kind == TypeKind::Any) {
                spec.returnType = std::make_shared<Type>(TypeKind::Void);
            }
            
            specializations.push_back(spec);
        }
    }

    // Process Class Methods
    for (const auto& stmt : program->body) {
        if (auto cls = dynamic_cast<ast::ClassDeclaration*>(stmt.get())) {
            auto classType = analyzer.getSymbolTable().lookupType(cls->name);
            if (!classType || classType->kind != TypeKind::Class) continue;
            auto ct = std::static_pointer_cast<ClassType>(classType);

            for (const auto& member : cls->members) {
                if (auto method = dynamic_cast<ast::MethodDefinition*>(member.get())) {
                    Specialization spec;
                    spec.originalName = method->name;
                    spec.node = method;
                    spec.classType = ct;

                    if (method->isStatic) {
                        spec.specializedName = cls->name + "_static_" + method->name;
                        if (ct->staticMethods.count(method->name)) {
                            auto methodType = ct->staticMethods[method->name];
                            spec.argTypes = methodType->paramTypes;
                            spec.returnType = methodType->returnType;
                        } else {
                            spec.returnType = std::make_shared<Type>(TypeKind::Void);
                        }
                    } else {
                        spec.specializedName = cls->name + "_" + method->name;
                        // Construct argTypes: [ClassType, explicitParams...]
                        spec.argTypes.push_back(ct);
                        
                        if (ct->methods.count(method->name)) {
                            auto methodType = ct->methods[method->name];
                            spec.argTypes.insert(spec.argTypes.end(), 
                                methodType->paramTypes.begin(), methodType->paramTypes.end());
                            spec.returnType = methodType->returnType;
                        } else {
                            spec.returnType = std::make_shared<Type>(TypeKind::Void);
                        }
                    }

                    specializations.push_back(spec);
                }
            }
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
            case TypeKind::Class: 
                name += std::static_pointer_cast<ClassType>(type)->name; 
                break;
            case TypeKind::Interface:
                name += std::static_pointer_cast<InterfaceType>(type)->name;
                break;
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
