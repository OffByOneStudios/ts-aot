/**
 * Test: Dynamic property access on plain objects via interfaces
 *
 * Verifies that ts_object_get_property and ts_try_virtual_dispatch_via_vbase
 * don't crash when accessing properties on plain TsMap objects through
 * interface-typed variables.
 */

// Interface with optional properties (common pattern in options objects)
interface Config {
    iterations?: number;
    warmup?: number;
    name?: string;
    enabled?: boolean;
}

// Default values as module-scoped const
const defaultConfig: Config = {
    iterations: 100,
    warmup: 10,
    name: "default",
    enabled: true
};

// Function that accepts interface-typed parameter
function processConfig(config: Config): string {
    let iterations: number = 100;
    let warmup: number = 10;
    let name: string = "default";

    if (config !== undefined) {
        if (config.iterations !== undefined) iterations = config.iterations;
        if (config.warmup !== undefined) warmup = config.warmup;
        if (config.name !== undefined) name = config.name;
    }

    return name + ":" + iterations + ":" + warmup;
}

// Interface with nested object property
interface Result {
    timing: {
        min: number;
        max: number;
        avg: number;
    };
    name: string;
}

function formatResult(result: Result): string {
    return result.name + " min=" + result.timing.min + " max=" + result.timing.max + " avg=" + result.timing.avg;
}

// Class with private fields accessed via this
class Suite {
    private name: string;
    private items: string[] = [];

    constructor(name: string) {
        this.name = name;
    }

    add(item: string): void {
        this.items.push(item);
    }

    run(): string {
        return "Suite: " + this.name + " items=" + this.items.length;
    }
}

function user_main(): number {
    let passed = 0;
    let failed = 0;

    // Test 1: Access properties on plain object through interface
    const cfg: Config = { iterations: 50, warmup: 5, name: "test" };
    const result1 = processConfig(cfg);
    if (result1 === "test:50:5") {
        console.log("PASS: interface property access");
        passed++;
    } else {
        console.log("FAIL: interface property access, got: " + result1);
        failed++;
    }

    // Test 2: Access default config (module-scoped object)
    const result2 = processConfig(defaultConfig);
    if (result2 === "default:100:10") {
        console.log("PASS: module-scoped object property access");
        passed++;
    } else {
        console.log("FAIL: module-scoped object property access, got: " + result2);
        failed++;
    }

    // Test 3: Access with undefined optional properties
    const sparse: Config = { iterations: 200 };
    const result3 = processConfig(sparse);
    if (result3 === "default:200:10") {
        console.log("PASS: sparse optional property access");
        passed++;
    } else {
        console.log("FAIL: sparse optional property access, got: " + result3);
        failed++;
    }

    // Test 4: Nested object property access
    const res: Result = {
        timing: { min: 1, max: 100, avg: 50 },
        name: "bench1"
    };
    const result4 = formatResult(res);
    if (result4 === "bench1 min=1 max=100 avg=50") {
        console.log("PASS: nested object property access");
        passed++;
    } else {
        console.log("FAIL: nested object property access, got: " + result4);
        failed++;
    }

    // Test 5: Class with private fields
    const suite = new Suite("MySuite");
    suite.add("item1");
    suite.add("item2");
    const result5 = suite.run();
    if (result5 === "Suite: MySuite items=2") {
        console.log("PASS: class private field access");
        passed++;
    } else {
        console.log("FAIL: class private field access, got: " + result5);
        failed++;
    }

    // Test 6: Object.keys on plain object
    const obj = { a: 1, b: 2, c: 3 };
    const keys = Object.keys(obj);
    if (keys.length === 3) {
        console.log("PASS: Object.keys on plain object");
        passed++;
    } else {
        console.log("FAIL: Object.keys on plain object, got length: " + keys.length);
        failed++;
    }

    // Test 7: Dynamic property access with bracket notation
    const data: any = { x: 42, y: "hello" };
    const xVal = data["x"];
    const yVal = data["y"];
    if (xVal === 42 && yVal === "hello") {
        console.log("PASS: bracket notation property access");
        passed++;
    } else {
        console.log("FAIL: bracket notation property access");
        failed++;
    }

    console.log("\n" + passed + "/" + (passed + failed) + " tests passed");

    if (failed > 0) return 1;
    return 0;
}
