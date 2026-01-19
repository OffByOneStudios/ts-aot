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

    // Cipher class - returned by crypto.createCipheriv()
    auto cipherClass = std::make_shared<ClassType>("Cipher");

    auto cipherUpdate = std::make_shared<FunctionType>();
    cipherUpdate->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any)); // data (string or Buffer)
    cipherUpdate->returnType = std::make_shared<ClassType>("Buffer"); // returns encrypted chunk
    cipherClass->methods["update"] = cipherUpdate;

    auto cipherFinal = std::make_shared<FunctionType>();
    cipherFinal->returnType = std::make_shared<ClassType>("Buffer"); // returns final chunk
    cipherClass->methods["final"] = cipherFinal;

    auto cipherGetAuthTag = std::make_shared<FunctionType>();
    cipherGetAuthTag->returnType = std::make_shared<ClassType>("Buffer"); // for GCM mode
    cipherClass->methods["getAuthTag"] = cipherGetAuthTag;

    auto cipherSetAAD = std::make_shared<FunctionType>();
    cipherSetAAD->paramTypes.push_back(std::make_shared<ClassType>("Buffer")); // additional auth data
    cipherSetAAD->returnType = cipherClass; // returns this for chaining
    cipherClass->methods["setAAD"] = cipherSetAAD;

    symbols.defineType("Cipher", cipherClass);

    // Decipher class - returned by crypto.createDecipheriv()
    auto decipherClass = std::make_shared<ClassType>("Decipher");

    auto decipherUpdate = std::make_shared<FunctionType>();
    decipherUpdate->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any)); // data (string or Buffer)
    decipherUpdate->returnType = std::make_shared<ClassType>("Buffer"); // returns decrypted chunk
    decipherClass->methods["update"] = decipherUpdate;

    auto decipherFinal = std::make_shared<FunctionType>();
    decipherFinal->returnType = std::make_shared<ClassType>("Buffer"); // returns final chunk
    decipherClass->methods["final"] = decipherFinal;

    auto decipherSetAuthTag = std::make_shared<FunctionType>();
    decipherSetAuthTag->paramTypes.push_back(std::make_shared<ClassType>("Buffer")); // auth tag
    decipherSetAuthTag->returnType = decipherClass; // returns this for chaining
    decipherClass->methods["setAuthTag"] = decipherSetAuthTag;

    auto decipherSetAAD = std::make_shared<FunctionType>();
    decipherSetAAD->paramTypes.push_back(std::make_shared<ClassType>("Buffer")); // additional auth data
    decipherSetAAD->returnType = decipherClass; // returns this for chaining
    decipherClass->methods["setAAD"] = decipherSetAAD;

    symbols.defineType("Decipher", decipherClass);

    // Sign class - returned by crypto.createSign()
    auto signClass = std::make_shared<ClassType>("Sign");

    auto signUpdate = std::make_shared<FunctionType>();
    signUpdate->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any)); // data (string or Buffer)
    signUpdate->returnType = signClass; // returns this for chaining
    signClass->methods["update"] = signUpdate;

    auto signSign = std::make_shared<FunctionType>();
    signSign->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any)); // privateKey
    signSign->paramTypes.push_back(std::make_shared<Type>(TypeKind::String)); // outputEncoding (optional)
    signSign->returnType = std::make_shared<ClassType>("Buffer"); // returns Buffer
    signClass->methods["sign"] = signSign;

    symbols.defineType("Sign", signClass);

    // Verify class - returned by crypto.createVerify()
    auto verifyClass = std::make_shared<ClassType>("Verify");

    auto verifyUpdate = std::make_shared<FunctionType>();
    verifyUpdate->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any)); // data (string or Buffer)
    verifyUpdate->returnType = verifyClass; // returns this for chaining
    verifyClass->methods["update"] = verifyUpdate;

    auto verifyVerify = std::make_shared<FunctionType>();
    verifyVerify->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any)); // publicKey
    verifyVerify->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any)); // signature
    verifyVerify->paramTypes.push_back(std::make_shared<Type>(TypeKind::String)); // signatureEncoding (optional)
    verifyVerify->returnType = std::make_shared<Type>(TypeKind::Boolean);
    verifyClass->methods["verify"] = verifyVerify;

    symbols.defineType("Verify", verifyClass);

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

    auto getCiphers = std::make_shared<FunctionType>();
    getCiphers->returnType = std::make_shared<ArrayType>(std::make_shared<Type>(TypeKind::String));
    cryptoModule->fields["getCiphers"] = getCiphers;

    // Cipher/Decipher functions
    auto createCipheriv = std::make_shared<FunctionType>();
    createCipheriv->paramTypes.push_back(std::make_shared<Type>(TypeKind::String)); // algorithm
    createCipheriv->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));    // key
    createCipheriv->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));    // iv
    createCipheriv->returnType = cipherClass;
    cryptoModule->fields["createCipheriv"] = createCipheriv;

    auto createDecipheriv = std::make_shared<FunctionType>();
    createDecipheriv->paramTypes.push_back(std::make_shared<Type>(TypeKind::String)); // algorithm
    createDecipheriv->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));    // key
    createDecipheriv->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));    // iv
    createDecipheriv->returnType = decipherClass;
    cryptoModule->fields["createDecipheriv"] = createDecipheriv;

    // Sign/Verify functions
    auto createSign = std::make_shared<FunctionType>();
    createSign->paramTypes.push_back(std::make_shared<Type>(TypeKind::String)); // algorithm
    createSign->returnType = signClass;
    cryptoModule->fields["createSign"] = createSign;

    auto createVerify = std::make_shared<FunctionType>();
    createVerify->paramTypes.push_back(std::make_shared<Type>(TypeKind::String)); // algorithm
    createVerify->returnType = verifyClass;
    cryptoModule->fields["createVerify"] = createVerify;

    // One-shot sign/verify
    auto signOneshot = std::make_shared<FunctionType>();
    signOneshot->paramTypes.push_back(std::make_shared<Type>(TypeKind::String)); // algorithm
    signOneshot->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));    // data
    signOneshot->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));    // key
    signOneshot->returnType = bufferType;
    cryptoModule->fields["sign"] = signOneshot;

    auto verifyOneshot = std::make_shared<FunctionType>();
    verifyOneshot->paramTypes.push_back(std::make_shared<Type>(TypeKind::String)); // algorithm
    verifyOneshot->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));    // data
    verifyOneshot->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));    // key
    verifyOneshot->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));    // signature
    verifyOneshot->returnType = std::make_shared<Type>(TypeKind::Boolean);
    cryptoModule->fields["verify"] = verifyOneshot;

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

    // randomFill(buffer, offset?, size?, callback) -> void
    auto randomFillCallbackType = std::make_shared<FunctionType>();
    randomFillCallbackType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any)); // err
    randomFillCallbackType->paramTypes.push_back(bufferType); // buffer
    randomFillCallbackType->returnType = std::make_shared<Type>(TypeKind::Void);

    auto randomFill = std::make_shared<FunctionType>();
    randomFill->paramTypes.push_back(bufferType); // buffer
    randomFill->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int)); // offset (optional)
    randomFill->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int)); // size (optional)
    randomFill->paramTypes.push_back(randomFillCallbackType); // callback
    randomFill->returnType = std::make_shared<Type>(TypeKind::Void);
    cryptoModule->fields["randomFill"] = randomFill;

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

    // Async versions of key derivation functions
    auto pbkdf2 = std::make_shared<FunctionType>();
    pbkdf2->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any)); // password
    pbkdf2->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any)); // salt
    pbkdf2->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int)); // iterations
    pbkdf2->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int)); // keylen
    pbkdf2->paramTypes.push_back(std::make_shared<Type>(TypeKind::String)); // digest
    pbkdf2->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any)); // callback
    pbkdf2->returnType = std::make_shared<Type>(TypeKind::Void);
    cryptoModule->fields["pbkdf2"] = pbkdf2;

    auto scrypt = std::make_shared<FunctionType>();
    scrypt->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any)); // password
    scrypt->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any)); // salt
    scrypt->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int)); // keylen
    scrypt->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any)); // options or callback
    scrypt->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any)); // callback (if options provided)
    scrypt->returnType = std::make_shared<Type>(TypeKind::Void);
    cryptoModule->fields["scrypt"] = scrypt;

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

    // Key generation functions
    // generateKeyPairSync(type, options?) -> { publicKey: string, privateKey: string }
    auto keyPairResultType = std::make_shared<ObjectType>();
    keyPairResultType->fields["publicKey"] = std::make_shared<Type>(TypeKind::String);
    keyPairResultType->fields["privateKey"] = std::make_shared<Type>(TypeKind::String);

    auto generateKeyPairSync = std::make_shared<FunctionType>();
    generateKeyPairSync->paramTypes.push_back(std::make_shared<Type>(TypeKind::String)); // type ('rsa', 'ec', etc.)
    generateKeyPairSync->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));    // options (optional)
    generateKeyPairSync->returnType = keyPairResultType;
    cryptoModule->fields["generateKeyPairSync"] = generateKeyPairSync;

    // generateKeyPair(type, options, callback) -> void
    auto generateKeyPair = std::make_shared<FunctionType>();
    generateKeyPair->paramTypes.push_back(std::make_shared<Type>(TypeKind::String)); // type
    generateKeyPair->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));    // options
    generateKeyPair->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));    // callback(err, publicKey, privateKey)
    generateKeyPair->returnType = std::make_shared<Type>(TypeKind::Void);
    cryptoModule->fields["generateKeyPair"] = generateKeyPair;

    // generateKeySync(type, options?) -> Buffer
    auto generateKeySync = std::make_shared<FunctionType>();
    generateKeySync->paramTypes.push_back(std::make_shared<Type>(TypeKind::String)); // type ('aes', 'hmac')
    generateKeySync->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));    // options (optional)
    generateKeySync->returnType = bufferType;
    cryptoModule->fields["generateKeySync"] = generateKeySync;

    // generateKey(type, options, callback) -> void
    auto generateKey = std::make_shared<FunctionType>();
    generateKey->paramTypes.push_back(std::make_shared<Type>(TypeKind::String)); // type
    generateKey->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));    // options
    generateKey->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));    // callback(err, key)
    generateKey->returnType = std::make_shared<Type>(TypeKind::Void);
    cryptoModule->fields["generateKey"] = generateKey;

    // createPrivateKey(key) -> string (PEM)
    auto createPrivateKey = std::make_shared<FunctionType>();
    createPrivateKey->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any)); // key (string, Buffer, or KeyObject options)
    createPrivateKey->returnType = std::make_shared<Type>(TypeKind::String);
    cryptoModule->fields["createPrivateKey"] = createPrivateKey;

    // createPublicKey(key) -> string (PEM)
    auto createPublicKey = std::make_shared<FunctionType>();
    createPublicKey->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any)); // key
    createPublicKey->returnType = std::make_shared<Type>(TypeKind::String);
    cryptoModule->fields["createPublicKey"] = createPublicKey;

    // createSecretKey(key, encoding?) -> Buffer
    auto createSecretKey = std::make_shared<FunctionType>();
    createSecretKey->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));    // key
    createSecretKey->paramTypes.push_back(std::make_shared<Type>(TypeKind::String)); // encoding (optional)
    createSecretKey->returnType = bufferType;
    cryptoModule->fields["createSecretKey"] = createSecretKey;

    // hkdfSync(digest, ikm, salt, info, keylen) -> Buffer
    auto hkdfSync = std::make_shared<FunctionType>();
    hkdfSync->paramTypes.push_back(std::make_shared<Type>(TypeKind::String)); // digest algorithm
    hkdfSync->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));    // ikm (input keying material)
    hkdfSync->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));    // salt
    hkdfSync->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));    // info
    hkdfSync->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int));    // keylen
    hkdfSync->returnType = bufferType;
    cryptoModule->fields["hkdfSync"] = hkdfSync;

    symbols.define("crypto", cryptoModule);
}

} // namespace ts
