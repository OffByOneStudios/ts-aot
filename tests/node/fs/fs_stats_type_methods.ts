// Test stats type methods: isBlockDevice, isCharacterDevice, isFIFO, isSocket
import * as fs from 'fs';

function user_main(): number {
    let passed = 0;
    let failed = 0;

    // Test 1: stats.isFile() on a regular file
    const testFile = 'test_stats_methods.txt';
    fs.writeFileSync(testFile, 'test content');

    const fileStats = fs.statSync(testFile);
    if (fileStats.isFile() === true) {
        console.log('PASS: isFile() returns true for regular file');
        passed++;
    } else {
        console.log('FAIL: isFile() should return true for regular file');
        failed++;
    }

    // Test 2: stats.isDirectory() on a regular file (should be false)
    if (fileStats.isDirectory() === false) {
        console.log('PASS: isDirectory() returns false for regular file');
        passed++;
    } else {
        console.log('FAIL: isDirectory() should return false for regular file');
        failed++;
    }

    // Test 3: stats.isBlockDevice() on a regular file (should be false)
    if (fileStats.isBlockDevice() === false) {
        console.log('PASS: isBlockDevice() returns false for regular file');
        passed++;
    } else {
        console.log('FAIL: isBlockDevice() should return false for regular file');
        failed++;
    }

    // Test 4: stats.isCharacterDevice() on a regular file (should be false)
    if (fileStats.isCharacterDevice() === false) {
        console.log('PASS: isCharacterDevice() returns false for regular file');
        passed++;
    } else {
        console.log('FAIL: isCharacterDevice() should return false for regular file');
        failed++;
    }

    // Test 5: stats.isFIFO() on a regular file (should be false)
    if (fileStats.isFIFO() === false) {
        console.log('PASS: isFIFO() returns false for regular file');
        passed++;
    } else {
        console.log('FAIL: isFIFO() should return false for regular file');
        failed++;
    }

    // Test 6: stats.isSocket() on a regular file (should be false)
    if (fileStats.isSocket() === false) {
        console.log('PASS: isSocket() returns false for regular file');
        passed++;
    } else {
        console.log('FAIL: isSocket() should return false for regular file');
        failed++;
    }

    // Test 7: stats.isSymbolicLink() on a regular file (should be false)
    if (fileStats.isSymbolicLink() === false) {
        console.log('PASS: isSymbolicLink() returns false for regular file');
        passed++;
    } else {
        console.log('FAIL: isSymbolicLink() should return false for regular file');
        failed++;
    }

    // Test 8: stats.isDirectory() on a directory
    const testDir = 'test_stats_dir';
    fs.mkdirSync(testDir);

    const dirStats = fs.statSync(testDir);
    if (dirStats.isDirectory() === true) {
        console.log('PASS: isDirectory() returns true for directory');
        passed++;
    } else {
        console.log('FAIL: isDirectory() should return true for directory');
        failed++;
    }

    // Test 9: stats.isFile() on a directory (should be false)
    if (dirStats.isFile() === false) {
        console.log('PASS: isFile() returns false for directory');
        passed++;
    } else {
        console.log('FAIL: isFile() should return false for directory');
        failed++;
    }

    // Cleanup
    fs.unlinkSync(testFile);
    fs.rmdirSync(testDir);

    console.log('Tests: ' + passed + ' passed, ' + failed + ' failed');
    return failed;
}
