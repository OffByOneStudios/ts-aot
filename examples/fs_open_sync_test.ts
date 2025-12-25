const fs = require('fs');

const testFile = 'test_open_sync.txt';

console.log('Testing openSync/closeSync...');
const fd = fs.openSync(testFile, 'w');
console.log('File opened, fd:', fd);

if (fd > 0) {
    console.log('Closing file...');
    fs.closeSync(fd);
    console.log('File closed.');
}

// Cleanup
if (fs.existsSync(testFile)) {
    fs.unlinkSync(testFile);
}

console.log('Done!');
