function testTypeGuard(x: string | number) {
    if (typeof x === "string") {
        ts_console_log(x);
    } else {
        ts_console_log("it is a number");
    }
}

testTypeGuard("hello");
testTypeGuard(123);
