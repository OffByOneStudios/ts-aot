/**
 * Fair GC Comparison Benchmark
 *
 * Workloads designed for apples-to-apples comparison between ts-aot's custom GC
 * and V8's garbage collector. Each workload exercises a specific GC behavior pattern.
 *
 * Design principles:
 * - Same TypeScript source runs on both ts-aot and Node.js
 * - Forced GC at consistent points (not relying on heuristic triggers)
 * - Measures wall-clock time, peak memory, and GC overhead
 * - Prevents dead code elimination with observable side effects
 * - Workloads sized to guarantee multiple GC cycles
 *
 * Output format: JSON lines, one per workload result, for machine parsing.
 */

// ============================================================================
// Measurement helpers (work in both ts-aot and Node.js)
// ============================================================================

function getHeapUsed(): number {
    return process.memoryUsage().heapUsed;
}

function getRSS(): number {
    return process.memoryUsage().rss;
}

function timeMs(): number {
    return performance.now();
}

// Force a GC if available. In Node.js, requires --expose-gc.
// In ts-aot, gc() is always available.
function forceGC(): void {
    if (typeof gc === 'function') {
        gc();
    }
}

interface WorkloadResult {
    name: string;
    wallTimeMs: number;
    heapBefore: number;
    heapAfter: number;
    peakHeap: number;
    rssAfter: number;
    itemsProcessed: number;
    checksum: number;  // Prevent dead code elimination
}

function reportResult(r: WorkloadResult): void {
    // Print as structured output for machine parsing
    console.log(`RESULT:${r.name}|wall=${r.wallTimeMs.toFixed(2)}|heapBefore=${r.heapBefore}|heapAfter=${r.heapAfter}|peakHeap=${r.peakHeap}|rss=${r.rssAfter}|items=${r.itemsProcessed}|checksum=${r.checksum}`);
}

// ============================================================================
// Workload 1: Short-lived allocation throughput
//
// Creates objects that become garbage immediately. Measures raw allocation
// speed and how efficiently the GC reclaims short-lived objects.
// This is the bread-and-butter of generational GC.
// ============================================================================

function workloadShortLived(): WorkloadResult {
    const name = "short_lived_alloc";
    const N = 500000;  // 500k objects per round, 10 rounds = 5M total

    forceGC();
    const heapBefore = getHeapUsed();
    let peakHeap = heapBefore;
    let checksum: number = 0;

    const start = timeMs();

    for (let round = 0; round < 10; round++) {
        for (let i = 0; i < N; i++) {
            const obj = { x: i, y: i * 2, z: i + round };
            checksum = checksum + obj.z;
        }
        // Force GC after each round to measure consistent collection behavior
        forceGC();
        const h = getHeapUsed();
        if (h > peakHeap) peakHeap = h;
    }

    const wallTimeMs = timeMs() - start;
    const heapAfter = getHeapUsed();

    return {
        name: name,
        wallTimeMs: wallTimeMs,
        heapBefore: heapBefore,
        heapAfter: heapAfter,
        peakHeap: peakHeap,
        rssAfter: getRSS(),
        itemsProcessed: N * 10,
        checksum: checksum
    };
}

// ============================================================================
// Workload 2: Long-lived object graph
//
// Builds a large persistent data structure, then modifies parts of it.
// Tests how the GC handles old-generation objects and cross-generation
// references (old objects pointing to new objects).
// ============================================================================

interface TreeNode {
    value: number;
    left: TreeNode | null;
    right: TreeNode | null;
    data: string;
}

function buildTree(depth: number, base: number): TreeNode {
    if (depth === 0) {
        return { value: base, left: null, right: null, data: "leaf_" + base };
    }
    return {
        value: base,
        left: buildTree(depth - 1, base * 2),
        right: buildTree(depth - 1, base * 2 + 1),
        data: "node_" + base
    };
}

function treeChecksum(node: TreeNode | null): number {
    if (node === null) return 0;
    return node.value + treeChecksum(node.left) + treeChecksum(node.right);
}

function replaceSubtree(node: TreeNode, depth: number, newBase: number): void {
    if (depth === 0 || node.left === null) return;
    // Replace the left subtree with a new one (creates garbage + new objects)
    node.left = buildTree(depth - 1, newBase);
}

