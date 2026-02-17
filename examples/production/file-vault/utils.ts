/**
 * Secure File Vault — Utility Functions
 *
 * Hex encoding, validation, and formatting helpers.
 */

import * as util from 'util';

// Vault file format constants
export const VAULT_MAGIC = 'TSVAULT1';
export const VAULT_EXTENSION = '.vault';
export const SALT_LENGTH = 32;
export const IV_LENGTH = 16;
export const AUTH_TAG_LENGTH = 16;
export const HMAC_LENGTH = 32;
export const PBKDF2_ITERATIONS = 100000;
export const KEY_LENGTH = 32; // 256-bit key

export interface VaultHeader {
    magic: string;
    salt: Buffer;
    iv: Buffer;
    authTag: Buffer;
    hmac: Buffer;
    originalSize: number;
    originalName: string;
}

// Header layout: magic(8) + salt(32) + iv(16) + authTag(16) + hmac(32) + origSize(8) + nameLen(2) + name(N)
export const HEADER_FIXED_SIZE = 8 + SALT_LENGTH + IV_LENGTH + AUTH_TAG_LENGTH + HMAC_LENGTH + 8 + 2;

export function formatBytes(bytes: number): string {
    if (bytes === 0) return '0 B';
    const units = ['B', 'KB', 'MB', 'GB'];
    let idx = 0;
    let size = bytes;
    while (size >= 1024 && idx < units.length - 1) {
        size = size / 1024;
        idx++;
    }
    const rounded = Math.round(size * 100) / 100;
    return util.format('%s %s', rounded, units[idx]);
}

export function validatePassword(password: string): boolean {
    if (password.length < 4) {
        console.error('Password must be at least 4 characters');
        return false;
    }
    return true;
}

export function serializeHeader(header: VaultHeader): Buffer {
    const nameBytes = Buffer.from(header.originalName, 'utf8');
    const totalSize = HEADER_FIXED_SIZE + nameBytes.length;
    const buf = Buffer.alloc(totalSize);

    let offset = 0;

    // Magic bytes
    buf.write(VAULT_MAGIC, offset, 'utf8');
    offset += 8;

    // Salt
    header.salt.copy(buf, offset);
    offset += SALT_LENGTH;

    // IV
    header.iv.copy(buf, offset);
    offset += IV_LENGTH;

    // Auth tag
    header.authTag.copy(buf, offset);
    offset += AUTH_TAG_LENGTH;

    // HMAC
    header.hmac.copy(buf, offset);
    offset += HMAC_LENGTH;

    // Original size (8 bytes, big-endian as two 32-bit writes)
    const high = Math.floor(header.originalSize / 0x100000000);
    const low = header.originalSize % 0x100000000;
    buf.writeUInt32BE(high, offset);
    buf.writeUInt32BE(low, offset + 4);
    offset += 8;

    // Name length (2 bytes) + name
    buf.writeUInt16BE(nameBytes.length, offset);
    offset += 2;
    nameBytes.copy(buf, offset);

    return buf;
}

export function deserializeHeader(data: Buffer): VaultHeader {
    if (data.length < HEADER_FIXED_SIZE) {
        throw new Error('File too small to be a vault file');
    }

    let offset = 0;

    const magic = data.subarray(offset, offset + 8).toString('utf8');
    offset += 8;
    if (magic !== VAULT_MAGIC) {
        throw new Error('Invalid vault file: bad magic bytes');
    }

    const salt = Buffer.alloc(SALT_LENGTH);
    data.copy(salt, 0, offset, offset + SALT_LENGTH);
    offset += SALT_LENGTH;

    const iv = Buffer.alloc(IV_LENGTH);
    data.copy(iv, 0, offset, offset + IV_LENGTH);
    offset += IV_LENGTH;

    const authTag = Buffer.alloc(AUTH_TAG_LENGTH);
    data.copy(authTag, 0, offset, offset + AUTH_TAG_LENGTH);
    offset += AUTH_TAG_LENGTH;

    const hmac = Buffer.alloc(HMAC_LENGTH);
    data.copy(hmac, 0, offset, offset + HMAC_LENGTH);
    offset += HMAC_LENGTH;

    const high = data.readUInt32BE(offset);
    const low = data.readUInt32BE(offset + 4);
    const originalSize = high * 0x100000000 + low;
    offset += 8;

    const nameLen = data.readUInt16BE(offset);
    offset += 2;

    const originalName = data.subarray(offset, offset + nameLen).toString('utf8');

    return { magic, salt, iv, authTag, hmac, originalSize, originalName };
}
