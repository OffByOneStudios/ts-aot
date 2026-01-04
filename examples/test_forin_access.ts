// Test for-in with computed access

const obj = { a: 1, b: 2 };

console.log("Test 1: Direct for-in values");
for (const k in obj) {
    console.log("  key=" + k + " value=" + obj[k]);
}

console.log("Test 2: Store key first");
for (const k in obj) {
    const key: string = k;
    const val = obj[key];
    console.log("  key=" + key + " value=" + val);
}

console.log("Done");