function workloadLongLived(): WorkloadResult {
    const name = "long_lived_graph";
    const TREE_DEPTH = 16;  // 2^16 - 1 = 65535 nodes
    const MUTATIONS = 1000; // Replace subtrees this many times

    forceGC();
    const heapBefore = getHeapUsed();
    let peakHeap = heapBefore;

    const start = timeMs();

    // Phase 1: Build the tree (promoted to old gen)
    const root = buildTree(TREE_DEPTH, 1);
    forceGC();  // Ensure tree is in old generation
    let h = getHeapUsed();
    if (h > peakHeap) peakHeap = h;

    // Phase 2: Mutate subtrees (creates cross-gen references)
    let checksum: number = 0;
    for (let i = 0; i < MUTATIONS; i++) {
        // Walk to a random-ish interior node and replace its subtree
        let node = root;
        const bits = i % 8;  // Navigate left/right based on bits
        for (let b = 0; b < 4; b++) {
            if ((bits >> b) & 1) {
                if (node.right !== null) node = node.right;
            } else {
                if (node.left !== null) node = node.left;
            }
        }
        // Replace subtree at depth 6 (64 new nodes each time)
        replaceSubtree(node, 6, i * 100);

        if (i % 100 === 0) {
            forceGC();
            h = getHeapUsed();
            if (h > peakHeap) peakHeap = h;
        }
    }

    checksum = treeChecksum(root);

    const wallTimeMs = timeMs() - start;
    forceGC();
    const heapAfter = getHeapUsed();

    return {
        name: name,
        wallTimeMs: wallTimeMs,
        heapBefore: heapBefore,
        heapAfter: heapAfter,
        peakHeap: peakHeap,
        rssAfter: getRSS(),
        itemsProcessed: 65535 + MUTATIONS * 63,
        checksum: checksum
    };
}

// ============================================================================
// Workload 3: Mixed lifetime allocation (realistic pattern)
//
// Simulates a request processing pipeline where some objects are kept
// (accumulated results) and most are temporary (per-request processing).
// This is the most realistic GC workload pattern.
// ============================================================================

function workloadMixedLifetime(): WorkloadResult {
    const name = "mixed_lifetime";
    const REQUESTS = 10000;
    const ITEMS_PER_REQUEST = 50;

    forceGC();
    const heapBefore = getHeapUsed();
    let peakHeap = heapBefore;

    const start = timeMs();

    // Long-lived accumulator (survives entire benchmark)
    const results: Array<{ id: number; summary: string; total: number }> = [];
    let checksum: number = 0;

    for (let req = 0; req < REQUESTS; req++) {
        // Short-lived: per-request working set
        const items: Array<{ key: string; value: number; tags: string[] }> = [];
        for (let i = 0; i < ITEMS_PER_REQUEST; i++) {
            items.push({
                key: "item_" + req + "_" + i,
                value: req * ITEMS_PER_REQUEST + i,
                tags: ["tag_a", "tag_b"]
            });
        }

        // Process items (all temporary)
        let total: number = 0;
        for (let i = 0; i < items.length; i++) {
            total = total + items[i].value;
        }

        // Keep 5% of results (long-lived)
        if (req % 20 === 0) {
            results.push({
                id: req,
                summary: "processed_" + req,
                total: total
            });
        }

        checksum = checksum + total;

        // Periodic GC to measure steady-state behavior
        if (req % 500 === 0) {
            forceGC();
            const h = getHeapUsed();
            if (h > peakHeap) peakHeap = h;
        }
    }

    // Ensure results aren't DCE'd
    checksum = checksum + results.length;

    const wallTimeMs = timeMs() - start;
    forceGC();
    const heapAfter = getHeapUsed();

    return {
        name: name,
        wallTimeMs: wallTimeMs,
        heapBefore: heapBefore,
        heapAfter: heapAfter,
        peakHeap: peakHeap,
        rssAfter: getRSS(),
        itemsProcessed: REQUESTS * ITEMS_PER_REQUEST,
        checksum: checksum
    };
}

// ============================================================================
// Workload 4: Map-heavy workload
//
// Tests hash map allocation and GC interaction. Maps are common in JS
// and exercise the write barrier (old-gen map → new keys/values).
// ============================================================================

function workloadMapHeavy(): WorkloadResult {
    const name = "map_heavy";
    const ITERATIONS = 100;
    const MAP_SIZE = 5000;

    forceGC();
    const heapBefore = getHeapUsed();
    let peakHeap = heapBefore;

    const start = timeMs();

    let checksum: number = 0;

    for (let iter = 0; iter < ITERATIONS; iter++) {
        // Create a Map, populate it, query it, then discard
        const m = new Map<string, number>();
        for (let i = 0; i < MAP_SIZE; i++) {
            m.set("key_" + iter + "_" + i, i * iter);
        }

        // Read back values
        for (let i = 0; i < MAP_SIZE; i++) {
            const v = m.get("key_" + iter + "_" + i);
            if (v !== undefined) {
                checksum = checksum + v;
            }
        }

        // Delete half the entries
        for (let i = 0; i < MAP_SIZE; i += 2) {
            m.delete("key_" + iter + "_" + i);
        }

        checksum = checksum + m.size;

        if (iter % 10 === 0) {
            forceGC();
            const h = getHeapUsed();
            if (h > peakHeap) peakHeap = h;
        }
    }

    const wallTimeMs = timeMs() - start;
    forceGC();
    const heapAfter = getHeapUsed();

    return {
        name: name,
        wallTimeMs: wallTimeMs,
        heapBefore: heapBefore,
        heapAfter: heapAfter,
        peakHeap: peakHeap,
        rssAfter: getRSS(),
        itemsProcessed: ITERATIONS * MAP_SIZE * 3,
        checksum: checksum
    };
}

