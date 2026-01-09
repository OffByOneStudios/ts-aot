// Test class getters and setters

class Person {
    private _name: string;

    constructor(name: string) {
        this._name = name;
    }

    get name(): string {
        return this._name;
    }
}

class Counter {
    private _count: number = 0;

    get count(): number {
        return this._count;
    }

    set count(value: number) {
        if (value >= 0) {
            this._count = value;
        }
    }
}

class Rectangle {
    width: number;
    height: number;

    constructor(w: number, h: number) {
        this.width = w;
        this.height = h;
    }

    get area(): number {
        return this.width * this.height;
    }
}

function user_main(): number {
    // Test 1: Basic getter
    console.log("Test 1 - Basic getter:");
    const p = new Person("Alice");
    console.log(p.name);  // Should print "Alice"

    // Test 2: Getter and Setter
    console.log("Test 2 - Getter and Setter:");
    const c = new Counter();
    console.log(c.count);  // Should print 0
    c.count = 10;
    console.log(c.count);  // Should print 10
    c.count = -5;  // Should be ignored
    console.log(c.count);  // Should still print 10

    // Test 3: Computed property in getter
    console.log("Test 3 - Computed property:");
    const rect = new Rectangle(5, 3);
    console.log(rect.area);  // Should print 15

    return 0;
}
