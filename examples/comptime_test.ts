const secret = ts_aot.comptime(() => {
    return 42;
});

const message = ts_aot.comptime(() => {
    return "Hello from comptime!";
});

class Test {
    @ts_aot.comptime
    static #privateField = 123;

    static init() {
        console.log("Private field value:", Test.#privateField);
    }
}

console.log("Secret value:", secret);
console.log("Message:", message);
Test.init();
