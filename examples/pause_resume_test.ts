const fs = require('fs');
const readStream = fs.createReadStream('examples/test_simple.ts');

let count = 0;
readStream.on('data', (chunk) => {
    count++;
    console.log('Received chunk ' + count);
    if (count === 1) {
        console.log('Pausing stream...');
        readStream.pause();
        setTimeout(() => {
            console.log('Resuming stream...');
            readStream.resume();
        }, 100);
    }
});

readStream.on('end', () => {
    console.log('Stream ended');
});
