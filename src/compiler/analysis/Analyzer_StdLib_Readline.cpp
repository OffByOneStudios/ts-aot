#include "Analyzer.h"

namespace ts {

using namespace ast;

void Analyzer::registerReadline() {
    // =========================================================================
    // readline module - Line-by-line reading from readable streams
    //
    // Provides readline.createInterface() and Interface class for
    // transforming a readable stream into line-oriented events.
    // =========================================================================

    // Interface class (returned by createInterface)
    auto interfaceClass = std::make_shared<ClassType>("Interface");

    // Inherit EventEmitter methods
    auto eventEmitterType = symbols.lookupType("EventEmitter");
    if (eventEmitterType) {
        auto eventEmitterClass = std::dynamic_pointer_cast<ClassType>(eventEmitterType);
        if (eventEmitterClass) {
            // Copy EventEmitter methods
            for (const auto& [name, method] : eventEmitterClass->methods) {
                interfaceClass->methods[name] = method;
            }
        }
    }

    // Properties
    interfaceClass->fields["line"] = std::make_shared<Type>(TypeKind::String);
    interfaceClass->fields["cursor"] = std::make_shared<Type>(TypeKind::Int);

    // close(): void
    auto closeFn = std::make_shared<FunctionType>();
    closeFn->returnType = std::make_shared<Type>(TypeKind::Void);
    interfaceClass->methods["close"] = closeFn;

    // pause(): this
    auto pauseFn = std::make_shared<FunctionType>();
    pauseFn->returnType = interfaceClass;
    interfaceClass->methods["pause"] = pauseFn;

    // resume(): this
    auto resumeFn = std::make_shared<FunctionType>();
    resumeFn->returnType = interfaceClass;
    interfaceClass->methods["resume"] = resumeFn;

    // prompt(preserveCursor?: boolean): void
    auto promptFn = std::make_shared<FunctionType>();
    promptFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::Boolean));  // optional
    promptFn->returnType = std::make_shared<Type>(TypeKind::Void);
    interfaceClass->methods["prompt"] = promptFn;

    // setPrompt(prompt: string): void
    auto setPromptFn = std::make_shared<FunctionType>();
    setPromptFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    setPromptFn->returnType = std::make_shared<Type>(TypeKind::Void);
    interfaceClass->methods["setPrompt"] = setPromptFn;

    // question(query: string, callback: (answer: string) => void): void
    auto questionFn = std::make_shared<FunctionType>();
    questionFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    questionFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::Function));
    questionFn->returnType = std::make_shared<Type>(TypeKind::Void);
    interfaceClass->methods["question"] = questionFn;

    // write(data: string): void
    auto writeFn = std::make_shared<FunctionType>();
    writeFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    writeFn->returnType = std::make_shared<Type>(TypeKind::Void);
    interfaceClass->methods["write"] = writeFn;

    // getPrompt(): string
    auto getPromptFn = std::make_shared<FunctionType>();
    getPromptFn->returnType = std::make_shared<Type>(TypeKind::String);
    interfaceClass->methods["getPrompt"] = getPromptFn;

    // [Symbol.asyncIterator](): AsyncIterableIterator<string>
    // Returns an async iterator for the interface
    auto asyncIteratorFn = std::make_shared<FunctionType>();
    asyncIteratorFn->returnType = std::make_shared<Type>(TypeKind::Any);  // AsyncIterator object
    interfaceClass->methods["Symbol.asyncIterator"] = asyncIteratorFn;

    // Register the Interface type
    symbols.defineType("Interface", interfaceClass);

    // Also register as ReadlineInterface for clarity
    symbols.defineType("ReadlineInterface", interfaceClass);

    // Module object for 'import * as readline from "readline"'
    auto readlineModule = std::make_shared<ObjectType>();

    // createInterface(options: ReadlineOptions): Interface
    auto createInterfaceFn = std::make_shared<FunctionType>();
    createInterfaceFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));  // options object
    createInterfaceFn->returnType = interfaceClass;
    readlineModule->fields["createInterface"] = createInterfaceFn;

    // Utility functions

    // clearLine(stream: WritableStream, dir: number): boolean
    auto clearLineFn = std::make_shared<FunctionType>();
    clearLineFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));  // stream
    clearLineFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int));  // dir
    clearLineFn->returnType = std::make_shared<Type>(TypeKind::Boolean);
    readlineModule->fields["clearLine"] = clearLineFn;

    // clearScreenDown(stream: WritableStream): boolean
    auto clearScreenDownFn = std::make_shared<FunctionType>();
    clearScreenDownFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));  // stream
    clearScreenDownFn->returnType = std::make_shared<Type>(TypeKind::Boolean);
    readlineModule->fields["clearScreenDown"] = clearScreenDownFn;

    // cursorTo(stream: WritableStream, x: number, y?: number): boolean
    auto cursorToFn = std::make_shared<FunctionType>();
    cursorToFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));  // stream
    cursorToFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int));  // x
    cursorToFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int));  // y (optional)
    cursorToFn->returnType = std::make_shared<Type>(TypeKind::Boolean);
    readlineModule->fields["cursorTo"] = cursorToFn;

    // moveCursor(stream: WritableStream, dx: number, dy: number): boolean
    auto moveCursorFn = std::make_shared<FunctionType>();
    moveCursorFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));  // stream
    moveCursorFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int));  // dx
    moveCursorFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int));  // dy
    moveCursorFn->returnType = std::make_shared<Type>(TypeKind::Boolean);
    readlineModule->fields["moveCursor"] = moveCursorFn;

    // emitKeypressEvents(stream: Readable, interface?: Interface): void
    // Causes the readable stream to begin emitting 'keypress' events
    auto emitKeypressEventsFn = std::make_shared<FunctionType>();
    emitKeypressEventsFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));  // stream
    emitKeypressEventsFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));  // interface (optional)
    emitKeypressEventsFn->returnType = std::make_shared<Type>(TypeKind::Void);
    readlineModule->fields["emitKeypressEvents"] = emitKeypressEventsFn;

    // Also expose Interface class on the module
    readlineModule->fields["Interface"] = interfaceClass;

    symbols.define("readline", readlineModule);
}

} // namespace ts
