#include "Analyzer.h"
#include "../ast/AstNodes.h"
#include <fmt/core.h>
#include <iostream>
#include <set>

namespace ts {
using namespace ast;
void Analyzer::visitNumericLiteral(ast::NumericLiteral* node) {
    if (node->value == (int64_t)node->value) {
        lastType = std::make_shared<Type>(TypeKind::Int);
    } else {
        lastType = std::make_shared<Type>(TypeKind::Double);
    }
    node->inferredType = lastType;
}

void Analyzer::visitBigIntLiteral(ast::BigIntLiteral* node) {
    lastType = std::make_shared<Type>(TypeKind::BigInt);
    node->inferredType = lastType;
}

void Analyzer::visitBooleanLiteral(ast::BooleanLiteral* node) {
    lastType = std::make_shared<Type>(TypeKind::Boolean);
    node->inferredType = lastType;
}

void Analyzer::visitNullLiteral(ast::NullLiteral* node) {
    lastType = std::make_shared<Type>(TypeKind::Null);
    node->inferredType = lastType;
}

void Analyzer::visitUndefinedLiteral(ast::UndefinedLiteral* node) {
    lastType = std::make_shared<Type>(TypeKind::Undefined);
    node->inferredType = lastType;
}

void Analyzer::visitStringLiteral(ast::StringLiteral* node) {
    lastType = std::make_shared<Type>(TypeKind::String);
    node->inferredType = lastType;
}

void Analyzer::visitRegularExpressionLiteral(ast::RegularExpressionLiteral* node) {
    lastType = symbols.lookupType("RegExp");
    if (!lastType) {
        lastType = std::make_shared<Type>(TypeKind::Any);
    }
}

void Analyzer::visitArrayLiteralExpression(ast::ArrayLiteralExpression* node) {
    std::vector<std::shared_ptr<Type>> elementTypes;
    for (auto& el : node->elements) {
        visit(el.get());
        elementTypes.push_back(lastType ? lastType : std::make_shared<Type>(TypeKind::Any));
    }
    
    // Determine if all elements have the same type (homogeneous array)
    // If so, use ArrayType; otherwise use TupleType for heterogeneous arrays
    if (elementTypes.empty()) {
        // Empty array - default to Array<Any>
        lastType = std::make_shared<ArrayType>(std::make_shared<Type>(TypeKind::Any));
    } else {
        bool homogeneous = true;
        auto firstType = elementTypes[0];
        std::string firstTypeStr = firstType->toString();
        for (size_t i = 1; i < elementTypes.size(); ++i) {
            if (elementTypes[i]->toString() != firstTypeStr) {
                homogeneous = false;
                break;
            }
        }
        
        if (homogeneous) {
            // All elements same type -> ArrayType
            lastType = std::make_shared<ArrayType>(firstType);
        } else {
            // Mixed types -> TupleType
            lastType = std::make_shared<TupleType>(elementTypes);
        }
    }
    node->inferredType = lastType;
}

void Analyzer::visitObjectLiteralExpression(ast::ObjectLiteralExpression* node) {
    auto objType = std::make_shared<ObjectType>();
    std::set<std::string> seenProperties; // For strict mode duplicate check

    for (auto& prop : node->properties) {
        visit(prop.get());
        if (auto pa = dynamic_cast<ast::PropertyAssignment*>(prop.get())) {
            // Strict mode: check for duplicate properties
            if (strictMode && seenProperties.count(pa->name)) {
                reportError("Strict mode: Duplicate data property '" + pa->name + "' in object literal");
            }
            seenProperties.insert(pa->name);
            objType->fields[pa->name] = lastType;
        } else if (auto spa = dynamic_cast<ast::ShorthandPropertyAssignment*>(prop.get())) {
            if (strictMode && seenProperties.count(spa->name)) {
                reportError("Strict mode: Duplicate data property '" + spa->name + "' in object literal");
            }
            seenProperties.insert(spa->name);
            objType->fields[spa->name] = lastType;
        } else if (auto md = dynamic_cast<ast::MethodDefinition*>(prop.get())) {
            if (md->isGetter) {
                // Getter: store the return type as a field, and the getter function
                auto funcType = std::dynamic_pointer_cast<FunctionType>(lastType);
                if (funcType) {
                    objType->getters[md->name] = funcType;
                    objType->fields[md->name] = funcType->returnType;
                }
            } else if (md->isSetter) {
                // Setter: store the setter function
                auto funcType = std::dynamic_pointer_cast<FunctionType>(lastType);
                if (funcType) {
                    objType->setters[md->name] = funcType;
                    // Don't overwrite field type if getter already set it
                    if (objType->fields.find(md->name) == objType->fields.end() && !funcType->paramTypes.empty()) {
                        objType->fields[md->name] = funcType->paramTypes[0];
                    }
                }
            } else {
                objType->fields[md->name] = lastType;
            }
        }
    }
    lastType = objType;
    node->inferredType = objType;
}

void Analyzer::visitPropertyAssignment(ast::PropertyAssignment* node) {
    if (node->nameNode) {
        if (dynamic_cast<ast::ComputedPropertyName*>(node->nameNode.get())) {
            visit(node->nameNode.get());
        }
    }
    visit(node->initializer.get());
}

void Analyzer::visitShorthandPropertyAssignment(ast::ShorthandPropertyAssignment* node) {
    auto sym = symbols.lookup(node->name);
    lastType = sym ? sym->type : std::make_shared<Type>(TypeKind::Any);
}

void Analyzer::visitComputedPropertyName(ast::ComputedPropertyName* node) {
    visit(node->expression.get());
}

void Analyzer::visitTemplateExpression(ast::TemplateExpression* node) {
    for (auto& span : node->spans) {
        visit(span.expression.get());
    }
    lastType = std::make_shared<Type>(TypeKind::String);
    node->inferredType = lastType;
}

void Analyzer::visitTaggedTemplateExpression(ast::TaggedTemplateExpression* node) {
    visit(node->tag.get());

    std::vector<std::shared_ptr<Type>> argTypes;
    argTypes.push_back(std::make_shared<ArrayType>(std::make_shared<Type>(TypeKind::String))); // strings array

    // For TemplateExpression (with substitutions), add each expression's type
    // For StringLiteral (no substitutions), don't add anything - only the strings array is passed
    if (auto* templateExpr = dynamic_cast<ast::TemplateExpression*>(node->templateExpr.get())) {
        for (auto& span : templateExpr->spans) {
            visit(span.expression.get());
            argTypes.push_back(span.expression->inferredType);
        }
    }
    // StringLiteral case: no additional args beyond strings array

    if (auto id = dynamic_cast<ast::Identifier*>(node->tag.get())) {
        functionUsages[id->name].push_back({argTypes, {}});
    }

    lastType = std::make_shared<Type>(TypeKind::String);
    node->inferredType = lastType;
}

} // namespace ts


