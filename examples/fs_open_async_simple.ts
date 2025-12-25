const fs = require('fs');

async function test() {
    const testFile = 'test_open_async.txt';
    
    console.log('Testing fs.promises.open...');
    const fd = await fs.promises.open(testFile, 'w');
    console.log('Async file opened, fd:', fd);
    
    console.log('Async closing file...');
    await fs.promises.close(fd);
    console.log('Async file closed.');

    if (fs.existsSync(testFile)) {
        fs.unlinkSync(testFile);
    }
    
    console.log('Done!');
}

test();
