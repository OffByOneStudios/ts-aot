const fs = require('fs');

function testSync() {
    const testFile = 'test_rw_sync.txt';
    const buffer = Buffer.alloc(100);
    
    buffer[0] = 72; // H
    buffer[1] = 101; // e
    buffer[2] = 108; // l
    buffer[3] = 108; // l
    buffer[4] = 111; // o

    console.log('Testing writeSync...');
    const fd = fs.openSync(testFile, 'w');
    const bytesWritten = fs.writeSync(fd, buffer, 0, 5, 0);
    console.log('Bytes written:', bytesWritten);
    fs.closeSync(fd);

    console.log('Testing readSync...');
    const fd2 = fs.openSync(testFile, 'r');
    const readBuffer = Buffer.alloc(100);
    const bytesRead = fs.readSync(fd2, readBuffer, 0, 100, 0);
    console.log('Bytes read:', bytesRead);
    fs.closeSync(fd2);

    if (bytesRead === 5 && readBuffer[0] === 72 && readBuffer[4] === 111) {
        console.log('Sync Read/Write SUCCESS');
    } else {
        console.log('Sync Read/Write FAILED');
    }

    if (fs.existsSync(testFile)) {
        fs.unlinkSync(testFile);
    }
}

async function testAsync() {
    const testFile = 'test_rw_async.txt';
    const buffer = Buffer.alloc(100);
    
    buffer[0] = 65; // A
    buffer[1] = 115; // s
    buffer[2] = 121; // y
    buffer[3] = 110; // n
    buffer[4] = 99; // c

    console.log('Testing fs.promises.write...');
    const fd = await fs.promises.open(testFile, 'w');
    const bytesWritten = await fs.promises.write(fd, buffer, 0, 5, 0);
    console.log('Async bytes written:', bytesWritten);
    await fs.promises.close(fd);

    console.log('Testing fs.promises.read...');
    const fd2 = await fs.promises.open(testFile, 'r');
    const readBuffer = Buffer.alloc(100);
    const bytesRead = await fs.promises.read(fd2, readBuffer, 0, 100, 0);
    console.log('Async bytes read:', bytesRead);
    await fs.promises.close(fd2);

    if (bytesRead === 5 && readBuffer[0] === 65 && readBuffer[4] === 99) {
        console.log('Async Read/Write SUCCESS');
    } else {
        console.log('Async Read/Write FAILED');
    }

    if (fs.existsSync(testFile)) {
        fs.unlinkSync(testFile);
    }
}

testSync();
testAsync();
