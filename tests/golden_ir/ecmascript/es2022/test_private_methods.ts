// Test ES2022 private methods

class Counter {
    #count: number = 0;

    #increment(): void {
        this.#count++;
    }

    #getDouble(): number {
        return this.#count * 2;
    }

    public tick(): void {
        this.#increment();
    }

    public getCount(): number {
        return this.#count;
    }

    public getDoubleCount(): number {
        return this.#getDouble();
    }
}

class Calculator {
    #value: number;

    constructor(initial: number) {
        this.#value = initial;
    }

    #add(n: number): number {
        this.#value += n;
        return this.#value;
    }

    #multiply(n: number): number {
        this.#value *= n;
        return this.#value;
    }

    public compute(a: number, b: number): number {
        this.#add(a);
        return this.#multiply(b);
    }

    public getValue(): number {
        return this.#value;
    }
}

function user_main(): number {
    console.log("Testing ES2022 private methods...");

    // Test 1: Private method calls via public methods
    const counter = new Counter();
    counter.tick();
    counter.tick();
    counter.tick();
    console.log("Count after 3 ticks: " + counter.getCount());
    console.log("Double count: " + counter.getDoubleCount());

    // Test 2: Private methods with parameters
    const calc = new Calculator(5);
    const result = calc.compute(3, 2);  // (5 + 3) * 2 = 16
    console.log("Calculator result: " + result);
    console.log("Calculator value: " + calc.getValue());

    // Test 3: Multiple private method calls
    const counter2 = new Counter();
    for (let i = 0; i < 5; i++) {
        counter2.tick();
    }
    console.log("Count after 5 ticks: " + counter2.getCount());
    console.log("Double of 5: " + counter2.getDoubleCount());

    console.log("All private methods tests passed!");
    return 0;
}
