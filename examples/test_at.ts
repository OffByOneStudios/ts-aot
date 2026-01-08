// Test Array.prototype.at()

function user_main(): number {
    const arr: number[] = [10, 20, 30, 40, 50];

    // Test positive index
    console.log("arr.at(0): " + arr.at(0));  // 10
    console.log("arr.at(2): " + arr.at(2));  // 30

    // Test negative index
    console.log("arr.at(-1): " + arr.at(-1));  // 50
    console.log("arr.at(-2): " + arr.at(-2));  // 40

    return 0;
}
