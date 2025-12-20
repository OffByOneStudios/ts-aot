async function testFs() {
    const path = "test_node_parity.txt";
    const content = "Hello from ts-aot Node parity!";
    
    console.log("Writing file...");
    await fs.promises.writeFile(path, content);
    
    console.log("Checking if file exists...");
    if (fs.existsSync(path)) {
        console.log("File exists!");
    } else {
        console.log("File does NOT exist!");
    }
    
    console.log("Reading file...");
    const readContent = await fs.promises.readFile(path);
    console.log("Read content: " + readContent);
    
    if (readContent === content) {
        console.log("Content matches!");
    } else {
        console.log("Content mismatch!");
    }

    console.log("Testing mkdirSync...");
    const dir = "test_dir";
    fs.mkdirSync(dir);
    if (fs.existsSync(dir)) {
        console.log("Directory created!");
    }

    console.log("Testing statSync...");
    const stats = fs.statSync(path);
    console.log("File size: " + stats.size);
    if (stats.isFile()) {
        console.log("Is a file!");
    }

    console.log("Testing unlinkSync...");
    fs.unlinkSync(path);
    if (!fs.existsSync(path)) {
        console.log("File deleted!");
    }

    console.log("Testing rmdirSync...");
    fs.rmdirSync(dir);
    if (!fs.existsSync(dir)) {
        console.log("Directory deleted!");
    }

    console.log("Testing async mkdir...");
    const asyncDir = "test_async_dir";
    await fs.promises.mkdir(asyncDir);
    if (fs.existsSync(asyncDir)) {
        console.log("Async directory created!");
        fs.rmdirSync(asyncDir);
    }

    console.log("Testing async stat...");
    const asyncStats = await fs.promises.stat("CMakeLists.txt");
    console.log("CMakeLists.txt size: " + asyncStats.size);
}

function testProcess() {
    console.log("Process argv length: " + process.argv.length);
    for (let i = 0; i < process.argv.length; i++) {
        console.log("Arg " + i + ": " + process.argv[i]);
    }

    console.log("Process cwd: " + process.cwd());
    
    console.log("Process env PATH: " + process.env["PATH"]);
    if (process.env["PATH"]) {
        console.log("Found PATH environment variable");
    }

    console.log("Exiting...");
    process.exit(0);
    console.log("This should not be printed");
}

async function main() {
    await testFs();
    testProcess();
}

main();
