#include "IRGenerator.h"
#include "../ast/AstNodes.h"

namespace ts {

// JSX is transformed to createElement calls at compile time
// <div className="foo">Hello</div>
// becomes: createElement("div", { className: "foo" }, "Hello")

void IRGenerator::visitJsxElement(ast::JsxElement* node) {
    // Build createElement call:
    // createElement(tagName, props, ...children)

    // 1. Get tag name as string
    auto ft_str = llvm::FunctionType::get(builder->getPtrTy(), {builder->getPtrTy()}, false);
    auto fn_str = module->getOrInsertFunction("ts_string_create", ft_str);
    llvm::Value* tagNameStr = builder->CreateGlobalStringPtr(node->tagName, "jsx_tag");
    llvm::Value* tagName = createCall(ft_str, fn_str.getCallee(), {tagNameStr});

    // 2. Build props object from attributes - use ts_object_create(nullptr) for empty object
    auto ft_obj = llvm::FunctionType::get(builder->getPtrTy(), {builder->getPtrTy()}, false);
    auto fn_obj = module->getOrInsertFunction("ts_object_create", ft_obj);
    llvm::Value* nullProto = llvm::ConstantPointerNull::get(builder->getPtrTy());
    llvm::Value* props = createCall(ft_obj, fn_obj.getCallee(), {nullProto});

    for (auto& attr : node->attributes) {
        if (auto* jsxAttr = dynamic_cast<ast::JsxAttribute*>(attr.get())) {
            // Set property: props[name] = value using ts_object_set_dynamic
            llvm::Value* keyStr = builder->CreateGlobalStringPtr(jsxAttr->name, "attr_key");
            llvm::Value* key = createCall(ft_str, fn_str.getCallee(), {keyStr});
            // Box the key as a string value
            auto ft_make_str = llvm::FunctionType::get(builder->getPtrTy(), {builder->getPtrTy()}, false);
            auto fn_make_str = module->getOrInsertFunction("ts_value_make_string", ft_make_str);
            llvm::Value* boxedKey = createCall(ft_make_str, fn_make_str.getCallee(), {key});

            llvm::Value* value = nullptr;
            if (jsxAttr->initializer) {
                visit(jsxAttr->initializer.get());
                value = lastValue;
                // Box the value if it's a string (visitStringLiteral returns raw TsString*)
                // ts_object_set_dynamic expects boxed TsValue* for all arguments
                if (!boxedValues.count(value)) {
                    // Check if the initializer is a string literal - box it
                    if (dynamic_cast<ast::StringLiteral*>(jsxAttr->initializer.get())) {
                        value = createCall(ft_make_str, fn_make_str.getCallee(), {value});
                    } else {
                        // For other types, use generic boxing
                        auto ft_box = llvm::FunctionType::get(builder->getPtrTy(), {builder->getPtrTy()}, false);
                        auto fn_box = module->getOrInsertFunction("ts_value_box_any", ft_box);
                        value = createCall(ft_box, fn_box.getCallee(), {value});
                    }
                }
            } else {
                // Boolean attribute (no value means true)
                auto ft_bool = llvm::FunctionType::get(builder->getPtrTy(), {builder->getInt1Ty()}, false);
                auto fn_bool = module->getOrInsertFunction("ts_value_make_bool", ft_bool);
                value = createCall(ft_bool, fn_bool.getCallee(), {builder->getInt1(true)});
            }

            auto ft_set = llvm::FunctionType::get(builder->getVoidTy(),
                {builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy()}, false);
            auto fn_set = module->getOrInsertFunction("ts_object_set_dynamic", ft_set);
            createCall(ft_set, fn_set.getCallee(), {props, boxedKey, value});
        } else if (auto* spreadAttr = dynamic_cast<ast::JsxSpreadAttribute*>(attr.get())) {
            // Object.assign(props, spreadAttr.expression)
            visit(spreadAttr->expression.get());
            llvm::Value* spreadObj = lastValue;

            auto ft_assign = llvm::FunctionType::get(builder->getPtrTy(),
                {builder->getPtrTy(), builder->getPtrTy()}, false);
            auto fn_assign = module->getOrInsertFunction("ts_object_assign", ft_assign);
            props = createCall(ft_assign, fn_assign.getCallee(), {props, spreadObj});
        }
    }

    // 3. Build children array
    auto ft_arr = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
    auto fn_arr = module->getOrInsertFunction("ts_array_create", ft_arr);
    llvm::Value* children = createCall(ft_arr, fn_arr.getCallee(), {});

    auto ft_push = llvm::FunctionType::get(builder->getInt64Ty(),
        {builder->getPtrTy(), builder->getPtrTy()}, false);
    auto fn_push = module->getOrInsertFunction("ts_array_push", ft_push);

    for (auto& child : node->children) {
        visit(child.get());
        createCall(ft_push, fn_push.getCallee(), {children, lastValue});
    }

    // 4. Call ts_jsx_create_element(tagName, props, children)
    auto ft_create = llvm::FunctionType::get(builder->getPtrTy(),
        {builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy()}, false);
    auto fn_create = module->getOrInsertFunction("ts_jsx_create_element", ft_create);
    lastValue = createCall(ft_create, fn_create.getCallee(), {tagName, props, children});
    boxedValues.insert(lastValue);
}

void IRGenerator::visitJsxSelfClosingElement(ast::JsxSelfClosingElement* node) {
    // Same as JsxElement but with no children

    // 1. Get tag name as string
    auto ft_str = llvm::FunctionType::get(builder->getPtrTy(), {builder->getPtrTy()}, false);
    auto fn_str = module->getOrInsertFunction("ts_string_create", ft_str);
    llvm::Value* tagNameStr = builder->CreateGlobalStringPtr(node->tagName, "jsx_tag");
    llvm::Value* tagName = createCall(ft_str, fn_str.getCallee(), {tagNameStr});

    // 2. Build props object - use ts_object_create(nullptr)
    auto ft_obj = llvm::FunctionType::get(builder->getPtrTy(), {builder->getPtrTy()}, false);
    auto fn_obj = module->getOrInsertFunction("ts_object_create", ft_obj);
    llvm::Value* nullProto = llvm::ConstantPointerNull::get(builder->getPtrTy());
    llvm::Value* props = createCall(ft_obj, fn_obj.getCallee(), {nullProto});

    for (auto& attr : node->attributes) {
        if (auto* jsxAttr = dynamic_cast<ast::JsxAttribute*>(attr.get())) {
            llvm::Value* keyStr = builder->CreateGlobalStringPtr(jsxAttr->name, "attr_key");
            llvm::Value* key = createCall(ft_str, fn_str.getCallee(), {keyStr});
            // Box the key
            auto ft_make_str = llvm::FunctionType::get(builder->getPtrTy(), {builder->getPtrTy()}, false);
            auto fn_make_str = module->getOrInsertFunction("ts_value_make_string", ft_make_str);
            llvm::Value* boxedKey = createCall(ft_make_str, fn_make_str.getCallee(), {key});

            llvm::Value* value = nullptr;
            if (jsxAttr->initializer) {
                visit(jsxAttr->initializer.get());
                value = lastValue;
                // Box the value if it's a string (visitStringLiteral returns raw TsString*)
                // ts_object_set_dynamic expects boxed TsValue* for all arguments
                if (!boxedValues.count(value)) {
                    // Check if the initializer is a string literal - box it
                    if (dynamic_cast<ast::StringLiteral*>(jsxAttr->initializer.get())) {
                        value = createCall(ft_make_str, fn_make_str.getCallee(), {value});
                    } else {
                        // For other types, use generic boxing
                        auto ft_box = llvm::FunctionType::get(builder->getPtrTy(), {builder->getPtrTy()}, false);
                        auto fn_box = module->getOrInsertFunction("ts_value_box_any", ft_box);
                        value = createCall(ft_box, fn_box.getCallee(), {value});
                    }
                }
            } else {
                auto ft_bool = llvm::FunctionType::get(builder->getPtrTy(), {builder->getInt1Ty()}, false);
                auto fn_bool = module->getOrInsertFunction("ts_value_make_bool", ft_bool);
                value = createCall(ft_bool, fn_bool.getCallee(), {builder->getInt1(true)});
            }

            auto ft_set = llvm::FunctionType::get(builder->getVoidTy(),
                {builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy()}, false);
            auto fn_set = module->getOrInsertFunction("ts_object_set_dynamic", ft_set);
            createCall(ft_set, fn_set.getCallee(), {props, boxedKey, value});
        } else if (auto* spreadAttr = dynamic_cast<ast::JsxSpreadAttribute*>(attr.get())) {
            visit(spreadAttr->expression.get());
            llvm::Value* spreadObj = lastValue;

            auto ft_assign = llvm::FunctionType::get(builder->getPtrTy(),
                {builder->getPtrTy(), builder->getPtrTy()}, false);
            auto fn_assign = module->getOrInsertFunction("ts_object_assign", ft_assign);
            props = createCall(ft_assign, fn_assign.getCallee(), {props, spreadObj});
        }
    }

    // 3. Empty children array
    auto ft_arr = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
    auto fn_arr = module->getOrInsertFunction("ts_array_create", ft_arr);
    llvm::Value* children = createCall(ft_arr, fn_arr.getCallee(), {});

    // 4. Call ts_jsx_create_element
    auto ft_create = llvm::FunctionType::get(builder->getPtrTy(),
        {builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy()}, false);
    auto fn_create = module->getOrInsertFunction("ts_jsx_create_element", ft_create);
    lastValue = createCall(ft_create, fn_create.getCallee(), {tagName, props, children});
    boxedValues.insert(lastValue);
}

void IRGenerator::visitJsxFragment(ast::JsxFragment* node) {
    // Fragments are createElement(Fragment, null, ...children)
    // For now, we'll use a special "Fragment" tag

    auto ft_str = llvm::FunctionType::get(builder->getPtrTy(), {builder->getPtrTy()}, false);
    auto fn_str = module->getOrInsertFunction("ts_string_create", ft_str);
    llvm::Value* tagNameStr = builder->CreateGlobalStringPtr("Fragment", "jsx_fragment");
    llvm::Value* tagName = createCall(ft_str, fn_str.getCallee(), {tagNameStr});

    // No props for fragments
    llvm::Value* props = llvm::ConstantPointerNull::get(builder->getPtrTy());

    // Build children array
    auto ft_arr = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
    auto fn_arr = module->getOrInsertFunction("ts_array_create", ft_arr);
    llvm::Value* children = createCall(ft_arr, fn_arr.getCallee(), {});

    auto ft_push = llvm::FunctionType::get(builder->getInt64Ty(),
        {builder->getPtrTy(), builder->getPtrTy()}, false);
    auto fn_push = module->getOrInsertFunction("ts_array_push", ft_push);

    for (auto& child : node->children) {
        visit(child.get());
        createCall(ft_push, fn_push.getCallee(), {children, lastValue});
    }

    // Call createElement
    auto ft_create = llvm::FunctionType::get(builder->getPtrTy(),
        {builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy()}, false);
    auto fn_create = module->getOrInsertFunction("ts_jsx_create_element", ft_create);
    lastValue = createCall(ft_create, fn_create.getCallee(), {tagName, props, children});
    boxedValues.insert(lastValue);
}

void IRGenerator::visitJsxExpression(ast::JsxExpression* node) {
    if (node->expression) {
        visit(node->expression.get());
        // lastValue is already set
    } else {
        // Empty expression {} returns undefined
        lastValue = getUndefinedValue();
    }
}

void IRGenerator::visitJsxText(ast::JsxText* node) {
    // Convert text to string
    auto ft_str = llvm::FunctionType::get(builder->getPtrTy(), {builder->getPtrTy()}, false);
    auto fn_str = module->getOrInsertFunction("ts_string_create", ft_str);
    llvm::Value* textStr = builder->CreateGlobalStringPtr(node->text, "jsx_text");
    lastValue = createCall(ft_str, fn_str.getCallee(), {textStr});
    boxedValues.insert(lastValue);
}

} // namespace ts
