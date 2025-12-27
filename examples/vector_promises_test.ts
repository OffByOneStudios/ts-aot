import * as fs from 'node:fs';

async function test() {
    try {
        const path = 'test_vector.txt';
        const buf1 = Buffer.from('Hello ');
        const buf2 = Buffer.from('World!');
        
        console.log('Opening for write...');
        const writeHandle = await fs.promises.open(path, 'w');
        console.log('Writing...');
        await fs.promises.writev(writeHandle, [buf1, buf2]);
        await writeHandle.close();
        console.log('Vector write complete');

        // readv
        console.log('Opening for read...');
        const handle = await fs.promises.open(path, 'r');
        const out1 = Buffer.alloc(6);
        const out2 = Buffer.alloc(6);
        console.log('Reading...');
        const { bytesRead, buffers } = await fs.promises.readv(handle, [out1, out2]);
        
        console.log('Bytes read:', bytesRead);
        console.log('Buffer 1:', out1.toString());
        console.log('Buffer 2:', out2.toString());

        await handle.close();
        await fs.promises.unlink(path);
        console.log('Test passed');
    } catch (e) {
        console.log('Test failed:', e);
        process.exit(1);
    }
}

test();
