// Simple test for fs.promises.watch() - no for await
import * as fs from 'fs';

async function user_main(): Promise<number> {
    console.log("=== Simple fs.promises.watch() Test ===");

    // Create a test file to watch
    const testFile = "tests/test_watch_file.txt";
    fs.writeFileSync(testFile, "initial content");

    console.log("\n1. Creating watcher for file...");
    const watcher = fs.promises.watch(testFile);
    console.log("  Watcher created, type: " + typeof watcher);

    // Manually call next() after a delay
    setTimeout(async () => {
        console.log("\n3. Timeout fired - modifying file...");
        fs.writeFileSync(testFile, "modified content");
        console.log("  File modified");
    }, 100);

    console.log("\n2. Calling next() manually...");
    // Get the async iterator
    const asyncIter = (watcher as any)[Symbol.asyncIterator]();
    console.log("  Got async iterator");

    // Call next() - this should return a promise
    console.log("  Calling next()...");
    const nextResult = asyncIter.next();
    console.log("  next() returned, awaiting result...");

    const result = await nextResult;
    console.log("  Result received!");
    console.log("    done: " + result.done);
    if (!result.done) {
        console.log("    value.eventType: " + result.value.eventType);
        console.log("    value.filename: " + result.value.filename);
    }

    console.log("\n4. Cleaning up...");
    fs.unlinkSync(testFile);

    console.log("\nTest completed!");
    return result.done ? 1 : 0;
}
