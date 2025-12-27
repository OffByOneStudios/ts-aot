import * as fs from 'fs';

async function test() {
    console.log("Testing fstat...");
    const handle = await fs.promises.open("test_fstat.txt", "w+");
    console.log("Opened file");

    const stats = await handle.stat();
    console.log("File size: " + stats.size);
    console.log("Is file: " + stats.isFile());

    await handle.write(Buffer.from("Hello fstat!"));
    
    const stats2 = await handle.stat();
    console.log("File size after write: " + stats2.size);

    await handle.close();
    console.log("Closed handle");
}

test().catch(e => console.log("Error: " + e));
