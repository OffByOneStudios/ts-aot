// Test this keyword functionality

class Counter {
    private count: number = 0;

    constructor(initial: number) {
        this.count = initial;
    }

    increment(): Counter {
        this.count++;
        return this;  // Fluent interface using this
    }

    decrement(): Counter {
        this.count--;
        return this;
    }

    getCount(): number {
        return this.count;
    }

    static create(initial: number): Counter {
        return new Counter(initial);
    }
}

function user_main(): number {
    let passed = 0;
    let failed = 0;

    // Test 1: Basic this in constructor
    const c1 = new Counter(10);
    if (c1.getCount() === 10) {
        console.log("PASS: this works in constructor");
        passed++;
    } else {
        console.log("FAIL: this in constructor");
        failed++;
    }

    // Test 2: this in methods
    c1.increment();
    if (c1.getCount() === 11) {
        console.log("PASS: this works in methods");
        passed++;
    } else {
        console.log("FAIL: this in methods");
        failed++;
    }

    // Test 3: Fluent interface (returning this)
    const c2 = new Counter(0);
    c2.increment().increment().increment();
    if (c2.getCount() === 3) {
        console.log("PASS: fluent interface with this return");
        passed++;
    } else {
        console.log("FAIL: fluent interface");
        failed++;
    }

    // Test 4: this in object literal
    const obj = {
        value: 42,
        getValue(): number {
            return this.value;
        },
        setValue(v: number): void {
            this.value = v;
        }
    };
    if (obj.getValue() === 42) {
        console.log("PASS: this in object literal method");
        passed++;
    } else {
        console.log("FAIL: this in object literal");
        failed++;
    }

    // Test 5: this in getter
    const obj2 = {
        _x: 100,
        get x(): number {
            return this._x;
        },
        set x(v: number) {
            this._x = v;
        }
    };
    if (obj2.x === 100) {
        console.log("PASS: this in getter");
        passed++;
    } else {
        console.log("FAIL: this in getter");
        failed++;
    }

    // Test 6: this in setter
    obj2.x = 200;
    if (obj2.x === 200) {
        console.log("PASS: this in setter");
        passed++;
    } else {
        console.log("FAIL: this in setter");
        failed++;
    }

    console.log(`Results: ${passed} passed, ${failed} failed`);
    return failed > 0 ? 1 : 0;
}
