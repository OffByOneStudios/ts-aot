function testPromiseStatic() {
    console.log("Starting Promise.all test...");
    
    const p1 = Promise.resolve(1);
    const p2 = Promise.resolve(2);
    const p3 = Promise.resolve(3);
    
    Promise.all([p1, p2, p3]).then((results) => {
        console.log("Promise.all resolved");
        // results is an array [1, 2, 3]
        // We don't have full array indexing yet in a way that's easy to test here,
        // but we can check if it's an array.
    });

    console.log("Starting Promise.race test...");
    const r1 = Promise.resolve("race win");
    Promise.race([r1]).then((winner) => {
        console.log("Promise.race resolved with: " + winner);
    });

    console.log("Starting Promise.allSettled test...");
    const s1 = Promise.resolve(1);
    const s2 = Promise.reject("error");
    
    Promise.allSettled([s1, s2]).then((results) => {
        console.log("Promise.allSettled resolved");
    });

    console.log("Starting Promise.any test...");
    const a1 = Promise.reject("fail 1");
    const a2 = Promise.resolve("win");
    const a3 = Promise.reject("fail 2");

    Promise.any([a1, a2, a3]).then((winner) => {
        console.log("Promise.any resolved with: " + winner);
    });
}

testPromiseStatic();
