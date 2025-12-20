async function testPost() {
    console.log("Testing POST fetch...");
    try {
        const response = await fetch("http://httpbin.org/post", {
            method: "POST",
            headers: {
                "Content-Type": "application/json",
                "X-Custom-Header": "hello"
            },
            body: JSON.stringify({ foo: "bar" })
        });
        
        const text = await response.text();
        console.log("Response status: " + response.status);
        console.log("Response body: " + text);
        
        const json = JSON.parse(text);
        console.log("JSON foo: " + json.json.foo);
        console.log("Header check: " + json.headers["X-Custom-Header"]);
    } catch (e) {
        console.log("POST failed: " + e);
    }
}

testPost();
