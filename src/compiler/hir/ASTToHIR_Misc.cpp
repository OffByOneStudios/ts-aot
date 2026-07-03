#include "ASTToHIR_Internal.h"

namespace ts::hir {


void ASTToHIR::visitImportDeclaration(ast::ImportDeclaration* node) {
    setSourceLine(node);
    // Type-only imports are erased entirely - no runtime effect
    if (node->isTypeOnly) return;

    // Track named imports from extension modules so we can route their calls
    // through the extension registry instead of treating them as user functions.
    // E.g., `import { join } from 'path'` -> extensionImports_["join"] = {"path", "join"}
    auto& registry = ext::ExtensionRegistry::instance();
    std::string modSpec = node->moduleSpecifier;
    // Strip "node:" prefix
    if (modSpec.size() > 5 && modSpec.substr(0, 5) == "node:") {
        modSpec = modSpec.substr(5);
    }

    if (registry.isRegisteredModule(modSpec) || registry.isRegisteredObject(modSpec)) {
        for (const auto& spec : node->namedImports) {
            // Skip per-specifier type-only imports: import { type Foo, bar } from '...'
            if (spec.isTypeOnly) continue;
            std::string exportedName = spec.propertyName.empty() ? spec.name : spec.propertyName;
            extensionImports_[spec.name] = { modSpec, exportedName };
        }
        if (!node->defaultImport.empty()) {
            extensionImports_[node->defaultImport] = { modSpec, "default" };
        }
    }
}

void ASTToHIR::visitExportDeclaration(ast::ExportDeclaration* node) {
    setSourceLine(node);
    // Exports are handled at module resolution time
    // ExportDeclaration has moduleSpecifier, namedExports, isStarExport, namespaceExport
    // but no direct declaration - nothing to lower at HIR level
}

void ASTToHIR::visitExportAssignment(ast::ExportAssignment* node) {
    setSourceLine(node);
    // `export default <expression>`: evaluate at source position AND store as
    // the module's "default" export. Module-init bodies bind an `exports`
    // local (the preamble creates `exports = module.exports`); store through
    // it so import() namespaces see the value. Outside a module-init body
    // (no `exports` binding), keep the old evaluate-only behavior.
    if (!node->expression) return;
    auto val = boxValueIfNeeded(lowerExpression(node->expression.get()));
    ast::Identifier exportsId;
    exportsId.name = "exports";
    exportsId.inferredType = std::make_shared<ts::Type>(ts::TypeKind::Any);
    if (lookupVariableInfo("exports")) {
        exportsId.accept(this);
        auto exportsVal = lastValue_;
        if (exportsVal && val) {
            builder_.createSetPropStatic(exportsVal, "default", val);
        }
    }
}

void ASTToHIR::visitNamespaceDeclaration(ast::NamespaceDeclaration* node) {
    setSourceLine(node);
    // Namespaces have no runtime code — type-only construct
}

void ASTToHIR::visitImportEqualsDeclaration(ast::ImportEqualsDeclaration* node) {
    setSourceLine(node);
    // Import equals has no runtime code — resolved at analysis time
}

//==============================================================================
// Expression Lowering
//==============================================================================

void ASTToHIR::visitInterfaceDeclaration(ast::InterfaceDeclaration* node) {
    setSourceLine(node);
    // Interfaces are type-only, nothing to generate
}

void ASTToHIR::visitObjectBindingPattern(ast::ObjectBindingPattern* node) {
    setSourceLine(node);
    // Handled during variable declaration
}

void ASTToHIR::visitArrayBindingPattern(ast::ArrayBindingPattern* node) {
    setSourceLine(node);
    // Handled during variable declaration
}

void ASTToHIR::visitBindingElement(ast::BindingElement* node) {
    setSourceLine(node);
    // Handled during variable declaration
}

void ASTToHIR::visitSpreadElement(ast::SpreadElement* node) {
    setSourceLine(node);
    if (node->expression) {
        lastValue_ = lowerExpression(node->expression.get());
    }
}

void ASTToHIR::visitOmittedExpression(ast::OmittedExpression* node) {
    setSourceLine(node);
    lastValue_ = builder_.createConstUndefined();
}

void ASTToHIR::visitTypeAliasDeclaration(ast::TypeAliasDeclaration* node) {
    setSourceLine(node);
    // Type aliases are type-only, nothing to generate
}

// Compile-time constant expression evaluator for enum member initializers.
// Returns {true, value} on success, {false, 0} if expression cannot be evaluated.
std::pair<bool, int64_t> ASTToHIR::constEvalEnumExpr(
    ast::Node* expr, const std::map<std::string, EnumValue>& members,
    const std::string& enumName) {

    if (auto* numLit = dynamic_cast<ast::NumericLiteral*>(expr)) {
        return {true, static_cast<int64_t>(numLit->value)};
    }

    if (auto* binExpr = dynamic_cast<ast::BinaryExpression*>(expr)) {
        auto [lok, lval] = constEvalEnumExpr(binExpr->left.get(), members, enumName);
        auto [rok, rval] = constEvalEnumExpr(binExpr->right.get(), members, enumName);
        if (!lok || !rok) return {false, 0};

        if (binExpr->op == "+") return {true, lval + rval};
        if (binExpr->op == "-") return {true, lval - rval};
        if (binExpr->op == "*") return {true, lval * rval};
        if (binExpr->op == "/" && rval != 0) return {true, lval / rval};
        if (binExpr->op == "%") return {true, lval % rval};
        if (binExpr->op == "<<") return {true, lval << rval};
        if (binExpr->op == ">>") return {true, lval >> rval};
        if (binExpr->op == "|") return {true, lval | rval};
        if (binExpr->op == "&") return {true, lval & rval};
        if (binExpr->op == "^") return {true, lval ^ rval};
        return {false, 0};
    }

    // Identifier referencing another enum member
    if (auto* ident = dynamic_cast<ast::Identifier*>(expr)) {
        auto it = members.find(ident->name);
        if (it != members.end() && !it->second.isString) {
            return {true, it->second.numValue};
        }
        return {false, 0};
    }

    // PropertyAccess: "hello".length or EnumName.Member
    if (auto* propAccess = dynamic_cast<ast::PropertyAccessExpression*>(expr)) {
        if (propAccess->name == "length") {
            if (auto* strLit = dynamic_cast<ast::StringLiteral*>(propAccess->expression.get())) {
                return {true, static_cast<int64_t>(strLit->value.size())};
            }
        }
        // EnumName.Member reference
        if (auto* ident = dynamic_cast<ast::Identifier*>(propAccess->expression.get())) {
            if (ident->name == enumName) {
                auto it = members.find(propAccess->name);
                if (it != members.end() && !it->second.isString) {
                    return {true, it->second.numValue};
                }
            }
        }
        return {false, 0};
    }

    // Math.floor/ceil/round/trunc/abs(expr)
    if (auto* callExpr = dynamic_cast<ast::CallExpression*>(expr)) {
        auto* prop = dynamic_cast<ast::PropertyAccessExpression*>(callExpr->callee.get());
        if (prop) {
            auto* obj = dynamic_cast<ast::Identifier*>(prop->expression.get());
            if (obj && obj->name == "Math" && callExpr->arguments.size() == 1) {
                auto [ok, val] = constEvalEnumExpr(callExpr->arguments[0].get(), members, enumName);
                if (!ok) return {false, 0};
                double dval = static_cast<double>(val);
                // Also handle float literal arguments directly
                if (auto* flit = dynamic_cast<ast::NumericLiteral*>(callExpr->arguments[0].get())) {
                    dval = flit->value;
                }
                if (prop->name == "floor") return {true, static_cast<int64_t>(std::floor(dval))};
                if (prop->name == "ceil") return {true, static_cast<int64_t>(std::ceil(dval))};
                if (prop->name == "round") return {true, static_cast<int64_t>(std::round(dval))};
                if (prop->name == "trunc") return {true, static_cast<int64_t>(std::trunc(dval))};
                if (prop->name == "abs") return {true, static_cast<int64_t>(std::abs(dval))};
            }
        }
        return {false, 0};
    }

    // Unary prefix: -expr, ~expr
    if (auto* prefix = dynamic_cast<ast::PrefixUnaryExpression*>(expr)) {
        auto [ok, val] = constEvalEnumExpr(prefix->operand.get(), members, enumName);
        if (!ok) return {false, 0};
        if (prefix->op == "-") return {true, -val};
        if (prefix->op == "~") return {true, ~val};
        return {false, 0};
    }

    // Parenthesized expression
    if (auto* paren = dynamic_cast<ast::ParenthesizedExpression*>(expr)) {
        return constEvalEnumExpr(paren->expression.get(), members, enumName);
    }

    return {false, 0};
}

void ASTToHIR::visitEnumDeclaration(ast::EnumDeclaration* node) {
    setSourceLine(node);
    // Process enum members and store values
    std::map<std::string, EnumValue> members;
    std::map<int64_t, std::string> reverseMap;
    int64_t autoValue = 0;

    for (auto& member : node->members) {
        EnumValue ev;

        if (member.initializer) {
            // Has an explicit initializer
            if (auto* numLit = dynamic_cast<ast::NumericLiteral*>(member.initializer.get())) {
                ev.isString = false;
                ev.numValue = static_cast<int64_t>(numLit->value);
                autoValue = ev.numValue + 1;
                reverseMap[ev.numValue] = member.name;
            } else if (auto* strLit = dynamic_cast<ast::StringLiteral*>(member.initializer.get())) {
                ev.isString = true;
                ev.strValue = strLit->value;
            } else {
                // Try const-eval for computed initializers
                auto [ok, val] = constEvalEnumExpr(member.initializer.get(), members, node->name);
                if (ok) {
                    ev.isString = false;
                    ev.numValue = val;
                    autoValue = val + 1;
                    reverseMap[ev.numValue] = member.name;
                } else {
                    // Fallback to auto-increment
                    ev.isString = false;
                    ev.numValue = autoValue++;
                    reverseMap[ev.numValue] = member.name;
                }
            }
        } else {
            // Auto-increment numeric value
            ev.isString = false;
            ev.numValue = autoValue++;
            reverseMap[ev.numValue] = member.name;
        }

        members[member.name] = ev;
    }

    enumValues_[node->name] = std::move(members);
    if (!reverseMap.empty()) {
        enumReverseMap_[node->name] = std::move(reverseMap);
    }
}

//==============================================================================
// JSX Lowering
//==============================================================================

// Helper to lower JSX attributes into a props object
std::shared_ptr<HIRValue> ASTToHIR::lowerJsxAttributes(const std::vector<ast::NodePtr>& attributes) {
    // Create a new object for props
    auto propsObj = builder_.createNewObjectDynamic();

    for (const auto& attr : attributes) {
        if (auto* jsxAttr = dynamic_cast<ast::JsxAttribute*>(attr.get())) {
            // Regular attribute: <div name={value} /> or <div name="string" />
            auto propName = builder_.createConstString(jsxAttr->name);
            std::shared_ptr<HIRValue> propValue;

            if (jsxAttr->initializer) {
                // Attribute has a value
                propValue = lowerExpression(jsxAttr->initializer.get());
            } else {
                // Boolean attribute: <div disabled /> means disabled={true}
                propValue = builder_.createConstBool(true);
            }

            builder_.createSetPropDynamic(propsObj, propName, propValue);
        } else if (auto* spreadAttr = dynamic_cast<ast::JsxSpreadAttribute*>(attr.get())) {
            // Spread attribute: <div {...props} />
            // For now, we'll just skip spread attributes (would need Object.assign)
            // A more complete implementation would merge the spread object into props
            if (spreadAttr->expression) {
                // TODO: Implement spread merging with Object.assign
                // For now, just evaluate the expression for side effects
                lowerExpression(spreadAttr->expression.get());
            }
        }
    }

    return propsObj;
}

// Helper to lower JSX children into an array
std::shared_ptr<HIRValue> ASTToHIR::lowerJsxChildren(const std::vector<ast::ExprPtr>& children) {
    // Create a new array for children
    auto childArray = builder_.createNewArrayBoxed(builder_.createConstInt(static_cast<int64_t>(children.size())));

    int64_t index = 0;
    for (const auto& child : children) {
        auto childValue = lowerExpression(child.get());
        auto indexVal = builder_.createConstInt(index++);
        builder_.createSetElem(childArray, indexVal, childValue);
    }

    return childArray;
}

void ASTToHIR::visitJsxElement(ast::JsxElement* node) {
    setSourceLine(node);
    // Lower JSX element: <tagName attributes>children</tagName>
    // Creates an object { type: tagName, props: {...}, children: [...] }

    // Create tag name string
    auto tagName = builder_.createConstString(node->tagName);

    // Lower attributes to props object
    auto props = lowerJsxAttributes(node->attributes);

    // Lower children to array
    auto children = lowerJsxChildren(node->children);

    // Call ts_jsx_create_element(tagName, props, children)
    lastValue_ = builder_.createCall("ts_jsx_create_element", {tagName, props, children}, HIRType::makeObject());
}

void ASTToHIR::visitJsxSelfClosingElement(ast::JsxSelfClosingElement* node) {
    setSourceLine(node);
    // Lower self-closing JSX element: <tagName attributes />
    // Same as JsxElement but with empty children

    // Create tag name string
    auto tagName = builder_.createConstString(node->tagName);

    // Lower attributes to props object
    auto props = lowerJsxAttributes(node->attributes);

    // Create empty children array
    auto children = builder_.createNewArrayBoxed(builder_.createConstInt(0));

    // Call ts_jsx_create_element(tagName, props, children)
    lastValue_ = builder_.createCall("ts_jsx_create_element", {tagName, props, children}, HIRType::makeObject());
}

void ASTToHIR::visitJsxFragment(ast::JsxFragment* node) {
    setSourceLine(node);
    // Lower JSX fragment: <>children</>
    // Fragments have null tagName and no props

    // Null tagName for fragments
    auto tagName = builder_.createConstNull();

    // Empty props object for fragments
    auto props = builder_.createNewObjectDynamic();

    // Lower children to array
    auto children = lowerJsxChildren(node->children);

    // Call ts_jsx_create_element(null, {}, children)
    lastValue_ = builder_.createCall("ts_jsx_create_element", {tagName, props, children}, HIRType::makeObject());
}

void ASTToHIR::visitJsxExpression(ast::JsxExpression* node) {
    setSourceLine(node);
    if (node->expression) {
        lastValue_ = lowerExpression(node->expression.get());
    }
}

void ASTToHIR::visitJsxText(ast::JsxText* node) {
    setSourceLine(node);
    lastValue_ = builder_.createConstString(node->text);
}

}  // namespace ts::hir
