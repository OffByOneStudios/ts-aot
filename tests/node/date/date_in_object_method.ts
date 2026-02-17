// Test Date usage inside object literal methods (worker pattern)

function user_main(): number {
    let failures = 0;

    // Test 1: Object method calling new Date().toISOString()
    const logger = {
        getTimestamp(): string {
            return new Date().toISOString();
        }
    };
    const ts = logger.getTimestamp();
    if (ts.indexOf("T") > -1 && ts.indexOf("Z") > -1) {
        console.log("PASS: object method returns ISO timestamp");
    } else {
        console.log("FAIL: expected ISO format, got '" + ts + "'");
        failures++;
    }

    // Test 2: Date.now() inside object method
    const timer = {
        now(): number {
            return Date.now();
        }
    };
    const t = timer.now();
    if (t > 0) {
        console.log("PASS: Date.now() works inside object method");
    } else {
        console.log("FAIL: Date.now() in object method returned " + t);
        failures++;
    }

    // Test 3: Template literal with Date inside object method
    const ms = 1704067200000;
    const formatter = {
        format(timestamp: number): string {
            const d = new Date(timestamp);
            return `[${d.toISOString()}] message`;
        }
    };
    const formatted = formatter.format(ms);
    if (formatted.indexOf("[") === 0 && formatted.indexOf("] message") > -1) {
        console.log("PASS: template literal with Date in object method");
    } else {
        console.log("FAIL: expected '[...] message', got '" + formatted + "'");
        failures++;
    }

    // Test 4: Object with method returning Date-based string
    const worker = {
        processJob(id: number): string {
            const start = Date.now();
            return "Job " + id + " started at " + start;
        }
    };
    const result = worker.processJob(42);
    if (result.indexOf("Job 42 started at ") === 0) {
        console.log("PASS: object method with Date.now() in string concat");
    } else {
        console.log("FAIL: expected 'Job 42 started at ...', got '" + result + "'");
        failures++;
    }

    console.log("---");
    if (failures === 0) {
        console.log("All date-in-object-method tests passed!");
    } else {
        console.log(failures + " test(s) failed");
    }

    return failures;
}
