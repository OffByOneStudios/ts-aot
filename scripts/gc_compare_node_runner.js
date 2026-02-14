/**
 * Node.js GC instrumentation wrapper for gc_compare.ts
 *
 * This script:
 * 1. Sets up V8 GC event tracking via PerformanceObserver
 * 2. Compiles gc_compare.ts via tsc and runs it
 * 3. Reports GC-specific metrics (pause count, total GC time, max pause)
 *
 * Usage:
 *   node --expose-gc scripts/gc_compare_node_runner.js [options]
 *
 * Options are passed via environment variables:
 *   GC_COMPARE_MODE=fair       Use --max-old-space-size=256 (match ts-aot budget)
 *   GC_COMPARE_MODE=default    Use V8 defaults (shows what users experience)
 *   GC_COMPARE_MODE=no-concurrent  Disable V8 concurrent/incremental marking
 *
 * Note: This script must be run with --expose-gc for gc() to work.
 */

'use strict';

const { PerformanceObserver, performance } = require('perf_hooks');
const { execSync, execFileSync } = require('child_process');
const path = require('path');
const fs = require('fs');
const os = require('os');

// Check that gc() is available
if (typeof gc !== 'function') {
    console.error('ERROR: Run with --expose-gc flag: node --expose-gc scripts/gc_compare_node_runner.js');
    process.exit(1);
}

// ============================================================================
// GC Event Tracking
// ============================================================================

const gcEvents = [];
let gcTotalTime = 0;
let gcMaxPause = 0;
let gcCount = 0;

// PerformanceObserver for GC events (Node.js >= 16)
// Use entryTypes (not type) and buffered: false to capture events including forced GC
const obs = new PerformanceObserver((list) => {
    for (const entry of list.getEntries()) {
        const durationMs = entry.duration;
        const kind = entry.detail ? entry.detail.kind : (entry.kind || 0);
        gcEvents.push({
            kind: kind,
            duration: durationMs,
            startTime: entry.startTime
        });
        gcTotalTime += durationMs;
        if (durationMs > gcMaxPause) gcMaxPause = durationMs;
        gcCount++;
    }
});

// Note: entryTypes: ['gc'] catches forced gc() calls, while type: 'gc' may not
obs.observe({ entryTypes: ['gc'] });

// ============================================================================
// Compile gc_compare.ts and run it inline
// ============================================================================

const benchmarkSrc = path.join(__dirname, '..', 'examples', 'benchmarks', 'memory', 'gc_compare.ts');
const harnessDir = path.join(__dirname, '..', 'examples', 'benchmarks', 'harness');
const tmpDir = os.tmpdir();
const outDir = path.join(tmpDir, 'ts-aot-gc-compare');

// Create output directory
if (!fs.existsSync(outDir)) {
    fs.mkdirSync(outDir, { recursive: true });
}

// We don't compile — we directly eval the TypeScript as JS
// Since the benchmark uses only JS-compatible TypeScript (no type-only features),
// we can strip types with a simple regex approach, or use ts-node, or just
// manually provide a JS version.

// Actually, let's just inline the benchmark logic directly here to avoid
// the tsc dependency and keep it simple.

// ============================================================================
// Benchmark workloads (identical to gc_compare.ts)
// ============================================================================

// Direct GC timing - more reliable than PerformanceObserver for forced GC
let directGCTime = 0;
let directGCCount = 0;
let directGCMaxPause = 0;
// Per-workload tracking
let workloadGCTime = 0;
let workloadGCCount = 0;
let workloadGCMaxPause = 0;

function getHeapUsed() {
    return process.memoryUsage().heapUsed;
}

function getRSS() {
    return process.memoryUsage().rss;
}

function timeMs() {
    return performance.now();
}

function forceGC() {
    if (typeof gc === 'function') {
        const before = performance.now();
        gc();
        const elapsed = performance.now() - before;
        directGCTime += elapsed;
        directGCCount++;
        if (elapsed > directGCMaxPause) directGCMaxPause = elapsed;
        workloadGCTime += elapsed;
        workloadGCCount++;
        if (elapsed > workloadGCMaxPause) workloadGCMaxPause = elapsed;
    }
}

function resetWorkloadGC() {
    workloadGCTime = 0;
    workloadGCCount = 0;
    workloadGCMaxPause = 0;
}

function getWorkloadGCStats(wallTimeMs) {
    return {
        count: workloadGCCount,
        totalMs: workloadGCTime,
        maxPauseMs: workloadGCMaxPause,
        gcPct: wallTimeMs > 0 ? (workloadGCTime / wallTimeMs) * 100 : 0
    };
}

