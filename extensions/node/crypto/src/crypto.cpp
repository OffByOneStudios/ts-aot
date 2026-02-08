#include "TsString.h"
#include "TsBuffer.h"
#include "TsObject.h"
#include "TsArray.h"
#include "TsMap.h"
#include "TsRuntime.h"
#include "GC.h"
#include "md5.h"

#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <openssl/kdf.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/ec.h>

#include <uv.h>

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

// ============================================================================
// ASYNC Key Derivation Functions (using libuv thread pool)
// ============================================================================

struct Pbkdf2WorkData {
    // Input data (copied for thread safety)
    unsigned char* passData;
    size_t passLen;
    unsigned char* saltData;
    size_t saltLen;
    int iterations;
    int keylen;
    const EVP_MD* md;
    void* callback;
    // Output
    TsBuffer* result;
    bool success;
};

static void pbkdf2_work_cb(uv_work_t* req) {
    Pbkdf2WorkData* data = (Pbkdf2WorkData*)req->data;
    data->result = TsBuffer::Create((size_t)data->keylen);
    data->success = PKCS5_PBKDF2_HMAC(
        (const char*)data->passData, (int)data->passLen,
        data->saltData, (int)data->saltLen,
        data->iterations,
        data->md,
        data->keylen,
        data->result->GetData()) == 1;
}

static void pbkdf2_after_work_cb(uv_work_t* req, int status) {
    Pbkdf2WorkData* data = (Pbkdf2WorkData*)req->data;

    TsValue* err = nullptr;
    TsValue* result = nullptr;

    if (!data->success) {
        err = (TsValue*)ts_error_create(TsString::Create("PBKDF2 failed"));
        result = ts_value_make_undefined();
    } else {
        err = ts_value_make_undefined();
        result = ts_value_make_object(data->result);
    }

    // Call callback(err, derivedKey)
    if (data->callback) {
        TsValue* args[2] = { err, result };
        ts_function_call((TsValue*)data->callback, 2, args);
    }

    // Cleanup (GC will handle TsBuffer)
    // Note: passData and saltData are GC-allocated, don't free
    delete req;
}

// crypto.pbkdf2(password, salt, iterations, keylen, digest, callback)
void ts_crypto_pbkdf2(void* password, void* salt, int64_t iterations,
                      int64_t keylen, void* digest, void* callback) {
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

    if (!passData || !saltData) {
        if (callback) {
            TsValue* err = (TsValue*)ts_error_create(TsString::Create("Invalid password or salt"));
            TsValue* result = ts_value_make_undefined();
            TsValue* args[2] = { err, result };
            ts_function_call((TsValue*)callback, 2, args);
        }
        return;
    }

    // Get digest algorithm
    const EVP_MD* md = EVP_sha256(); // default
    if (digest) {
        void* rawDigest = ts_value_get_string((TsValue*)digest);
        if (rawDigest) {
            TsString* digestStr = (TsString*)rawDigest;
            const EVP_MD* customMd = EVP_get_digestbyname(digestStr->ToUtf8());
            if (customMd) md = customMd;
        }
    }

    // Copy data for thread safety
    Pbkdf2WorkData* data = (Pbkdf2WorkData*)ts_alloc(sizeof(Pbkdf2WorkData));
    data->passData = (unsigned char*)ts_alloc(passLen);
    memcpy(data->passData, passData, passLen);
    data->passLen = passLen;
    data->saltData = (unsigned char*)ts_alloc(saltLen);
    memcpy(data->saltData, saltData, saltLen);
    data->saltLen = saltLen;
    data->iterations = (int)iterations;
    data->keylen = (int)keylen;
    data->md = md;
    data->callback = callback;
    data->result = nullptr;
    data->success = false;

    uv_work_t* req = new uv_work_t();
    req->data = data;

    uv_queue_work(uv_default_loop(), req, pbkdf2_work_cb, pbkdf2_after_work_cb);
}

struct ScryptWorkData {
    // Input data (copied for thread safety)
    unsigned char* passData;
    size_t passLen;
    unsigned char* saltData;
    size_t saltLen;
    size_t keylen;
    uint64_t N;
    uint64_t r;
    uint64_t p;
    void* callback;
    // Output
    TsBuffer* result;
    bool success;
};

static void scrypt_work_cb(uv_work_t* req) {
    ScryptWorkData* data = (ScryptWorkData*)req->data;
    data->result = TsBuffer::Create(data->keylen);
    data->success = EVP_PBE_scrypt(
        (const char*)data->passData, data->passLen,
        data->saltData, data->saltLen,
        data->N, data->r, data->p,
        0, // maxmem (0 = default)
        data->result->GetData(), data->keylen) == 1;
}

static void scrypt_after_work_cb(uv_work_t* req, int status) {
    ScryptWorkData* data = (ScryptWorkData*)req->data;

    TsValue* err = nullptr;
    TsValue* result = nullptr;

    if (!data->success) {
        err = (TsValue*)ts_error_create(TsString::Create("scrypt failed"));
        result = ts_value_make_undefined();
    } else {
        err = ts_value_make_undefined();
        result = ts_value_make_object(data->result);
    }

    // Call callback(err, derivedKey)
    if (data->callback) {
        TsValue* args[2] = { err, result };
        ts_function_call((TsValue*)data->callback, 2, args);
    }

    // Cleanup
    delete req;
}

// crypto.scrypt(password, salt, keylen, options, callback)
void ts_crypto_scrypt(void* password, void* salt, int64_t keylen,
                      int64_t N, int64_t r, int64_t p, void* callback) {
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

    if (!passData || !saltData) {
        if (callback) {
            TsValue* err = (TsValue*)ts_error_create(TsString::Create("Invalid password or salt"));
            TsValue* result = ts_value_make_undefined();
            TsValue* args[2] = { err, result };
            ts_function_call((TsValue*)callback, 2, args);
        }
        return;
    }

    // Default scrypt parameters
    if (N <= 0) N = 16384;  // 2^14
    if (r <= 0) r = 8;
    if (p <= 0) p = 1;

    // Copy data for thread safety
    ScryptWorkData* data = (ScryptWorkData*)ts_alloc(sizeof(ScryptWorkData));
    data->passData = (unsigned char*)ts_alloc(passLen);
    memcpy(data->passData, passData, passLen);
    data->passLen = passLen;
    data->saltData = (unsigned char*)ts_alloc(saltLen);
    memcpy(data->saltData, saltData, saltLen);
    data->saltLen = saltLen;
    data->keylen = (size_t)keylen;
    data->N = (uint64_t)N;
    data->r = (uint64_t)r;
    data->p = (uint64_t)p;
    data->callback = callback;
    data->result = nullptr;
    data->success = false;

    uv_work_t* req = new uv_work_t();
    req->data = data;

    uv_queue_work(uv_default_loop(), req, scrypt_work_cb, scrypt_after_work_cb);
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
    // Unbox if needed - JS files pass boxed TsValue*
    void* raw = ts_value_get_string((TsValue*)str);
    if (!raw) raw = str;
    TsString* s = (TsString*)raw;
    std::string hash = md5(s->ToUtf8());
    return TsString::Create(hash.c_str());
}

// ============================================================================
// Cipher class - wraps OpenSSL EVP_CIPHER_CTX for symmetric encryption
// ============================================================================

class TsCryptoCipher : public TsObject {
public:
    static constexpr uint32_t MAGIC = 0x43495048; // "CIPH"

    EVP_CIPHER_CTX* ctx;
    const EVP_CIPHER* cipher;
    bool finalized;
    bool isGCM;  // Track if using GCM mode for auth tag handling
    unsigned char authTag[16];  // For GCM mode
    int authTagLen;

    static TsCryptoCipher* Create(const char* algorithm, const void* key, size_t keyLen,
                                   const void* iv, size_t ivLen) {
        const EVP_CIPHER* cipher = EVP_get_cipherbyname(algorithm);
        if (!cipher) return nullptr;

        void* mem = ts_alloc(sizeof(TsCryptoCipher));
        TsCryptoCipher* c = new(mem) TsCryptoCipher();
        c->magic = MAGIC;
        c->cipher = cipher;
        c->finalized = false;
        c->isGCM = (strstr(algorithm, "gcm") != nullptr || strstr(algorithm, "GCM") != nullptr);
        c->authTagLen = 0;
        c->ctx = EVP_CIPHER_CTX_new();

        if (!c->ctx) return nullptr;

        // Initialize cipher context for encryption
        if (EVP_EncryptInit_ex(c->ctx, cipher, nullptr,
                               (const unsigned char*)key,
                               (const unsigned char*)iv) != 1) {
            EVP_CIPHER_CTX_free(c->ctx);
            return nullptr;
        }

        return c;
    }

    ~TsCryptoCipher() {
        if (ctx) {
            EVP_CIPHER_CTX_free(ctx);
            ctx = nullptr;
        }
    }

