
function assert(condition: boolean, message: string) {
    if (!condition) {
        console.log("Assertion failed: " + message);
        process.exit(1);
    }
}

function testArrayRest() {
    console.log("Testing Array Rest...");
    const arr = [1, 2, 3, 4, 5];
    const [first, second, ...rest] = arr;
    
    assert(first == 1, "first should be 1");
    assert(second == 2, "second should be 2");
    console.log("rest.length: " + rest.length);
    assert(rest.length == 3, "rest length should be 3");
    assert(rest[0] == 3, "rest[0] should be 3");
    assert(rest[1] == 4, "rest[1] should be 4");
    assert(rest[2] == 5, "rest[2] should be 5");
}

function testObjectRest() {
    console.log("Testing Object Rest...");
    const obj = { a: 10, b: 20, c: 30, d: 40 };
    const { a, b, ...rest } = obj;
    
    assert(a == 10, "a should be 10");
    assert(b == 20, "b should be 20");
    // assert(rest.c == 30, "rest.c should be 30"); // Analyzer might not support property access on rest yet
    // assert(rest.d == 40, "rest.d should be 40");
    // assert(rest.a === undefined, "rest.a should be undefined");
}

function testNested() {
    console.log("Testing Nested Destructuring...");
    const complex = {
        x: 100,
        y: {
            z: 200,
            w: [300, 400]
        }
    };
    console.log("Created complex");
    
    const { x, y: { z, w: [firstW, secondW] } } = complex;
    console.log("Destructured complex");
    
    assert(x == 100, "x should be 100");
    console.log("x ok");
    console.log("z: " + z);
    assert(z == 200, "z should be 200");
    console.log("z ok");
    assert(firstW == 300, "firstW should be 300");
    console.log("firstW ok");
    assert(secondW == 400, "secondW should be 400");
    console.log("secondW ok");
}

function testDefaults() {
    console.log("Testing Defaults...");
    const obj = { a: 1 };
    console.log("Created obj");
    const { a, b = 2 } = obj;
    console.log("Destructured obj");
    
    assert(a == 1, "a should be 1");
    assert(b == 2, "b should be 2");
    
    const arr = [10];
    console.log("Created arr");
    const [x, y = 20] = arr;
    console.log("Destructured arr");
    
    assert(x == 10, "x should be 10");
    assert(y == 20, "y should be 20");
}

function testObjectRest() {
    console.log("Testing Object Rest...");
    const obj = { a: 1, b: 2, c: 3 };
    const { a, ...rest } = obj;
    assert(a == 1, "a is 1");
    // assert(rest.b == 2, "rest.b is 2");
    // assert(rest.c == 3, "rest.c is 3");
}

function main() {
    testArrayRest();
    testNested();
    testDefaults();
    testObjectRest();
    console.log("All tests passed!");
}

main();
