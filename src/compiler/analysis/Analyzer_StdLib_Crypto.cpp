#include "Analyzer.h"

namespace ts {

void Analyzer::registerCrypto() {
    // Hash class - returned by crypto.createHash()
    auto hashClass = std::make_shared<ClassType>("Hash");

    auto hashUpdate = std::make_shared<FunctionType>();
    hashUpdate->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any)); // data (string or Buffer)
    hashUpdate->returnType = hashClass; // returns this for chaining
    hashClass->methods["update"] = hashUpdate;

    auto hashDigest = std::make_shared<FunctionType>();
    hashDigest->paramTypes.push_back(std::make_shared<Type>(TypeKind::String)); // encoding (optional)
    hashDigest->returnType = std::make_shared<Type>(TypeKind::String); // Returns string when encoding provided
    hashClass->methods["digest"] = hashDigest;

    auto hashCopy = std::make_shared<FunctionType>();
    hashCopy->returnType = hashClass;
    hashClass->methods["copy"] = hashCopy;

    symbols.defineType("Hash", hashClass);

    // Hmac class - returned by crypto.createHmac()
    auto hmacClass = std::make_shared<ClassType>("Hmac");

    auto hmacUpdate = std::make_shared<FunctionType>();
    hmacUpdate->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any)); // data
    hmacUpdate->returnType = hmacClass;
    hmacClass->methods["update"] = hmacUpdate;

    auto hmacDigest = std::make_shared<FunctionType>();
    hmacDigest->paramTypes.push_back(std::make_shared<Type>(TypeKind::String)); // encoding
    hmacDigest->returnType = std::make_shared<Type>(TypeKind::String); // Returns string when encoding provided
    hmacClass->methods["digest"] = hmacDigest;

    symbols.defineType("Hmac", hmacClass);

    // Buffer class reference for return types
    auto bufferType = symbols.lookupType("Buffer");
    if (!bufferType) {
        bufferType = std::make_shared<ClassType>("Buffer");
    }

    // crypto module object
    auto cryptoModule = std::make_shared<ObjectType>();

    // Hash functions
    auto createHash = std::make_shared<FunctionType>();
    createHash->paramTypes.push_back(std::make_shared<Type>(TypeKind::String)); // algorithm
    createHash->returnType = hashClass;
    cryptoModule->fields["createHash"] = createHash;

    auto getHashes = std::make_shared<FunctionType>();
    getHashes->returnType = std::make_shared<ArrayType>(std::make_shared<Type>(TypeKind::String));
    cryptoModule->fields["getHashes"] = getHashes;

    // HMAC functions
    auto createHmac = std::make_shared<FunctionType>();
    createHmac->paramTypes.push_back(std::make_shared<Type>(TypeKind::String)); // algorithm
    createHmac->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));    // key
    createHmac->returnType = hmacClass;
    cryptoModule->fields["createHmac"] = createHmac;

    // Random functions
    auto randomBytes = std::make_shared<FunctionType>();
    randomBytes->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int)); // size
    randomBytes->returnType = bufferType;
    cryptoModule->fields["randomBytes"] = randomBytes;

    auto randomFillSync = std::make_shared<FunctionType>();
    randomFillSync->paramTypes.push_back(bufferType); // buffer
    randomFillSync->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int)); // offset
    randomFillSync->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int)); // size
    randomFillSync->returnType = bufferType;
    cryptoModule->fields["randomFillSync"] = randomFillSync;

    auto randomInt = std::make_shared<FunctionType>();
    randomInt->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int)); // min
    randomInt->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int)); // max
    randomInt->returnType = std::make_shared<Type>(TypeKind::Int);
    cryptoModule->fields["randomInt"] = randomInt;

    auto randomUUID = std::make_shared<FunctionType>();
    randomUUID->returnType = std::make_shared<Type>(TypeKind::String);
    cryptoModule->fields["randomUUID"] = randomUUID;

    // Key derivation functions
    auto pbkdf2Sync = std::make_shared<FunctionType>();
    pbkdf2Sync->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any)); // password
    pbkdf2Sync->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any)); // salt
    pbkdf2Sync->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int)); // iterations
    pbkdf2Sync->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int)); // keylen
    pbkdf2Sync->paramTypes.push_back(std::make_shared<Type>(TypeKind::String)); // digest
    pbkdf2Sync->returnType = bufferType;
    cryptoModule->fields["pbkdf2Sync"] = pbkdf2Sync;

    auto scryptSync = std::make_shared<FunctionType>();
    scryptSync->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any)); // password
    scryptSync->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any)); // salt
    scryptSync->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int)); // keylen
    // Options object handled separately (N, r, p)
    scryptSync->returnType = bufferType;
    cryptoModule->fields["scryptSync"] = scryptSync;

    // Utility functions
    auto timingSafeEqual = std::make_shared<FunctionType>();
    timingSafeEqual->paramTypes.push_back(bufferType); // a
    timingSafeEqual->paramTypes.push_back(bufferType); // b
    timingSafeEqual->returnType = std::make_shared<Type>(TypeKind::Boolean);
    cryptoModule->fields["timingSafeEqual"] = timingSafeEqual;

    // Legacy md5 helper (non-standard)
    auto md5 = std::make_shared<FunctionType>();
    md5->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    md5->returnType = std::make_shared<Type>(TypeKind::String);
    cryptoModule->fields["md5"] = md5;

    symbols.define("crypto", cryptoModule);
}

} // namespace ts
