/**
 * File System Utilities
 */

import * as fs from 'fs';
import * as path from 'path';

/**
 * Check if a path exists
 */
export function exists(filePath: string): boolean {
    return fs.existsSync(filePath);
}

/**
 * Check if a path is a directory
 */
export function isDirectory(filePath: string): boolean {
    if (!exists(filePath)) return false;
    const stats = fs.statSync(filePath);
    return stats.isDirectory();
}

/**
 * Check if a path is a file
 */
export function isFile(filePath: string): boolean {
    if (!exists(filePath)) return false;
    const stats = fs.statSync(filePath);
    return stats.isFile();
}

/**
 * Create a directory (and parents if needed)
 */
export function mkdir(dirPath: string): void {
    if (!exists(dirPath)) {
        fs.mkdirSync(dirPath, { recursive: true });
    }
}

/**
 * Write text to a file
 */
export function writeFile(filePath: string, content: string): void {
    const dir = path.dirname(filePath);
    mkdir(dir);
    fs.writeFileSync(filePath, content, 'utf8');
}

/**
 * Read text from a file
 */
export function readFile(filePath: string): string {
    return fs.readFileSync(filePath, 'utf8');
}

/**
 * Copy a file
 */
export function copyFile(src: string, dest: string): void {
    const dir = path.dirname(dest);
    mkdir(dir);
    fs.copyFileSync(src, dest);
}

/**
 * Delete a file or directory
 */
export function remove(filePath: string): void {
    if (!exists(filePath)) return;

    const stats = fs.statSync(filePath);
    if (stats.isDirectory()) {
        // Remove directory contents first
        const entries = fs.readdirSync(filePath);
        for (const entry of entries) {
            remove(path.join(filePath, entry));
        }
        fs.rmdirSync(filePath);
    } else {
        fs.unlinkSync(filePath);
    }
}

/**
 * List files in a directory
 */
export function listFiles(dirPath: string, recursive: boolean = false): string[] {
    if (!isDirectory(dirPath)) return [];

    const results: string[] = [];
    const entries = fs.readdirSync(dirPath);

    for (const entry of entries) {
        const fullPath = path.join(dirPath, entry);
        const stats = fs.statSync(fullPath);

        if (stats.isFile()) {
            results.push(fullPath);
        } else if (stats.isDirectory() && recursive) {
            const subFiles = listFiles(fullPath, true);
            for (const subFile of subFiles) {
                results.push(subFile);
            }
        }
    }

    return results;
}

/**
 * Get file size in bytes
 */
export function fileSize(filePath: string): number {
    if (!isFile(filePath)) return 0;
    const stats = fs.statSync(filePath);
    return stats.size;
}

/**
 * Format bytes to human-readable string
 */
export function formatBytes(bytes: number): string {
    if (bytes < 1024) return bytes + ' B';
    if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(1) + ' KB';
    if (bytes < 1024 * 1024 * 1024) return (bytes / (1024 * 1024)).toFixed(1) + ' MB';
    return (bytes / (1024 * 1024 * 1024)).toFixed(1) + ' GB';
}
