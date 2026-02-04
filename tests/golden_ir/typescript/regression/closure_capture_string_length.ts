// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// OUTPUT: textA length: 53
// OUTPUT: textB length (captured): 107
// OUTPUT: textC length: 161
// OUTPUT: closure result: 107

// Test that string.length works correctly when the string variable is captured by a closure.
// Previously, cell variables (used for closure capture) returned boxed TsValue* from ts_cell_get,
// but ts_value_get_object didn't extract STRING_PTR types, causing .length to return 0.

function generateText(lines: number): string {
    const baseLine = 'hello world user@example.com https://example.com/path';
    let result = baseLine;
    for (let i = 1; i < lines; i++) {
        result = result + '\n' + baseLine;
    }
    return result;
}

function user_main(): number {
    // textA - not captured
    const textA = generateText(1);

    // textB - captured by closure (becomes a cell variable)
    const textB = generateText(2);

    // textC - not captured
    const textC = generateText(3);

    // Access length BEFORE defining the closure
    console.log("textA length: " + textA.length);
    console.log("textB length (captured): " + textB.length);
    console.log("textC length: " + textC.length);

    // Define a closure that captures textB
    const captureB = () => {
        return textB.length;
    };

    // Call the closure
    console.log("closure result: " + captureB());

    return 0;
}
