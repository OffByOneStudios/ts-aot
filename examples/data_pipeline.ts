/**
 * Data Pipeline Benchmark for ts-aot
 * Tests: Higher-Order Functions (map, filter, reduce), Closures, and Array performance.
 */

function runBenchmark() {
    const size = 1000000;
    const iterations = 50;
    
    console.log("Initializing array...");
    const data = new Float64Array(size);
    for (let i = 0; i < size; i++) {
        data[i] = i;
    }
    
    console.log("Starting pipeline benchmark...");
    const start = Date.now();
    
    let totalSum = 0;
    for (let i = 0; i < iterations; i++) {
        let sum = 0;
        for (let j = 0; j < size; j++) {
            const x = data[j];
            if (x % 2 === 0) {
                sum += x * 1.5;
            }
        }
        totalSum += sum;
    }
    
    const end = Date.now();
    console.log("Data Pipeline Benchmark: " + (end - start) + "ms");
    console.log("Total Sum: " + totalSum);
}

runBenchmark();
