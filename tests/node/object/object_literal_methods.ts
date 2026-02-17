// Test shorthand methods on object literals

function user_main(): number {
    let failures = 0;

    // Test 1: Basic shorthand method
    const obj = {
        greet() {
            return "hello";
        }
    };
    if (obj.greet() === "hello") {
        console.log("PASS: basic shorthand method");
    } else {
        console.log("FAIL: expected 'hello', got '" + obj.greet() + "'");
        failures++;
    }

    // Test 2: Method using captured variable from outer scope
    const name = "world";
    const greeter = {
        sayHello() {
            return "hello " + name;
        }
    };
    if (greeter.sayHello() === "hello world") {
        console.log("PASS: method with closure capture");
    } else {
        console.log("FAIL: expected 'hello world', got '" + greeter.sayHello() + "'");
        failures++;
    }

    // Test 3: Method with multiple parameters and logic
    const calc = {
        add(a: number, b: number) {
            return a + b;
        },
        square(x: number) {
            return x * x;
        }
    };
    if (calc.add(3, 7) === 10) {
        console.log("PASS: shorthand method with two params");
    } else {
        console.log("FAIL: add(3, 7) expected 10, got " + calc.add(3, 7));
        failures++;
    }
    if (calc.square(5) === 25) {
        console.log("PASS: shorthand method with computation");
    } else {
        console.log("FAIL: square(5) expected 25, got " + calc.square(5));
        failures++;
    }

    // Test 4: Method returning another object's method result
    const inner = {
        getValue() {
            return 42;
        }
    };
    const outer = {
        getInnerValue() {
            return inner.getValue();
        }
    };
    if (outer.getInnerValue() === 42) {
        console.log("PASS: method calling another object's method");
    } else {
        console.log("FAIL: expected 42, got " + outer.getInnerValue());
        failures++;
    }

    // Test 5: Shorthand method with parameters
    const math = {
        add(a: number, b: number) {
            return a + b;
        },
        multiply(a: number, b: number) {
            return a * b;
        }
    };
    if (math.add(3, 4) === 7 && math.multiply(3, 4) === 12) {
        console.log("PASS: shorthand methods with parameters");
    } else {
        console.log("FAIL: add or multiply returned wrong value");
        failures++;
    }

    console.log("---");
    if (failures === 0) {
        console.log("All object literal method tests passed!");
    } else {
        console.log(failures + " test(s) failed");
    }

    return failures;
}
