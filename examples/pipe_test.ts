import * as fs from 'fs';

const readStream = fs.createReadStream('examples/drain_test.ts');
const writeStream = fs.createWriteStream('examples/pipe_test_out.txt');

readStream.pipe(writeStream);

writeStream.on('finish', () => {
    console.log('Pipe finished');
});
