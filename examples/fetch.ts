async function testFetch() {
    console.log("Testing fetch...");
    try {
        // Note: This requires internet access or a local server.
        // For testing purposes, we might want to use a local mock server if possible.
        // But for now, let's try a simple HTTP request.
        const response = await fetch("http://www.google.com");
        const text = await response.text();
        
        console.log(text);
    } catch (e) {
        console.log("Fetch failed: " + e);
    }
}

async function main() {
    await testFetch();
}

main();
