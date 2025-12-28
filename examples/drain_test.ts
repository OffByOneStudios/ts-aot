import * as fs from 'fs';

const writeStream = fs.createWriteStream('examples/drain_test.txt');
const chunk = Buffer.alloc(4096); // 4KB
let count = 0;
const max = 20; // 20 * 4KB = 80KB. HighWaterMark is 16KB.

function write() {
    let ok = true;
    while (count < max && ok) {
        count++;
        console.log('Writing chunk ' + count);
        ok = writeStream.write(chunk);
        if (!ok) {
            console.log('Buffer full at ' + count);
        }
    }
    if (count < max) {
        writeStream.once('drain', () => {
            console.log('Drain event received');
            write();
        });
    } else {
        console.log('Finished writing');
        writeStream.end();
    }
}

write();

writeStream.on('finish', () => {
    console.log('Stream finished');
});