    TsBuffer* Update(const void* data, size_t len) {
        if (finalized || !ctx) return nullptr;

        int outLen = (int)len + EVP_CIPHER_CTX_block_size(ctx);
        TsBuffer* buf = TsBuffer::Create(outLen);

        if (EVP_EncryptUpdate(ctx, buf->GetData(), &outLen,
                              (const unsigned char*)data, (int)len) != 1) {
            return nullptr;
        }

        // Resize buffer to actual output length
        if ((size_t)outLen < buf->GetLength()) {
            TsBuffer* resized = TsBuffer::Create(outLen);
            memcpy(resized->GetData(), buf->GetData(), outLen);
            return resized;
        }
        return buf;
    }

    TsBuffer* Final() {
        if (finalized || !ctx) return nullptr;
        finalized = true;

        int outLen = EVP_CIPHER_CTX_block_size(ctx);
        TsBuffer* buf = TsBuffer::Create(outLen);

        if (EVP_EncryptFinal_ex(ctx, buf->GetData(), &outLen) != 1) {
            return nullptr;
        }

        // For GCM mode, get the authentication tag
        if (isGCM) {
            authTagLen = 16;
            EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, authTag);
        }

        // Resize buffer to actual output length
        if ((size_t)outLen < buf->GetLength()) {
            TsBuffer* resized = TsBuffer::Create(outLen);
            memcpy(resized->GetData(), buf->GetData(), outLen);
            return resized;
        }
        return buf;
    }

    TsBuffer* GetAuthTag() {
        if (!isGCM || authTagLen == 0) return nullptr;
        TsBuffer* buf = TsBuffer::Create(authTagLen);
        memcpy(buf->GetData(), authTag, authTagLen);
        return buf;
    }

    bool SetAAD(const void* data, size_t len) {
        if (finalized || !ctx || !isGCM) return false;
        int outLen;
        return EVP_EncryptUpdate(ctx, nullptr, &outLen,
                                 (const unsigned char*)data, (int)len) == 1;
    }
};

// ============================================================================
// Decipher class - wraps OpenSSL EVP_CIPHER_CTX for symmetric decryption
// ============================================================================

class TsCryptoDecipher : public TsObject {
public:
    static constexpr uint32_t MAGIC = 0x44454349; // "DECI"

    EVP_CIPHER_CTX* ctx;
    const EVP_CIPHER* cipher;
    bool finalized;
    bool isGCM;
    bool authTagSet;

    static TsCryptoDecipher* Create(const char* algorithm, const void* key, size_t keyLen,
                                     const void* iv, size_t ivLen) {
        const EVP_CIPHER* cipher = EVP_get_cipherbyname(algorithm);
        if (!cipher) return nullptr;

        void* mem = ts_alloc(sizeof(TsCryptoDecipher));
        TsCryptoDecipher* d = new(mem) TsCryptoDecipher();
        d->magic = MAGIC;
        d->cipher = cipher;
        d->finalized = false;
        d->isGCM = (strstr(algorithm, "gcm") != nullptr || strstr(algorithm, "GCM") != nullptr);
        d->authTagSet = false;
        d->ctx = EVP_CIPHER_CTX_new();

        if (!d->ctx) return nullptr;

        // Initialize cipher context for decryption
        if (EVP_DecryptInit_ex(d->ctx, cipher, nullptr,
                               (const unsigned char*)key,
                               (const unsigned char*)iv) != 1) {
            EVP_CIPHER_CTX_free(d->ctx);
            return nullptr;
        }

        return d;
    }

    ~TsCryptoDecipher() {
        if (ctx) {
            EVP_CIPHER_CTX_free(ctx);
            ctx = nullptr;
        }
    }

    TsBuffer* Update(const void* data, size_t len) {
        if (finalized || !ctx) return nullptr;

        int outLen = (int)len + EVP_CIPHER_CTX_block_size(ctx);
        TsBuffer* buf = TsBuffer::Create(outLen);

        if (EVP_DecryptUpdate(ctx, buf->GetData(), &outLen,
                              (const unsigned char*)data, (int)len) != 1) {
            return nullptr;
        }

        // Resize buffer to actual output length
        if ((size_t)outLen < buf->GetLength()) {
            TsBuffer* resized = TsBuffer::Create(outLen);
            memcpy(resized->GetData(), buf->GetData(), outLen);
            return resized;
        }
        return buf;
    }

    TsBuffer* Final() {
        if (finalized || !ctx) return nullptr;
        finalized = true;

        int outLen = EVP_CIPHER_CTX_block_size(ctx);
        TsBuffer* buf = TsBuffer::Create(outLen);

        if (EVP_DecryptFinal_ex(ctx, buf->GetData(), &outLen) != 1) {
            // Decryption failed - possibly bad padding or auth tag
            return nullptr;
        }

        // Resize buffer to actual output length
        if ((size_t)outLen < buf->GetLength()) {
            TsBuffer* resized = TsBuffer::Create(outLen);
            memcpy(resized->GetData(), buf->GetData(), outLen);
            return resized;
        }
        return buf;
    }

    bool SetAuthTag(const void* tag, size_t len) {
        if (finalized || !ctx || !isGCM) return false;
        authTagSet = true;
        return EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, (int)len, (void*)tag) == 1;
    }

    bool SetAAD(const void* data, size_t len) {
        if (finalized || !ctx || !isGCM) return false;
        int outLen;
        return EVP_DecryptUpdate(ctx, nullptr, &outLen,
                                 (const unsigned char*)data, (int)len) == 1;
    }
};

// ============================================================================
// C API - Cipher functions
// ============================================================================

// crypto.createCipheriv(algorithm, key, iv) -> Cipher
void* ts_crypto_createCipheriv(void* algorithm, void* key, void* iv) {
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
            TsObject* obj = (TsObject*)raw;
            if (obj->magic == TsBuffer::MAGIC) {
                TsBuffer* buf = (TsBuffer*)obj;
                keyData = buf->GetData();
                keyLen = buf->GetLength();
            } else {
                TsString* keyStr = (TsString*)raw;
                keyData = keyStr->ToUtf8();
                keyLen = strlen((const char*)keyData);
            }
        }
    }

    // Extract IV data
    const void* ivData = nullptr;
    size_t ivLen = 0;

    TsValue* ivVal = (TsValue*)iv;
    if (ivVal->type == ValueType::STRING_PTR) {
        TsString* ivStr = (TsString*)ivVal->ptr_val;
        ivData = ivStr->ToUtf8();
        ivLen = strlen((const char*)ivData);
    } else if (ivVal->type == ValueType::OBJECT_PTR) {
        TsObject* obj = (TsObject*)ivVal->ptr_val;
        if (obj && obj->magic == TsBuffer::MAGIC) {
            TsBuffer* buf = (TsBuffer*)obj;
            ivData = buf->GetData();
            ivLen = buf->GetLength();
        }
    } else {
        void* raw = ts_value_get_object(ivVal);
        if (raw) {
            TsObject* obj = (TsObject*)raw;
            if (obj->magic == TsBuffer::MAGIC) {
                TsBuffer* buf = (TsBuffer*)obj;
                ivData = buf->GetData();
                ivLen = buf->GetLength();
            } else {
                TsString* ivStr = (TsString*)raw;
                ivData = ivStr->ToUtf8();
                ivLen = strlen((const char*)ivData);
            }
        }
    }

    if (!keyData) return nullptr;

    return TsCryptoCipher::Create(algStr, keyData, keyLen, ivData, ivLen);
}

// cipher.update(data) -> Buffer
void* ts_crypto_cipher_update(void* cipherObj, void* data) {
    TsCryptoCipher* cipher = (TsCryptoCipher*)cipherObj;
    if (!cipher || cipher->magic != TsCryptoCipher::MAGIC) return nullptr;

    // Handle string or buffer
    TsValue* val = (TsValue*)data;
    if (!val) return nullptr;

    if (val->type == ValueType::STRING_PTR) {
        TsString* str = (TsString*)val->ptr_val;
        return cipher->Update(str->ToUtf8(), strlen(str->ToUtf8()));
    } else if (val->type == ValueType::OBJECT_PTR) {
        TsObject* obj = (TsObject*)val->ptr_val;
        if (obj && obj->magic == TsBuffer::MAGIC) {
            TsBuffer* buf = (TsBuffer*)obj;
            return cipher->Update(buf->GetData(), buf->GetLength());
        }
    } else {
        void* raw = ts_value_get_object(val);
        if (raw) {
            TsObject* obj = (TsObject*)raw;
            if (obj->magic == TsBuffer::MAGIC) {
                TsBuffer* buf = (TsBuffer*)obj;
                return cipher->Update(buf->GetData(), buf->GetLength());
            } else {
                TsString* str = (TsString*)raw;
                return cipher->Update(str->ToUtf8(), strlen(str->ToUtf8()));
            }
        }
    }

    return nullptr;
}

