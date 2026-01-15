// Test console.table()
function user_main(): number {
    console.log("Testing console.table()...");

    // Test with array of primitives
    console.log("\n--- Array of numbers ---");
    const numbers: number[] = [10, 20, 30];
    console.table(numbers);

    // Test with array of strings
    console.log("\n--- Array of strings ---");
    const fruits: string[] = ["apple", "banana", "cherry"];
    console.table(fruits);

    // Test with object
    console.log("\n--- Object ---");
    const person = {
        name: "Alice",
        age: 30,
        city: "NYC"
    };
    console.table(person);

    // Test console.dirxml (alias for dir)
    console.log("\n--- console.dirxml ---");
    console.dirxml({ test: 123 });

    console.log("\nAll console.table() tests passed!");
    return 0;
}
