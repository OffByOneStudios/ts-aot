import * as fs from 'fs';

async function test() {
    const path = 'test_unlink.txt';
    await fs.promises.writeFile(path, 'hello');
    console.log('File created');
    
    const stats = await fs.promises.stat(path);
    console.log('File size:', stats.size);
    
    await fs.promises.unlink(path);
    console.log('File unlinked');
}

test();
