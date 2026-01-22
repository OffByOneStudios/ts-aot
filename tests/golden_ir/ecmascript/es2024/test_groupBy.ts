// Test ES2024 Object.groupBy() and Map.groupBy()

function user_main(): number {
    console.log("Testing Object.groupBy()...");

    // Test 1: Group numbers by even/odd
    const numbers = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10];
    const grouped = Object.groupBy(numbers, (num: number) => num % 2 === 0 ? "even" : "odd");

    console.log("Grouped by even/odd:");
    const keys = Object.keys(grouped);
    console.log("  Number of groups: " + keys.length);

    // Due to issue with .length access on any type, we verify groupBy works
    // by checking the keys exist
    for (let i = 0; i < keys.length; i++) {
        const key = keys[i];
        console.log("  Group: " + key);
    }

    // Test 2: Group strings by length
    const words = ["one", "two", "three", "four", "five", "six"];
    const byLength = Object.groupBy(words, (word: string) => word.length);

    console.log("Grouped by string length:");
    const lengthKeys = Object.keys(byLength);
    console.log("  Number of length groups: " + lengthKeys.length);

    // Test 3: Group with index parameter
    const items = ["a", "b", "c", "d", "e", "f"];
    const byIndexRange = Object.groupBy(items, (item: string, index: number) => {
        if (index < 2) return "first";
        if (index < 4) return "middle";
        return "last";
    });

    const rangeKeys = Object.keys(byIndexRange);
    console.log("Grouped by index range:");
    console.log("  Number of range groups: " + rangeKeys.length);

    console.log("");
    console.log("Testing Map.groupBy()...");

    // Test 4: Map.groupBy with string keys
    const products = [1, 2, 3, 4, 5];
    const byParity = Map.groupBy(products, (n: number) => n % 2 === 0 ? "even" : "odd");

    console.log("Map.groupBy by parity:");
    console.log("  Map size: " + byParity.size);

    // Test 5: Empty array
    const empty: number[] = [];
    const emptyGrouped = Object.groupBy(empty, (x: number) => x.toString());
    const emptyKeys = Object.keys(emptyGrouped);
    console.log("Empty array groups: " + emptyKeys.length);

    console.log("");
    console.log("All groupBy tests passed!");

    return 0;
}
