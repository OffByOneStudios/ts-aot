#include "TsString.h"
#include "TsBuffer.h"
#include "TsObject.h"
#include "TsArray.h"
#include "TsRuntime.h"
#include "GC.h"
#include "md5.h"

#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <openssl/kdf.h>
#include <openssl/err.h>

#include <string.h>
#include <new>

// ============================================================================
// Hash class - wraps OpenSSL EVP_MD_CTX for streaming hash computation
// ============================================================================

class TsCryptoHash : public TsObject {
public:
    static constexpr uint32_t MAGIC = 0x48415348; // "HASH"

    EVP_MD_CTX* ctx;
    const EVP_MD* md;
    bool finalized;

    static TsCryptoHash* Create(const char* algorithm) {
        const EVP_MD* md = EVP_get_digestbyname(algorithm);
        if (!md) return nullptr;

        void* mem = ts_alloc(sizeof(TsCryptoHash));
        TsCryptoHash* hash = new(mem) TsCryptoHash();
        hash->magic = MAGIC;
        hash->md = md;
        hash->finalized = false;
        hash->ctx = EVP_MD_CTX_new();

        if (!hash->ctx || EVP_DigestInit_ex(hash->ctx, md, nullptr) != 1) {
            if (hash->ctx) EVP_MD_CTX_free(hash->ctx);
            return nullptr;
        }

        return hash;
    }

    ~TsCryptoHash() {
        if (ctx) {
            EVP_MD_CTX_free(ctx);
            ctx = nullptr;
        }
    }

    bool Update(const void* data, size_t len) {
        if (finalized || !ctx) return false;
        return EVP_DigestUpdate(ctx, data, len) == 1;
    }

    TsBuffer* Digest() {
        if (finalized || !ctx) return nullptr;
        finalized = true;

        unsigned char digest[EVP_MAX_MD_SIZE];
        unsigned int digestLen = 0;

        if (EVP_DigestFinal_ex(ctx, digest, &digestLen) != 1) {
            return nullptr;
        }

        TsBuffer* buf = TsBuffer::Create(digestLen);
        memcpy(buf->GetData(), digest, digestLen);
        return buf;
    }

    TsString* DigestHex() {
        TsBuffer* buf = Digest();
        if (!buf) return nullptr;
        return buf->ToHex();
    }

    TsString* DigestBase64() {
        TsBuffer* buf = Digest();
        if (!buf) return nullptr;
        return buf->ToBase64();
    }

    // Copy the hash state for incremental hashing
    TsCryptoHash* Copy() {
        if (finalized) return nullptr;

        void* mem = ts_alloc(sizeof(TsCryptoHash));
        TsCryptoHash* copy = new(mem) TsCryptoHash();
        copy->magic = MAGIC;
        copy->md = md;
        copy->finalized = false;
        copy->ctx = EVP_MD_CTX_new();

        if (!copy->ctx || EVP_MD_CTX_copy_ex(copy->ctx, ctx) != 1) {
            if (copy->ctx) EVP_MD_CTX_free(copy->ctx);
            return nullptr;
        }

        return copy;
    }
};

// ============================================================================
// Hmac class - wraps OpenSSL HMAC for keyed hashing
// ============================================================================

class TsCryptoHmac : public TsObject {
public:
    static constexpr uint32_t MAGIC = 0x484D4143; // "HMAC"

    EVP_MD_CTX* ctx;
    EVP_PKEY* pkey;
    const EVP_MD* md;
    bool finalized;

    static TsCryptoHmac* Create(const char* algorithm, const void* key, size_t keyLen) {
        const EVP_MD* md = EVP_get_digestbyname(algorithm);
        if (!md) return nullptr;

        void* mem = ts_alloc(sizeof(TsCryptoHmac));
        TsCryptoHmac* hmac = new(mem) TsCryptoHmac();
        hmac->magic = MAGIC;
        hmac->md = md;
        hmac->finalized = false;

        // Create HMAC key
        hmac->pkey = EVP_PKEY_new_mac_key(EVP_PKEY_HMAC, nullptr,
                                          (const unsigned char*)key, (int)keyLen);
        if (!hmac->pkey) return nullptr;

        hmac->ctx = EVP_MD_CTX_new();
        if (!hmac->ctx) {
            EVP_PKEY_free(hmac->pkey);
            return nullptr;
        }

        if (EVP_DigestSignInit(hmac->ctx, nullptr, md, nullptr, hmac->pkey) != 1) {
            EVP_MD_CTX_free(hmac->ctx);
            EVP_PKEY_free(hmac->pkey);
            return nullptr;
        }

        return hmac;
    }

