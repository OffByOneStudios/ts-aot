async function add(a: number, b: number): Promise<number> {
    return a + b;
}

async function test() {
    console.log("Starting async test");
    const sum = await add(5, 10);
    console.log("Sum: " + sum);
    
    const p = Promise.resolve(42);
    const val = await p;
    console.log("Resolved val: " + val);
}

test();
