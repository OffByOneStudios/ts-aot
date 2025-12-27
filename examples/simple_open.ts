import * as fs from 'fs';

async function test() {
    console.log("Opening file...");
    const handle = await fs.promises.open("simple_open_test.txt", "w+");
    console.log("Opened successfully");
    await handle.close();
    console.log("Closed successfully");
}

test();