// cipher.final() -> Buffer
void* ts_crypto_cipher_final(void* cipherObj) {
    TsCryptoCipher* cipher = (TsCryptoCipher*)cipherObj;
    if (!cipher || cipher->magic != TsCryptoCipher::MAGIC) return nullptr;
    return cipher->Final();
}

// cipher.getAuthTag() -> Buffer (for GCM mode)
void* ts_crypto_cipher_getAuthTag(void* cipherObj) {
    TsCryptoCipher* cipher = (TsCryptoCipher*)cipherObj;
    if (!cipher || cipher->magic != TsCryptoCipher::MAGIC) return nullptr;
    return cipher->GetAuthTag();
}

// cipher.setAAD(buffer) -> Cipher (for GCM mode)
void* ts_crypto_cipher_setAAD(void* cipherObj, void* data) {
    TsCryptoCipher* cipher = (TsCryptoCipher*)cipherObj;
    if (!cipher || cipher->magic != TsCryptoCipher::MAGIC) return cipherObj;

    TsValue* val = (TsValue*)data;
    if (!val) return cipherObj;

    if (val->type == ValueType::OBJECT_PTR) {
        TsObject* obj = (TsObject*)val->ptr_val;
        if (obj && obj->magic == TsBuffer::MAGIC) {
            TsBuffer* buf = (TsBuffer*)obj;
            cipher->SetAAD(buf->GetData(), buf->GetLength());
        }
    }

    return cipherObj;
}

// crypto.createDecipheriv(algorithm, key, iv) -> Decipher
void* ts_crypto_createDecipheriv(void* algorithm, void* key, void* iv) {
    TsString* alg = (TsString*)algorithm;
    if (!alg) return nullptr;

    const char* algStr = alg->ToUtf8();

    // Extract key data (same logic as cipher)
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
            TsObject* obj = (TsObject*)raw;
            if (obj->magic == TsBuffer::MAGIC) {
                TsBuffer* buf = (TsBuffer*)obj;
                keyData = buf->GetData();
                keyLen = buf->GetLength();
            } else {
                TsString* keyStr = (TsString*)raw;
                keyData = keyStr->ToUtf8();
                keyLen = strlen((const char*)keyData);
            }
        }
    }

    // Extract IV data
    const void* ivData = nullptr;
    size_t ivLen = 0;

    TsValue* ivVal = (TsValue*)iv;
    if (ivVal->type == ValueType::STRING_PTR) {
        TsString* ivStr = (TsString*)ivVal->ptr_val;
        ivData = ivStr->ToUtf8();
        ivLen = strlen((const char*)ivData);
    } else if (ivVal->type == ValueType::OBJECT_PTR) {
        TsObject* obj = (TsObject*)ivVal->ptr_val;
        if (obj && obj->magic == TsBuffer::MAGIC) {
            TsBuffer* buf = (TsBuffer*)obj;
            ivData = buf->GetData();
            ivLen = buf->GetLength();
        }
    } else {
        void* raw = ts_value_get_object(ivVal);
        if (raw) {
            TsObject* obj = (TsObject*)raw;
            if (obj->magic == TsBuffer::MAGIC) {
                TsBuffer* buf = (TsBuffer*)obj;
                ivData = buf->GetData();
                ivLen = buf->GetLength();
            } else {
                TsString* ivStr = (TsString*)raw;
                ivData = ivStr->ToUtf8();
                ivLen = strlen((const char*)ivData);
            }
        }
    }

    if (!keyData) return nullptr;

    return TsCryptoDecipher::Create(algStr, keyData, keyLen, ivData, ivLen);
}

// decipher.update(data) -> Buffer
void* ts_crypto_decipher_update(void* decipherObj, void* data) {
    TsCryptoDecipher* decipher = (TsCryptoDecipher*)decipherObj;
    if (!decipher || decipher->magic != TsCryptoDecipher::MAGIC) return nullptr;

    TsValue* val = (TsValue*)data;
    if (!val) return nullptr;

    if (val->type == ValueType::STRING_PTR) {
        TsString* str = (TsString*)val->ptr_val;
        return decipher->Update(str->ToUtf8(), strlen(str->ToUtf8()));
    } else if (val->type == ValueType::OBJECT_PTR) {
        TsObject* obj = (TsObject*)val->ptr_val;
        if (obj && obj->magic == TsBuffer::MAGIC) {
            TsBuffer* buf = (TsBuffer*)obj;
            return decipher->Update(buf->GetData(), buf->GetLength());
        }
    } else {
        void* raw = ts_value_get_object(val);
        if (raw) {
            TsObject* obj = (TsObject*)raw;
            if (obj->magic == TsBuffer::MAGIC) {
                TsBuffer* buf = (TsBuffer*)obj;
                return decipher->Update(buf->GetData(), buf->GetLength());
            } else {
                TsString* str = (TsString*)raw;
                return decipher->Update(str->ToUtf8(), strlen(str->ToUtf8()));
            }
        }
    }

    return nullptr;
}

// decipher.final() -> Buffer
void* ts_crypto_decipher_final(void* decipherObj) {
    TsCryptoDecipher* decipher = (TsCryptoDecipher*)decipherObj;
    if (!decipher || decipher->magic != TsCryptoDecipher::MAGIC) return nullptr;
    return decipher->Final();
}

// decipher.setAuthTag(tag) -> Decipher (for GCM mode)
void* ts_crypto_decipher_setAuthTag(void* decipherObj, void* tag) {
    TsCryptoDecipher* decipher = (TsCryptoDecipher*)decipherObj;
    if (!decipher || decipher->magic != TsCryptoDecipher::MAGIC) return decipherObj;

    TsValue* val = (TsValue*)tag;
    if (!val) return decipherObj;

    if (val->type == ValueType::OBJECT_PTR) {
        TsObject* obj = (TsObject*)val->ptr_val;
        if (obj && obj->magic == TsBuffer::MAGIC) {
            TsBuffer* buf = (TsBuffer*)obj;
            decipher->SetAuthTag(buf->GetData(), buf->GetLength());
        }
    } else {
        void* raw = ts_value_get_object(val);
        if (raw) {
            TsObject* obj = (TsObject*)raw;
            if (obj->magic == TsBuffer::MAGIC) {
                TsBuffer* buf = (TsBuffer*)obj;
                decipher->SetAuthTag(buf->GetData(), buf->GetLength());
            }
        }
    }

    return decipherObj;
}

// decipher.setAAD(buffer) -> Decipher (for GCM mode)
void* ts_crypto_decipher_setAAD(void* decipherObj, void* data) {
    TsCryptoDecipher* decipher = (TsCryptoDecipher*)decipherObj;
    if (!decipher || decipher->magic != TsCryptoDecipher::MAGIC) return decipherObj;

    TsValue* val = (TsValue*)data;
    if (!val) return decipherObj;

    if (val->type == ValueType::OBJECT_PTR) {
        TsObject* obj = (TsObject*)val->ptr_val;
        if (obj && obj->magic == TsBuffer::MAGIC) {
            TsBuffer* buf = (TsBuffer*)obj;
            decipher->SetAAD(buf->GetData(), buf->GetLength());
        }
    }

    return decipherObj;
}

// ============================================================================
// VTable entry points for Cipher class
// ============================================================================

void* Cipher_update(void* cipherObj, void* data) {
    return ts_crypto_cipher_update(cipherObj, data);
}

void* Cipher_final(void* cipherObj) {
    return ts_crypto_cipher_final(cipherObj);
}

void* Cipher_getAuthTag(void* cipherObj) {
    return ts_crypto_cipher_getAuthTag(cipherObj);
}

void* Cipher_setAAD(void* cipherObj, void* data) {
    return ts_crypto_cipher_setAAD(cipherObj, data);
}

// ============================================================================
// VTable entry points for Decipher class
// ============================================================================

void* Decipher_update(void* decipherObj, void* data) {
    return ts_crypto_decipher_update(decipherObj, data);
}

void* Decipher_final(void* decipherObj) {
    return ts_crypto_decipher_final(decipherObj);
}

void* Decipher_setAuthTag(void* decipherObj, void* tag) {
    return ts_crypto_decipher_setAuthTag(decipherObj, tag);
}

