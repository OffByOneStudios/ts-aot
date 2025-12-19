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
}

function testProcess() {
    console.log("Process argv length: " + process.argv.length);
    for (let i = 0; i < process.argv.length; i++) {
        console.log("Arg " + i + ": " + process.argv[i]);
    }
}

async function main() {
    await testFs();
    testProcess();
}

main();
