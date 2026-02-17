/**
 * Secure File Vault — Storage Layer
 *
 * File I/O, gzip compression, and vault file management.
 */

import * as fs from 'fs';
import * as path from 'path';
import * as zlib from 'zlib';
import { deriveKeys, encrypt, decrypt, computeHmac, verifyHmac } from './crypto-engine';
import {
    VaultHeader, VAULT_EXTENSION, formatBytes,
    serializeHeader, deserializeHeader
} from './utils';

export interface VaultFileInfo {
    path: string;
    originalName: string;
    originalSize: number;
    vaultSize: number;
}

/**
 * Encrypt and store a file as a .vault archive.
 * Pipeline: read -> compress (gzip) -> encrypt (AES-GCM) -> HMAC -> write
 */
export function encryptFile(inputPath: string, password: string, outputDir?: string): string {
    // Read the source file
    const inputData = fs.readFileSync(inputPath);
    const fileName = path.basename(inputPath);
    const originalSize = inputData.length;

    console.log('  Reading: %s (%s)', fileName, formatBytes(originalSize));

    // Compress with gzip
    const compressed = zlib.gzipSync(inputData);
    const ratio = Math.round((1 - compressed.length / originalSize) * 100);
    console.log('  Compressed: %s (-%s%%)', formatBytes(compressed.length), ratio);

    // Derive encryption and HMAC keys
    const keys = deriveKeys(password);
    console.log('  Key derived (PBKDF2, 100k iterations)');

    // Encrypt the compressed data
    const encResult = encrypt(compressed, keys.encryptionKey);
    console.log('  Encrypted: AES-256-GCM (%s)', formatBytes(encResult.ciphertext.length));

    // Compute HMAC over the ciphertext for integrity
    const hmac = computeHmac(encResult.ciphertext, keys.hmacKey);

    // Build vault header
    const header: VaultHeader = {
        magic: 'TSVAULT1',
        salt: keys.salt,
        iv: encResult.iv,
        authTag: encResult.authTag,
        hmac: hmac,
        originalSize: originalSize,
        originalName: fileName
    };

    // Serialize header + ciphertext
    const headerBuf = serializeHeader(header);
    const outputData = Buffer.concat([headerBuf, encResult.ciphertext]);

    // Write output file
    const dir = outputDir || path.dirname(inputPath);
    const baseName = path.basename(inputPath, path.extname(inputPath));
    const outputPath = path.join(dir, baseName + VAULT_EXTENSION);

    fs.writeFileSync(outputPath, outputData);
    console.log('  Written: %s (%s)', path.basename(outputPath), formatBytes(outputData.length));

    return outputPath;
}

/**
 * Decrypt a .vault file and restore the original file.
 * Pipeline: read -> parse header -> verify HMAC -> decrypt -> decompress -> write
 */
export function decryptFile(vaultPath: string, password: string, outputDir?: string): string {
    // Read the vault file
    const vaultData = fs.readFileSync(vaultPath);
    console.log('  Reading: %s (%s)', path.basename(vaultPath), formatBytes(vaultData.length));

    // Parse header
    const header = deserializeHeader(vaultData);
    console.log('  Original: %s (%s)', header.originalName, formatBytes(header.originalSize));

    // Calculate where ciphertext starts (header size)
    const nameLen = Buffer.from(header.originalName, 'utf8').length;
    const headerSize = 8 + 32 + 16 + 16 + 32 + 8 + 2 + nameLen;
    const ciphertext = vaultData.subarray(headerSize);

    // Derive keys from password + stored salt
    const keys = deriveKeys(password, header.salt);
    console.log('  Key derived (PBKDF2, 100k iterations)');

    // Verify HMAC integrity
    const hmacValid = verifyHmac(ciphertext, keys.hmacKey, header.hmac);
    if (!hmacValid) {
        throw new Error('HMAC verification failed: wrong password or corrupted file');
    }
    console.log('  Integrity: HMAC-SHA256 verified');

    // Decrypt
    const compressed = decrypt(ciphertext, keys.encryptionKey, header.iv, header.authTag);
    console.log('  Decrypted: AES-256-GCM');

    // Decompress
    const originalData = zlib.gunzipSync(compressed);
    console.log('  Decompressed: %s', formatBytes(originalData.length));

    // Write original file
    const dir = outputDir || path.dirname(vaultPath);
    const outputPath = path.join(dir, header.originalName);

    fs.writeFileSync(outputPath, originalData);
    console.log('  Restored: %s', header.originalName);

    return outputPath;
}

/**
 * List all .vault files in a directory with metadata.
 */
export function listVaultFiles(dirPath: string): VaultFileInfo[] {
    const results: VaultFileInfo[] = [];
    const entries = fs.readdirSync(dirPath);

    for (let i = 0; i < entries.length; i++) {
        const entry = entries[i];
        if (!entry.endsWith(VAULT_EXTENSION)) continue;

        const fullPath = path.join(dirPath, entry);
        const stat = fs.statSync(fullPath);
        if (!stat.isFile()) continue;

        try {
            const data = fs.readFileSync(fullPath);
            const header = deserializeHeader(data);
            results.push({
                path: fullPath,
                originalName: header.originalName,
                originalSize: header.originalSize,
                vaultSize: stat.size
            });
        } catch (err) {
            // Skip files that fail to parse
            console.error('  Warning: could not read %s', entry);
        }
    }

    return results;
}
