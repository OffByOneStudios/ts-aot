async function add(a: number, b: number): Promise<number> {
    return a + b;
}

async function test() {
    ts_console_log("Starting test...");
    const sum = await add(5, 10);
    ts_console_log("Sum: " + sum);
}

test();
