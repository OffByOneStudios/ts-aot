/**
 * File Copy Benchmark
 *
 * Tests file system I/O throughput.
 * Measures: file reading, writing, and buffer handling.
 */

import * as fs from 'fs';
import * as path from 'path';
import { benchmark, BenchmarkSuite, formatBytes } from '../harness/benchmark';

const TEMP_DIR = process.env.TEMP || process.env.TMP || '/tmp';

/**
 * Generate a test file of specified size
 */
function generateTestFile(filePath: string, sizeBytes: number): void {
    const chunkSize = 64 * 1024; // 64KB chunks
    const chunk = Buffer.alloc(chunkSize);

    // Fill with pattern
    for (let i = 0; i < chunkSize; i++) {
        chunk[i] = i % 256;
    }

    const fd = fs.openSync(filePath, 'w');
    let written = 0;

    while (written < sizeBytes) {
        const toWrite = Math.min(chunkSize, sizeBytes - written);
        fs.writeSync(fd, chunk, 0, toWrite);
        written += toWrite;
    }

    fs.closeSync(fd);
}

/**
 * Copy file using streams (most efficient for large files)
 */
function copyFileStream(src: string, dest: string): Promise<void> {
    return new Promise((resolve, reject) => {
        const readStream = fs.createReadStream(src);
        const writeStream = fs.createWriteStream(dest);

        readStream.on('error', reject);
        writeStream.on('error', reject);
        writeStream.on('finish', () => resolve());

        readStream.pipe(writeStream);
    });
}

/**
 * Copy file using sync read/write with chunks
 */
function copyFileChunked(src: string, dest: string, chunkSize: number = 64 * 1024): void {
    const fdSrc = fs.openSync(src, 'r');
    const fdDest = fs.openSync(dest, 'w');
    const buffer = Buffer.alloc(chunkSize);

    let bytesRead = 0;
    while ((bytesRead = fs.readSync(fdSrc, buffer, 0, chunkSize, null)) > 0) {
        fs.writeSync(fdDest, buffer, 0, bytesRead);
    }

    fs.closeSync(fdSrc);
    fs.closeSync(fdDest);
}

/**
 * Copy file by reading entirely into memory
 */
function copyFileMemory(src: string, dest: string): void {
    const data = fs.readFileSync(src);
    fs.writeFileSync(dest, data);
}

/**
 * Clean up test files
 */
function cleanup(files: string[]): void {
    for (const file of files) {
        try {
            if (fs.existsSync(file)) {
                fs.unlinkSync(file);
            }
        } catch (e) {
            // Ignore cleanup errors
        }
    }
}

function user_main(): number {
    console.log('File Copy Benchmark');
    console.log('===================');
    console.log('');

    const SIZE_1MB = 1 * 1024 * 1024;
    const SIZE_10MB = 10 * 1024 * 1024;
    const SIZE_100MB = 100 * 1024 * 1024;

    const srcFile = path.join(TEMP_DIR, 'bench_src.dat');
    const destFile = path.join(TEMP_DIR, 'bench_dest.dat');

    // Generate test files
    console.log('Generating test files...');

    console.log(`Creating 1MB test file...`);
    generateTestFile(srcFile, SIZE_1MB);
    console.log(`Creating 10MB test file...`);
    const srcFile10 = path.join(TEMP_DIR, 'bench_src_10mb.dat');
    generateTestFile(srcFile10, SIZE_10MB);
    console.log(`Creating 100MB test file...`);
    const srcFile100 = path.join(TEMP_DIR, 'bench_src_100mb.dat');
    generateTestFile(srcFile100, SIZE_100MB);
    console.log('');

    // Verify files exist
    const stat1 = fs.statSync(srcFile);
    console.log(`1MB file: ${formatBytes(stat1.size)}`);
    const stat10 = fs.statSync(srcFile10);
    console.log(`10MB file: ${formatBytes(stat10.size)}`);
    const stat100 = fs.statSync(srcFile100);
    console.log(`100MB file: ${formatBytes(stat100.size)}`);
    console.log('');

    const suite = new BenchmarkSuite('File I/O');

    // Read benchmarks
    suite.add('readFileSync (1MB)', () => {
        const data = fs.readFileSync(srcFile);
        if (!data) console.log('unreachable');
    }, { iterations: 100, warmup: 10 });

    suite.add('readFileSync (10MB)', () => {
        const data = fs.readFileSync(srcFile10);
        if (!data) console.log('unreachable');
    }, { iterations: 20, warmup: 5 });

    suite.add('readFileSync (100MB)', () => {
        const data = fs.readFileSync(srcFile100);
        if (!data) console.log('unreachable');
    }, { iterations: 5, warmup: 1 });

    // Write benchmarks
    const writeData1 = fs.readFileSync(srcFile);
    suite.add('writeFileSync (1MB)', () => {
        fs.writeFileSync(destFile, writeData1);
    }, { iterations: 50, warmup: 5 });

    const writeData10 = fs.readFileSync(srcFile10);
    suite.add('writeFileSync (10MB)', () => {
        fs.writeFileSync(destFile, writeData10);
    }, { iterations: 10, warmup: 2 });

    // Chunked copy benchmarks
    suite.add('copy chunked 64KB (10MB)', () => {
        copyFileChunked(srcFile10, destFile, 64 * 1024);
    }, { iterations: 10, warmup: 2 });

    suite.add('copy chunked 1MB (10MB)', () => {
        copyFileChunked(srcFile10, destFile, 1024 * 1024);
    }, { iterations: 10, warmup: 2 });

    // Full memory copy
    suite.add('copy memory (10MB)', () => {
        copyFileMemory(srcFile10, destFile);
    }, { iterations: 10, warmup: 2 });

    // fs.copyFileSync (built-in)
    suite.add('fs.copyFileSync (10MB)', () => {
        fs.copyFileSync(srcFile10, destFile);
    }, { iterations: 10, warmup: 2 });

    suite.add('fs.copyFileSync (100MB)', () => {
        fs.copyFileSync(srcFile100, destFile);
    }, { iterations: 3, warmup: 1 });

    suite.run();
    suite.printSummary();

    // Cleanup
    console.log('');
    console.log('Cleaning up test files...');
    cleanup([srcFile, srcFile10, srcFile100, destFile]);
    console.log('Done.');

    return 0;
}
