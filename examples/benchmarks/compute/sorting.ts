/**
 * Sorting Benchmark
 *
 * Tests array manipulation performance with various sorting algorithms.
 * Measures: array creation, element access, swaps, comparisons.
 */

import { benchmark, printResult, BenchmarkSuite } from '../harness/benchmark';

/**
 * Generate array of random integers
 */
function generateRandomArray(size: number, max: number = 1000000): number[] {
    const arr: number[] = [];
    for (let i = 0; i < size; i++) {
        arr.push(Math.floor(Math.random() * max));
    }
    return arr;
}

/**
 * Generate sorted array
 */
function generateSortedArray(size: number): number[] {
    const arr: number[] = [];
    for (let i = 0; i < size; i++) {
        arr.push(i);
    }
    return arr;
}

/**
 * Generate reverse-sorted array
 */
function generateReversedArray(size: number): number[] {
    const arr: number[] = [];
    for (let i = size - 1; i >= 0; i--) {
        arr.push(i);
    }
    return arr;
}

/**
 * QuickSort implementation
 */
function quickSort(arr: number[], low: number = 0, high: number = arr.length - 1): void {
    if (low < high) {
        const pivotIndex = partition(arr, low, high);
        quickSort(arr, low, pivotIndex - 1);
        quickSort(arr, pivotIndex + 1, high);
    }
}

function partition(arr: number[], low: number, high: number): number {
    const pivot = arr[high];
    let i = low - 1;

    for (let j = low; j < high; j++) {
        if (arr[j] <= pivot) {
            i++;
            const temp = arr[i];
            arr[i] = arr[j];
            arr[j] = temp;
        }
    }

    const temp = arr[i + 1];
    arr[i + 1] = arr[high];
    arr[high] = temp;

    return i + 1;
}

/**
 * MergeSort implementation
 */
function mergeSort(arr: number[]): number[] {
    if (arr.length <= 1) return arr;

    const mid = Math.floor(arr.length / 2);
    const left = arr.slice(0, mid);
    const right = arr.slice(mid);

    return merge(mergeSort(left), mergeSort(right));
}

function merge(left: number[], right: number[]): number[] {
    const result: number[] = [];
    let i = 0;
    let j = 0;

    while (i < left.length && j < right.length) {
        if (left[i] <= right[j]) {
            result.push(left[i]);
            i++;
        } else {
            result.push(right[j]);
            j++;
        }
    }

    while (i < left.length) {
        result.push(left[i]);
        i++;
    }

    while (j < right.length) {
        result.push(right[j]);
        j++;
    }

    return result;
}

/**
 * Bubble sort (for comparison - O(n^2))
 */
function bubbleSort(arr: number[]): void {
    const n = arr.length;
    for (let i = 0; i < n - 1; i++) {
        for (let j = 0; j < n - i - 1; j++) {
            if (arr[j] > arr[j + 1]) {
                const temp = arr[j];
                arr[j] = arr[j + 1];
                arr[j + 1] = temp;
            }
        }
    }
}

/**
 * Verify array is sorted
 */
function isSorted(arr: number[]): boolean {
    for (let i = 1; i < arr.length; i++) {
        if (arr[i] < arr[i - 1]) return false;
    }
    return true;
}

function user_main(): number {
    console.log('Sorting Benchmark');
    console.log('=================');
    console.log('');

    const SIZE_SMALL = 10000;
    const SIZE_MEDIUM = 100000;
    const SIZE_LARGE = 1000000;

    // Pre-generate test data
    console.log('Generating test data...');
    const smallRandom = generateRandomArray(SIZE_SMALL);
    const mediumRandom = generateRandomArray(SIZE_MEDIUM);
    const largeRandom = generateRandomArray(SIZE_LARGE);
    console.log('');

    // Verify implementations
    const testArr = generateRandomArray(100);
    const testCopy = testArr.slice();
    quickSort(testCopy);
    console.log(`QuickSort verification: ${isSorted(testCopy) ? 'PASS' : 'FAIL'}`);
    console.log('');

    const suite = new BenchmarkSuite('Sorting Algorithms');

    // Built-in sort (baseline)
    suite.add('Array.sort (100k)', () => {
        const arr = mediumRandom.slice();
        arr.sort((a, b) => a - b);
    }, { iterations: 10, warmup: 2 });

    // QuickSort
    suite.add('QuickSort (100k)', () => {
        const arr = mediumRandom.slice();
        quickSort(arr);
    }, { iterations: 10, warmup: 2 });

    // MergeSort
    suite.add('MergeSort (100k)', () => {
        const arr = mediumRandom.slice();
        mergeSort(arr);
    }, { iterations: 10, warmup: 2 });

    // Large array quicksort
    suite.add('QuickSort (1M)', () => {
        const arr = largeRandom.slice();
        quickSort(arr);
    }, { iterations: 3, warmup: 1 });

    // Bubble sort on small array (O(n^2) comparison)
    suite.add('BubbleSort (10k)', () => {
        const arr = smallRandom.slice();
        bubbleSort(arr);
    }, { iterations: 3, warmup: 1 });

    suite.run();
    suite.printSummary();

    return 0;
}
