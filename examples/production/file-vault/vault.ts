/**
 * Secure File Vault
 *
 * Encrypt, compress, and store files securely with password-based
 * key derivation (PBKDF2), AES-256-GCM encryption, gzip compression,
 * and HMAC-SHA256 integrity verification.
 *
 * Usage:
 *   vault encrypt <file> --password <pw>     Encrypt a file
 *   vault decrypt <file.vault> --password <pw>  Decrypt a vault file
 *   vault list <directory>                    List vault files
 *   vault help                                Show usage information
 *
 * Node modules: crypto, fs, buffer, zlib, path, process, util
 */

import * as path from 'path';
import * as fs from 'fs';
import * as util from 'util';
import { encryptFile, decryptFile, listVaultFiles } from './storage';
import { formatBytes, validatePassword, VAULT_EXTENSION } from './utils';

function printUsage(): void {
    console.log('Secure File Vault');
    console.log('');
    console.log('Usage:');
    console.log('  vault encrypt <file> --password <pw>');
    console.log('  vault decrypt <file.vault> --password <pw>');
    console.log('  vault list <directory>');
    console.log('  vault help');
    console.log('');
    console.log('Options:');
    console.log('  --password <pw>   Password for encryption/decryption');
    console.log('  --output <dir>    Output directory (default: same as input)');
}

function parseCliArgs(argv: string[]): { command: string; target: string; password: string; outputDir: string } {
    // AOT: argv[0] = executable, argv[1+] = user args
    const args = argv.slice(1);

    let command = '';
    let target = '';
    let password = '';
    let outputDir = '';

    if (args.length === 0) {
        command = 'help';
        return { command, target, password, outputDir };
    }

    command = args[0];

    let i = 1;
    while (i < args.length) {
        if (args[i] === '--password' && i + 1 < args.length) {
            password = args[i + 1];
            i += 2;
        } else if (args[i] === '--output' && i + 1 < args.length) {
            outputDir = args[i + 1];
            i += 2;
        } else if (target === '') {
            target = args[i];
            i++;
        } else {
            i++;
        }
    }

    return { command, target, password, outputDir };
}

function cmdEncrypt(target: string, password: string, outputDir: string): number {
    if (!target) {
        console.error('Error: no input file specified');
        return 1;
    }
    if (!password) {
        console.error('Error: --password is required');
        return 1;
    }
    if (!validatePassword(password)) {
        return 1;
    }
    if (!fs.existsSync(target)) {
        console.error(util.format('Error: file not found: %s', target));
        return 1;
    }

    const resolvedPath = path.resolve(target);
    console.log('Encrypting file...');

    try {
        const outputPath = encryptFile(resolvedPath, password, outputDir || '');
        console.log('');
        console.log('Encryption complete: %s', path.basename(outputPath));
        return 0;
    } catch (err) {
        const error = err as Error;
        console.error(util.format('Encryption failed: %s', error.message));
        return 1;
    }
}

function cmdDecrypt(target: string, password: string, outputDir: string): number {
    if (!target) {
        console.error('Error: no vault file specified');
        return 1;
    }
    if (!password) {
        console.error('Error: --password is required');
        return 1;
    }
    if (!fs.existsSync(target)) {
        console.error(util.format('Error: file not found: %s', target));
        return 1;
    }
    if (!target.endsWith(VAULT_EXTENSION)) {
        console.error(util.format('Error: file must have %s extension', VAULT_EXTENSION));
        return 1;
    }

    const resolvedPath = path.resolve(target);
    console.log('Decrypting vault...');

    try {
        const outputPath = decryptFile(resolvedPath, password, outputDir || '');
        console.log('');
        console.log('Decryption complete: %s', path.basename(outputPath));
        return 0;
    } catch (err) {
        const error = err as Error;
        console.error(util.format('Decryption failed: %s', error.message));
        return 1;
    }
}

function cmdList(target: string): number {
    const dirPath = target || process.cwd();
    if (!fs.existsSync(dirPath)) {
        console.error(util.format('Error: directory not found: %s', dirPath));
        return 1;
    }

    console.log(util.format('Vault files in: %s', dirPath));
    console.log('');

    const files = listVaultFiles(dirPath);

    if (files.length === 0) {
        console.log('  No vault files found.');
        return 0;
    }

    // Table header
    console.log('  %-30s  %-30s  %-12s  %-12s', 'Vault File', 'Original Name', 'Orig Size', 'Vault Size');
    console.log('  %s  %s  %s  %s', '------------------------------', '------------------------------', '------------', '------------');

    for (let i = 0; i < files.length; i++) {
        const f = files[i];
        console.log('  %-30s  %-30s  %-12s  %-12s',
            path.basename(f.path),
            f.originalName,
            formatBytes(f.originalSize),
            formatBytes(f.vaultSize)
        );
    }

    console.log('');
    console.log('  Total: %s vault file(s)', files.length);
    return 0;
}

function user_main(): number {
    const { command, target, password, outputDir } = parseCliArgs(process.argv);

    switch (command) {
        case 'encrypt':
            return cmdEncrypt(target, password, outputDir);
        case 'decrypt':
            return cmdDecrypt(target, password, outputDir);
        case 'list':
            return cmdList(target);
        case 'help':
            printUsage();
            return 0;
        default:
            console.error(util.format('Unknown command: %s', command));
            printUsage();
            return 1;
    }
}
