// Simulate toFinite to see where it breaks

// Constants - use unique names to avoid clashes
const MY_INFINITY = 1 / 0;
const MY_MAX_INTEGER = 1.7976931348623157e+308;

console.log("MY_INFINITY:", MY_INFINITY);
console.log("typeof MY_INFINITY:", typeof MY_INFINITY);
console.log("MY_INFINITY === Infinity:", MY_INFINITY === Infinity);

// toNumber simulation - simple version
function toNumber(value: any): any {
    if (typeof value == 'number') {
        return value;
    }
    return 0;
}

// toFinite simulation
function toFinite(value: any): any {
    console.log("  toFinite input:", value);
    if (!value) {
        console.log("  !value branch");
        return value === 0 ? value : 0;
    }
    value = toNumber(value);
    console.log("  after toNumber:", value);
    console.log("  value === MY_INFINITY:", value === MY_INFINITY);
    console.log("  value === -MY_INFINITY:", value === -MY_INFINITY);
    if (value === MY_INFINITY || value === -MY_INFINITY) {
        const sign = (value < 0 ? -1 : 1);
        return sign * MY_MAX_INTEGER;
    }
    console.log("  value === value:", value === value);
    return value === value ? value : 0;
}

console.log("\n=== Testing toFinite ===");
console.log("toFinite(2):", toFinite(2));
console.log("toFinite(0):", toFinite(0));
console.log("toFinite(2.5):", toFinite(2.5));
