function testUnknown(x: unknown) {
    if (typeof x === "string") {
        ts_console_log("x is string: " + x);
    } else if (typeof x === "number") {
        ts_console_log("x is number: " + x);
    } else {
        ts_console_log("x is unknown");
    }
}

function fail(msg: string): never {
    ts_console_log("Fatal error: " + msg);
    // In a real system this would exit or throw
    // For testing, we just return, but the type system thinks it never returns
    return undefined as never;
}

function testNever(x: string | number) {
    if (typeof x === "string") {
        ts_console_log("string: " + x);
    } else if (typeof x === "number") {
        ts_console_log("number: " + x);
    } else {
        // x is never here
        let y: never = x;
        ts_console_log("this should not happen");
    }
}

function main() {
    testUnknown("hello");
    testUnknown(42);
    testUnknown(true);

    testNever("world");
    testNever(100);
    
    ts_console_log("Done");
}
