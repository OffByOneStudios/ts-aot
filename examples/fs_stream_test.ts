const fs = require('fs');

const readStream = fs.createReadStream('examples/test_simple.ts');
const writeStream = fs.createWriteStream('examples/test_copy.ts');

readStream.on('data', (chunk) => {
    ts_console_log("Received chunk of size: " + chunk.length);
    writeStream.write(chunk);
});

readStream.on('end', () => {
    ts_console_log("Read stream ended");
    writeStream.end();
});

writeStream.on('finish', () => {
    ts_console_log("Write stream finished");
});

writeStream.on('close', () => {
    ts_console_log("Write stream closed");
});
