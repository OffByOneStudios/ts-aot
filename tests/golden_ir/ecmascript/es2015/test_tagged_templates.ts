// Test tagged template literals

function highlight(strings: string[], ...values: any[]): string {
    let result = strings[0];
    for (let i = 0; i < values.length; i++) {
        result += "[" + values[i] + "]" + strings[i + 1];
    }
    return result;
}

function user_main(): number {
    console.log("Testing tagged templates...");

    const name = "World";
    const count = 42;

    // Basic tagged template with multiple interpolations
    const result = highlight`Hello ${name}! Count: ${count}.`;
    console.log(result);
    // Expected: Hello [World]! Count: [42].

    // Tagged template with no interpolations
    const simple = highlight`Just a string`;
    console.log(simple);
    // Expected: Just a string

    // Tagged template with expressions
    const math = highlight`Sum: ${1 + 2 + 3}`;
    console.log(math);
    // Expected: Sum: [6]

    // Tagged template with different types
    const mixed = highlight`Bool: ${true}, Float: ${3.14}`;
    console.log(mixed);
    // Expected: Bool: [true], Float: [3.14]

    console.log("Tagged templates tests complete!");
    return 0;
}
