// Test ES2024 Promise.withResolvers()

function user_main(): number {
    console.log("Testing Promise.withResolvers()...");

    // Test 1: Verify object structure
    console.log("\n1. Object structure:");
    const resolvers = Promise.withResolvers();
    console.log("has promise: " + (resolvers.promise !== undefined));
    console.log("has resolve: " + (resolvers.resolve !== undefined));
    console.log("has reject: " + (resolvers.reject !== undefined));

    // Test 2: Basic resolve with then
    console.log("\n2. Basic resolve:");
    const { promise: p1, resolve: resolve1 } = Promise.withResolvers();
    p1.then((value: any) => {
        console.log("resolved with: " + value);
    });
    resolve1("success");

    // Test 3: Basic reject with catch
    console.log("\n3. Basic reject:");
    const { promise: p2, reject: reject2 } = Promise.withResolvers();
    p2.catch((reason: any) => {
        console.log("rejected with: " + reason);
    });
    reject2("error");

    // Test 4: Multiple resolves (second should be ignored)
    console.log("\n4. Multiple resolves (second ignored):");
    const { promise: p3, resolve: resolve3 } = Promise.withResolvers();
    p3.then((value: any) => {
        console.log("p3 resolved with: " + value);
    });
    resolve3("first");
    resolve3("second");  // Should be ignored

    // Test 5: Deferred resolution
    console.log("\n5. Deferred resolution:");
    const { promise: p4, resolve: resolve4 } = Promise.withResolvers();
    p4.then((value: any) => {
        console.log("p4 resolved with: " + value);
    });

    setTimeout(() => {
        resolve4("delayed");
    }, 10);

    console.log("\nAll Promise.withResolvers() tests complete!");
    return 0;
}
