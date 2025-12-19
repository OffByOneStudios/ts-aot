function testUnhandled() {
    console.log("testUnhandled called");
    Promise.reject("This should be unhandled");
}

console.log("Top level start");
testUnhandled();
console.log("Top level end");