// ============================================================================
// Workload 5: Array churn
//
// Repeatedly creates, grows, shrinks, and replaces arrays.
// Tests array buffer reallocation and GC's handling of backing stores.
// ============================================================================

function workloadArrayChurn(): WorkloadResult {
    const name = "array_churn";
    const ROUNDS = 200;
    const ARRAY_SIZE = 10000;

    forceGC();
    const heapBefore = getHeapUsed();
    let peakHeap = heapBefore;

    const start = timeMs();

    let checksum: number = 0;

    // Keep a pool of arrays alive (old-gen)
    const pool: number[][] = [];
    for (let i = 0; i < 20; i++) {
        pool.push([]);
    }

    for (let round = 0; round < ROUNDS; round++) {
        // Create a fresh array, fill it
        const arr: number[] = [];
        for (let i = 0; i < ARRAY_SIZE; i++) {
            arr.push(round * ARRAY_SIZE + i);
        }

        // Replace one pool entry (old array becomes garbage)
        pool[round % pool.length] = arr;

        // Reduce: touch all elements
        let sum: number = 0;
        for (let i = 0; i < arr.length; i++) {
            sum = sum + arr[i];
        }
        checksum = checksum + sum;

        if (round % 20 === 0) {
            forceGC();
            const h = getHeapUsed();
            if (h > peakHeap) peakHeap = h;
        }
    }

    // Touch pool to prevent DCE
    for (let i = 0; i < pool.length; i++) {
        checksum = checksum + pool[i].length;
    }

    const wallTimeMs = timeMs() - start;
    forceGC();
    const heapAfter = getHeapUsed();

    return {
        name: name,
        wallTimeMs: wallTimeMs,
        heapBefore: heapBefore,
        heapAfter: heapAfter,
        peakHeap: peakHeap,
        rssAfter: getRSS(),
        itemsProcessed: ROUNDS * ARRAY_SIZE,
        checksum: checksum
    };
}

// ============================================================================
// Workload 6: Linked list with pointer chasing
//
// Creates and traverses linked lists. This is the worst case for cache
// locality and stresses the GC's ability to trace pointer-heavy structures.
// ============================================================================

interface LLNode {
    value: number;
    next: LLNode | null;
}

function workloadLinkedList(): WorkloadResult {
    const name = "linked_list";
    const LIST_SIZE = 50000;
    const ROUNDS = 20;

    forceGC();
    const heapBefore = getHeapUsed();
    let peakHeap = heapBefore;

    const start = timeMs();

    let checksum: number = 0;

    for (let round = 0; round < ROUNDS; round++) {
        // Build a linked list
        let head: LLNode | null = null;
        for (let i = 0; i < LIST_SIZE; i++) {
            head = { value: round * LIST_SIZE + i, next: head };
        }

        // Traverse and sum
        let sum: number = 0;
        let node: LLNode | null = head;
        while (node !== null) {
            sum = sum + node.value;
            node = node.next;
        }
        checksum = checksum + sum;

        // List becomes garbage after this iteration
        forceGC();
        const h = getHeapUsed();
        if (h > peakHeap) peakHeap = h;
    }

    const wallTimeMs = timeMs() - start;
    forceGC();
    const heapAfter = getHeapUsed();

    return {
        name: name,
        wallTimeMs: wallTimeMs,
        heapBefore: heapBefore,
        heapAfter: heapAfter,
        peakHeap: peakHeap,
        rssAfter: getRSS(),
        itemsProcessed: ROUNDS * LIST_SIZE,
        checksum: checksum
    };
}

// ============================================================================
// Main: Run all workloads sequentially
// ============================================================================

function runAndReport(result: WorkloadResult): void {
    reportResult(result);
}

function user_main(): number {
    console.log("GC_COMPARE_START");

    // Print runtime info - ts-aot doesn't have process.versions.node
    console.log("RUNTIME:ts-aot");

    // Initial state
    forceGC();
    console.log("HEAP_INITIAL:" + getHeapUsed());
    console.log("RSS_INITIAL:" + getRSS());

    // Run workloads directly (function arrays don't work reliably in ts-aot)
    forceGC();
    runAndReport(workloadShortLived());

    forceGC();
    runAndReport(workloadLongLived());

    forceGC();
    runAndReport(workloadMixedLifetime());

    forceGC();
    runAndReport(workloadMapHeavy());

    forceGC();
    runAndReport(workloadArrayChurn());

    forceGC();
    runAndReport(workloadLinkedList());

    // Final state
    forceGC();
    console.log("HEAP_FINAL:" + getHeapUsed());
    console.log("RSS_FINAL:" + getRSS());
    console.log("GC_COMPARE_END");

    return 0;
}
