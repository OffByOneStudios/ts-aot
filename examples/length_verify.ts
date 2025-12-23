
function test() {
    const arr = [1, 2, 3];
    console.log("Array length: " + arr.length);
    
    const str = "hello";
    console.log("String length: " + str.length);
    
    const u8 = new Uint8Array(10);
    console.log("Uint8Array length: " + u8.length);
    
    const f64 = new Float64Array(5);
    console.log("Float64Array length: " + f64.length);
}

test();
