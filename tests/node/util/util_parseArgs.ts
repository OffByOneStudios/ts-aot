// Test util.parseArgs
import * as util from 'util';

let passed = 0;
let failed = 0;

function test(name: string, fn: () => boolean): void {
    try {
        if (fn()) {
            console.log("PASS: " + name);
            passed++;
        } else {
            console.log("FAIL: " + name);
            failed++;
        }
    } catch (e) {
        console.log("FAIL: " + name + " (exception)");
        failed++;
    }
}

function user_main(): number {
    console.log("Testing util.parseArgs");
    console.log("======================");
    console.log("");

    // Test 1: Basic call with no config
    test("parseArgs with no config returns object", () => {
        const result = util.parseArgs();
        return typeof result === "object" && result !== null;
    });

    // Test 2: Result has values property
    test("parseArgs result has values property", () => {
        const result = util.parseArgs();
        return "values" in result;
    });

    // Test 3: Result has positionals property
    test("parseArgs result has positionals property", () => {
        const result = util.parseArgs();
        return "positionals" in result;
    });

    // Test 4: Positionals is an array
    test("positionals is an array", () => {
        const result = util.parseArgs();
        return Array.isArray(result.positionals);
    });

    // Test 5: Values is an object
    test("values is an object", () => {
        const result = util.parseArgs();
        return typeof result.values === "object";
    });

    // Test 6: With empty options config
    test("parseArgs with empty options", () => {
        const result = util.parseArgs({ options: {} });
        return typeof result === "object" && "values" in result;
    });

    // Test 7: Boolean option from args array
    test("boolean option from args", () => {
        const result = util.parseArgs({
            args: ["--verbose"],
            options: { verbose: { type: "boolean" } }
        });
        return result.values.verbose === true;
    });

    // Test 8: String option from args array
    test("string option from args", () => {
        const result = util.parseArgs({
            args: ["--name", "test"],
            options: { name: { type: "string" } }
        });
        return result.values.name === "test";
    });

    // Test 9: Multiple options
    test("multiple options", () => {
        const result = util.parseArgs({
            args: ["--verbose", "--name", "myapp"],
            options: {
                verbose: { type: "boolean" },
                name: { type: "string" }
            }
        });
        return result.values.verbose === true && result.values.name === "myapp";
    });

    // Test 10: Short alias for boolean
    test("short alias for boolean option", () => {
        const result = util.parseArgs({
            args: ["-v"],
            options: {
                verbose: { type: "boolean", short: "v" }
            }
        });
        return result.values.verbose === true;
    });

    // Test 11: Short alias for string
    test("short alias for string option", () => {
        const result = util.parseArgs({
            args: ["-n", "test"],
            options: {
                name: { type: "string", short: "n" }
            }
        });
        return result.values.name === "test";
    });

    // Test 12: Long option with equals sign
    test("option with equals sign", () => {
        const result = util.parseArgs({
            args: ["--name=test"],
            options: { name: { type: "string" } }
        });
        return result.values.name === "test";
    });

    console.log("");
    console.log("Results: " + passed + "/" + (passed + failed) + " passed");
    return failed > 0 ? 1 : 0;
}
