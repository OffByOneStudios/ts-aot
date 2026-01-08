// Test Array.reduceRight

function user_main(): number {
    // Basic reduceRight - accumulates from right to left
    const arr1 = [1, 2, 3, 4, 5];
    const sum = arr1.reduceRight((acc, val) => acc + val, 0);
    console.log(sum);  // Expected: 15

    // reduceRight without initial value (uses last element as initial)
    const arr2 = [1, 2, 3, 4];
    const product = arr2.reduceRight((acc, val) => acc * val);
    console.log(product);  // Expected: 24 (4*3*2*1)

    // Compare reduce vs reduceRight order
    const arr3 = [1, 2, 3];
    const leftToRight = arr3.reduce((acc, val) => acc - val, 0);
    const rightToLeft = arr3.reduceRight((acc, val) => acc - val, 0);
    console.log(leftToRight);   // Expected: -6 (0-1-2-3)
    console.log(rightToLeft);   // Expected: -6 (0-3-2-1)

    return 0;
}
