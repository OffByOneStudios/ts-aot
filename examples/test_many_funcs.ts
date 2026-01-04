// Test creating many functions
console.log("Starting function creation test...");

const funcs: any = {};

// Create 1000 functions
for (let i = 0; i < 1000; i = i + 1) {
    const key = "fn" + i;
    funcs[key] = function(x: any): any {
        return x;
    };
}

console.log("Done creating 1000 functions");
console.log("Function count:", Object.keys(funcs).length);

// Call one to verify
console.log("Test call:", funcs.fn0(42));