void* Decipher_setAAD(void* decipherObj, void* data) {
    return ts_crypto_decipher_setAAD(decipherObj, data);
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

// ============================================================================
// Sign class - wraps OpenSSL EVP_MD_CTX for digital signature creation
// Accumulates data during update() calls, signs when sign() is called
// ============================================================================

class TsCryptoSign : public TsObject {
public:
    static constexpr uint32_t MAGIC = 0x5349474E; // "SIGN"

    const EVP_MD* md;
    bool finalized;
    // Accumulated data buffer
    uint8_t* data;
    size_t dataLen;
    size_t dataCapacity;

    static TsCryptoSign* Create(const char* algorithm) {
        const EVP_MD* md = EVP_get_digestbyname(algorithm);
        if (!md) return nullptr;

        void* mem = ts_alloc(sizeof(TsCryptoSign));
        TsCryptoSign* sign = new(mem) TsCryptoSign();
        sign->magic = MAGIC;
        sign->md = md;
        sign->finalized = false;
        sign->data = nullptr;
        sign->dataLen = 0;
        sign->dataCapacity = 0;

        return sign;
    }

    ~TsCryptoSign() {
        // Data is GC allocated, no need to free
    }

    bool Update(const void* newData, size_t len) {
        if (finalized) return false;
        if (len == 0) return true;

        // Ensure capacity
        if (dataLen + len > dataCapacity) {
            size_t newCap = dataCapacity == 0 ? 256 : dataCapacity * 2;
            while (newCap < dataLen + len) newCap *= 2;
            uint8_t* newBuf = (uint8_t*)ts_alloc(newCap);
            if (data && dataLen > 0) {
                memcpy(newBuf, data, dataLen);
            }
            data = newBuf;
            dataCapacity = newCap;
        }

        memcpy(data + dataLen, newData, len);
        dataLen += len;
        return true;
    }

    TsBuffer* Sign(EVP_PKEY* pkey) {
        if (finalized || !pkey) return nullptr;
        finalized = true;

        // Initialize signing context
        EVP_MD_CTX* ctx = EVP_MD_CTX_new();
        if (!ctx) return nullptr;

        if (EVP_DigestSignInit(ctx, nullptr, md, nullptr, pkey) != 1) {
            EVP_MD_CTX_free(ctx);
            return nullptr;
        }

        // Add accumulated data
        if (data && dataLen > 0) {
            if (EVP_DigestSignUpdate(ctx, data, dataLen) != 1) {
                EVP_MD_CTX_free(ctx);
                return nullptr;
            }
        }

        // Get signature length first
        size_t sigLen = 0;
        if (EVP_DigestSignFinal(ctx, nullptr, &sigLen) != 1) {
            EVP_MD_CTX_free(ctx);
            return nullptr;
        }

        TsBuffer* sigBuf = TsBuffer::Create(sigLen);
        if (EVP_DigestSignFinal(ctx, sigBuf->GetData(), &sigLen) != 1) {
            EVP_MD_CTX_free(ctx);
            return nullptr;
        }

        EVP_MD_CTX_free(ctx);

        // Resize if needed (RSA signatures are usually exact size)
        if (sigLen < sigBuf->GetLength()) {
            TsBuffer* resized = TsBuffer::Create(sigLen);
            memcpy(resized->GetData(), sigBuf->GetData(), sigLen);
            return resized;
        }

        return sigBuf;
    }
};

// ============================================================================
// Verify class - wraps OpenSSL EVP_MD_CTX for signature verification
// Accumulates data during update() calls, verifies when verify() is called
// ============================================================================

class TsCryptoVerify : public TsObject {
public:
    static constexpr uint32_t MAGIC = 0x56455249; // "VERI"

    const EVP_MD* md;
    bool finalized;
    // Accumulated data buffer
    uint8_t* data;
    size_t dataLen;
    size_t dataCapacity;

    static TsCryptoVerify* Create(const char* algorithm) {
        const EVP_MD* md = EVP_get_digestbyname(algorithm);
        if (!md) return nullptr;

        void* mem = ts_alloc(sizeof(TsCryptoVerify));
        TsCryptoVerify* verify = new(mem) TsCryptoVerify();
        verify->magic = MAGIC;
        verify->md = md;
        verify->finalized = false;
        verify->data = nullptr;
        verify->dataLen = 0;
        verify->dataCapacity = 0;

        return verify;
    }

    ~TsCryptoVerify() {
        // Data is GC allocated, no need to free
    }

    bool Update(const void* newData, size_t len) {
        if (finalized) return false;
        if (len == 0) return true;

        // Ensure capacity
        if (dataLen + len > dataCapacity) {
            size_t newCap = dataCapacity == 0 ? 256 : dataCapacity * 2;
            while (newCap < dataLen + len) newCap *= 2;
            uint8_t* newBuf = (uint8_t*)ts_alloc(newCap);
            if (data && dataLen > 0) {
                memcpy(newBuf, data, dataLen);
            }
            data = newBuf;
            dataCapacity = newCap;
        }

        memcpy(data + dataLen, newData, len);
        dataLen += len;
        return true;
    }

    bool Verify(EVP_PKEY* pkey, const void* sig, size_t sigLen) {
        if (finalized || !pkey) return false;
        finalized = true;

        EVP_MD_CTX* ctx = EVP_MD_CTX_new();
        if (!ctx) return false;

        if (EVP_DigestVerifyInit(ctx, nullptr, md, nullptr, pkey) != 1) {
            EVP_MD_CTX_free(ctx);
            return false;
        }

        // Add accumulated data
        if (data && dataLen > 0) {
            if (EVP_DigestVerifyUpdate(ctx, data, dataLen) != 1) {
                EVP_MD_CTX_free(ctx);
                return false;
            }
        }

        int result = EVP_DigestVerifyFinal(ctx, (const unsigned char*)sig, sigLen);
        EVP_MD_CTX_free(ctx);

        return result == 1;
    }
};

// Helper function to load a private key from PEM
static EVP_PKEY* LoadPrivateKey(const void* keyData, size_t keyLen) {
    BIO* bio = BIO_new_mem_buf(keyData, (int)keyLen);
    if (!bio) return nullptr;

    EVP_PKEY* pkey = PEM_read_bio_PrivateKey(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);
    return pkey;
}

// Helper function to load a public key from PEM
static EVP_PKEY* LoadPublicKey(const void* keyData, size_t keyLen) {
    BIO* bio = BIO_new_mem_buf(keyData, (int)keyLen);
    if (!bio) return nullptr;

    // Try reading as public key first
    EVP_PKEY* pkey = PEM_read_bio_PUBKEY(bio, nullptr, nullptr, nullptr);
    if (!pkey) {
        // Try reading as certificate to extract public key
        BIO_reset(bio);
        X509* cert = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);
        if (cert) {
            pkey = X509_get_pubkey(cert);
            X509_free(cert);
        }
    }
    BIO_free(bio);
    return pkey;
}

// Helper to extract key data from TsValue (string or buffer)
static bool ExtractKeyData(void* key, const void** outData, size_t* outLen) {
    TsValue* keyVal = (TsValue*)key;
    if (!keyVal) return false;

    if (keyVal->type == ValueType::STRING_PTR) {
        TsString* keyStr = (TsString*)keyVal->ptr_val;
        *outData = keyStr->ToUtf8();
        *outLen = strlen((const char*)*outData);
        return true;
    } else if (keyVal->type == ValueType::OBJECT_PTR) {
        TsObject* obj = (TsObject*)keyVal->ptr_val;
        if (obj && obj->magic == TsBuffer::MAGIC) {
            TsBuffer* buf = (TsBuffer*)obj;
            *outData = buf->GetData();
            *outLen = buf->GetLength();
            return true;
        }
    } else {
        void* raw = ts_value_get_object(keyVal);
        if (raw) {
            TsObject* obj = (TsObject*)raw;
            if (obj->magic == TsBuffer::MAGIC) {
                TsBuffer* buf = (TsBuffer*)obj;
                *outData = buf->GetData();
                *outLen = buf->GetLength();
                return true;
            } else {
                TsString* keyStr = (TsString*)raw;
                *outData = keyStr->ToUtf8();
                *outLen = strlen((const char*)*outData);
                return true;
            }
        }
    }
    return false;
}

// ============================================================================
// C API - Sign/Verify functions
// ============================================================================

// crypto.createSign(algorithm) -> Sign
void* ts_crypto_createSign(void* algorithm) {
    TsString* alg = (TsString*)algorithm;
    if (!alg) return nullptr;
    return TsCryptoSign::Create(alg->ToUtf8());
}

// sign.update(data) -> Sign (chainable)
void* ts_crypto_sign_update(void* signObj, void* data) {
    TsCryptoSign* sign = (TsCryptoSign*)signObj;
    if (!sign || sign->magic != TsCryptoSign::MAGIC) return signObj;

    TsValue* val = (TsValue*)data;
    if (!val) return signObj;

    if (val->type == ValueType::STRING_PTR) {
        TsString* str = (TsString*)val->ptr_val;
        sign->Update(str->ToUtf8(), strlen(str->ToUtf8()));
    } else if (val->type == ValueType::OBJECT_PTR) {
        TsObject* obj = (TsObject*)val->ptr_val;
        if (obj && obj->magic == TsBuffer::MAGIC) {
            TsBuffer* buf = (TsBuffer*)obj;
            sign->Update(buf->GetData(), buf->GetLength());
        }
    } else {
        void* raw = ts_value_get_object(val);
        if (raw) {
            TsObject* obj = (TsObject*)raw;
            if (obj->magic == TsBuffer::MAGIC) {
                TsBuffer* buf = (TsBuffer*)obj;
                sign->Update(buf->GetData(), buf->GetLength());
            } else {
                TsString* str = (TsString*)raw;
                sign->Update(str->ToUtf8(), strlen(str->ToUtf8()));
            }
        }
    }

    return signObj;
}

