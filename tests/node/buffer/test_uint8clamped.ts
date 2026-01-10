// Test Uint8ClampedArray clamping behavior

function user_main(): number {
    console.log("Testing Uint8ClampedArray...");

    // Create from array with values that need clamping
    const clamped = new Uint8ClampedArray([100, 300, -50, 0, 255]);

    // Values should be clamped
    console.log("Normal value (100):", clamped[0]);       // Should be 100
    console.log("Clamped high (300->255):", clamped[1]);  // Should be 255
    console.log("Clamped low (-50->0):", clamped[2]);     // Should be 0
    console.log("Zero:", clamped[3]);                     // Should be 0
    console.log("Max (255):", clamped[4]);                // Should be 255

    // Test length
    console.log("Length:", clamped.length);  // Should be 5

    console.log("Uint8ClampedArray tests complete!");
    return 0;
}
