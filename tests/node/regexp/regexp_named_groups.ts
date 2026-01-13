// Test RegExp named capture groups (ES2018)

function user_main(): number {
    console.log("=== RegExp Named Capture Groups Test ===");

    // Test 1: Basic named capture groups
    console.log("Test 1: Basic named groups");
    const regex1 = /(?<year>\d{4})-(?<month>\d{2})-(?<day>\d{2})/;
    const result1 = regex1.exec("2024-12-25");

    if (result1) {
        console.log("Match found:");
        console.log(result1[0]);  // "2024-12-25"
        console.log(result1[1]);  // "2024"
        console.log(result1[2]);  // "12"
        console.log(result1[3]);  // "25"

        console.log("Groups:");
        const groups1 = result1.groups;
        if (groups1) {
            console.log(groups1.year);   // "2024"
            console.log(groups1.month);  // "12"
            console.log(groups1.day);    // "25"
        } else {
            console.log("No groups!");
        }
    } else {
        console.log("No match!");
    }

    // Test 2: No named groups - groups should be undefined
    console.log("Test 2: No named groups");
    const regex2 = /(\d+)-(\d+)/;
    const result2 = regex2.exec("123-456");
    if (result2) {
        console.log(result2[0]);  // "123-456"
        if (result2.groups) {
            console.log("Has groups (unexpected)");
        } else {
            console.log("No groups (expected)");
        }
    }

    // Test 3: Mixed named and unnamed groups
    console.log("Test 3: Mixed named and unnamed");
    const regex3 = /(\d+)-(?<name>\w+)-(\d+)/;
    const result3 = regex3.exec("123-hello-456");
    if (result3) {
        console.log(result3[0]);  // "123-hello-456"
        console.log(result3[1]);  // "123"
        console.log(result3[2]);  // "hello"
        console.log(result3[3]);  // "456"

        const groups3 = result3.groups;
        if (groups3) {
            console.log(groups3.name);  // "hello"
        }
    }

    // Test 4: Named groups with d flag (indices)
    console.log("Test 4: Named groups with d flag");
    const regex4 = /(?<first>\w+)\s+(?<second>\w+)/d;
    const result4 = regex4.exec("Hello World");
    if (result4) {
        console.log(result4[0]);  // "Hello World"

        const groups4 = result4.groups;
        if (groups4) {
            console.log(groups4.first);   // "Hello"
            console.log(groups4.second);  // "World"
        }

        // Also check indices
        const indices4 = result4.indices;
        if (indices4) {
            console.log("Has indices");
        }
    }

    // Test 5: Backreference to named group
    console.log("Test 5: Named groups in longer pattern");
    const regex5 = /(?<protocol>https?):\/\/(?<host>[^/]+)(?<path>\/.*)?/;
    const result5 = regex5.exec("https://example.com/path/to/page");
    if (result5) {
        const groups5 = result5.groups;
        if (groups5) {
            console.log(groups5.protocol);  // "https"
            console.log(groups5.host);      // "example.com"
            console.log(groups5.path);      // "/path/to/page"
        }
    }

    console.log("=== Tests complete ===");
    return 0;
}
