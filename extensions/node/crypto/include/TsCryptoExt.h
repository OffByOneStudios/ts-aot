#ifndef TS_CRYPTO_EXT_H
#define TS_CRYPTO_EXT_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Hash functions
void* ts_crypto_createHash(void* algorithm);
void* ts_crypto_hash_update(void* hashObj, void* data);
void* ts_crypto_hash_digest(void* hashObj, void* encoding);
void* ts_crypto_hash_copy(void* hashObj);

// HMAC functions
void* ts_crypto_createHmac(void* algorithm, void* key);
void* ts_crypto_hmac_update(void* hmacObj, void* data);
void* ts_crypto_hmac_digest(void* hmacObj, void* encoding);

// Random functions
void* ts_crypto_randomBytes(int64_t size);
void* ts_crypto_randomFillSync(void* bufferObj, int64_t offset, int64_t size);
void ts_crypto_randomFill(void* bufferObj, int64_t offset, int64_t size, void* callback);
int64_t ts_crypto_randomInt(int64_t min, int64_t max);
void* ts_crypto_randomUUID();

// Key derivation functions
void* ts_crypto_pbkdf2Sync(void* password, void* salt, int64_t iterations,
                           int64_t keylen, void* digest);
void ts_crypto_pbkdf2(void* password, void* salt, int64_t iterations,
                      int64_t keylen, void* digest, void* callback);
void* ts_crypto_scryptSync(void* password, void* salt, int64_t keylen,
                           int64_t N, int64_t r, int64_t p, int64_t maxmem);
void ts_crypto_scrypt(void* password, void* salt, int64_t keylen,
                      int64_t N, int64_t r, int64_t p, int64_t maxmem, void* callback);
void* ts_crypto_hkdfSync(void* digestArg, void* ikmArg, void* saltArg, void* infoArg, int64_t keylen);

// Utility functions
bool ts_crypto_timingSafeEqual(void* a, void* b);
void* ts_crypto_getHashes();
void* ts_crypto_getCiphers();
void* ts_crypto_md5(void* str);

// Cipher functions
void* ts_crypto_createCipheriv(void* algorithm, void* key, void* iv);
void* ts_crypto_cipher_update(void* cipherObj, void* data);
void* ts_crypto_cipher_final(void* cipherObj);
void* ts_crypto_cipher_getAuthTag(void* cipherObj);
void* ts_crypto_cipher_setAAD(void* cipherObj, void* data);

// Decipher functions
void* ts_crypto_createDecipheriv(void* algorithm, void* key, void* iv);
void* ts_crypto_decipher_update(void* decipherObj, void* data);
void* ts_crypto_decipher_final(void* decipherObj);
void* ts_crypto_decipher_setAuthTag(void* decipherObj, void* tag);
void* ts_crypto_decipher_setAAD(void* decipherObj, void* data);

// Sign/Verify functions
void* ts_crypto_createSign(void* algorithm);
void* ts_crypto_sign_update(void* signObj, void* data);
void* ts_crypto_sign_sign(void* signObj, void* privateKey, void* outputEncoding);
void* ts_crypto_createVerify(void* algorithm);
void* ts_crypto_verify_update(void* verifyObj, void* data);
bool ts_crypto_verify_verify(void* verifyObj, void* publicKey, void* signature, void* signatureEncoding);

// One-shot sign/verify
void* ts_crypto_sign_oneshot(void* algorithm, void* data, void* key);
bool ts_crypto_verify_oneshot(void* algorithm, void* data, void* key, void* signature);

// Key generation
void* ts_crypto_generateKeyPairSync(void* typeArg, int64_t modulusLength, void* namedCurve);
void ts_crypto_generateKeyPair(void* typeArg, int64_t modulusLength, void* namedCurve, void* callback);
void* ts_crypto_generateKeySync(void* typeArg, int64_t length);
void ts_crypto_generateKey(void* typeArg, int64_t length, void* callback);

// Key creation
void* ts_crypto_createPrivateKey(void* keyArg);
void* ts_crypto_createPublicKey(void* keyArg);
void* ts_crypto_createSecretKey(void* keyArg, void* encoding);

#ifdef __cplusplus
}
#endif

#endif // TS_CRYPTO_EXT_H
