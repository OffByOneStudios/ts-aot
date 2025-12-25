const fs = require('fs');

async function test() {
    const testFile = 'test_open.txt';
    
    console.log('Testing openSync/closeSync...');
    const fd = fs.openSync(testFile, 'w');
    console.log('File opened, fd:', fd);
    
    if (fd > 0) {
        console.log('Closing file...');
        fs.closeSync(fd);
        console.log('File closed.');
    }

    console.log('Testing fs.promises.open/close...');
    try {
        const fd2 = await fs.promises.open(testFile, 'r');
        console.log('Async file opened, fd:', fd2);
        
        console.log('Async closing file...');
        await fs.promises.close(fd2);
        console.log('Async file closed.');
    } catch (e) {
        console.log('Async open failed (expected if file not found or other error):', e);
    }

    // Cleanup
    if (fs.existsSync(testFile)) {
        fs.unlinkSync(testFile);
    }
    
    console.log('Done!');
}

test();
