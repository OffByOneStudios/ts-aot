// Test Promise.allSettled()

async function user_main(): Promise<number> {
    const p1 = Promise.resolve(1);
    const p2 = Promise.reject("error");
    const p3 = Promise.resolve(3);

    const results = await Promise.allSettled([p1, p2, p3]);
    console.log("allSettled count: " + results.length);

    return 0;
}
