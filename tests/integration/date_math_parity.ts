function testMath() {
    console.log("Testing Math parity...");
    console.log(Math.log10(100)); // 2
    console.log(Math.log2(8));    // 3
    console.log(Math.trunc(3.9)); // 3
    console.log(Math.trunc(-3.9)); // -3
    console.log(Math.cbrt(27));   // 3
    console.log(Math.clz32(1));   // 31
    console.log(Math.hypot(3, 4)); // 5
    console.log(Math.fround(1.5)); // 1.5
    console.log(Math.sign(10));    // 1
    console.log(Math.sign(-10));   // -1
    console.log(Math.sign(0));     // 0
}

function testDate() {
    console.log("Testing Date parity...");
    const d = new Date(1609459200000); // 2021-01-01T00:00:00.000Z
    console.log(d.getTime());
    console.log(d.getUTCFullYear());
    console.log(d.getUTCMonth());
    console.log(d.getUTCDate());
    
    d.setUTCFullYear(2022);
    console.log(d.getUTCFullYear());
    
    console.log(d.toISOString());
}

testMath();
testDate();
