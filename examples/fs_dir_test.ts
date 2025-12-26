import * as fs from 'fs';

async function testDir() {
    console.log("Testing readdirSync...");
    const files = fs.readdirSync(".");
    console.log("Files in current dir:", files.length);

    console.log("\nTesting opendirSync...");
    const dir = fs.opendirSync(".");
    console.log("Path:", dir.path);
    let entry = dir.readSync();
    let count = 0;
    while (entry) {
        if (count < 5) {
            console.log("Entry:", entry.name, "isFile:", entry.isFile(), "isDir:", entry.isDirectory());
        }
        count++;
        entry = dir.readSync();
    }
    console.log("Total entries:", count);
    dir.closeSync();

    console.log("\nTesting promises.readdir...");
    const aFiles = await fs.promises.readdir(".");
    console.log("Async files count:", aFiles.length);

    console.log("\nTesting promises.opendir...");
    const aDir = await fs.promises.opendir(".");
    let aEntry = await aDir.read();
    let aCount = 0;
    while (aEntry) {
        if (aCount < 5) {
            console.log("Async Entry:", aEntry.name);
        }
        aCount++;
        aEntry = await aDir.read();
    }
    console.log("Total async entries:", aCount);
    await aDir.close();
}

testDir();
