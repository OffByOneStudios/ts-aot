// Test BigInt support (ES2020)

function user_main(): number {
    // Basic BigInt literals
    const a = 10n;
    const b = 20n;

    // Arithmetic operations
    const sum = a + b;
    const diff = b - a;
    const prod = a * b;
    const quot = b / a;
    const rem = 25n % 10n;

    console.log("Arithmetic:");
    console.log(sum);   // 30
    console.log(diff);  // 10
    console.log(prod);  // 200
    console.log(quot);  // 2
    console.log(rem);   // 5

    // Large numbers (beyond safe integer range)
    const big1 = 9007199254740993n;  // MAX_SAFE_INTEGER + 2
    const big2 = 9007199254740994n;
    console.log("Large numbers:");
    console.log(big1 + 1n);  // 9007199254740994

    // Comparisons
    console.log("Comparisons:");
    console.log(a < b);   // true
    console.log(a > b);   // false
    console.log(a === a); // true
    console.log(a !== b); // true
    console.log(a <= 10n); // true
    console.log(b >= 20n); // true

    // Bitwise operations
    const x = 0b1010n;  // 10
    const y = 0b1100n;  // 12
    console.log("Bitwise:");
    console.log(x & y);  // 8 (0b1000)
    console.log(x | y);  // 14 (0b1110)
    console.log(x ^ y);  // 6 (0b0110)

    // Negation
    const neg = -a;
    console.log("Negation:");
    console.log(neg);   // -10

    return 0;
}
