// Test program that crashes - for demonstrating auto-debug skill

// Infinite recursion to cause stack overflow
function causeStackOverflow(depth: number): number {
    console.log("Depth: " + depth);
    return causeStackOverflow(depth + 1);
}

function main(): void {
    console.log("Starting crash test...");
    const result = causeStackOverflow(0);
    console.log("Result: " + result);
}

main();
