function testNullNarrowing(x: string | null) {
    if (x !== null) {
        ts_console_log("x is string: " + x);
    } else {
        ts_console_log("x is null");
    }
}

function testTruthiness(x: string | undefined) {
    if (x) {
        ts_console_log("x is truthy: " + x);
    } else {
        ts_console_log("x is falsy");
    }
}

function testUnknownNarrowing(x: unknown) {
    if (typeof x === "string") {
        ts_console_log("x is string: " + x);
    } else {
        // ts_console_log(x.length); // Should fail if I uncomment this
        ts_console_log("x is not string");
    }
}

function main() {
    testNullNarrowing("hello");
    testNullNarrowing(null);
    
    testTruthiness("world");
    testTruthiness(undefined);
    
    testUnknownNarrowing("test");
    testUnknownNarrowing(123);
    
    ts_console_log("Done");
}
