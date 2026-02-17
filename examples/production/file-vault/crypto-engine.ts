/**
 * Secure File Vault — Crypto Engine
 *
 * Key derivation (PBKDF2), AES-256-GCM encryption/decryption,
 * and HMAC integrity verification.
 */

import * as crypto from 'crypto';
import { PBKDF2_ITERATIONS, KEY_LENGTH, IV_LENGTH, SALT_LENGTH } from './utils';

export interface DerivedKeys {
    encryptionKey: Buffer;
    hmacKey: Buffer;
    salt: Buffer;
}

export interface EncryptResult {
    ciphertext: Buffer;
    iv: Buffer;
    authTag: Buffer;
}

/**
 * Derive encryption and HMAC keys from a password using PBKDF2.
 * Uses a random salt if none provided (encryption), or the stored salt (decryption).
 */
export function deriveKeys(password: string, existingSalt?: Buffer): DerivedKeys {
    const salt = existingSalt || crypto.randomBytes(SALT_LENGTH);

    // Derive 64 bytes: first 32 for encryption, next 32 for HMAC
    const derived = crypto.pbkdf2Sync(password, salt, PBKDF2_ITERATIONS, KEY_LENGTH * 2, 'sha512');

    const encryptionKey = derived.subarray(0, KEY_LENGTH);
    const hmacKey = derived.subarray(KEY_LENGTH, KEY_LENGTH * 2);

    return { encryptionKey, hmacKey, salt };
}

/**
 * Encrypt data using AES-256-GCM with a random IV.
 */
export function encrypt(data: Buffer, key: Buffer): EncryptResult {
    const iv = crypto.randomBytes(IV_LENGTH);

    const cipher = crypto.createCipheriv('aes-256-gcm', key, iv);
    const encrypted = cipher.update(data);
    const final = cipher.final();
    const ciphertext = Buffer.concat([encrypted, final]);
    const authTag = cipher.getAuthTag();

    return { ciphertext, iv, authTag };
}

/**
 * Decrypt data using AES-256-GCM.
 */
export function decrypt(ciphertext: Buffer, key: Buffer, iv: Buffer, authTag: Buffer): Buffer {
    const decipher = crypto.createDecipheriv('aes-256-gcm', key, iv);
    decipher.setAuthTag(authTag);

    const decrypted = decipher.update(ciphertext);
    const final = decipher.final();

    return Buffer.concat([decrypted, final]);
}

/**
 * Compute HMAC-SHA256 over data for integrity verification.
 */
export function computeHmac(data: Buffer, key: Buffer): Buffer {
    const hmac = crypto.createHmac('sha256', key);
    hmac.update(data);
    const digest = hmac.digest('hex');
    return Buffer.from(digest, 'hex');
}

/**
 * Verify HMAC-SHA256 integrity.
 * Uses timing-safe comparison to prevent timing attacks.
 */
export function verifyHmac(data: Buffer, key: Buffer, expectedHmac: Buffer): boolean {
    const computed = computeHmac(data, key);
    if (computed.length !== expectedHmac.length) {
        return false;
    }
    return crypto.timingSafeEqual(computed, expectedHmac);
}
