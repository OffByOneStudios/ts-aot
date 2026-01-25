/**
 * Allocation Benchmark
 *
 * Tests object allocation and garbage collection performance.
 * Measures: object creation, property access, array manipulation.
 */

import { benchmark, BenchmarkSuite, formatBytes, getMemoryUsage } from '../harness/benchmark';

/**
 * Simple object to allocate
 */
interface SimpleObject {
    id: number;
    name: string;
    value: number;
    active: boolean;
}

/**
 * Complex object with nested properties
 */
interface ComplexObject {
    id: number;
    data: {
        name: string;
        values: number[];
        metadata: {
            created: number;
            modified: number;
            tags: string[];
        };
    };
    children: SimpleObject[];
}

/**
 * Create N simple objects
 */
function createSimpleObjects(count: number): SimpleObject[] {
    const objects: SimpleObject[] = [];
    for (let i = 0; i < count; i++) {
        objects.push({
            id: i,
            name: 'object_' + i,
            value: Math.random() * 1000,
            active: i % 2 === 0
        });
    }
    return objects;
}

/**
 * Create N complex objects
 */
function createComplexObjects(count: number): ComplexObject[] {
    const objects: ComplexObject[] = [];
    for (let i = 0; i < count; i++) {
        objects.push({
            id: i,
            data: {
                name: 'complex_' + i,
                values: [i, i * 2, i * 3, i * 4, i * 5],
                metadata: {
                    created: Date.now(),
                    modified: Date.now(),
                    tags: ['tag1', 'tag2', 'tag3']
                }
            },
            children: [
                { id: i * 10, name: 'child1', value: 1, active: true },
                { id: i * 10 + 1, name: 'child2', value: 2, active: false }
            ]
        });
    }
    return objects;
}

/**
 * Allocate and immediately discard objects
 */
function allocateAndDiscard(count: number): void {
    for (let i = 0; i < count; i++) {
        const obj = {
            id: i,
            name: 'temp_' + i,
            data: new Array(10).fill(i)
        };
        // Object becomes garbage after this iteration
        if (obj.id < 0) console.log('unreachable');
    }
}

/**
 * Create linked list nodes
 */
interface ListNode {
    value: number;
    next: ListNode | null;
}

function createLinkedList(length: number): ListNode {
    const head: ListNode = { value: 0, next: null };
    let current = head;

    for (let i = 1; i < length; i++) {
        current.next = { value: i, next: null };
        current = current.next;
    }

    return head;
}

/**
 * Traverse linked list
 */
function traverseLinkedList(head: ListNode): number {
    let count = 0;
    let current: ListNode | null = head;

    while (current !== null) {
        count++;
        current = current.next;
    }

    return count;
}

function user_main(): number {
    console.log('Allocation Benchmark');
    console.log('====================');
    console.log('');

    // Initial memory state
    const initialMemory = getMemoryUsage();
    console.log(`Initial heap: ${formatBytes(initialMemory.heapUsed)}`);
    console.log(`Initial RSS: ${formatBytes(initialMemory.rss)}`);
    console.log('');

    const suite = new BenchmarkSuite('Memory Allocation');

    // Simple object allocation
    suite.add('alloc 10k simple objects', () => {
        const objects = createSimpleObjects(10000);
        if (objects.length < 0) console.log('unreachable');
    }, { iterations: 100, warmup: 10, collectMemory: true });

    suite.add('alloc 100k simple objects', () => {
        const objects = createSimpleObjects(100000);
        if (objects.length < 0) console.log('unreachable');
    }, { iterations: 10, warmup: 2, collectMemory: true });

    // Complex object allocation
    suite.add('alloc 10k complex objects', () => {
        const objects = createComplexObjects(10000);
        if (objects.length < 0) console.log('unreachable');
    }, { iterations: 20, warmup: 5, collectMemory: true });

    // Allocate and discard (GC pressure)
    suite.add('alloc/discard 100k objects', () => {
        allocateAndDiscard(100000);
    }, { iterations: 10, warmup: 2, collectMemory: true });

    suite.add('alloc/discard 1M objects', () => {
        allocateAndDiscard(1000000);
    }, { iterations: 3, warmup: 1, collectMemory: true });

    // Array allocation
    suite.add('alloc 10k arrays (size 100)', () => {
        const arrays: number[][] = [];
        for (let i = 0; i < 10000; i++) {
            arrays.push(new Array(100).fill(i));
        }
        if (arrays.length < 0) console.log('unreachable');
    }, { iterations: 20, warmup: 5, collectMemory: true });

    // String allocation
    suite.add('alloc 10k strings', () => {
        const strings: string[] = [];
        for (let i = 0; i < 10000; i++) {
            strings.push('string_' + i + '_' + Math.random().toString(36).substring(7));
        }
        if (strings.length < 0) console.log('unreachable');
    }, { iterations: 50, warmup: 10, collectMemory: true });

    // Linked list (pointer-heavy structure)
    suite.add('create linked list (10k nodes)', () => {
        const list = createLinkedList(10000);
        if (list.value < 0) console.log('unreachable');
    }, { iterations: 100, warmup: 10, collectMemory: true });

    suite.add('create linked list (100k nodes)', () => {
        const list = createLinkedList(100000);
        if (list.value < 0) console.log('unreachable');
    }, { iterations: 10, warmup: 2, collectMemory: true });

    // Map allocation
    suite.add('alloc Map with 10k entries', () => {
        const map = new Map<number, string>();
        for (let i = 0; i < 10000; i++) {
            map.set(i, 'value_' + i);
        }
        if (map.size < 0) console.log('unreachable');
    }, { iterations: 50, warmup: 10, collectMemory: true });

    suite.run();
    suite.printSummary();

    // Final memory state
    const finalMemory = getMemoryUsage();
    console.log('');
    console.log(`Final heap: ${formatBytes(finalMemory.heapUsed)}`);
    console.log(`Final RSS: ${formatBytes(finalMemory.rss)}`);
    console.log(`Heap delta: ${formatBytes(finalMemory.heapUsed - initialMemory.heapUsed)}`);

    return 0;
}
