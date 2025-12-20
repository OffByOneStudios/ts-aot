async function testBuffer() {
    console.log("Testing Buffer...");
    const buf = Buffer.alloc(10);
    console.log("Buffer length: " + buf.length);
    
    for (let i = 0; i < buf.length; i++) {
        buf[i] = i * 10;
    }
    
    console.log("Buffer[5]: " + buf[5]);
    
    const strBuf = Buffer.from("Hello World");
    console.log("String Buffer length: " + strBuf.length);
    console.log("String Buffer content: " + strBuf.toString());
}

async function testURL() {
    console.log("Testing URL...");
    const url = new URL("https://example.com:8080/path/to/resource?query=1#hash");
    console.log("URL href: " + url.href);
    console.log("URL protocol: " + url.protocol);
    console.log("URL host: " + url.host);
    console.log("URL hostname: " + url.hostname);
    console.log("URL port: " + url.port);
    console.log("URL pathname: " + url.pathname);
    console.log("URL search: " + url.search);
    console.log("URL hash: " + url.hash);
}

async function testFetch() {
    console.log("Testing fetch...");
    try {
        // Note: This requires internet access or a local server.
        // For testing purposes, we might want to use a local mock server if possible.
        // But for now, let's try a simple HTTP request.
        const response = await fetch("http://www.google.com");
        console.log("Response status: " + response.status);
        console.log("Response statusText: " + response.statusText);
        
        const text = await response.text();
        console.log("Response body length: " + text.length);
        // console.log("Response body: " + text.substring(0, 100));
    } catch (e) {
        console.log("Fetch failed: " + e);
    }
}

async function main() {
    await testBuffer();
    await testURL();
    await testFetch();
}

main();
