// ES2018 for await...of - Async Iteration

async function user_main(): Promise<number> {
    console.log("Testing for await...of");

    // Test for await...of with array of resolved promises
    console.log("Creating promises:");
    const promises: Promise<number>[] = [
        Promise.resolve(1),
        Promise.resolve(2),
        Promise.resolve(3)
    ];
    console.log("Array created with length:");
    console.log(promises.length);

    console.log("Starting for await...of:");
    let sum = 0;
    for await (const value of promises) {
        console.log("Got value:");
        console.log(value);
        sum = sum + value;
    }

    console.log("Sum:");
    console.log(sum);

    return 0;
}
