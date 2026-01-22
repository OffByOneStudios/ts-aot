// Test custom iterables with Symbol.iterator

function user_main(): number {
    console.log("Testing custom iterables...");

    // Test 1: Basic custom iterable using closure
    console.log("\n1. Basic custom iterable:");
    const data1: number[] = [10, 20, 30];
    const customIterable: any = {
        "[Symbol.iterator]": function(): any {
            let index = 0;
            return {
                next: function(): any {
                    if (index < data1.length) {
                        return { value: data1[index++], done: false };
                    }
                    return { value: undefined, done: true };
                }
            };
        }
    };

    let sum = 0;
    for (const val of customIterable) {
        console.log("Value: " + val);
        sum += val;
    }
    console.log("Sum: " + sum);

    // Test 2: String iteration (should still work via fast path)
    console.log("\n2. String iteration:");
    let chars = "";
    for (const c of "ABC") {
        chars += c + " ";
    }
    console.log("Chars: " + chars);

    // Test 3: Array iteration (should still work via fast path)
    console.log("\n3. Array iteration:");
    const arr: number[] = [1, 2, 3];
    let arrSum = 0;
    for (const v of arr) {
        arrSum += v;
    }
    console.log("Array sum: " + arrSum);

    // Test 4: Iterator that returns objects
    console.log("\n4. Object-returning iterator:");
    const people: any[] = [
        { name: "Alice", age: 30 },
        { name: "Bob", age: 25 }
    ];
    const peopleIterable: any = {
        "[Symbol.iterator]": function(): any {
            let i = 0;
            return {
                next: function(): any {
                    if (i < people.length) {
                        return { value: people[i++], done: false };
                    }
                    return { value: undefined, done: true };
                }
            };
        }
    };

    for (const person of peopleIterable) {
        console.log("Person: " + person.name + ", age " + person.age);
    }

    // Test 5: Empty iterator
    console.log("\n5. Empty iterator:");
    const emptyIterable: any = {
        "[Symbol.iterator]": function(): any {
            return {
                next: function(): any {
                    return { value: undefined, done: true };
                }
            };
        }
    };

    let emptyCount = 0;
    for (const _ of emptyIterable) {
        emptyCount++;
    }
    console.log("Empty iterator count: " + emptyCount);

    // Test 6: Counter iterator
    console.log("\n6. Counter iterator (1 to 5):");
    const counterIterable: any = {
        "[Symbol.iterator]": function(): any {
            let count = 0;
            return {
                next: function(): any {
                    count++;
                    if (count <= 5) {
                        return { value: count, done: false };
                    }
                    return { value: undefined, done: true };
                }
            };
        }
    };

    let counterSum = 0;
    for (const n of counterIterable) {
        console.log("Counter: " + n);
        counterSum += n;
    }
    console.log("Counter sum: " + counterSum);

    console.log("\nAll custom iterable tests passed!");
    return 0;
}
