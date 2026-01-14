#include "Analyzer.h"

namespace ts {

void Analyzer::registerBuffer() {
    auto bufferClass = std::make_shared<ClassType>("Buffer");
    bufferClass->fields["length"] = std::make_shared<Type>(TypeKind::Int);
    bufferClass->fields["byteLength"] = std::make_shared<Type>(TypeKind::Int);
    bufferClass->fields["byteOffset"] = std::make_shared<Type>(TypeKind::Int);
    bufferClass->fields["buffer"] = bufferClass; // buffer.buffer returns the ArrayBuffer (same as buffer for Node.js)
    
    auto bufferToString = std::make_shared<FunctionType>();
    bufferToString->paramTypes.push_back(std::make_shared<Type>(TypeKind::String)); // encoding
    bufferToString->returnType = std::make_shared<Type>(TypeKind::String);
    bufferClass->methods["toString"] = bufferToString;

    auto bufferFill = std::make_shared<FunctionType>();
    bufferFill->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any)); // value
    bufferFill->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int)); // start
    bufferFill->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int)); // end
    bufferFill->returnType = bufferClass;
    bufferClass->methods["fill"] = bufferFill;

    auto bufferSlice = std::make_shared<FunctionType>();
    bufferSlice->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int));
    bufferSlice->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int));
    bufferSlice->returnType = bufferClass;
    bufferClass->methods["slice"] = bufferSlice;
    
    auto bufferSubarray = std::make_shared<FunctionType>();
    bufferSubarray->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int)); // start
    bufferSubarray->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int)); // end
    bufferSubarray->returnType = bufferClass;
    bufferClass->methods["subarray"] = bufferSubarray;
    
    auto bufferCopy = std::make_shared<FunctionType>();
    bufferCopy->paramTypes.push_back(bufferClass);  // target
    bufferCopy->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int)); // targetStart
    bufferCopy->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int)); // sourceStart
    bufferCopy->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int)); // sourceEnd
    bufferCopy->returnType = std::make_shared<Type>(TypeKind::Int); // returns bytes copied
    bufferClass->methods["copy"] = bufferCopy;

    // Read methods - all take offset, return the value
    auto readInt8 = std::make_shared<FunctionType>();
    readInt8->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int)); // offset
    readInt8->returnType = std::make_shared<Type>(TypeKind::Int);
    bufferClass->methods["readInt8"] = readInt8;

    auto readUInt8 = std::make_shared<FunctionType>();
    readUInt8->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int)); // offset
    readUInt8->returnType = std::make_shared<Type>(TypeKind::Int);
    bufferClass->methods["readUInt8"] = readUInt8;

    auto readInt16LE = std::make_shared<FunctionType>();
    readInt16LE->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int)); // offset
    readInt16LE->returnType = std::make_shared<Type>(TypeKind::Int);
    bufferClass->methods["readInt16LE"] = readInt16LE;

    auto readInt16BE = std::make_shared<FunctionType>();
    readInt16BE->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int)); // offset
    readInt16BE->returnType = std::make_shared<Type>(TypeKind::Int);
    bufferClass->methods["readInt16BE"] = readInt16BE;

    auto readUInt16LE = std::make_shared<FunctionType>();
    readUInt16LE->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int)); // offset
    readUInt16LE->returnType = std::make_shared<Type>(TypeKind::Int);
    bufferClass->methods["readUInt16LE"] = readUInt16LE;

    auto readUInt16BE = std::make_shared<FunctionType>();
    readUInt16BE->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int)); // offset
    readUInt16BE->returnType = std::make_shared<Type>(TypeKind::Int);
    bufferClass->methods["readUInt16BE"] = readUInt16BE;

    auto readInt32LE = std::make_shared<FunctionType>();
    readInt32LE->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int)); // offset
    readInt32LE->returnType = std::make_shared<Type>(TypeKind::Int);
    bufferClass->methods["readInt32LE"] = readInt32LE;

    auto readInt32BE = std::make_shared<FunctionType>();
    readInt32BE->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int)); // offset
    readInt32BE->returnType = std::make_shared<Type>(TypeKind::Int);
    bufferClass->methods["readInt32BE"] = readInt32BE;

    auto readUInt32LE = std::make_shared<FunctionType>();
    readUInt32LE->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int)); // offset
    readUInt32LE->returnType = std::make_shared<Type>(TypeKind::Int);
    bufferClass->methods["readUInt32LE"] = readUInt32LE;

    auto readUInt32BE = std::make_shared<FunctionType>();
    readUInt32BE->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int)); // offset
    readUInt32BE->returnType = std::make_shared<Type>(TypeKind::Int);
    bufferClass->methods["readUInt32BE"] = readUInt32BE;

    auto readFloatLE = std::make_shared<FunctionType>();
    readFloatLE->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int)); // offset
    readFloatLE->returnType = std::make_shared<Type>(TypeKind::Double);
    bufferClass->methods["readFloatLE"] = readFloatLE;

    auto readFloatBE = std::make_shared<FunctionType>();
    readFloatBE->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int)); // offset
    readFloatBE->returnType = std::make_shared<Type>(TypeKind::Double);
    bufferClass->methods["readFloatBE"] = readFloatBE;

    auto readDoubleLE = std::make_shared<FunctionType>();
    readDoubleLE->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int)); // offset
    readDoubleLE->returnType = std::make_shared<Type>(TypeKind::Double);
    bufferClass->methods["readDoubleLE"] = readDoubleLE;

    auto readDoubleBE = std::make_shared<FunctionType>();
    readDoubleBE->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int)); // offset
    readDoubleBE->returnType = std::make_shared<Type>(TypeKind::Double);
    bufferClass->methods["readDoubleBE"] = readDoubleBE;

    auto readBigInt64LE = std::make_shared<FunctionType>();
    readBigInt64LE->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int)); // offset
    readBigInt64LE->returnType = std::make_shared<Type>(TypeKind::BigInt);
    bufferClass->methods["readBigInt64LE"] = readBigInt64LE;

    auto readBigInt64BE = std::make_shared<FunctionType>();
    readBigInt64BE->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int)); // offset
    readBigInt64BE->returnType = std::make_shared<Type>(TypeKind::BigInt);
    bufferClass->methods["readBigInt64BE"] = readBigInt64BE;

    auto readBigUInt64LE = std::make_shared<FunctionType>();
    readBigUInt64LE->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int)); // offset
    readBigUInt64LE->returnType = std::make_shared<Type>(TypeKind::BigInt);
    bufferClass->methods["readBigUInt64LE"] = readBigUInt64LE;

    auto readBigUInt64BE = std::make_shared<FunctionType>();
    readBigUInt64BE->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int)); // offset
    readBigUInt64BE->returnType = std::make_shared<Type>(TypeKind::BigInt);
    bufferClass->methods["readBigUInt64BE"] = readBigUInt64BE;

    // Write methods - take value and offset, return offset after written bytes
    auto writeInt8 = std::make_shared<FunctionType>();
    writeInt8->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int)); // value
    writeInt8->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int)); // offset
    writeInt8->returnType = std::make_shared<Type>(TypeKind::Int);
    bufferClass->methods["writeInt8"] = writeInt8;

    auto writeUInt8 = std::make_shared<FunctionType>();
    writeUInt8->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int)); // value
    writeUInt8->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int)); // offset
    writeUInt8->returnType = std::make_shared<Type>(TypeKind::Int);
    bufferClass->methods["writeUInt8"] = writeUInt8;

    auto writeInt16LE = std::make_shared<FunctionType>();
    writeInt16LE->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int)); // value
    writeInt16LE->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int)); // offset
    writeInt16LE->returnType = std::make_shared<Type>(TypeKind::Int);
    bufferClass->methods["writeInt16LE"] = writeInt16LE;

    auto writeInt16BE = std::make_shared<FunctionType>();
    writeInt16BE->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int)); // value
    writeInt16BE->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int)); // offset
    writeInt16BE->returnType = std::make_shared<Type>(TypeKind::Int);
    bufferClass->methods["writeInt16BE"] = writeInt16BE;

    auto writeUInt16LE = std::make_shared<FunctionType>();
    writeUInt16LE->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int)); // value
    writeUInt16LE->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int)); // offset
    writeUInt16LE->returnType = std::make_shared<Type>(TypeKind::Int);
    bufferClass->methods["writeUInt16LE"] = writeUInt16LE;

    auto writeUInt16BE = std::make_shared<FunctionType>();
    writeUInt16BE->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int)); // value
    writeUInt16BE->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int)); // offset
    writeUInt16BE->returnType = std::make_shared<Type>(TypeKind::Int);
    bufferClass->methods["writeUInt16BE"] = writeUInt16BE;

    auto writeInt32LE = std::make_shared<FunctionType>();
    writeInt32LE->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int)); // value
    writeInt32LE->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int)); // offset
    writeInt32LE->returnType = std::make_shared<Type>(TypeKind::Int);
    bufferClass->methods["writeInt32LE"] = writeInt32LE;

    auto writeInt32BE = std::make_shared<FunctionType>();
    writeInt32BE->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int)); // value
    writeInt32BE->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int)); // offset
    writeInt32BE->returnType = std::make_shared<Type>(TypeKind::Int);
    bufferClass->methods["writeInt32BE"] = writeInt32BE;

    auto writeUInt32LE = std::make_shared<FunctionType>();
    writeUInt32LE->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int)); // value
    writeUInt32LE->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int)); // offset
    writeUInt32LE->returnType = std::make_shared<Type>(TypeKind::Int);
    bufferClass->methods["writeUInt32LE"] = writeUInt32LE;

    auto writeUInt32BE = std::make_shared<FunctionType>();
    writeUInt32BE->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int)); // value
    writeUInt32BE->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int)); // offset
    writeUInt32BE->returnType = std::make_shared<Type>(TypeKind::Int);
    bufferClass->methods["writeUInt32BE"] = writeUInt32BE;

    auto writeFloatLE = std::make_shared<FunctionType>();
    writeFloatLE->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double)); // value
    writeFloatLE->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int)); // offset
    writeFloatLE->returnType = std::make_shared<Type>(TypeKind::Int);
    bufferClass->methods["writeFloatLE"] = writeFloatLE;

    auto writeFloatBE = std::make_shared<FunctionType>();
    writeFloatBE->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double)); // value
    writeFloatBE->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int)); // offset
    writeFloatBE->returnType = std::make_shared<Type>(TypeKind::Int);
    bufferClass->methods["writeFloatBE"] = writeFloatBE;

    auto writeDoubleLE = std::make_shared<FunctionType>();
    writeDoubleLE->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double)); // value
    writeDoubleLE->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int)); // offset
    writeDoubleLE->returnType = std::make_shared<Type>(TypeKind::Int);
    bufferClass->methods["writeDoubleLE"] = writeDoubleLE;

    auto writeDoubleBE = std::make_shared<FunctionType>();
    writeDoubleBE->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double)); // value
    writeDoubleBE->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int)); // offset
    writeDoubleBE->returnType = std::make_shared<Type>(TypeKind::Int);
    bufferClass->methods["writeDoubleBE"] = writeDoubleBE;

    auto writeBigInt64LE = std::make_shared<FunctionType>();
    writeBigInt64LE->paramTypes.push_back(std::make_shared<Type>(TypeKind::BigInt)); // value
    writeBigInt64LE->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int)); // offset
    writeBigInt64LE->returnType = std::make_shared<Type>(TypeKind::Int);
    bufferClass->methods["writeBigInt64LE"] = writeBigInt64LE;

    auto writeBigInt64BE = std::make_shared<FunctionType>();
    writeBigInt64BE->paramTypes.push_back(std::make_shared<Type>(TypeKind::BigInt)); // value
    writeBigInt64BE->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int)); // offset
    writeBigInt64BE->returnType = std::make_shared<Type>(TypeKind::Int);
    bufferClass->methods["writeBigInt64BE"] = writeBigInt64BE;

    auto writeBigUInt64LE = std::make_shared<FunctionType>();
    writeBigUInt64LE->paramTypes.push_back(std::make_shared<Type>(TypeKind::BigInt)); // value
    writeBigUInt64LE->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int)); // offset
    writeBigUInt64LE->returnType = std::make_shared<Type>(TypeKind::Int);
    bufferClass->methods["writeBigUInt64LE"] = writeBigUInt64LE;

    auto writeBigUInt64BE = std::make_shared<FunctionType>();
    writeBigUInt64BE->paramTypes.push_back(std::make_shared<Type>(TypeKind::BigInt)); // value
    writeBigUInt64BE->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int)); // offset
    writeBigUInt64BE->returnType = std::make_shared<Type>(TypeKind::Int);
    bufferClass->methods["writeBigUInt64BE"] = writeBigUInt64BE;

    // Utility methods
    auto bufferCompare = std::make_shared<FunctionType>();
    bufferCompare->paramTypes.push_back(bufferClass); // other buffer
    bufferCompare->returnType = std::make_shared<Type>(TypeKind::Int);
    bufferClass->methods["compare"] = bufferCompare;

    auto bufferEquals = std::make_shared<FunctionType>();
    bufferEquals->paramTypes.push_back(bufferClass); // other buffer
    bufferEquals->returnType = std::make_shared<Type>(TypeKind::Boolean);
    bufferClass->methods["equals"] = bufferEquals;

    auto bufferIndexOf = std::make_shared<FunctionType>();
    bufferIndexOf->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any)); // value (can be number or Buffer)
    bufferIndexOf->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int)); // byteOffset
    bufferIndexOf->returnType = std::make_shared<Type>(TypeKind::Int);
    bufferClass->methods["indexOf"] = bufferIndexOf;

    auto bufferLastIndexOf = std::make_shared<FunctionType>();
    bufferLastIndexOf->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any)); // value
    bufferLastIndexOf->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int)); // byteOffset
    bufferLastIndexOf->returnType = std::make_shared<Type>(TypeKind::Int);
    bufferClass->methods["lastIndexOf"] = bufferLastIndexOf;

    auto bufferIncludes = std::make_shared<FunctionType>();
    bufferIncludes->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any)); // value
    bufferIncludes->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int)); // byteOffset
    bufferIncludes->returnType = std::make_shared<Type>(TypeKind::Boolean);
    bufferClass->methods["includes"] = bufferIncludes;

    // Iterator methods
    auto bufferEntries = std::make_shared<FunctionType>();
    bufferEntries->returnType = std::make_shared<ArrayType>(std::make_shared<ArrayType>(std::make_shared<Type>(TypeKind::Int)));
    bufferClass->methods["entries"] = bufferEntries;

    auto bufferKeys = std::make_shared<FunctionType>();
    bufferKeys->returnType = std::make_shared<ArrayType>(std::make_shared<Type>(TypeKind::Int));
    bufferClass->methods["keys"] = bufferKeys;

    auto bufferValues = std::make_shared<FunctionType>();
    bufferValues->returnType = std::make_shared<ArrayType>(std::make_shared<Type>(TypeKind::Int));
    bufferClass->methods["values"] = bufferValues;

    // toJSON method - returns object { type: "Buffer", data: number[] }
    auto bufferToJSON = std::make_shared<FunctionType>();
    bufferToJSON->returnType = std::make_shared<Type>(TypeKind::Any);
    bufferClass->methods["toJSON"] = bufferToJSON;

    symbols.defineType("Buffer", bufferClass);

    auto bufferStatic = std::make_shared<ObjectType>();
    
    auto bufferAlloc = std::make_shared<FunctionType>();
    bufferAlloc->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int));
    bufferAlloc->returnType = bufferClass;
    bufferStatic->fields["alloc"] = bufferAlloc;
    
    auto bufferAllocUnsafe = std::make_shared<FunctionType>();
    bufferAllocUnsafe->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int));
    bufferAllocUnsafe->returnType = bufferClass;
    bufferStatic->fields["allocUnsafe"] = bufferAllocUnsafe;
    
    auto bufferFrom = std::make_shared<FunctionType>();
    bufferFrom->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    bufferFrom->paramTypes.push_back(std::make_shared<Type>(TypeKind::String)); // encoding
    bufferFrom->returnType = bufferClass;
    bufferStatic->fields["from"] = bufferFrom;
    
    auto bufferConcat = std::make_shared<FunctionType>();
    bufferConcat->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any)); // list of buffers
    bufferConcat->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int)); // totalLength (optional)
    bufferConcat->returnType = bufferClass;
    bufferStatic->fields["concat"] = bufferConcat;

    auto bufferIsBuffer = std::make_shared<FunctionType>();
    bufferIsBuffer->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    bufferIsBuffer->returnType = std::make_shared<Type>(TypeKind::Boolean);
    bufferStatic->fields["isBuffer"] = bufferIsBuffer;

    auto bufferStaticCompare = std::make_shared<FunctionType>();
    bufferStaticCompare->paramTypes.push_back(bufferClass); // buf1
    bufferStaticCompare->paramTypes.push_back(bufferClass); // buf2
    bufferStaticCompare->returnType = std::make_shared<Type>(TypeKind::Int);
    bufferStatic->fields["compare"] = bufferStaticCompare;

    auto bufferIsEncoding = std::make_shared<FunctionType>();
    bufferIsEncoding->paramTypes.push_back(std::make_shared<Type>(TypeKind::String)); // encoding
    bufferIsEncoding->returnType = std::make_shared<Type>(TypeKind::Boolean);
    bufferStatic->fields["isEncoding"] = bufferIsEncoding;

    symbols.define("Buffer", bufferStatic);

    auto bufferModule = std::make_shared<ObjectType>();
    bufferModule->fields["Buffer"] = bufferStatic;
    symbols.define("buffer", bufferModule);
}

} // namespace ts
