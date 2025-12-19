function testPromise() {
    console.log("Starting promise test...");

    // Test catch
    Promise.reject("Error 1").catch((err) => {
        console.log("Caught error: " + err);
        return "Recovered";
    }).then((val) => {
        console.log("Then after catch: " + val);
    });

    // Test finally
    Promise.resolve("Success").finally(() => {
        console.log("Finally called for success");
    }).then((val) => {
        console.log("Value after finally: " + val);
    });

    Promise.reject("Failure").catch((err) => {
        console.log("Caught for finally test: " + err);
    }).finally(() => {
        console.log("Finally called for failure");
    });

    console.log("Promise test complete.");
}

testPromise();

