async function testHttps() {
    console.log("Testing HTTPS fetch...");
    try {
        const response = await fetch("https://httpbin.org/get");
        console.log("Response status: " + response.status);
        const text = await response.text();
        console.log("Response body length: " + text.length);
        if (text.length > 0) {
            console.log("HTTPS fetch successful!");
        }
    } catch (e) {
        console.log("HTTPS fetch failed: " + e);
    }
}

testHttps();
