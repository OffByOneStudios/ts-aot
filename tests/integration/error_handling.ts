function throwError(msg: string) {
    ts_console_log("Throwing error...");
    throw msg;
}

function testTryCatch() {
    try {
        ts_console_log("In try block");
        throwError("Something went wrong");
        ts_console_log("This should not be printed");
    } catch (e) {
        ts_console_log("Caught error:");
        ts_console_log(e as string);
    } finally {
        ts_console_log("In finally block");
    }
    ts_console_log("After try-catch-finally");
}

testTryCatch();
