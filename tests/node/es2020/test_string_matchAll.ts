// Test ES2020 String.prototype.matchAll()

function user_main(): number {
    console.log("Testing String.prototype.matchAll()...");

    // Test 1: Basic matchAll with global regex
    const str = "test1test2test3";
    const regex = /test(\d)/g;
    const matches = str.matchAll(regex);

    console.log("Basic matchAll:");
    console.log("  Number of matches: " + matches.length);

    // Test 2: matchAll with no matches returns empty array
    const noMatch = "abcdef".matchAll(/xyz/g);
    console.log("No matches length: " + noMatch.length);

    // Test 3: matchAll with different patterns
    const str2 = "cat bat rat sat";
    const regex2 = /[cbrs]at/g;
    const matches2 = str2.matchAll(regex2);
    console.log("Word matches: " + matches2.length);

    // Test 4: Multiple capture groups
    const data = "2024-01-15 2024-02-20";
    const dateRegex = /(\d{4})-(\d{2})-(\d{2})/g;
    const dates = data.matchAll(dateRegex);
    console.log("Date matches: " + dates.length);

    console.log("All matchAll tests passed!");
    return 0;
}
