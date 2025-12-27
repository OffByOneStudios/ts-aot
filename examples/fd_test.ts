import * as fs from 'fs';

async function test() {
    console.log("Testing FileHandle...");
    
    const filename = "test_fd_" + Date.now() + ".txt";
    const handle = await fs.promises.open(filename, "w+");
    console.log("Opened file, fd:", handle.fd);

    const buffer = Buffer.from("Hello from FileHandle!");
    const { bytesWritten } = await handle.write(buffer);
    console.log("Bytes written:", bytesWritten);

    // await handle.datasync();
    // console.log("Datasync complete");

    const readBuffer = Buffer.alloc(buffer.length);
    const result = await handle.read(readBuffer, 0, readBuffer.length, 0);
    console.log("Bytes read:", result.bytesRead);
    console.log("Read content:", readBuffer.toString());

    /*
    const stats = await handle.stat();
    console.log("File size:", stats.size);

    await handle.chmod(0o644);
    console.log("Chmod complete");

    await handle.close();
    console.log("Handle closed");
    */

    /*
    console.log("\nTesting sync FD operations...");
    const fd = fs.openSync("test_fd_sync.txt", "w+");
    fs.writeSync(fd, Buffer.from("Sync write"));
    fs.fsyncSync(fd);
    
    const syncStats = fs.fstatSync(fd);
    console.log("Sync file size:", syncStats.size);
    
    fs.closeSync(fd);
    console.log("Sync closed");
    */
}

test().catch(e => console.log(e));
