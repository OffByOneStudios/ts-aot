function testFinally() {
    console.log("--- testFinally ---");
    try {
        console.log("In try");
        throw "Error";
    } catch (e) {
        console.log("In catch: " + e);
    } finally {
        console.log("In finally");
    }
}

function testUncaughtFinally() {
    console.log("--- testUncaughtFinally ---");
    try {
        try {
            console.log("In try (uncaught)");
            throw "uncaught";
        } finally {
            console.log("In finally (uncaught)");
        }
    } catch (e) {
        console.log("Caught outer: " + e);
    }
}

function testCatchRethrow() {
    console.log("--- testCatchRethrow ---");
    try {
        try {
            console.log("In try");
            throw "A";
        } catch (e) {
            console.log("In catch: " + e);
            throw "B";
        } finally {
            console.log("In finally");
        }
    } catch (e) {
        console.log("Caught outer: " + e);
    }
}

function testNoCatchFinally() {
    console.log("--- testNoCatchFinally ---");
    try {
        try {
            console.log("In try");
            throw "C";
        } finally {
            console.log("In finally");
        }
    } catch (e) {
        console.log("Caught outer: " + e);
    }
}

testFinally();
testUncaughtFinally();
testCatchRethrow();
testNoCatchFinally();

function testReturnInTry() {
    console.log("--- testReturnInTry ---");
    try {
        console.log("In try");
        return "returned from try";
    } finally {
        console.log("In finally");
    }
}

console.log("Result:", testReturnInTry());

function testReturnInFinally() {
    console.log("--- testReturnInFinally ---");
    try {
        return "from try";
    } finally {
        return "from finally";
    }
}

console.log("Result 2:", testReturnInFinally());

function testNestedFinallyReturn() {
    console.log("--- testNestedFinallyReturn ---");
    try {
        try {
            return "inner try";
        } finally {
            console.log("inner finally");
        }
    } finally {
        console.log("outer finally");
    }
}

console.log("Result 3:", testNestedFinallyReturn());

function testBreakInTry() {
    console.log("--- testBreakInTry ---");
    let i = 0;
    while (i < 10) {
        try {
            console.log("In try, i=" + i);
            if (i === 5) {
                console.log("Breaking");
                break;
            }
        } finally {
            console.log("In finally, i=" + i);
        }
        i++;
    }
    console.log("After loop");
}

testBreakInTry();

function testContinueInTry() {
    console.log("--- testContinueInTry ---");
    let i = 0;
    while (i < 5) {
        try {
            console.log("In try, i=" + i);
            if (i === 2) {
                console.log("Continuing");
                i++;
                continue;
            }
        } finally {
            console.log("In finally, i=" + i);
        }
        i++;
    }
    console.log("After loop");
}

function testBreakInSwitch() {
    console.log("--- testBreakInSwitch ---");
    let x = 1;
    switch (x) {
        case 1:
            try {
                console.log("In try");
                break;
            } finally {
                console.log("In finally");
            }
            // console.log("Should not reach here");
    }
    console.log("After switch");
}

function testBreakInSwitchNested() {
    console.log("--- testBreakInSwitchNested ---");
    let x = 1;
    switch (x) {
        case 1:
            try {
                try {
                    console.log("In inner try");
                    break;
                } finally {
                    console.log("In inner finally");
                }
            } finally {
                console.log("In outer finally");
            }
    }
    console.log("After switch nested");
}

function testErrorStack() {
    console.log("--- testErrorStack ---");
    try {
        throw new Error("Something went wrong");
    } catch (e) {
        console.log("Caught: " + e.message);
        console.log("Stack trace:");
        console.log(e.stack);
    }
}

testBreakInSwitch();
testBreakInSwitchNested();
testErrorStack();
