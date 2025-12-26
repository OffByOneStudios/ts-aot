import * as fs from 'fs';

async function test() {
    const testFile = 'test_manip.txt';
    const renamedFile = 'test_renamed.txt';
    const copiedFile = 'test_copied.txt';
    const tempDirPrefix = 'test_temp_';

    console.log('Testing Sync Manipulation...');
    
    // writeFileSync
    fs.writeFileSync(testFile, 'Hello World');
    console.log('File created.');

    // renameSync
    fs.renameSync(testFile, renamedFile);
    console.log('File renamed.');
    if (!fs.existsSync(renamedFile)) throw new Error('Rename failed');

    // copyFileSync
    fs.copyFileSync(renamedFile, copiedFile);
    console.log('File copied.');
    if (!fs.existsSync(copiedFile)) throw new Error('Copy failed');

    // truncateSync
    fs.truncateSync(copiedFile, 5);
    const truncated = fs.readFileSync(copiedFile);
    console.log('Truncated content: ' + truncated);
    if (truncated !== 'Hello') throw new Error('Truncate failed');

    // appendFileSync
    fs.appendFileSync(copiedFile, ' World');
    const appended = fs.readFileSync(copiedFile);
    console.log('Appended content: ' + appended);
    if (appended !== 'Hello World') throw new Error('Append failed');

    // mkdtempSync
    const tempDir = fs.mkdtempSync(tempDirPrefix);
    console.log('Temp dir created: ' + tempDir);
    if (!fs.existsSync(tempDir)) throw new Error('mkdtemp failed');

    // mkdirSync & rmdirSync
    const subDir = tempDir + '/subdir';
    fs.mkdirSync(subDir);
    console.log('Subdir created.');
    fs.rmdirSync(subDir);
    console.log('Subdir removed.');

    // rmSync
    fs.rmSync(renamedFile);
    fs.rmSync(copiedFile);
    fs.rmSync(tempDir);
    console.log('Cleanup done.');

    console.log('\nTesting Promises Manipulation...');
    try {
        const aTestFile = 'test_async.txt';
        const aRenamedFile = 'test_async_renamed.txt';
        const aCopiedFile = 'test_async_copy.txt';

        await fs.promises.writeFile(aTestFile, 'Hello World');
        console.log('Async file created.');

        await fs.promises.rename(aTestFile, aRenamedFile);
        console.log('Async file renamed.');

        await fs.promises.copyFile(aRenamedFile, aCopiedFile);
        console.log('Async file copied.');

        await fs.promises.truncate(aCopiedFile, 5);
        console.log('Async truncated content: ' + fs.readFileSync(aCopiedFile));

        await fs.promises.appendFile(aCopiedFile, ' World');
        console.log('Async appended content: ' + fs.readFileSync(aCopiedFile));

        const aTempDir = await fs.promises.mkdtemp('test_async_temp_');
        console.log('Async temp dir created: ' + aTempDir);

        await fs.promises.mkdir(aTempDir + '/subdir');
        console.log('Async subdir created.');

        await fs.promises.rm(aTempDir + '/subdir', { recursive: true });
        console.log('Async subdir removed.');

        await fs.promises.rm(aRenamedFile);
        await fs.promises.rm(aCopiedFile);
        await fs.promises.rm(aTempDir, { recursive: true });
        console.log('Async cleanup done.');
    } catch (e) {
        console.log('Async test failed: ' + e);
    }
}

test();
