#include "IRGenerator.h"
#include "BoxingPolicy.h"
#include <spdlog/spdlog.h>

namespace ts {

// Static helper to register readline module's runtime functions once
static bool readlineFunctionsRegistered = false;
static void ensureReadlineFunctionsRegistered(BoxingPolicy& bp) {
    if (readlineFunctionsRegistered) return;
    readlineFunctionsRegistered = true;

    // readline module functions
    bp.registerRuntimeApi("ts_readline_create_interface", {true}, false);  // options boxed, returns boxed

    // Interface methods
    bp.registerRuntimeApi("ts_readline_close", {true}, false);
    bp.registerRuntimeApi("ts_readline_pause", {true}, false);
    bp.registerRuntimeApi("ts_readline_resume", {true}, false);
    bp.registerRuntimeApi("ts_readline_prompt", {true, true}, false);
    bp.registerRuntimeApi("ts_readline_set_prompt", {true, true}, false);
    bp.registerRuntimeApi("ts_readline_question", {true, true, true}, false);
    bp.registerRuntimeApi("ts_readline_write", {true, true}, false);
    bp.registerRuntimeApi("ts_readline_get_line", {true}, false);   // rl boxed, returns raw TsString*
    bp.registerRuntimeApi("ts_readline_get_cursor", {true}, false); // rl boxed, returns raw int64
    bp.registerRuntimeApi("ts_readline_get_prompt", {true}, false); // rl boxed, returns raw TsString*

    // Utility functions
    bp.registerRuntimeApi("ts_readline_clear_line", {true, true}, false);
    bp.registerRuntimeApi("ts_readline_clear_screen_down", {true}, false);
    bp.registerRuntimeApi("ts_readline_cursor_to", {true, true, true}, false);
    bp.registerRuntimeApi("ts_readline_move_cursor", {true, true, true}, false);
    bp.registerRuntimeApi("ts_readline_emit_keypress_events", {true, true}, false);

    // Async iterator support
    bp.registerRuntimeApi("ts_readline_get_async_iterator", {true}, true);  // returns boxed iterator
    bp.registerRuntimeApi("ts_readline_async_iterator_next", {true}, true); // returns boxed promise

    // Signal event emitters
    bp.registerRuntimeApi("ts_readline_emit_sigint", {true}, false);
    bp.registerRuntimeApi("ts_readline_emit_sigtstp", {true}, false);
    bp.registerRuntimeApi("ts_readline_emit_sigcont", {true}, false);
}

bool IRGenerator::tryGenerateReadlineCall(ast::CallExpression* node, ast::PropertyAccessExpression* prop) {
    ensureReadlineFunctionsRegistered(boxingPolicy);

    // Check if this is a readline.* call
    auto id = dynamic_cast<ast::Identifier*>(prop->expression.get());
    if (!id || id->name != "readline") return false;

    SPDLOG_DEBUG("tryGenerateReadlineCall: readline.{}", prop->name);

    if (prop->name == "createInterface") {
        // readline.createInterface(options): Interface
        llvm::Value* options = llvm::ConstantPointerNull::get(builder->getPtrTy());

        if (node->arguments.size() > 0) {
            visit(node->arguments[0].get());
            options = boxValue(lastValue, node->arguments[0]->inferredType);
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
            { builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_readline_create_interface", ft);
        lastValue = createCall(ft, fn.getCallee(), { options });
        return true;
    }

    if (prop->name == "clearLine") {
        // readline.clearLine(stream, dir): boolean
        llvm::Value* stream = llvm::ConstantPointerNull::get(builder->getPtrTy());
        llvm::Value* dir = llvm::ConstantPointerNull::get(builder->getPtrTy());

        if (node->arguments.size() > 0) {
            visit(node->arguments[0].get());
            stream = boxValue(lastValue, node->arguments[0]->inferredType);
        }
        if (node->arguments.size() > 1) {
            visit(node->arguments[1].get());
            dir = boxValue(lastValue, node->arguments[1]->inferredType);
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getVoidTy(),
            { builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_readline_clear_line", ft);
        createCall(ft, fn.getCallee(), { stream, dir });
        lastValue = builder->getTrue();
        return true;
    }

    if (prop->name == "clearScreenDown") {
        // readline.clearScreenDown(stream): boolean
        llvm::Value* stream = llvm::ConstantPointerNull::get(builder->getPtrTy());

        if (node->arguments.size() > 0) {
            visit(node->arguments[0].get());
            stream = boxValue(lastValue, node->arguments[0]->inferredType);
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getVoidTy(),
            { builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_readline_clear_screen_down", ft);
        createCall(ft, fn.getCallee(), { stream });
        lastValue = builder->getTrue();
        return true;
    }

    if (prop->name == "cursorTo") {
        // readline.cursorTo(stream, x, y?): boolean
        llvm::Value* stream = llvm::ConstantPointerNull::get(builder->getPtrTy());
        llvm::Value* x = llvm::ConstantPointerNull::get(builder->getPtrTy());
        llvm::Value* y = llvm::ConstantPointerNull::get(builder->getPtrTy());

        if (node->arguments.size() > 0) {
            visit(node->arguments[0].get());
            stream = boxValue(lastValue, node->arguments[0]->inferredType);
        }
        if (node->arguments.size() > 1) {
            visit(node->arguments[1].get());
            x = boxValue(lastValue, node->arguments[1]->inferredType);
        }
        if (node->arguments.size() > 2) {
            visit(node->arguments[2].get());
            y = boxValue(lastValue, node->arguments[2]->inferredType);
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getVoidTy(),
            { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_readline_cursor_to", ft);
        createCall(ft, fn.getCallee(), { stream, x, y });
        lastValue = builder->getTrue();
        return true;
    }

    if (prop->name == "moveCursor") {
        // readline.moveCursor(stream, dx, dy): boolean
        llvm::Value* stream = llvm::ConstantPointerNull::get(builder->getPtrTy());
        llvm::Value* dx = llvm::ConstantPointerNull::get(builder->getPtrTy());
        llvm::Value* dy = llvm::ConstantPointerNull::get(builder->getPtrTy());

        if (node->arguments.size() > 0) {
            visit(node->arguments[0].get());
            stream = boxValue(lastValue, node->arguments[0]->inferredType);
        }
        if (node->arguments.size() > 1) {
            visit(node->arguments[1].get());
            dx = boxValue(lastValue, node->arguments[1]->inferredType);
        }
        if (node->arguments.size() > 2) {
            visit(node->arguments[2].get());
            dy = boxValue(lastValue, node->arguments[2]->inferredType);
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getVoidTy(),
            { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_readline_move_cursor", ft);
        createCall(ft, fn.getCallee(), { stream, dx, dy });
        lastValue = builder->getTrue();
        return true;
    }

    if (prop->name == "emitKeypressEvents") {
        // readline.emitKeypressEvents(stream, interface?): void
        llvm::Value* stream = llvm::ConstantPointerNull::get(builder->getPtrTy());
        llvm::Value* iface = llvm::ConstantPointerNull::get(builder->getPtrTy());

        if (node->arguments.size() > 0) {
            visit(node->arguments[0].get());
            stream = boxValue(lastValue, node->arguments[0]->inferredType);
        }
        if (node->arguments.size() > 1) {
            visit(node->arguments[1].get());
            iface = boxValue(lastValue, node->arguments[1]->inferredType);
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getVoidTy(),
            { builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_readline_emit_keypress_events", ft);
        createCall(ft, fn.getCallee(), { stream, iface });
        lastValue = getUndefinedValue();
        return true;
    }

    return false;
}

bool IRGenerator::tryGenerateReadlineInterfaceCall(ast::CallExpression* node, ast::PropertyAccessExpression* prop) {
    ensureReadlineFunctionsRegistered(boxingPolicy);

    // Get the object being called on
    auto objectType = prop->expression->inferredType;
    if (!objectType) return false;

    auto classType = std::dynamic_pointer_cast<ClassType>(objectType);
    if (!classType || (classType->name != "Interface" && classType->name != "ReadlineInterface")) return false;

    SPDLOG_DEBUG("tryGenerateReadlineInterfaceCall: rl.{}", prop->name);

    // Get the interface object
    visit(prop->expression.get());
    llvm::Value* rl = lastValue;

    if (prop->name == "close") {
        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getVoidTy(),
            { builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_readline_close", ft);
        createCall(ft, fn.getCallee(), { rl });
        lastValue = getUndefinedValue();
        return true;
    }

    if (prop->name == "pause") {
        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getVoidTy(),
            { builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_readline_pause", ft);
        createCall(ft, fn.getCallee(), { rl });
        lastValue = rl;  // Returns this for chaining
        return true;
    }

    if (prop->name == "resume") {
        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getVoidTy(),
            { builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_readline_resume", ft);
        createCall(ft, fn.getCallee(), { rl });
        lastValue = rl;  // Returns this for chaining
        return true;
    }

    if (prop->name == "prompt") {
        llvm::Value* preserveCursor = llvm::ConstantPointerNull::get(builder->getPtrTy());

        if (node->arguments.size() > 0) {
            visit(node->arguments[0].get());
            preserveCursor = boxValue(lastValue, node->arguments[0]->inferredType);
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getVoidTy(),
            { builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_readline_prompt", ft);
        createCall(ft, fn.getCallee(), { rl, preserveCursor });
        lastValue = getUndefinedValue();
        return true;
    }

    if (prop->name == "setPrompt") {
        llvm::Value* prompt = llvm::ConstantPointerNull::get(builder->getPtrTy());

        if (node->arguments.size() > 0) {
            visit(node->arguments[0].get());
            prompt = boxValue(lastValue, node->arguments[0]->inferredType);
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getVoidTy(),
            { builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_readline_set_prompt", ft);
        createCall(ft, fn.getCallee(), { rl, prompt });
        lastValue = getUndefinedValue();
        return true;
    }

    if (prop->name == "question") {
        llvm::Value* query = llvm::ConstantPointerNull::get(builder->getPtrTy());
        llvm::Value* callback = llvm::ConstantPointerNull::get(builder->getPtrTy());

        if (node->arguments.size() > 0) {
            visit(node->arguments[0].get());
            query = boxValue(lastValue, node->arguments[0]->inferredType);
        }
        if (node->arguments.size() > 1) {
            visit(node->arguments[1].get());
            callback = boxValue(lastValue, node->arguments[1]->inferredType);
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getVoidTy(),
            { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_readline_question", ft);
        createCall(ft, fn.getCallee(), { rl, query, callback });
        lastValue = getUndefinedValue();
        return true;
    }

    if (prop->name == "write") {
        llvm::Value* data = llvm::ConstantPointerNull::get(builder->getPtrTy());

        if (node->arguments.size() > 0) {
            visit(node->arguments[0].get());
            data = boxValue(lastValue, node->arguments[0]->inferredType);
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getVoidTy(),
            { builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_readline_write", ft);
        createCall(ft, fn.getCallee(), { rl, data });
        lastValue = getUndefinedValue();
        return true;
    }

    if (prop->name == "getPrompt") {
        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
            { builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_readline_get_prompt", ft);
        lastValue = createCall(ft, fn.getCallee(), { rl });
        return true;
    }

    return false;
}

} // namespace ts
