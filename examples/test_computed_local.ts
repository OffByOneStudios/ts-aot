// Test computed access without global

function test() {
    const obj = { a: 1 };
    const key = "a";
    console.log("Value: " + obj[key]);
}

test();
console.log("Done");
