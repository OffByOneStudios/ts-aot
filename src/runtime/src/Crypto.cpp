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
    TsString* s = (TsString*)str;
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

} // extern "C"
