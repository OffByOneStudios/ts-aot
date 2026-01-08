// Test all remaining ES6 Math methods

function user_main(): number {
    // Math.expm1 (e^x - 1)
    console.log("Math.expm1(0): " + Math.expm1(0));  // 0

    // Math.log1p (ln(1+x))
    console.log("Math.log1p(0): " + Math.log1p(0));  // 0

    // Math.fround (nearest 32-bit float)
    console.log("Math.fround(1.5): " + Math.fround(1.5));  // 1.5

    // Math.clz32 (count leading zeros in 32-bit int)
    console.log("Math.clz32(1): " + Math.clz32(1));  // 31

    return 0;
}