// sign.sign(privateKey, outputEncoding?) -> Buffer|string
void* ts_crypto_sign_sign(void* signObj, void* privateKey, void* outputEncoding) {
    TsCryptoSign* sign = (TsCryptoSign*)signObj;
    if (!sign || sign->magic != TsCryptoSign::MAGIC) return nullptr;

    const void* keyData = nullptr;
    size_t keyLen = 0;
    if (!ExtractKeyData(privateKey, &keyData, &keyLen)) return nullptr;

    EVP_PKEY* pkey = LoadPrivateKey(keyData, keyLen);
    if (!pkey) return nullptr;

    // Use the class's Sign method which handles accumulated data
    TsBuffer* sigBuf = sign->Sign(pkey);
    EVP_PKEY_free(pkey);

    if (!sigBuf) return nullptr;

    // Check if encoding requested
    if (outputEncoding) {
        TsString* enc = nullptr;
        TsValue* encVal = (TsValue*)outputEncoding;
        if (encVal->type == ValueType::STRING_PTR) {
            enc = (TsString*)encVal->ptr_val;
        } else {
            void* raw = ts_value_get_object(encVal);
            if (raw) enc = (TsString*)raw;
        }

        if (enc) {
            const char* encStr = enc->ToUtf8();
            if (strcmp(encStr, "hex") == 0) {
                return sigBuf->ToHex();
            } else if (strcmp(encStr, "base64") == 0) {
                return sigBuf->ToBase64();
            }
        }
    }

    return sigBuf;
}

// crypto.createVerify(algorithm) -> Verify
void* ts_crypto_createVerify(void* algorithm) {
    TsString* alg = (TsString*)algorithm;
    if (!alg) return nullptr;
    return TsCryptoVerify::Create(alg->ToUtf8());
}

// verify.update(data) -> Verify (chainable)
void* ts_crypto_verify_update(void* verifyObj, void* data) {
    TsCryptoVerify* verify = (TsCryptoVerify*)verifyObj;
    if (!verify || verify->magic != TsCryptoVerify::MAGIC) return verifyObj;

    TsValue* val = (TsValue*)data;
    if (!val) return verifyObj;

    if (val->type == ValueType::STRING_PTR) {
        TsString* str = (TsString*)val->ptr_val;
        verify->Update(str->ToUtf8(), strlen(str->ToUtf8()));
    } else if (val->type == ValueType::OBJECT_PTR) {
        TsObject* obj = (TsObject*)val->ptr_val;
        if (obj && obj->magic == TsBuffer::MAGIC) {
            TsBuffer* buf = (TsBuffer*)obj;
            verify->Update(buf->GetData(), buf->GetLength());
        }
    } else {
        void* raw = ts_value_get_object(val);
        if (raw) {
            TsObject* obj = (TsObject*)raw;
            if (obj->magic == TsBuffer::MAGIC) {
                TsBuffer* buf = (TsBuffer*)obj;
                verify->Update(buf->GetData(), buf->GetLength());
            } else {
                TsString* str = (TsString*)raw;
                verify->Update(str->ToUtf8(), strlen(str->ToUtf8()));
            }
        }
    }

    return verifyObj;
}

// verify.verify(publicKey, signature, signatureEncoding?) -> boolean
bool ts_crypto_verify_verify(void* verifyObj, void* publicKey, void* signature, void* signatureEncoding) {
    TsCryptoVerify* verify = (TsCryptoVerify*)verifyObj;
    if (!verify || verify->magic != TsCryptoVerify::MAGIC) return false;

    // Load public key
    const void* keyData = nullptr;
    size_t keyLen = 0;
    if (!ExtractKeyData(publicKey, &keyData, &keyLen)) return false;

    EVP_PKEY* pkey = LoadPublicKey(keyData, keyLen);
    if (!pkey) {
        // Try as private key (can extract public key from it)
        pkey = LoadPrivateKey(keyData, keyLen);
    }
    if (!pkey) return false;

    // Extract signature data
    const void* sigData = nullptr;
    size_t sigLen = 0;

    TsValue* sigVal = (TsValue*)signature;
    TsBuffer* sigBuf = nullptr;

    if (sigVal->type == ValueType::STRING_PTR) {
        TsString* sigStr = (TsString*)sigVal->ptr_val;
        // Check if encoding specified - need to decode
        if (signatureEncoding) {
            TsString* enc = nullptr;
            TsValue* encVal = (TsValue*)signatureEncoding;
            if (encVal->type == ValueType::STRING_PTR) {
                enc = (TsString*)encVal->ptr_val;
            } else {
                void* raw = ts_value_get_object(encVal);
                if (raw) enc = (TsString*)raw;
            }

            if (enc) {
                const char* encStr = enc->ToUtf8();
                if (strcmp(encStr, "hex") == 0) {
                    sigBuf = TsBuffer::FromHex(sigStr);
                } else if (strcmp(encStr, "base64") == 0) {
                    sigBuf = TsBuffer::FromBase64(sigStr);
                }
            }
        }

        if (sigBuf) {
            sigData = sigBuf->GetData();
            sigLen = sigBuf->GetLength();
        } else {
            sigData = sigStr->ToUtf8();
            sigLen = strlen((const char*)sigData);
        }
    } else if (sigVal->type == ValueType::OBJECT_PTR) {
        TsObject* obj = (TsObject*)sigVal->ptr_val;
        if (obj && obj->magic == TsBuffer::MAGIC) {
            TsBuffer* buf = (TsBuffer*)obj;
            sigData = buf->GetData();
            sigLen = buf->GetLength();
        }
    } else {
        void* raw = ts_value_get_object(sigVal);
        if (raw) {
            TsObject* obj = (TsObject*)raw;
            if (obj->magic == TsBuffer::MAGIC) {
                TsBuffer* buf = (TsBuffer*)obj;
                sigData = buf->GetData();
                sigLen = buf->GetLength();
            }
        }
    }

    if (!sigData || sigLen == 0) {
        EVP_PKEY_free(pkey);
        return false;
    }

    // Use the class's Verify method which handles accumulated data
    bool result = verify->Verify(pkey, sigData, sigLen);
    EVP_PKEY_free(pkey);

    return result;
}

// ============================================================================
// One-shot crypto.sign() and crypto.verify() functions
// ============================================================================

// crypto.sign(algorithm, data, key) -> Buffer
void* ts_crypto_sign_oneshot(void* algorithm, void* data, void* key) {
    TsString* alg = (TsString*)algorithm;
    if (!alg) return nullptr;

    const EVP_MD* md = EVP_get_digestbyname(alg->ToUtf8());
    if (!md) return nullptr;

    // Extract data
    const void* dataPtr = nullptr;
    size_t dataLen = 0;
    if (!ExtractKeyData(data, &dataPtr, &dataLen)) return nullptr;

    // Extract key
    const void* keyData = nullptr;
    size_t keyLen = 0;
    if (!ExtractKeyData(key, &keyData, &keyLen)) return nullptr;

    EVP_PKEY* pkey = LoadPrivateKey(keyData, keyLen);
    if (!pkey) return nullptr;

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) {
        EVP_PKEY_free(pkey);
        return nullptr;
    }

    if (EVP_DigestSignInit(ctx, nullptr, md, nullptr, pkey) != 1) {
        EVP_MD_CTX_free(ctx);
        EVP_PKEY_free(pkey);
        return nullptr;
    }

    // Use two-step API: Update then Final
    if (EVP_DigestSignUpdate(ctx, dataPtr, dataLen) != 1) {
        EVP_MD_CTX_free(ctx);
        EVP_PKEY_free(pkey);
        return nullptr;
    }

    // Get required signature size
    size_t sigLen = 0;
    if (EVP_DigestSignFinal(ctx, nullptr, &sigLen) != 1) {
        EVP_MD_CTX_free(ctx);
        EVP_PKEY_free(pkey);
        return nullptr;
    }

    TsBuffer* sigBuf = TsBuffer::Create(sigLen);
    memset(sigBuf->GetData(), 0, sigLen);  // Zero-initialize
    size_t actualSigLen = sigLen;
    int signResult = EVP_DigestSignFinal(ctx, sigBuf->GetData(), &actualSigLen);
    if (signResult != 1) {
        EVP_MD_CTX_free(ctx);
        EVP_PKEY_free(pkey);
        return nullptr;
    }

    EVP_MD_CTX_free(ctx);
    EVP_PKEY_free(pkey);

    // Resize if needed
    if (actualSigLen < sigLen) {
        TsBuffer* resized = TsBuffer::Create(actualSigLen);
        memcpy(resized->GetData(), sigBuf->GetData(), actualSigLen);
        return resized;
    }

    return sigBuf;
}

