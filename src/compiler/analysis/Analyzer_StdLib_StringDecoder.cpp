#include "Analyzer.h"

namespace ts {

using namespace ast;

void Analyzer::registerStringDecoder() {
    // =========================================================================
    // string_decoder module - String decoding utilities
    //
    // Provides StringDecoder class for converting Buffer streams to strings
    // while properly handling multi-byte characters.
    // =========================================================================

    // StringDecoder class
    auto stringDecoderClass = std::make_shared<ClassType>("StringDecoder");

    // Properties
    stringDecoderClass->fields["encoding"] = std::make_shared<Type>(TypeKind::String);

    // Methods
    // write(buffer: Buffer): string
    auto writeMethod = std::make_shared<FunctionType>();
    writeMethod->paramTypes.push_back(std::make_shared<ClassType>("Buffer"));
    writeMethod->returnType = std::make_shared<Type>(TypeKind::String);
    stringDecoderClass->methods["write"] = writeMethod;

    // end(buffer?: Buffer): string
    auto endMethod = std::make_shared<FunctionType>();
    endMethod->paramTypes.push_back(std::make_shared<ClassType>("Buffer"));  // Optional
    endMethod->returnType = std::make_shared<Type>(TypeKind::String);
    stringDecoderClass->methods["end"] = endMethod;

    // Register the class type for type lookups
    symbols.defineType("StringDecoder", stringDecoderClass);

    // Also register StringDecoder directly so `import { StringDecoder }` works
    // The class itself acts as its constructor
    symbols.define("StringDecoder", stringDecoderClass);

    // Module object for 'import * as string_decoder from "string_decoder"'
    auto stringDecoderModule = std::make_shared<ObjectType>();
    stringDecoderModule->fields["StringDecoder"] = stringDecoderClass;

    symbols.define("string_decoder", stringDecoderModule);
}

} // namespace ts
