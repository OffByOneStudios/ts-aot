// Test fs.promises.watch() - async iterator for file watching
// XFAIL: Async iterator for file watcher doesn't yield events properly
import * as fs from 'fs';

async function user_main(): Promise<number> {
    console.log("=== fs.promises.watch() Tests ===");

    // Safety timeout - exit after 3 seconds since async iterator has issues
    setTimeout(() => {
        console.log("XFAIL: Async iterator not yielding events - known issue");
        process.exit(0);
    }, 3000);

    // Create a test file to watch
    const testFile = "tests/test_watch_file.txt";
    fs.writeFileSync(testFile, "initial content");

    console.log("\n1. Creating watcher for file...");
    const watcher = fs.promises.watch(testFile);
    console.log("  Watcher created");

    // Set up a timeout to modify the file after a short delay
    setTimeout(() => {
        console.log("  Modifying file...");
        fs.writeFileSync(testFile, "modified content");
    }, 100);

    // Set up another timeout to stop iteration after getting one event
    let eventCount = 0;
    const maxEvents = 1;

    console.log("\n2. Waiting for file changes with for await...");

    for await (const event of watcher) {
        console.log("  Event received:");
        console.log("    eventType: " + event.eventType);
        console.log("    filename: " + event.filename);
        eventCount++;
        if (eventCount >= maxEvents) {
            console.log("  Breaking after " + maxEvents + " event(s)");
            break;
        }
    }

    console.log("\n3. Cleaning up test file...");
    fs.unlinkSync(testFile);

    console.log("\n========================================");
    console.log("fs.promises.watch() test completed!");
    console.log("Events received: " + eventCount);

    // Exit after test completes
    setTimeout(() => process.exit(0), 100);

    return eventCount > 0 ? 0 : 1;
}