// crypto.verify(algorithm, data, key, signature) -> boolean
bool ts_crypto_verify_oneshot(void* algorithm, void* data, void* key, void* signature) {
    TsString* alg = (TsString*)algorithm;
    if (!alg) {
        return false;
    }

    const EVP_MD* md = EVP_get_digestbyname(alg->ToUtf8());
    if (!md) {
        return false;
    }

    // Extract data
    const void* dataPtr = nullptr;
    size_t dataLen = 0;
    if (!ExtractKeyData(data, &dataPtr, &dataLen)) {
        return false;
    }

    // Extract key
    const void* keyData = nullptr;
    size_t keyLen = 0;
    if (!ExtractKeyData(key, &keyData, &keyLen)) return false;

    EVP_PKEY* pkey = LoadPublicKey(keyData, keyLen);
    if (!pkey) pkey = LoadPrivateKey(keyData, keyLen);
    if (!pkey) {
        return false;
    }

    // Extract signature - may be passed as raw Buffer* or boxed TsValue*
    const void* sigData = nullptr;
    size_t sigLen = 0;

    // Try ExtractKeyData which handles TsValue, TsBuffer, TsString
    if (!ExtractKeyData(signature, &sigData, &sigLen)) {
        EVP_PKEY_free(pkey);
        return false;
    }

    if (!sigData || sigLen == 0) {
        EVP_PKEY_free(pkey);
        return false;
    }

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) {
        EVP_PKEY_free(pkey);
        return false;
    }

    if (EVP_DigestVerifyInit(ctx, nullptr, md, nullptr, pkey) != 1) {
        EVP_MD_CTX_free(ctx);
        EVP_PKEY_free(pkey);
        return false;
    }

    int result = EVP_DigestVerify(ctx, (const unsigned char*)sigData, sigLen,
                                   (const unsigned char*)dataPtr, dataLen);

    EVP_MD_CTX_free(ctx);
    EVP_PKEY_free(pkey);

    return result == 1;
}

// ============================================================================
// VTable entry points for Sign class
// ============================================================================

void* Sign_update(void* signObj, void* data) {
    return ts_crypto_sign_update(signObj, data);
}

void* Sign_sign(void* signObj, void* privateKey, void* outputEncoding) {
    return ts_crypto_sign_sign(signObj, privateKey, outputEncoding);
}

// ============================================================================
// VTable entry points for Verify class
// ============================================================================

void* Verify_update(void* verifyObj, void* data) {
    return ts_crypto_verify_update(verifyObj, data);
}

bool Verify_verify(void* verifyObj, void* publicKey, void* signature, void* signatureEncoding) {
    return ts_crypto_verify_verify(verifyObj, publicKey, signature, signatureEncoding);
}

// ============================================================================
// Key Generation Functions
// ============================================================================

// Helper to convert EVP_PKEY to PEM encoded string
static TsString* PKeyToPrivatePEM(EVP_PKEY* pkey) {
    BIO* bio = BIO_new(BIO_s_mem());
    if (!bio) return nullptr;

    if (PEM_write_bio_PrivateKey(bio, pkey, nullptr, nullptr, 0, nullptr, nullptr) != 1) {
        BIO_free(bio);
        return nullptr;
    }

    char* data = nullptr;
    long len = BIO_get_mem_data(bio, &data);
    // Create null-terminated copy for TsString
    char* nullTerminated = (char*)ts_alloc(len + 1);
    memcpy(nullTerminated, data, len);
    nullTerminated[len] = '\0';
    TsString* result = TsString::Create(nullTerminated);
    BIO_free(bio);
    return result;
}

static TsString* PKeyToPublicPEM(EVP_PKEY* pkey) {
    BIO* bio = BIO_new(BIO_s_mem());
    if (!bio) return nullptr;

    if (PEM_write_bio_PUBKEY(bio, pkey) != 1) {
        BIO_free(bio);
        return nullptr;
    }

    char* data = nullptr;
    long len = BIO_get_mem_data(bio, &data);
    // Create null-terminated copy for TsString
    char* nullTerminated = (char*)ts_alloc(len + 1);
    memcpy(nullTerminated, data, len);
    nullTerminated[len] = '\0';
    TsString* result = TsString::Create(nullTerminated);
    BIO_free(bio);
    return result;
}

// crypto.generateKeyPairSync('rsa', options) -> { publicKey, privateKey }
// crypto.generateKeyPairSync('ec', options) -> { publicKey, privateKey }
void* ts_crypto_generateKeyPairSync(void* typeArg, int64_t modulusLength, void* namedCurve) {
    TsString* type = (TsString*)typeArg;
    if (!type) return nullptr;

    const char* typeStr = type->ToUtf8();
    EVP_PKEY* pkey = nullptr;
    EVP_PKEY_CTX* ctx = nullptr;

    if (strcmp(typeStr, "rsa") == 0 || strcmp(typeStr, "rsa-pss") == 0) {
        // RSA key generation
        if (modulusLength <= 0) modulusLength = 2048;

        ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr);
        if (!ctx) return nullptr;

        if (EVP_PKEY_keygen_init(ctx) <= 0) {
            EVP_PKEY_CTX_free(ctx);
            return nullptr;
        }

        if (EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, (int)modulusLength) <= 0) {
            EVP_PKEY_CTX_free(ctx);
            return nullptr;
        }

        if (EVP_PKEY_keygen(ctx, &pkey) <= 0) {
            EVP_PKEY_CTX_free(ctx);
            return nullptr;
        }

        EVP_PKEY_CTX_free(ctx);

    } else if (strcmp(typeStr, "ec") == 0 || strcmp(typeStr, "ecdsa") == 0) {
        // EC key generation
        const char* curve = "prime256v1"; // default to P-256
        if (namedCurve) {
            TsString* curveStr = (TsString*)namedCurve;
            curve = curveStr->ToUtf8();
        }

        int nid = OBJ_sn2nid(curve);
        if (nid == NID_undef) {
            // Try long name
            nid = OBJ_ln2nid(curve);
        }
        if (nid == NID_undef) {
            // Common curve name mappings
            if (strcmp(curve, "P-256") == 0 || strcmp(curve, "secp256r1") == 0) {
                nid = NID_X9_62_prime256v1;
            } else if (strcmp(curve, "P-384") == 0 || strcmp(curve, "secp384r1") == 0) {
                nid = NID_secp384r1;
            } else if (strcmp(curve, "P-521") == 0 || strcmp(curve, "secp521r1") == 0) {
                nid = NID_secp521r1;
            }
        }
        if (nid == NID_undef) return nullptr;

        ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_EC, nullptr);
        if (!ctx) return nullptr;

        if (EVP_PKEY_keygen_init(ctx) <= 0) {
            EVP_PKEY_CTX_free(ctx);
            return nullptr;
        }

        if (EVP_PKEY_CTX_set_ec_paramgen_curve_nid(ctx, nid) <= 0) {
            EVP_PKEY_CTX_free(ctx);
            return nullptr;
        }

        if (EVP_PKEY_keygen(ctx, &pkey) <= 0) {
            EVP_PKEY_CTX_free(ctx);
            return nullptr;
        }

        EVP_PKEY_CTX_free(ctx);

    } else if (strcmp(typeStr, "ed25519") == 0) {
        // Ed25519 key generation
        ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, nullptr);
        if (!ctx) return nullptr;

        if (EVP_PKEY_keygen_init(ctx) <= 0) {
            EVP_PKEY_CTX_free(ctx);
            return nullptr;
        }

        if (EVP_PKEY_keygen(ctx, &pkey) <= 0) {
            EVP_PKEY_CTX_free(ctx);
            return nullptr;
        }

        EVP_PKEY_CTX_free(ctx);

    } else if (strcmp(typeStr, "x25519") == 0) {
        // X25519 key generation
        ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_X25519, nullptr);
        if (!ctx) return nullptr;

        if (EVP_PKEY_keygen_init(ctx) <= 0) {
            EVP_PKEY_CTX_free(ctx);
            return nullptr;
        }

        if (EVP_PKEY_keygen(ctx, &pkey) <= 0) {
            EVP_PKEY_CTX_free(ctx);
            return nullptr;
        }

        EVP_PKEY_CTX_free(ctx);

    } else {
        return nullptr;
    }

    if (!pkey) return nullptr;

    // Convert to PEM format
    TsString* publicKeyPEM = PKeyToPublicPEM(pkey);
    TsString* privateKeyPEM = PKeyToPrivatePEM(pkey);

    EVP_PKEY_free(pkey);

    if (!publicKeyPEM || !privateKeyPEM) return nullptr;

    // Return object with publicKey and privateKey properties
    TsMap* result = TsMap::Create();
    TsValue pubKeyVal;
    pubKeyVal.type = ValueType::STRING_PTR;
    pubKeyVal.ptr_val = publicKeyPEM;
    TsValue privKeyVal;
    privKeyVal.type = ValueType::STRING_PTR;
    privKeyVal.ptr_val = privateKeyPEM;
    TsValue pubKeyName;
    pubKeyName.type = ValueType::STRING_PTR;
    pubKeyName.ptr_val = TsString::Create("publicKey");
    TsValue privKeyName;
    privKeyName.type = ValueType::STRING_PTR;
    privKeyName.ptr_val = TsString::Create("privateKey");
    result->Set(pubKeyName, pubKeyVal);
    result->Set(privKeyName, privKeyVal);

    return result;
}

