// Test ES6 Generator functions (function*, yield)

// Simple generator
function* countTo(n: number): Generator<number> {
    for (let i = 1; i <= n; i++) {
        yield i;
    }
}

// Generator with return value
function* range(start: number, end: number): Generator<number> {
    for (let i = start; i <= end; i++) {
        yield i;
    }
}

// Generator that yields different values
function* multiYield(): Generator<number> {
    yield 10;
    yield 20;
    yield 30;
}

// Test simple generator
const counter = countTo(3);
console.log(counter.next().value);  // 1
console.log(counter.next().value);  // 2
console.log(counter.next().value);  // 3
console.log(counter.next().done);   // true

// Test range generator
const rangeGen = range(5, 7);
console.log(rangeGen.next().value);  // 5
console.log(rangeGen.next().value);  // 6
console.log(rangeGen.next().value);  // 7
console.log(rangeGen.next().done);   // true

// Test multi-yield
const multi = multiYield();
console.log(multi.next().value);  // 10
console.log(multi.next().value);  // 20
console.log(multi.next().value);  // 30

function user_main(): number {
    return 0;
}
