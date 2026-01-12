// Test yield* delegation

// Test 1: yield* with another generator
function* innerGen(): Generator<number> {
    yield 10;
    yield 20;
}

function* outerGen(): Generator<number> {
    yield 1;
    yield* innerGen();
    yield 2;
}

// Test 2: yield* with array
function* yieldFromArray(): Generator<number> {
    yield* [100, 200, 300];
}

function user_main(): number {
    // Test generator delegation
    const gen1 = outerGen();
    console.log(gen1.next().value);  // 1
    console.log(gen1.next().value);  // 10
    console.log(gen1.next().value);  // 20
    console.log(gen1.next().value);  // 2
    console.log(gen1.next().done);   // true

    // Test array delegation
    const gen2 = yieldFromArray();
    console.log(gen2.next().value);  // 100
    console.log(gen2.next().value);  // 200
    console.log(gen2.next().value);  // 300
    console.log(gen2.next().done);   // true

    return 0;
}
