// Simple test
console.log("1. typeof global:", typeof global);
console.log("2. global value:", global);
console.log("3. global && true:", global && true);

const g = global;
console.log("4. g value:", g);
console.log("5. g && true:", g && true);

if (g) {
    console.log("6. Inside if(g) - SUCCESS");
    (g as any).testProp = 42;
    console.log("7. g.testProp:", (g as any).testProp);
} else {
    console.log("6. FAILED - g is falsy");
}
