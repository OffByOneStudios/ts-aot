// Test ES2019 Optional catch binding
// catch { } without parameter

function testOptionalCatch(): void {
    let caught = false;

    try {
        throw new Error("test error");
    } catch {
        // No parameter - ES2019 optional catch binding
        caught = true;
        console.log("Caught without parameter");
    }

    console.log(caught ? "true" : "false");
}

function testWithParameter(): void {
    try {
        throw new Error("test error 2");
    } catch (e) {
        // Traditional catch with parameter
        console.log("Caught with parameter");
    }
}

function testFinally(): void {
    let finallyRan = false;

    try {
        throw new Error("test");
    } catch {
        console.log("Caught in finally test");
    } finally {
        finallyRan = true;
    }

    console.log(finallyRan ? "finally ran" : "finally did not run");
}

function user_main(): number {
    testOptionalCatch();
    testWithParameter();
    testFinally();
    return 0;
}
