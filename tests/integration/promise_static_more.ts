function testPromiseStaticMore() {
    console.log("Starting Promise.allSettled test...");
    const p1 = Promise.resolve(1);
    const p2 = Promise.reject("error");
    
    Promise.allSettled([p1, p2]).then((results) => {
        console.log("Promise.allSettled resolved");
        // results is an array of objects
    });

    console.log("Starting Promise.any test...");
    const a1 = Promise.reject("fail 1");
    const a2 = Promise.resolve("win");
    const a3 = Promise.reject("fail 2");

    Promise.any([a1, a2, a3]).then((winner) => {
        console.log("Promise.any resolved with: " + winner);
    });

    console.log("Starting Promise.any all-fail test...");
    const f1 = Promise.reject("fail 1");
    const f2 = Promise.reject("fail 2");
    Promise.any([f1, f2]).catch((err) => {
        console.log("Promise.any rejected as expected");
    });
}

testPromiseStaticMore();