// Async key pair generation callback data
struct GenerateKeyPairWorkData {
    char* type;
    int modulusLength;
    char* namedCurve;
    void* callback;
    // Output
    TsString* publicKey;
    TsString* privateKey;
    bool success;
    char* errorMsg;
};

static void generateKeyPair_work_cb(uv_work_t* req) {
    GenerateKeyPairWorkData* data = (GenerateKeyPairWorkData*)req->data;

    EVP_PKEY* pkey = nullptr;
    EVP_PKEY_CTX* ctx = nullptr;

    if (strcmp(data->type, "rsa") == 0 || strcmp(data->type, "rsa-pss") == 0) {
        int bits = data->modulusLength > 0 ? data->modulusLength : 2048;

        ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr);
        if (!ctx) {
            data->errorMsg = strdup("Failed to create RSA context");
            data->success = false;
            return;
        }

        if (EVP_PKEY_keygen_init(ctx) <= 0 ||
            EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, bits) <= 0 ||
            EVP_PKEY_keygen(ctx, &pkey) <= 0) {
            EVP_PKEY_CTX_free(ctx);
            data->errorMsg = strdup("RSA key generation failed");
            data->success = false;
            return;
        }
        EVP_PKEY_CTX_free(ctx);

    } else if (strcmp(data->type, "ec") == 0 || strcmp(data->type, "ecdsa") == 0) {
        const char* curve = data->namedCurve ? data->namedCurve : "prime256v1";
        int nid = OBJ_sn2nid(curve);
        if (nid == NID_undef) nid = OBJ_ln2nid(curve);
        if (nid == NID_undef) {
            if (strcmp(curve, "P-256") == 0) nid = NID_X9_62_prime256v1;
            else if (strcmp(curve, "P-384") == 0) nid = NID_secp384r1;
            else if (strcmp(curve, "P-521") == 0) nid = NID_secp521r1;
        }

        if (nid == NID_undef) {
            data->errorMsg = strdup("Unknown EC curve");
            data->success = false;
            return;
        }

        ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_EC, nullptr);
        if (!ctx ||
            EVP_PKEY_keygen_init(ctx) <= 0 ||
            EVP_PKEY_CTX_set_ec_paramgen_curve_nid(ctx, nid) <= 0 ||
            EVP_PKEY_keygen(ctx, &pkey) <= 0) {
            if (ctx) EVP_PKEY_CTX_free(ctx);
            data->errorMsg = strdup("EC key generation failed");
            data->success = false;
            return;
        }
        EVP_PKEY_CTX_free(ctx);

    } else if (strcmp(data->type, "ed25519") == 0) {
        ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, nullptr);
        if (!ctx ||
            EVP_PKEY_keygen_init(ctx) <= 0 ||
            EVP_PKEY_keygen(ctx, &pkey) <= 0) {
            if (ctx) EVP_PKEY_CTX_free(ctx);
            data->errorMsg = strdup("Ed25519 key generation failed");
            data->success = false;
            return;
        }
        EVP_PKEY_CTX_free(ctx);

    } else if (strcmp(data->type, "x25519") == 0) {
        ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_X25519, nullptr);
        if (!ctx ||
            EVP_PKEY_keygen_init(ctx) <= 0 ||
            EVP_PKEY_keygen(ctx, &pkey) <= 0) {
            if (ctx) EVP_PKEY_CTX_free(ctx);
            data->errorMsg = strdup("X25519 key generation failed");
            data->success = false;
            return;
        }
        EVP_PKEY_CTX_free(ctx);

    } else {
        data->errorMsg = strdup("Unknown key type");
        data->success = false;
        return;
    }

    data->publicKey = PKeyToPublicPEM(pkey);
    data->privateKey = PKeyToPrivatePEM(pkey);
    EVP_PKEY_free(pkey);

    data->success = (data->publicKey && data->privateKey);
    if (!data->success) {
        data->errorMsg = strdup("Failed to convert keys to PEM");
    }
}

static void generateKeyPair_after_work_cb(uv_work_t* req, int status) {
    GenerateKeyPairWorkData* data = (GenerateKeyPairWorkData*)req->data;

    TsValue* err = nullptr;
    TsValue* pubKey = nullptr;
    TsValue* privKey = nullptr;

    if (!data->success) {
        err = (TsValue*)ts_error_create(TsString::Create(data->errorMsg ? data->errorMsg : "Key generation failed"));
        pubKey = ts_value_make_undefined();
        privKey = ts_value_make_undefined();
    } else {
        err = ts_value_make_undefined();
        pubKey = ts_value_make_string(data->publicKey);
        privKey = ts_value_make_string(data->privateKey);
    }

    // Call callback(err, publicKey, privateKey)
    if (data->callback) {
        TsValue* args[3] = { err, pubKey, privKey };
        ts_function_call((TsValue*)data->callback, 3, args);
    }

    // Cleanup
    if (data->type) free(data->type);
    if (data->namedCurve) free(data->namedCurve);
    if (data->errorMsg) free(data->errorMsg);
    delete req;
}

// crypto.generateKeyPair(type, options, callback)
void ts_crypto_generateKeyPair(void* typeArg, int64_t modulusLength, void* namedCurve, void* callback) {
    TsString* type = (TsString*)typeArg;
    if (!type) {
        if (callback) {
            TsValue* err = (TsValue*)ts_error_create(TsString::Create("Type is required"));
            TsValue* args[3] = { err, ts_value_make_undefined(), ts_value_make_undefined() };
            ts_function_call((TsValue*)callback, 3, args);
        }
        return;
    }

    GenerateKeyPairWorkData* data = (GenerateKeyPairWorkData*)ts_alloc(sizeof(GenerateKeyPairWorkData));
    data->type = strdup(type->ToUtf8());
    data->modulusLength = (int)modulusLength;
    data->namedCurve = namedCurve ? strdup(((TsString*)namedCurve)->ToUtf8()) : nullptr;
    data->callback = callback;
    data->publicKey = nullptr;
    data->privateKey = nullptr;
    data->success = false;
    data->errorMsg = nullptr;

    uv_work_t* req = new uv_work_t();
    req->data = data;

    uv_queue_work(uv_default_loop(), req, generateKeyPair_work_cb, generateKeyPair_after_work_cb);
}

// crypto.generateKeySync(type, options) -> Buffer (for symmetric keys like AES)
void* ts_crypto_generateKeySync(void* typeArg, int64_t length) {
    TsString* type = (TsString*)typeArg;
    if (!type) return nullptr;

    const char* typeStr = type->ToUtf8();

    // For symmetric keys, just generate random bytes
    size_t keyLen = 0;

    if (strcmp(typeStr, "aes") == 0) {
        // Default to 256-bit AES key
        keyLen = length > 0 ? (size_t)(length / 8) : 32;
    } else if (strcmp(typeStr, "hmac") == 0) {
        // Default to 256-bit HMAC key
        keyLen = length > 0 ? (size_t)(length / 8) : 32;
    } else {
        // Generic - use length directly as byte count
        keyLen = length > 0 ? (size_t)length : 32;
    }

    TsBuffer* keyBuf = TsBuffer::Create(keyLen);
    if (RAND_bytes(keyBuf->GetData(), (int)keyLen) != 1) {
        return nullptr;
    }

    return keyBuf;
}

// Async symmetric key generation
struct GenerateKeyWorkData {
    char* type;
    int length;
    void* callback;
    TsBuffer* result;
    bool success;
};

static void generateKey_work_cb(uv_work_t* req) {
    GenerateKeyWorkData* data = (GenerateKeyWorkData*)req->data;

    size_t keyLen = 0;
    if (strcmp(data->type, "aes") == 0 || strcmp(data->type, "hmac") == 0) {
        keyLen = data->length > 0 ? (size_t)(data->length / 8) : 32;
    } else {
        keyLen = data->length > 0 ? (size_t)data->length : 32;
    }

    data->result = TsBuffer::Create(keyLen);
    data->success = (RAND_bytes(data->result->GetData(), (int)keyLen) == 1);
}