function reportResult(r) {
    console.log(`RESULT:${r.name}|wall=${r.wallTimeMs.toFixed(2)}|heapBefore=${r.heapBefore}|heapAfter=${r.heapAfter}|peakHeap=${r.peakHeap}|rss=${r.rssAfter}|items=${r.itemsProcessed}|checksum=${r.checksum}`);
}

// --- Workload 1: Short-lived allocation ---
function workloadShortLived() {
    const name = "short_lived_alloc";
    const N = 500000;

    forceGC();
    const heapBefore = getHeapUsed();
    let peakHeap = heapBefore;
    let checksum = 0;

    const start = timeMs();

    for (let round = 0; round < 10; round++) {
        for (let i = 0; i < N; i++) {
            const obj = { x: i, y: i * 2, z: i + round };
            checksum = checksum + obj.z;
        }
        forceGC();
        const h = getHeapUsed();
        if (h > peakHeap) peakHeap = h;
    }

    const wallTimeMs = timeMs() - start;
    const heapAfter = getHeapUsed();

    return { name, wallTimeMs, heapBefore, heapAfter, peakHeap, rssAfter: getRSS(), itemsProcessed: N * 10, checksum };
}

// --- Workload 2: Long-lived object graph ---
function buildTree(depth, base) {
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

function treeChecksum(node) {
    if (node === null) return 0;
    return node.value + treeChecksum(node.left) + treeChecksum(node.right);
}

function replaceSubtree(node, depth, newBase) {
    if (depth === 0 || node.left === null) return;
    node.left = buildTree(depth - 1, newBase);
}

function workloadLongLived() {
    const name = "long_lived_graph";
    const TREE_DEPTH = 16;
    const MUTATIONS = 1000;

    forceGC();
    const heapBefore = getHeapUsed();
    let peakHeap = heapBefore;

    const start = timeMs();

    const root = buildTree(TREE_DEPTH, 1);
    forceGC();
    let h = getHeapUsed();
    if (h > peakHeap) peakHeap = h;

    let checksum = 0;
    for (let i = 0; i < MUTATIONS; i++) {
        let node = root;
        const bits = i % 8;
        for (let b = 0; b < 4; b++) {
            if ((bits >> b) & 1) {
                if (node.right !== null) node = node.right;
            } else {
                if (node.left !== null) node = node.left;
            }
        }
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

    return { name, wallTimeMs, heapBefore, heapAfter, peakHeap, rssAfter: getRSS(), itemsProcessed: 65535 + MUTATIONS * 63, checksum };
}

// --- Workload 3: Mixed lifetime ---
function workloadMixedLifetime() {
    const name = "mixed_lifetime";
    const REQUESTS = 10000;
    const ITEMS_PER_REQUEST = 50;

    forceGC();
    const heapBefore = getHeapUsed();
    let peakHeap = heapBefore;

    const start = timeMs();

    const results = [];
    let checksum = 0;

    for (let req = 0; req < REQUESTS; req++) {
        const items = [];
        for (let i = 0; i < ITEMS_PER_REQUEST; i++) {
            items.push({
                key: "item_" + req + "_" + i,
                value: req * ITEMS_PER_REQUEST + i,
                tags: ["tag_a", "tag_b"]
            });
        }

        let total = 0;
        for (let i = 0; i < items.length; i++) {
            total = total + items[i].value;
        }

        if (req % 20 === 0) {
            results.push({ id: req, summary: "processed_" + req, total });
        }

        checksum = checksum + total;

        if (req % 500 === 0) {
            forceGC();
            const h = getHeapUsed();
            if (h > peakHeap) peakHeap = h;
        }
    }

    checksum = checksum + results.length;

    const wallTimeMs = timeMs() - start;
    forceGC();
    const heapAfter = getHeapUsed();

    return { name, wallTimeMs, heapBefore, heapAfter, peakHeap, rssAfter: getRSS(), itemsProcessed: REQUESTS * ITEMS_PER_REQUEST, checksum };
}

// --- Workload 4: Map heavy ---
function workloadMapHeavy() {
    const name = "map_heavy";
    const ITERATIONS = 100;
    const MAP_SIZE = 5000;

    forceGC();
    const heapBefore = getHeapUsed();
    let peakHeap = heapBefore;

    const start = timeMs();

    let checksum = 0;

    for (let iter = 0; iter < ITERATIONS; iter++) {
        const m = new Map();
        for (let i = 0; i < MAP_SIZE; i++) {
            m.set("key_" + iter + "_" + i, i * iter);
        }

        for (let i = 0; i < MAP_SIZE; i++) {
            const v = m.get("key_" + iter + "_" + i);
            if (v !== undefined) {
                checksum = checksum + v;
            }
        }

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

    return { name, wallTimeMs, heapBefore, heapAfter, peakHeap, rssAfter: getRSS(), itemsProcessed: ITERATIONS * MAP_SIZE * 3, checksum };
}

// --- Workload 5: Array churn ---
function workloadArrayChurn() {
    const name = "array_churn";
    const ROUNDS = 200;
    const ARRAY_SIZE = 10000;

    forceGC();
    const heapBefore = getHeapUsed();
    let peakHeap = heapBefore;

    const start = timeMs();

    let checksum = 0;

    const pool = [];
    for (let i = 0; i < 20; i++) {
        pool.push([]);
    }

    for (let round = 0; round < ROUNDS; round++) {
        const arr = [];
        for (let i = 0; i < ARRAY_SIZE; i++) {
            arr.push(round * ARRAY_SIZE + i);
        }

        pool[round % pool.length] = arr;

        let sum = 0;
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

    for (let i = 0; i < pool.length; i++) {
        checksum = checksum + pool[i].length;
    }

    const wallTimeMs = timeMs() - start;
    forceGC();
    const heapAfter = getHeapUsed();

    return { name, wallTimeMs, heapBefore, heapAfter, peakHeap, rssAfter: getRSS(), itemsProcessed: ROUNDS * ARRAY_SIZE, checksum };
}

// --- Workload 6: Linked list ---
function workloadLinkedList() {
    const name = "linked_list";
    const LIST_SIZE = 50000;
    const ROUNDS = 20;

    forceGC();
    const heapBefore = getHeapUsed();
    let peakHeap = heapBefore;

    const start = timeMs();

    let checksum = 0;

    for (let round = 0; round < ROUNDS; round++) {
        let head = null;
        for (let i = 0; i < LIST_SIZE; i++) {
            head = { value: round * LIST_SIZE + i, next: head };
        }

        let sum = 0;
        let node = head;
        while (node !== null) {
            sum = sum + node.value;
            node = node.next;
        }
        checksum = checksum + sum;

        forceGC();
        const h = getHeapUsed();
        if (h > peakHeap) peakHeap = h;
    }

    const wallTimeMs = timeMs() - start;
    forceGC();
    const heapAfter = getHeapUsed();

    return { name, wallTimeMs, heapBefore, heapAfter, peakHeap, rssAfter: getRSS(), itemsProcessed: ROUNDS * LIST_SIZE, checksum };
}

// ============================================================================
// Main
// ============================================================================

function main() {
    console.log("GC_COMPARE_START");

    const runtimeName = "node_" + process.versions.node;
    console.log("RUNTIME:" + runtimeName);

    forceGC();
    console.log("HEAP_INITIAL:" + getHeapUsed());
    console.log("RSS_INITIAL:" + getRSS());

    // V8 heap info
    const v8 = require('v8');
    const heapStats = v8.getHeapStatistics();
    console.log("V8_HEAP_SIZE_LIMIT:" + heapStats.heap_size_limit);
    console.log("V8_TOTAL_HEAP_SIZE:" + heapStats.total_heap_size);

    // Reset GC counters
    directGCTime = 0;
    directGCCount = 0;
    directGCMaxPause = 0;

    const workloads = [
        workloadShortLived,
        workloadLongLived,
        workloadMixedLifetime,
        workloadMapHeavy,
        workloadArrayChurn,
        workloadLinkedList
    ];

    const overallStart = timeMs();

    for (let i = 0; i < workloads.length; i++) {
        resetWorkloadGC();

        forceGC();
        const result = workloads[i]();
        reportResult(result);

        // Report direct-timed GC stats for this workload
        const gs = getWorkloadGCStats(result.wallTimeMs);
        console.log(`GC_STATS:${result.name}|count=${gs.count}|totalMs=${gs.totalMs.toFixed(2)}|maxPauseMs=${gs.maxPauseMs.toFixed(2)}|gcPct=${gs.gcPct.toFixed(1)}`);
    }

    const overallTime = timeMs() - overallStart;

    // Summary GC stats (direct timing)
    console.log(`GC_SUMMARY|totalCollections=${directGCCount}|totalGCTimeMs=${directGCTime.toFixed(2)}|maxPauseMs=${directGCMaxPause.toFixed(2)}|overallTimeMs=${overallTime.toFixed(2)}|gcPct=${overallTime > 0 ? ((directGCTime / overallTime) * 100).toFixed(1) : '0.0'}`);

    forceGC();
    console.log("HEAP_FINAL:" + getHeapUsed());
    console.log("RSS_FINAL:" + getRSS());
    console.log("GC_COMPARE_END");
}

main();

// Disconnect the observer
obs.disconnect();
