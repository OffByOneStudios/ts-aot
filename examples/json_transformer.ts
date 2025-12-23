/**
 * JSON Transformer Benchmark for ts-aot
 * Tests: JSON.parse, JSON.stringify, String manipulation, and Object property access.
 */

interface DataItem {
    id: number;
    name: string;
    tags: string[];
    active: boolean;
    score: number;
}

function generateData(count: number): string {
    const data: DataItem[] = [];
    for (let i = 0; i < count; i++) {
        data.push({
            id: i,
            name: "Item " + i,
            tags: ["tag1", "tag2", "tag" + (i % 10)],
            active: i % 2 === 0,
            score: Math.random() * 100
        });
    }
    return JSON.stringify(data);
}

function transformData(json: string): string {
    const data: DataItem[] = JSON.parse(json);
    const transformed: any[] = [];
    for (let i = 0; i < data.length; i++) {
        const item = data[i];
        if (item.active) {
            transformed.push({
                id: item.id,
                name: item.name.toUpperCase(),
                tags: item.tags,
                active: item.active,
                score: item.score * 1.1,
                tagCount: item.tags.length
            });
        }
    }
    return JSON.stringify(transformed);
}

function runBenchmark() {
    const count = 10000;
    const iterations = 100;
    
    console.log("Generating data...");
    const json = generateData(count);
    
    console.log("Starting transformation benchmark...");
    const start = Date.now();
    
    let lastResult = "";
    for (let i = 0; i < iterations; i++) {
        lastResult = transformData(json);
    }
    
    const end = Date.now();
    console.log("JSON Transformer Benchmark: " + (end - start) + "ms");
    console.log("Result length: " + lastResult.length);
}

runBenchmark();
