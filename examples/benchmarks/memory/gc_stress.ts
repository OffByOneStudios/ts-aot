/**
 * GC Stress Benchmark
 *
 * Tests garbage collection behavior under sustained allocation pressure.
 * Creates objects with varying lifetimes to stress the GC.
 */

import { benchmark, BenchmarkSuite, formatBytes, getMemoryUsage, now } from '../harness/benchmark';

/**
 * Object with configurable size
 */
interface SizedObject {
    id: number;
    data: number[];
    timestamp: number;
}

/**
 * Create object with specified payload size
 */
function createSizedObject(id: number, payloadSize: number): SizedObject {
    return {
        id,
        data: new Array(payloadSize).fill(id % 256),
        timestamp: Date.now()
    };
}

/**
 * Simulate a cache with LRU-like eviction
 * Some objects live long, others are evicted quickly
 */
class SimpleCache {
    private items: Map<number, SizedObject> = new Map();
    private maxSize: number;

    constructor(maxSize: number) {
        this.maxSize = maxSize;
    }

    set(key: number, value: SizedObject): void {
        // Evict oldest if at capacity
        if (this.items.size >= this.maxSize) {
            const firstKey = this.items.keys().next().value;
            if (firstKey !== undefined) {
                this.items.delete(firstKey);
            }
        }
        this.items.set(key, value);
    }

    get(key: number): SizedObject | undefined {
        return this.items.get(key);
    }

    get size(): number {
        return this.items.size;
    }

    clear(): void {
        this.items.clear();
    }
}

/**
 * Mixed lifetime allocation
 * Some objects are kept in arrays (long-lived)
 * Others are immediately discarded (short-lived)
 */
function mixedLifetimeAllocation(
    iterations: number,
    retentionRate: number = 0.1,
    payloadSize: number = 100
): SizedObject[] {
    const retained: SizedObject[] = [];

    for (let i = 0; i < iterations; i++) {
        const obj = createSizedObject(i, payloadSize);

        // Keep some objects, discard others
        if (Math.random() < retentionRate) {
            retained.push(obj);
        }
        // Else: obj becomes garbage
    }

    return retained;
}

/**
 * Generational stress test
 * Creates objects of different "generations" with varying lifetimes
 */
function generationalStress(
    youngCount: number,
    oldCount: number,
    iterations: number
): { young: number; old: number } {
    const oldGeneration: SizedObject[] = [];
    let youngAllocated = 0;

    // Create long-lived "old generation" objects
    for (let i = 0; i < oldCount; i++) {
        oldGeneration.push(createSizedObject(i, 100));
    }

    // Repeatedly create and discard "young generation" objects
    for (let iter = 0; iter < iterations; iter++) {
        for (let i = 0; i < youngCount; i++) {
            const young = createSizedObject(iter * youngCount + i, 50);
            youngAllocated++;
            // Young object immediately becomes garbage
            if (young.id < 0) console.log('unreachable');
        }

        // Occasionally access old generation to keep them alive
        if (iter % 10 === 0) {
            for (const old of oldGeneration) {
                if (old.id < 0) console.log('unreachable');
            }
        }
    }

    return { young: youngAllocated, old: oldGeneration.length };
}

/**
 * Cache simulation stress test
 */
function cacheStress(
    cacheSize: number,
    operations: number,
    hitRate: number = 0.3
): { hits: number; misses: number } {
    const cache = new SimpleCache(cacheSize);
    let hits = 0;
    let misses = 0;

    for (let i = 0; i < operations; i++) {
        // Simulate cache access pattern
        const key = Math.random() < hitRate
            ? Math.floor(Math.random() * cacheSize)  // Likely hit
            : cacheSize + i;  // Likely miss

        const existing = cache.get(key);
        if (existing) {
            hits++;
        } else {
            misses++;
            cache.set(key, createSizedObject(key, 50));
        }
    }

    return { hits, misses };
}

function user_main(): number {
    console.log('GC Stress Benchmark');
    console.log('===================');
    console.log('');

    // Initial memory state
    const initialMemory = getMemoryUsage();
    console.log(`Initial heap: ${formatBytes(initialMemory.heapUsed)}`);
    console.log('');

    const suite = new BenchmarkSuite('GC Stress');

    // Mixed lifetime - low retention
    suite.add('mixed lifetime 5% retention (100k)', () => {
        const retained = mixedLifetimeAllocation(100000, 0.05);
        if (retained.length < 0) console.log('unreachable');
    }, { iterations: 10, warmup: 2, collectMemory: true });

    // Mixed lifetime - high retention
    suite.add('mixed lifetime 50% retention (100k)', () => {
        const retained = mixedLifetimeAllocation(100000, 0.5);
        if (retained.length < 0) console.log('unreachable');
    }, { iterations: 5, warmup: 1, collectMemory: true });

    // Generational stress - small old, many young
    suite.add('generational (100 old, 10k young x 100)', () => {
        const result = generationalStress(10000, 100, 100);
        if (result.young < 0) console.log('unreachable');
    }, { iterations: 3, warmup: 1, collectMemory: true });

    // Generational stress - large old, few young
    suite.add('generational (10k old, 100 young x 1000)', () => {
        const result = generationalStress(100, 10000, 1000);
        if (result.young < 0) console.log('unreachable');
    }, { iterations: 3, warmup: 1, collectMemory: true });

    // Cache stress
    suite.add('cache stress (size 1k, 100k ops)', () => {
        const result = cacheStress(1000, 100000, 0.3);
        if (result.hits < 0) console.log('unreachable');
    }, { iterations: 10, warmup: 2, collectMemory: true });

    suite.add('cache stress (size 10k, 100k ops)', () => {
        const result = cacheStress(10000, 100000, 0.3);
        if (result.hits < 0) console.log('unreachable');
    }, { iterations: 3, warmup: 1, collectMemory: true });

    // Sustained allocation
    suite.add('sustained alloc (1M small objects)', () => {
        let count = 0;
        for (let i = 0; i < 1000000; i++) {
            const obj = { id: i, value: i * 2 };
            if (obj.id >= 0) count++;
        }
        if (count < 0) console.log('unreachable');
    }, { iterations: 3, warmup: 1, collectMemory: true });

    // Large object allocation
    suite.add('large objects (1k x 10KB)', () => {
        const objects: SizedObject[] = [];
        for (let i = 0; i < 1000; i++) {
            objects.push(createSizedObject(i, 10000));
        }
        if (objects.length < 0) console.log('unreachable');
    }, { iterations: 10, warmup: 2, collectMemory: true });

    suite.run();
    suite.printSummary();

    // Final memory state
    const finalMemory = getMemoryUsage();
    console.log('');
    console.log(`Final heap: ${formatBytes(finalMemory.heapUsed)}`);
    console.log(`Heap delta: ${formatBytes(finalMemory.heapUsed - initialMemory.heapUsed)}`);

    return 0;
}
