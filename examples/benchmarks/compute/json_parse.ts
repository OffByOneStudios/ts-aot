/**
 * JSON Parse/Stringify Benchmark
 *
 * Tests JSON parsing and serialization performance.
 * Measures: string processing, object creation, property access.
 */

import { benchmark, BenchmarkSuite } from '../harness/benchmark';

/**
 * Generate a nested object structure
 */
function generateNestedObject(depth: number, breadth: number): any {
    if (depth <= 0) {
        return {
            id: Math.floor(Math.random() * 1000000),
            name: 'item_' + Math.floor(Math.random() * 1000),
            value: Math.random() * 1000,
            active: Math.random() > 0.5,
            tags: ['tag1', 'tag2', 'tag3']
        };
    }

    const obj: any = {};
    for (let i = 0; i < breadth; i++) {
        obj['child_' + i] = generateNestedObject(depth - 1, breadth);
    }
    return obj;
}

/**
 * Generate an array of flat objects
 */
function generateObjectArray(count: number): any[] {
    const arr: any[] = [];
    for (let i = 0; i < count; i++) {
        arr.push({
            id: i,
            name: 'User ' + i,
            email: 'user' + i + '@example.com',
            age: 20 + (i % 50),
            active: i % 2 === 0,
            created: '2024-01-' + ((i % 28) + 1).toString().padStart(2, '0'),
            scores: [Math.random() * 100, Math.random() * 100, Math.random() * 100],
            metadata: {
                lastLogin: '2024-12-15',
                loginCount: i * 10,
                preferences: {
                    theme: i % 2 === 0 ? 'dark' : 'light',
                    language: 'en'
                }
            }
        });
    }
    return arr;
}

/**
 * Deep clone via JSON round-trip
 */
function jsonClone<T>(obj: T): T {
    return JSON.parse(JSON.stringify(obj));
}

function user_main(): number {
    console.log('JSON Parse/Stringify Benchmark');
    console.log('==============================');
    console.log('');

    // Generate test data
    console.log('Generating test data...');
    const smallArray = generateObjectArray(100);
    const mediumArray = generateObjectArray(1000);
    const largeArray = generateObjectArray(10000);
    const nestedObject = generateNestedObject(4, 5); // ~780 leaf objects

    const smallJson = JSON.stringify(smallArray);
    const mediumJson = JSON.stringify(mediumArray);
    const largeJson = JSON.stringify(largeArray);
    const nestedJson = JSON.stringify(nestedObject);

    console.log(`Small array JSON: ${(smallJson.length / 1024).toFixed(1)} KB`);
    console.log(`Medium array JSON: ${(mediumJson.length / 1024).toFixed(1)} KB`);
    console.log(`Large array JSON: ${(largeJson.length / 1024).toFixed(1)} KB`);
    console.log(`Nested object JSON: ${(nestedJson.length / 1024).toFixed(1)} KB`);
    console.log('');

    // Verify round-trip
    const parsed = JSON.parse(mediumJson);
    const reparsed = JSON.stringify(parsed);
    console.log(`Round-trip verification: ${mediumJson === reparsed ? 'PASS' : 'FAIL'}`);
    console.log('');

    const suite = new BenchmarkSuite('JSON Operations');

    // Parse benchmarks
    suite.add('JSON.parse (small ~25KB)', () => {
        const result = JSON.parse(smallJson);
        if (!result) console.log('unreachable');
    }, { iterations: 1000, warmup: 100 });

    suite.add('JSON.parse (medium ~250KB)', () => {
        const result = JSON.parse(mediumJson);
        if (!result) console.log('unreachable');
    }, { iterations: 100, warmup: 10 });

    suite.add('JSON.parse (large ~2.5MB)', () => {
        const result = JSON.parse(largeJson);
        if (!result) console.log('unreachable');
    }, { iterations: 10, warmup: 2 });

    // Stringify benchmarks
    suite.add('JSON.stringify (small)', () => {
        const result = JSON.stringify(smallArray);
        if (!result) console.log('unreachable');
    }, { iterations: 1000, warmup: 100 });

    suite.add('JSON.stringify (medium)', () => {
        const result = JSON.stringify(mediumArray);
        if (!result) console.log('unreachable');
    }, { iterations: 100, warmup: 10 });

    suite.add('JSON.stringify (large)', () => {
        const result = JSON.stringify(largeArray);
        if (!result) console.log('unreachable');
    }, { iterations: 10, warmup: 2 });

    // Nested object benchmarks
    suite.add('JSON.parse (nested)', () => {
        const result = JSON.parse(nestedJson);
        if (!result) console.log('unreachable');
    }, { iterations: 100, warmup: 10 });

    suite.add('JSON.stringify (nested)', () => {
        const result = JSON.stringify(nestedObject);
        if (!result) console.log('unreachable');
    }, { iterations: 100, warmup: 10 });

    // Round-trip (clone)
    suite.add('JSON clone (medium)', () => {
        const result = jsonClone(mediumArray);
        if (!result) console.log('unreachable');
    }, { iterations: 50, warmup: 5 });

    suite.run();
    suite.printSummary();

    return 0;
}
