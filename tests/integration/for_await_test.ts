async function* asyncGen() {
    yield 1;
    yield 2;
    yield 3;
}

async function test() {
    console.log("Start for-await");
    for await (const x of asyncGen()) {
        console.log("Got: " + x);
    }
    console.log("End for-await");
}

test();
console.log("End of script");
