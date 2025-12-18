async function test() {
    const add = async (a: number, b: number) => {
        return a + b;
    };

    console.log("Starting arrow test...");
    const sum = await add(10, 20);
    console.log("Sum: " + sum);
}

test();