static void generateKey_after_work_cb(uv_work_t* req, int status) {
    GenerateKeyWorkData* data = (GenerateKeyWorkData*)req->data;

    TsValue* err = nullptr;
    TsValue* key = nullptr;

    if (!data->success) {
        err = (TsValue*)ts_error_create(TsString::Create("Key generation failed"));
        key = ts_value_make_undefined();
    } else {
        err = ts_value_make_undefined();
        key = ts_value_make_object(data->result);
    }

    if (data->callback) {
        TsValue* args[2] = { err, key };
        ts_function_call((TsValue*)data->callback, 2, args);
    }

    if (data->type) free(data->type);
    delete req;
}

// crypto.generateKey(type, options, callback)
void ts_crypto_generateKey(void* typeArg, int64_t length, void* callback) {
    TsString* type = (TsString*)typeArg;
    if (!type) {
        if (callback) {
            TsValue* err = (TsValue*)ts_error_create(TsString::Create("Type is required"));
            TsValue* args[2] = { err, ts_value_make_undefined() };
            ts_function_call((TsValue*)callback, 2, args);
        }
        return;
    }

    GenerateKeyWorkData* data = (GenerateKeyWorkData*)ts_alloc(sizeof(GenerateKeyWorkData));
    data->type = strdup(type->ToUtf8());
    data->length = (int)length;
    data->callback = callback;
    data->result = nullptr;
    data->success = false;

    uv_work_t* req = new uv_work_t();
    req->data = data;

    uv_queue_work(uv_default_loop(), req, generateKey_work_cb, generateKey_after_work_cb);
}

// ============================================================================
// Key Object Functions (createPrivateKey, createPublicKey, createSecretKey)
// These wrap key material in a KeyObject for use with other crypto functions
// ============================================================================

// For simplicity, we'll store keys as TsBuffer or TsString and return them directly
// In a full implementation, these would be KeyObject instances

// crypto.createPrivateKey(key) -> KeyObject (returns string PEM for now)
void* ts_crypto_createPrivateKey(void* keyArg) {
    const void* keyData = nullptr;
    size_t keyLen = 0;
    if (!ExtractKeyData(keyArg, &keyData, &keyLen)) return nullptr;

    // Try to parse the key to validate it
    EVP_PKEY* pkey = LoadPrivateKey(keyData, keyLen);
    if (!pkey) return nullptr;

    // Re-encode as PEM (normalized format)
    TsString* result = PKeyToPrivatePEM(pkey);
    EVP_PKEY_free(pkey);
    return result;
}

// crypto.createPublicKey(key) -> KeyObject (returns string PEM for now)
void* ts_crypto_createPublicKey(void* keyArg) {
    const void* keyData = nullptr;
    size_t keyLen = 0;
    if (!ExtractKeyData(keyArg, &keyData, &keyLen)) return nullptr;

    // Try to parse as public key
    EVP_PKEY* pkey = LoadPublicKey(keyData, keyLen);
    if (!pkey) {
        // Try as private key and extract public
        pkey = LoadPrivateKey(keyData, keyLen);
    }
    if (!pkey) return nullptr;

    TsString* result = PKeyToPublicPEM(pkey);
    EVP_PKEY_free(pkey);
    return result;
}

// crypto.createSecretKey(key, encoding?) -> KeyObject (returns Buffer for now)
void* ts_crypto_createSecretKey(void* keyArg, void* encoding) {
    const void* keyData = nullptr;
    size_t keyLen = 0;

    TsValue* keyVal = (TsValue*)keyArg;
    if (!keyVal) return nullptr;

    if (keyVal->type == ValueType::STRING_PTR) {
        TsString* keyStr = (TsString*)keyVal->ptr_val;
        // Check encoding
        if (encoding) {
            TsString* enc = nullptr;
            TsValue* encVal = (TsValue*)encoding;
            if (encVal->type == ValueType::STRING_PTR) {
                enc = (TsString*)encVal->ptr_val;
            } else {
                void* raw = ts_value_get_object(encVal);
                if (raw) enc = (TsString*)raw;
            }

            if (enc) {
                const char* encStr = enc->ToUtf8();
                if (strcmp(encStr, "hex") == 0) {
                    return TsBuffer::FromHex(keyStr);
                } else if (strcmp(encStr, "base64") == 0) {
                    return TsBuffer::FromBase64(keyStr);
                }
            }
        }
        // Return as buffer from UTF-8
        const char* str = keyStr->ToUtf8();
        TsBuffer* buf = TsBuffer::Create(strlen(str));
        memcpy(buf->GetData(), str, strlen(str));
        return buf;

    } else if (keyVal->type == ValueType::OBJECT_PTR) {
        TsObject* obj = (TsObject*)keyVal->ptr_val;
        if (obj && obj->magic == TsBuffer::MAGIC) {
            // Already a buffer, return copy
            TsBuffer* srcBuf = (TsBuffer*)obj;
            TsBuffer* buf = TsBuffer::Create(srcBuf->GetLength());
            memcpy(buf->GetData(), srcBuf->GetData(), srcBuf->GetLength());
            return buf;
        }
    } else {
        void* raw = ts_value_get_object(keyVal);
        if (raw) {
            TsObject* obj = (TsObject*)raw;
            if (obj->magic == TsBuffer::MAGIC) {
                TsBuffer* srcBuf = (TsBuffer*)obj;
                TsBuffer* buf = TsBuffer::Create(srcBuf->GetLength());
                memcpy(buf->GetData(), srcBuf->GetData(), srcBuf->GetLength());
                return buf;
            }
        }
    }

    return nullptr;
}

// ============================================================================
// HKDF (HMAC-based Key Derivation Function) - RFC 5869
// ============================================================================

// crypto.hkdfSync(digest, ikm, salt, info, keylen) -> Buffer
void* ts_crypto_hkdfSync(void* digestArg, void* ikmArg, void* saltArg, void* infoArg, int64_t keylen) {
    // Get digest algorithm - passed directly as TsString*
    TsString* digestStr = (TsString*)digestArg;
    if (!digestStr) return nullptr;

    const char* digest = digestStr->ToUtf8();
    const EVP_MD* md = EVP_get_digestbyname(digest);
    if (!md) return nullptr;

    // Get IKM (Input Keying Material)
    const void* ikmData = nullptr;
    size_t ikmLen = 0;
    if (!ExtractKeyData(ikmArg, &ikmData, &ikmLen)) return nullptr;

    // Get salt (optional - can be null/empty)
    const void* saltData = nullptr;
    size_t saltLen = 0;
    if (saltArg) {
        TsValue* saltVal = (TsValue*)saltArg;
        // Check for undefined or null pointer (null is represented as ptr_val == nullptr with OBJECT_PTR)
        if (saltVal->type != ValueType::UNDEFINED &&
            !(saltVal->type == ValueType::OBJECT_PTR && saltVal->ptr_val == nullptr)) {
            ExtractKeyData(saltArg, &saltData, &saltLen);
        }
    }

    // Get info (optional - can be null/empty)
    const void* infoData = nullptr;
    size_t infoLen = 0;
    if (infoArg) {
        TsValue* infoVal = (TsValue*)infoArg;
        if (infoVal->type != ValueType::UNDEFINED &&
            !(infoVal->type == ValueType::OBJECT_PTR && infoVal->ptr_val == nullptr)) {
            ExtractKeyData(infoArg, &infoData, &infoLen);
        }
    }

    if (keylen <= 0) return nullptr;

    // Allocate output buffer
    TsBuffer* result = TsBuffer::Create((size_t)keylen);
    if (!result) return nullptr;

    // Perform HKDF using OpenSSL EVP_KDF
    EVP_KDF* kdf = EVP_KDF_fetch(nullptr, "HKDF", nullptr);
    if (!kdf) return nullptr;

    EVP_KDF_CTX* kctx = EVP_KDF_CTX_new(kdf);
    EVP_KDF_free(kdf);
    if (!kctx) return nullptr;

    OSSL_PARAM params[6];
    int idx = 0;

    params[idx++] = OSSL_PARAM_construct_utf8_string("digest", (char*)digest, 0);
    params[idx++] = OSSL_PARAM_construct_octet_string("key", (void*)ikmData, ikmLen);

    if (saltData && saltLen > 0) {
        params[idx++] = OSSL_PARAM_construct_octet_string("salt", (void*)saltData, saltLen);
    }

    if (infoData && infoLen > 0) {
        params[idx++] = OSSL_PARAM_construct_octet_string("info", (void*)infoData, infoLen);
    }

    params[idx] = OSSL_PARAM_construct_end();

    if (EVP_KDF_derive(kctx, result->GetData(), (size_t)keylen, params) != 1) {
        EVP_KDF_CTX_free(kctx);
        return nullptr;
    }

    EVP_KDF_CTX_free(kctx);
    return result;
}

} // extern "C"
