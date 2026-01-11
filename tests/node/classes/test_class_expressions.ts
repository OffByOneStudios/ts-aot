// Test class expressions (ES6)

function user_main(): number {
    let passed = 0;
    let failed = 0;

    // Test 1: Basic named class expression
    const MyClass = class Animal {
        name: string;
        constructor(name: string) {
            this.name = name;
        }
        speak(): string {
            return "Hello from " + this.name;
        }
    };
    const obj1 = new MyClass("Cat");
    if (obj1.speak() === "Hello from Cat") {
        console.log("PASS: Basic named class expression");
        passed++;
    } else {
        console.log("FAIL: Basic named class expression - got " + obj1.speak());
        failed++;
    }

    // Test 2: Anonymous class expression
    const AnonClass = class {
        value: number;
        constructor(v: number) {
            this.value = v;
        }
        double(): number {
            return this.value * 2;
        }
    };
    const obj2 = new AnonClass(5);
    if (obj2.double() === 10) {
        console.log("PASS: Anonymous class expression");
        passed++;
    } else {
        console.log("FAIL: Anonymous class expression - got " + obj2.double());
        failed++;
    }

    console.log("---");
    console.log("Passed: " + passed);
    console.log("Failed: " + failed);

    return failed > 0 ? 1 : 0;
}
