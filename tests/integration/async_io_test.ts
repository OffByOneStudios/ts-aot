async function test() {
    console.log("Starting async IO test...");
    
    // Test fs.promises.readFile
    const content = await fs.promises.readFile("test_write.txt");
    console.log("File content: " + content);
    
    // Test fetch
    const response = await fetch("https://adventofcode.com/2023/day/1/input");
    console.log("Fetch response: " + response);
}

test();