    ~TsCryptoHmac() {
        if (ctx) EVP_MD_CTX_free(ctx);
        if (pkey) EVP_PKEY_free(pkey);
    }

    bool Update(const void* data, size_t len) {
        if (finalized || !ctx) return false;
        return EVP_DigestSignUpdate(ctx, data, len) == 1;
    }

    TsBuffer* Digest() {
        if (finalized || !ctx) return nullptr;
        finalized = true;

        size_t sigLen = 0;
        if (EVP_DigestSignFinal(ctx, nullptr, &sigLen) != 1) {
            return nullptr;
        }

        TsBuffer* buf = TsBuffer::Create(sigLen);
        if (EVP_DigestSignFinal(ctx, buf->GetData(), &sigLen) != 1) {
            return nullptr;
        }

        return buf;
    }

    TsString* DigestHex() {
        TsBuffer* buf = Digest();
        if (!buf) return nullptr;
        return buf->ToHex();
    }

    TsString* DigestBase64() {
        TsBuffer* buf = Digest();
        if (!buf) return nullptr;
        return buf->ToBase64();
    }
};

// ============================================================================
// C API - Hash functions
// ============================================================================

extern "C" {

// crypto.createHash(algorithm) -> Hash
void* ts_crypto_createHash(void* algorithm) {
    TsString* alg = (TsString*)algorithm;
    if (!alg) return nullptr;

    const char* algStr = alg->ToUtf8();
    TsCryptoHash* hash = TsCryptoHash::Create(algStr);

    return hash;
}

// hash.update(data) -> Hash (returns same hash for chaining)
void* ts_crypto_hash_update(void* hashObj, void* data) {
    TsCryptoHash* hash = (TsCryptoHash*)hashObj;
    if (!hash || hash->magic != TsCryptoHash::MAGIC) return hashObj;

    // Handle string or buffer
    TsValue* val = (TsValue*)data;
    if (!val) return hashObj;

    if (val->type == ValueType::STRING_PTR) {
        TsString* str = (TsString*)val->ptr_val;
        hash->Update(str->ToUtf8(), strlen(str->ToUtf8()));
    } else if (val->type == ValueType::OBJECT_PTR) {
        TsObject* obj = (TsObject*)val->ptr_val;
        if (obj && obj->magic == TsBuffer::MAGIC) {
            TsBuffer* buf = (TsBuffer*)obj;
            hash->Update(buf->GetData(), buf->GetLength());
        }
    } else {
        // Try as raw pointer (unboxed string)
        void* raw = ts_value_get_object(val);
        if (raw) {
            TsString* str = (TsString*)raw;
            hash->Update(str->ToUtf8(), strlen(str->ToUtf8()));
        }
    }

    return hashObj;
}

// hash.digest(encoding?) -> Buffer or string
void* ts_crypto_hash_digest(void* hashObj, void* encoding) {
    TsCryptoHash* hash = (TsCryptoHash*)hashObj;
    if (!hash || hash->magic != TsCryptoHash::MAGIC) return nullptr;

    if (encoding) {
        TsString* enc = (TsString*)encoding;
        const char* encStr = enc->ToUtf8();

        if (strcmp(encStr, "hex") == 0) {
            return hash->DigestHex();
        } else if (strcmp(encStr, "base64") == 0) {
            return hash->DigestBase64();
        }
    }

    return hash->Digest();
}

// hash.copy() -> Hash
void* ts_crypto_hash_copy(void* hashObj) {
    TsCryptoHash* hash = (TsCryptoHash*)hashObj;
    if (!hash || hash->magic != TsCryptoHash::MAGIC) return nullptr;
    return hash->Copy();
}

// ============================================================================
// C API - HMAC functions
// ============================================================================

// crypto.createHmac(algorithm, key) -> Hmac
void* ts_crypto_createHmac(void* algorithm, void* key) {
    TsString* alg = (TsString*)algorithm;
    if (!alg) return nullptr;

    const char* algStr = alg->ToUtf8();

    // Extract key data
    const void* keyData = nullptr;
    size_t keyLen = 0;

    TsValue* keyVal = (TsValue*)key;
    if (keyVal->type == ValueType::STRING_PTR) {
        TsString* keyStr = (TsString*)keyVal->ptr_val;
        keyData = keyStr->ToUtf8();
        keyLen = strlen((const char*)keyData);
    } else if (keyVal->type == ValueType::OBJECT_PTR) {
        TsObject* obj = (TsObject*)keyVal->ptr_val;
        if (obj && obj->magic == TsBuffer::MAGIC) {
            TsBuffer* buf = (TsBuffer*)obj;
            keyData = buf->GetData();
            keyLen = buf->GetLength();
        }
    } else {
        void* raw = ts_value_get_object(keyVal);
        if (raw) {
            TsString* keyStr = (TsString*)raw;
            keyData = keyStr->ToUtf8();
            keyLen = strlen((const char*)keyData);
        }
    }

    if (!keyData) return nullptr;

    return TsCryptoHmac::Create(algStr, keyData, keyLen);
}

// hmac.update(data) -> Hmac
void* ts_crypto_hmac_update(void* hmacObj, void* data) {
    TsCryptoHmac* hmac = (TsCryptoHmac*)hmacObj;
    if (!hmac || hmac->magic != TsCryptoHmac::MAGIC) return hmacObj;

    TsValue* val = (TsValue*)data;
    if (!val) return hmacObj;

    if (val->type == ValueType::STRING_PTR) {
        TsString* str = (TsString*)val->ptr_val;
        hmac->Update(str->ToUtf8(), strlen(str->ToUtf8()));
    } else if (val->type == ValueType::OBJECT_PTR) {
        TsObject* obj = (TsObject*)val->ptr_val;
        if (obj && obj->magic == TsBuffer::MAGIC) {
            TsBuffer* buf = (TsBuffer*)obj;
            hmac->Update(buf->GetData(), buf->GetLength());
        }
    } else {
        void* raw = ts_value_get_object(val);
        if (raw) {
            TsString* str = (TsString*)raw;
            hmac->Update(str->ToUtf8(), strlen(str->ToUtf8()));
        }
    }

    return hmacObj;
}

// hmac.digest(encoding?) -> Buffer or string
void* ts_crypto_hmac_digest(void* hmacObj, void* encoding) {
    TsCryptoHmac* hmac = (TsCryptoHmac*)hmacObj;
    if (!hmac || hmac->magic != TsCryptoHmac::MAGIC) return nullptr;

    if (encoding) {
        TsString* enc = (TsString*)encoding;
        const char* encStr = enc->ToUtf8();

        if (strcmp(encStr, "hex") == 0) {
            return hmac->DigestHex();
        } else if (strcmp(encStr, "base64") == 0) {
            return hmac->DigestBase64();
        }
    }

    return hmac->Digest();
}

// ============================================================================
// C API - Random functions
// ============================================================================

// crypto.randomBytes(size) -> Buffer
void* ts_crypto_randomBytes(int64_t size) {
    if (size <= 0) return nullptr;

    TsBuffer* buf = TsBuffer::Create((size_t)size);
    if (RAND_bytes(buf->GetData(), (int)size) != 1) {
        return nullptr;
    }

    return buf;
}

// crypto.randomFillSync(buffer, offset?, size?) -> Buffer
void* ts_crypto_randomFillSync(void* bufferObj, int64_t offset, int64_t size) {
    TsBuffer* buf = (TsBuffer*)bufferObj;
    if (!buf || buf->magic != TsBuffer::MAGIC) return bufferObj;

    size_t bufLen = buf->GetLength();
    size_t start = (offset >= 0) ? (size_t)offset : 0;
    size_t len = (size >= 0) ? (size_t)size : (bufLen - start);

    if (start + len > bufLen) {
        len = bufLen - start;
    }

    if (len > 0) {
        RAND_bytes(buf->GetData() + start, (int)len);
    }

    return bufferObj;
}

// crypto.randomFill(buffer, offset?, size?, callback) -> void
// This is the async version - calls callback(err, buffer) after filling
void ts_crypto_randomFill(void* bufferObj, int64_t offset, int64_t size, void* callback) {
    TsBuffer* buf = (TsBuffer*)bufferObj;

    TsValue* err = nullptr;
    TsValue* result = nullptr;

    if (!buf || buf->magic != TsBuffer::MAGIC) {
        err = (TsValue*)ts_error_create(TsString::Create("Buffer expected"));
        result = ts_value_make_undefined();
    } else {
        size_t bufLen = buf->GetLength();
        size_t start = (offset >= 0) ? (size_t)offset : 0;
        size_t len = (size >= 0) ? (size_t)size : (bufLen - start);

        if (start + len > bufLen) {
            len = bufLen - start;
        }

        if (len > 0) {
            if (RAND_bytes(buf->GetData() + start, (int)len) != 1) {
                err = (TsValue*)ts_error_create(TsString::Create("Random generation failed"));
                result = ts_value_make_undefined();
            } else {
                err = ts_value_make_undefined();
                result = ts_value_make_object(buf);
            }
        } else {
            err = ts_value_make_undefined();
            result = ts_value_make_object(buf);
        }
    }

    // Call the callback with (err, buffer)
    if (callback) {
        TsValue* args[2] = { err, result };
        ts_function_call((TsValue*)callback, 2, args);
    }
}

// crypto.randomInt(min, max) -> number
int64_t ts_crypto_randomInt(int64_t min, int64_t max) {
    if (min >= max) return min;

    uint64_t range = (uint64_t)(max - min);
    uint64_t randVal;

    RAND_bytes((unsigned char*)&randVal, sizeof(randVal));

    return min + (int64_t)(randVal % range);
}

// crypto.randomUUID() -> string
void* ts_crypto_randomUUID() {
    unsigned char uuid[16];
    RAND_bytes(uuid, 16);

    // Set version 4 (random) and variant (RFC 4122)
    uuid[6] = (uuid[6] & 0x0f) | 0x40;  // version 4
    uuid[8] = (uuid[8] & 0x3f) | 0x80;  // variant 1

    char uuidStr[37];
    snprintf(uuidStr, sizeof(uuidStr),
        "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        uuid[0], uuid[1], uuid[2], uuid[3],
        uuid[4], uuid[5],
        uuid[6], uuid[7],
        uuid[8], uuid[9],
        uuid[10], uuid[11], uuid[12], uuid[13], uuid[14], uuid[15]);

    return TsString::Create(uuidStr);
}

// ============================================================================
// C API - Key derivation functions
// ============================================================================

// crypto.pbkdf2Sync(password, salt, iterations, keylen, digest) -> Buffer
void* ts_crypto_pbkdf2Sync(void* password, void* salt, int64_t iterations,
                           int64_t keylen, void* digest) {
    const unsigned char* passData = nullptr;
    size_t passLen = 0;
    const unsigned char* saltData = nullptr;
    size_t saltLen = 0;

    // Extract password
    TsValue* passVal = (TsValue*)password;
    if (passVal->type == ValueType::STRING_PTR) {
        TsString* str = (TsString*)passVal->ptr_val;
        passData = (const unsigned char*)str->ToUtf8();
        passLen = strlen((const char*)passData);
    } else if (passVal->type == ValueType::OBJECT_PTR) {
        TsObject* obj = (TsObject*)passVal->ptr_val;
        if (obj && obj->magic == TsBuffer::MAGIC) {
            TsBuffer* buf = (TsBuffer*)obj;
            passData = buf->GetData();
            passLen = buf->GetLength();
        }
    }

    // Extract salt
    TsValue* saltVal = (TsValue*)salt;
    if (saltVal->type == ValueType::STRING_PTR) {
        TsString* str = (TsString*)saltVal->ptr_val;
        saltData = (const unsigned char*)str->ToUtf8();
        saltLen = strlen((const char*)saltData);
    } else if (saltVal->type == ValueType::OBJECT_PTR) {
        TsObject* obj = (TsObject*)saltVal->ptr_val;
        if (obj && obj->magic == TsBuffer::MAGIC) {
            TsBuffer* buf = (TsBuffer*)obj;
            saltData = buf->GetData();
            saltLen = buf->GetLength();
        }
    }

    if (!passData || !saltData) return nullptr;

    // Get digest algorithm
    const EVP_MD* md = EVP_sha256(); // default
    if (digest) {
        TsString* digestStr = (TsString*)digest;
        const EVP_MD* customMd = EVP_get_digestbyname(digestStr->ToUtf8());
        if (customMd) md = customMd;
    }

    TsBuffer* result = TsBuffer::Create((size_t)keylen);

    if (PKCS5_PBKDF2_HMAC((const char*)passData, (int)passLen,
                          saltData, (int)saltLen,
                          (int)iterations,
                          md,
                          (int)keylen,
                          result->GetData()) != 1) {
        return nullptr;
    }

    return result;
}

// crypto.scryptSync(password, salt, keylen, options?) -> Buffer
void* ts_crypto_scryptSync(void* password, void* salt, int64_t keylen,
                           int64_t N, int64_t r, int64_t p) {
    const unsigned char* passData = nullptr;
    size_t passLen = 0;
    const unsigned char* saltData = nullptr;
    size_t saltLen = 0;

    // Extract password
    TsValue* passVal = (TsValue*)password;
    if (passVal->type == ValueType::STRING_PTR) {
        TsString* str = (TsString*)passVal->ptr_val;
        passData = (const unsigned char*)str->ToUtf8();
        passLen = strlen((const char*)passData);
    } else if (passVal->type == ValueType::OBJECT_PTR) {
        TsObject* obj = (TsObject*)passVal->ptr_val;
        if (obj && obj->magic == TsBuffer::MAGIC) {
            TsBuffer* buf = (TsBuffer*)obj;
            passData = buf->GetData();
            passLen = buf->GetLength();
        }
    }

    // Extract salt
    TsValue* saltVal = (TsValue*)salt;
    if (saltVal->type == ValueType::STRING_PTR) {
        TsString* str = (TsString*)saltVal->ptr_val;
        saltData = (const unsigned char*)str->ToUtf8();
        saltLen = strlen((const char*)saltData);
    } else if (saltVal->type == ValueType::OBJECT_PTR) {
        TsObject* obj = (TsObject*)saltVal->ptr_val;
        if (obj && obj->magic == TsBuffer::MAGIC) {
            TsBuffer* buf = (TsBuffer*)obj;
            saltData = buf->GetData();
            saltLen = buf->GetLength();
        }
    }

    if (!passData || !saltData) return nullptr;

    // Default scrypt parameters
    if (N <= 0) N = 16384;  // 2^14
    if (r <= 0) r = 8;
    if (p <= 0) p = 1;

    TsBuffer* result = TsBuffer::Create((size_t)keylen);

    if (EVP_PBE_scrypt((const char*)passData, passLen,
                       saltData, saltLen,
                       (uint64_t)N, (uint64_t)r, (uint64_t)p,
                       0, // maxmem (0 = default)
                       result->GetData(), (size_t)keylen) != 1) {
        return nullptr;
    }

    return result;
}

// crypto.timingSafeEqual(a, b) -> boolean
bool ts_crypto_timingSafeEqual(void* a, void* b) {
    if (!a || !b) return false;

    TsBuffer* bufA = nullptr;
    TsBuffer* bufB = nullptr;

    // Try to get buffer from raw pointer or TsValue
    // First, check if it's a raw TsBuffer pointer
    TsObject* objA = (TsObject*)a;
    TsObject* objB = (TsObject*)b;

    if (objA && objA->magic == TsBuffer::MAGIC) {
        bufA = (TsBuffer*)objA;
    } else {
        // Maybe it's a TsValue
        TsValue* valA = (TsValue*)a;
        if (valA->type == ValueType::OBJECT_PTR && valA->ptr_val) {
            TsObject* innerObj = (TsObject*)valA->ptr_val;
            if (innerObj->magic == TsBuffer::MAGIC) {
                bufA = (TsBuffer*)innerObj;
            }
        }
    }

    if (objB && objB->magic == TsBuffer::MAGIC) {
        bufB = (TsBuffer*)objB;
    } else {
        TsValue* valB = (TsValue*)b;
        if (valB->type == ValueType::OBJECT_PTR && valB->ptr_val) {
            TsObject* innerObj = (TsObject*)valB->ptr_val;
            if (innerObj->magic == TsBuffer::MAGIC) {
                bufB = (TsBuffer*)innerObj;
            }
        }
    }

    if (!bufA || !bufB) return false;
    if (bufA->GetLength() != bufB->GetLength()) return false;

    return CRYPTO_memcmp(bufA->GetData(), bufB->GetData(), bufA->GetLength()) == 0;
}

// crypto.getHashes() -> string[]
void* ts_crypto_getHashes() {
    // Return commonly supported hash algorithms
    TsArray* arr = TsArray::Create();

    const char* hashes[] = {
        "md5", "sha1", "sha224", "sha256", "sha384", "sha512",
        "sha512-224", "sha512-256", "sha3-224", "sha3-256", "sha3-384", "sha3-512",
        "blake2b512", "blake2s256", "ripemd160", "whirlpool"
    };

    for (const char* hash : hashes) {
        if (EVP_get_digestbyname(hash) != nullptr) {
            arr->Push((int64_t)ts_value_make_string(TsString::Create(hash)));
        }
    }

    return arr;
}

// crypto.getCiphers() -> string[]
void* ts_crypto_getCiphers() {
    // Return commonly supported cipher algorithms
    TsArray* arr = TsArray::Create();

    const char* ciphers[] = {
        "aes-128-cbc", "aes-128-ecb", "aes-128-cfb", "aes-128-ofb", "aes-128-ctr", "aes-128-gcm",
        "aes-192-cbc", "aes-192-ecb", "aes-192-cfb", "aes-192-ofb", "aes-192-ctr", "aes-192-gcm",
        "aes-256-cbc", "aes-256-ecb", "aes-256-cfb", "aes-256-ofb", "aes-256-ctr", "aes-256-gcm",
        "aes128", "aes192", "aes256",
        "des-cbc", "des-ecb", "des-cfb", "des-ofb",
        "des-ede3-cbc", "des-ede3-ecb", "des-ede3-cfb", "des-ede3-ofb",
        "des3", "des-ede3",
        "bf-cbc", "bf-ecb", "bf-cfb", "bf-ofb",
        "rc4", "rc4-40",
        "chacha20", "chacha20-poly1305"
    };

    for (const char* cipher : ciphers) {
        if (EVP_get_cipherbyname(cipher) != nullptr) {
            arr->Push((int64_t)ts_value_make_string(TsString::Create(cipher)));
        }
    }

    return arr;
}

// Legacy md5 helper (keep for backwards compatibility)
void* ts_crypto_md5(void* str) {
    TsString* s = (TsString*)str;
    std::string hash = md5(s->ToUtf8());
    return TsString::Create(hash.c_str());
}

// ============================================================================
// VTable entry points for Hash class
// These are called by the generated code when using class method syntax
// ============================================================================

void* Hash_update(void* hashObj, void* data) {
    return ts_crypto_hash_update(hashObj, data);
}

void* Hash_digest(void* hashObj, void* encoding) {
    return ts_crypto_hash_digest(hashObj, encoding);
}

void* Hash_copy(void* hashObj) {
    return ts_crypto_hash_copy(hashObj);
}

// ============================================================================
// VTable entry points for Hmac class
// ============================================================================

void* Hmac_update(void* hmacObj, void* data) {
    return ts_crypto_hmac_update(hmacObj, data);
}

void* Hmac_digest(void* hmacObj, void* encoding) {
    return ts_crypto_hmac_digest(hmacObj, encoding);
}

} // extern "C"
