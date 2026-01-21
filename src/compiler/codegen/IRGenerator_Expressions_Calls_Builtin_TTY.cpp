#include "IRGenerator.h"
#include "BoxingPolicy.h"
#include <spdlog/spdlog.h>

namespace ts {

// Static helper to register TTY module's runtime functions once
static bool ttyFunctionsRegistered = false;
static void ensureTTYFunctionsRegistered(BoxingPolicy& bp) {
    if (ttyFunctionsRegistered) return;
    ttyFunctionsRegistered = true;

    // TTY module functions
    bp.registerRuntimeApi("ts_tty_isatty", {false}, false);  // fd -> bool

    // ReadStream
    bp.registerRuntimeApi("ts_tty_read_stream_create", {false}, true);           // fd -> boxed stream
    bp.registerRuntimeApi("ts_tty_read_stream_get_is_tty", {true}, false);       // stream -> bool
    bp.registerRuntimeApi("ts_tty_read_stream_get_is_raw", {true}, false);       // stream -> bool
    bp.registerRuntimeApi("ts_tty_read_stream_set_raw_mode", {true, false}, false); // stream, mode -> bool

    // WriteStream
    bp.registerRuntimeApi("ts_tty_write_stream_create", {false}, true);          // fd -> boxed stream
    bp.registerRuntimeApi("ts_tty_write_stream_get_is_tty", {true}, false);      // stream -> bool
    bp.registerRuntimeApi("ts_tty_write_stream_get_columns", {true}, false);     // stream -> int
    bp.registerRuntimeApi("ts_tty_write_stream_get_rows", {true}, false);        // stream -> int
    bp.registerRuntimeApi("ts_tty_write_stream_get_window_size", {true}, true);  // stream -> boxed array
    bp.registerRuntimeApi("ts_tty_write_stream_clear_line", {true, false}, false);       // stream, dir -> bool
    bp.registerRuntimeApi("ts_tty_write_stream_clear_screen_down", {true}, false);       // stream -> bool
    bp.registerRuntimeApi("ts_tty_write_stream_cursor_to", {true, false, false}, false); // stream, x, y -> bool
    bp.registerRuntimeApi("ts_tty_write_stream_move_cursor", {true, false, false}, false); // stream, dx, dy -> bool
    bp.registerRuntimeApi("ts_tty_write_stream_get_color_depth", {true}, false); // stream -> int
    bp.registerRuntimeApi("ts_tty_write_stream_has_colors", {true, false}, false); // stream, count -> bool
    bp.registerRuntimeApi("ts_tty_write_stream_write", {true, true}, false);     // stream, data -> bool
}

bool IRGenerator::tryGenerateTTYCall(ast::CallExpression* node, ast::PropertyAccessExpression* prop) {
    ensureTTYFunctionsRegistered(boxingPolicy);

    // Check if this is a tty.* call
    auto id = dynamic_cast<ast::Identifier*>(prop->expression.get());
    if (!id || id->name != "tty") return false;

    SPDLOG_DEBUG("tryGenerateTTYCall: tty.{}", prop->name);

    if (prop->name == "isatty") {
        // tty.isatty(fd): boolean
        if (node->arguments.size() < 1) {
            lastValue = builder->getFalse();
            return true;
        }

        visit(node->arguments[0].get());
        llvm::Value* fd = lastValue;

        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt1Ty(),
            { builder->getInt64Ty() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_tty_isatty", ft);
        lastValue = createCall(ft, fn.getCallee(), { fd });
        return true;
    }

    return false;
}

bool IRGenerator::tryGenerateTTYReadStreamCall(ast::CallExpression* node, ast::PropertyAccessExpression* prop) {
    ensureTTYFunctionsRegistered(boxingPolicy);

    // Get the object being called on
    auto objectType = prop->expression->inferredType;
    if (!objectType) return false;

    auto classType = std::dynamic_pointer_cast<ClassType>(objectType);
    if (!classType || classType->name != "TTYReadStream") return false;

    SPDLOG_DEBUG("tryGenerateTTYReadStreamCall: stream.{}", prop->name);

    // Get the stream object
    visit(prop->expression.get());
    llvm::Value* stream = lastValue;

    // Ensure stream is boxed for runtime calls
    if (!boxedValues.count(stream)) {
        stream = boxValue(stream, objectType);
    }

    if (prop->name == "setRawMode") {
        // stream.setRawMode(mode: boolean): this
        llvm::Value* mode = builder->getFalse();

        if (node->arguments.size() > 0) {
            visit(node->arguments[0].get());
            mode = lastValue;
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt1Ty(),
            { builder->getPtrTy(), builder->getInt1Ty() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_tty_read_stream_set_raw_mode", ft);
        createCall(ft, fn.getCallee(), { stream, mode });
        // Return stream for chaining
        lastValue = stream;
        boxedValues.insert(lastValue);
        return true;
    }

    return false;
}

bool IRGenerator::tryGenerateTTYWriteStreamCall(ast::CallExpression* node, ast::PropertyAccessExpression* prop) {
    ensureTTYFunctionsRegistered(boxingPolicy);

    // Get the object being called on
    auto objectType = prop->expression->inferredType;
    if (!objectType) return false;

    auto classType = std::dynamic_pointer_cast<ClassType>(objectType);
    if (!classType || classType->name != "TTYWriteStream") return false;

    SPDLOG_DEBUG("tryGenerateTTYWriteStreamCall: stream.{}", prop->name);

    // Get the stream object
    visit(prop->expression.get());
    llvm::Value* stream = lastValue;

    // Ensure stream is boxed for runtime calls
    if (!boxedValues.count(stream)) {
        stream = boxValue(stream, objectType);
    }

    if (prop->name == "getWindowSize") {
        // stream.getWindowSize(): [number, number]
        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
            { builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_tty_write_stream_get_window_size", ft);
        lastValue = createCall(ft, fn.getCallee(), { stream });
        boxedValues.insert(lastValue);
        return true;
    }

    if (prop->name == "clearLine") {
        // stream.clearLine(dir: number): boolean
        llvm::Value* dir = llvm::ConstantInt::get(builder->getInt64Ty(), 1);  // default: clear right

        if (node->arguments.size() > 0) {
            visit(node->arguments[0].get());
            dir = lastValue;
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt1Ty(),
            { builder->getPtrTy(), builder->getInt64Ty() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_tty_write_stream_clear_line", ft);
        lastValue = createCall(ft, fn.getCallee(), { stream, dir });
        return true;
    }

    if (prop->name == "clearScreenDown") {
        // stream.clearScreenDown(): boolean
        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt1Ty(),
            { builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_tty_write_stream_clear_screen_down", ft);
        lastValue = createCall(ft, fn.getCallee(), { stream });
        return true;
    }

    if (prop->name == "cursorTo") {
        // stream.cursorTo(x: number, y?: number): boolean
        llvm::Value* x = llvm::ConstantInt::get(builder->getInt64Ty(), 0);
        llvm::Value* y = llvm::ConstantInt::getSigned(builder->getInt64Ty(), -1);  // -1 means no y

        if (node->arguments.size() > 0) {
            visit(node->arguments[0].get());
            x = lastValue;
        }
        if (node->arguments.size() > 1) {
            visit(node->arguments[1].get());
            y = lastValue;
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt1Ty(),
            { builder->getPtrTy(), builder->getInt64Ty(), builder->getInt64Ty() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_tty_write_stream_cursor_to", ft);
        lastValue = createCall(ft, fn.getCallee(), { stream, x, y });
        return true;
    }

    if (prop->name == "moveCursor") {
        // stream.moveCursor(dx: number, dy: number): boolean
        llvm::Value* dx = llvm::ConstantInt::get(builder->getInt64Ty(), 0);
        llvm::Value* dy = llvm::ConstantInt::get(builder->getInt64Ty(), 0);

        if (node->arguments.size() > 0) {
            visit(node->arguments[0].get());
            dx = lastValue;
        }
        if (node->arguments.size() > 1) {
            visit(node->arguments[1].get());
            dy = lastValue;
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt1Ty(),
            { builder->getPtrTy(), builder->getInt64Ty(), builder->getInt64Ty() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_tty_write_stream_move_cursor", ft);
        lastValue = createCall(ft, fn.getCallee(), { stream, dx, dy });
        return true;
    }

    if (prop->name == "getColorDepth") {
        // stream.getColorDepth(): number
        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt64Ty(),
            { builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_tty_write_stream_get_color_depth", ft);
        lastValue = createCall(ft, fn.getCallee(), { stream });
        return true;
    }

    if (prop->name == "hasColors") {
        // stream.hasColors(count?: number): boolean
        llvm::Value* count = llvm::ConstantInt::get(builder->getInt64Ty(), 16);  // default 16 colors

        if (node->arguments.size() > 0) {
            visit(node->arguments[0].get());
            count = lastValue;
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt1Ty(),
            { builder->getPtrTy(), builder->getInt64Ty() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_tty_write_stream_has_colors", ft);
        lastValue = createCall(ft, fn.getCallee(), { stream, count });
        return true;
    }

    if (prop->name == "write") {
        // stream.write(data: string | Buffer): boolean
        if (node->arguments.size() < 1) {
            lastValue = builder->getFalse();
            return true;
        }

        visit(node->arguments[0].get());
        llvm::Value* data = boxValue(lastValue, node->arguments[0]->inferredType);

        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt1Ty(),
            { builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_tty_write_stream_write", ft);
        lastValue = createCall(ft, fn.getCallee(), { stream, data });
        return true;
    }

    return false;
}

bool IRGenerator::tryGenerateTTYNewExpression(ast::NewExpression* node) {
    ensureTTYFunctionsRegistered(boxingPolicy);

    // Check for tty.ReadStream or tty.WriteStream
    auto prop = dynamic_cast<ast::PropertyAccessExpression*>(node->expression.get());
    if (!prop) return false;

    auto id = dynamic_cast<ast::Identifier*>(prop->expression.get());
    if (!id || id->name != "tty") return false;

    if (prop->name == "ReadStream") {
        // new tty.ReadStream(fd)
        if (node->arguments.size() < 1) {
            lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
            return true;
        }

        visit(node->arguments[0].get());
        llvm::Value* fd = lastValue;

        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
            { builder->getInt64Ty() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_tty_read_stream_create", ft);
        lastValue = createCall(ft, fn.getCallee(), { fd });
        boxedValues.insert(lastValue);
        return true;
    }

    if (prop->name == "WriteStream") {
        // new tty.WriteStream(fd)
        if (node->arguments.size() < 1) {
            lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
            return true;
        }

        visit(node->arguments[0].get());
        llvm::Value* fd = lastValue;

        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
            { builder->getInt64Ty() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_tty_write_stream_create", ft);
        lastValue = createCall(ft, fn.getCallee(), { fd });
        boxedValues.insert(lastValue);
        return true;
    }

    return false;
}

} // namespace ts
