async function testBuffer() {
    console.log("Testing Buffer...");
    
    const buf = Buffer.alloc(10);
    console.log("Buffer length: " + buf.length);
    
    buf[0] = 72; // 'H'
    buf[1] = 101; // 'e'
    buf[2] = 108; // 'l'
    buf[3] = 108; // 'l'
    buf[4] = 111; // 'o'
    
    console.log("Buffer[0]: " + buf[0]);
    
    const str = Buffer.from("World");
    console.log("Buffer from string length: " + str.length);
    console.log("Buffer from string content: " + str.toString());
    
    const hello = buf.toString();
    console.log("Buffer to string: " + hello);
}

testBuffer();
