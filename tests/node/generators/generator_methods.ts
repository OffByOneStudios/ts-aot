// Test generator methods in classes (ES2015)

class NumberRange {
    start: number;
    end: number;

    constructor(start: number, end: number) {
        this.start = start;
        this.end = end;
    }

    *range() {
        for (let i = this.start; i <= this.end; i++) {
            yield i;
        }
    }

    *reverse() {
        for (let i = this.end; i >= this.start; i--) {
            yield i;
        }
    }
}

class Counter {
    count: number;

    constructor(count: number) {
        this.count = count;
    }

    *countdown() {
        for (let i = this.count; i > 0; i--) {
            yield i;
        }
    }
}

function user_main(): number {
    console.log("=== Generator Method Test ===");

    // Test 1: Basic generator method
    console.log("Test 1: Basic generator method");
    const range = new NumberRange(1, 5);
    const gen = range.range();

    let result = gen.next();
    while (!result.done) {
        console.log(result.value);
        result = gen.next();
    }

    // Test 2: Multiple generator methods on same class
    console.log("Test 2: Reverse generator");
    const reverseGen = range.reverse();
    result = reverseGen.next();
    while (!result.done) {
        console.log(result.value);
        result = reverseGen.next();
    }

    // Test 3: Counter countdown
    console.log("Test 3: Countdown");
    const counter = new Counter(3);
    const countdownGen = counter.countdown();
    let countResult = countdownGen.next();
    while (!countResult.done) {
        console.log(countResult.value);
        countResult = countdownGen.next();
    }

    console.log("=== Tests Complete ===");
    return 0;
}
