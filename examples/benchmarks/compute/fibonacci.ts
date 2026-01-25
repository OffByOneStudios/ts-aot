/**
 * Fibonacci Benchmark
 *
 * Tests recursive function call performance and stack management.
 * Classic exponential-time algorithm to stress call stack.
 *
 * fib(40) = 102334155 (requires ~331 million calls)
 */

import { benchmark, printResult, BenchmarkSuite } from '../harness/benchmark';

/**
 * Naive recursive fibonacci - O(2^n)
 * Deliberately inefficient to stress function calls
 */
function fibRecursive(n: number): number {
    if (n <= 1) return n;
    return fibRecursive(n - 1) + fibRecursive(n - 2);
}

/**
 * Iterative fibonacci - O(n)
 * For comparison
 */
function fibIterative(n: number): number {
    if (n <= 1) return n;

    let prev = 0;
    let curr = 1;

    for (let i = 2; i <= n; i++) {
        const next = prev + curr;
        prev = curr;
        curr = next;
    }

    return curr;
}

/**
 * Memoized fibonacci - O(n)
 * Tests object/map allocation and lookup
 */
function fibMemoized(n: number, memo: Map<number, number> = new Map()): number {
    if (n <= 1) return n;

    const cached = memo.get(n);
    if (cached !== undefined) return cached;

    const result = fibMemoized(n - 1, memo) + fibMemoized(n - 2, memo);
    memo.set(n, result);
    return result;
}

function user_main(): number {
    console.log('Fibonacci Benchmark');
    console.log('==================');
    console.log('');

    // Verify correctness first
    const expected40 = 102334155;
    const verifyResult = fibRecursive(20); // Quick verification
    console.log(`Verification: fib(20) = ${verifyResult}`);
    console.log('');

    const suite = new BenchmarkSuite('Fibonacci');

    // Recursive benchmark - the main stress test
    // Using n=35 for reasonable runtime (~5 seconds)
    suite.add('fib_recursive(35)', () => {
        const result = fibRecursive(35);
        // Prevent optimization from removing the call
        if (result < 0) console.log('unreachable');
    }, { iterations: 3, warmup: 1, minTime: 5000 });

    // Iterative benchmark - baseline comparison
    suite.add('fib_iterative(10000)', () => {
        const result = fibIterative(10000);
        if (result < 0) console.log('unreachable');
    }, { iterations: 1000, warmup: 100 });

    // Memoized benchmark - tests Map operations
    suite.add('fib_memoized(10000)', () => {
        const result = fibMemoized(10000, new Map());
        if (result < 0) console.log('unreachable');
    }, { iterations: 100, warmup: 10 });

    suite.run();
    suite.printSummary();

    return 0;
}
