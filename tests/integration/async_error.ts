
async function testAsyncError() {
    console.log("Starting async error test...");
    try {
        await Promise.reject("error");
        console.log("Should not be here");
    } catch (e) {
        console.log("Caught error: " + e);
    } finally {
        console.log("Finally block executed");
    }
}

testAsyncError();
