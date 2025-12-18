async function test() {
    console.log("Start");
    const p = Promise.resolve(42);
    const val = await p;
    console.log("Promise resolved with: " + val);
}

test();
console.log("End of script");

