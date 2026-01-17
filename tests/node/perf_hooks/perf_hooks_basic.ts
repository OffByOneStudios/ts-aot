// Test perf_hooks module basic functionality
// import { performance } from 'perf_hooks';

function user_main(): number {
    console.log("Testing perf_hooks module...");

    // Test performance.now()
    console.log("Testing performance.now()...");
    const start = performance.now();
    console.log("  Start time: " + start);

    // Do some work
    let sum = 0;
    for (let i = 0; i < 1000; i++) {
        sum += i;
    }

    const end = performance.now();
    console.log("  End time: " + end);
    const elapsed = end - start;
    console.log("  Elapsed: " + elapsed + "ms");

    if (elapsed >= 0) {
        console.log("  performance.now() works correctly");
    }

    // Test performance.timeOrigin
    console.log("Testing performance.timeOrigin...");
    const origin = performance.timeOrigin;
    console.log("  Time origin: " + origin);
    if (origin > 0) {
        console.log("  performance.timeOrigin works correctly");
    }

    // Test performance.mark()
    console.log("Testing performance.mark()...");
    const mark1 = performance.mark("test-start");
    console.log("  Mark name: " + mark1.name);
    console.log("  Mark entryType: " + mark1.entryType);
    console.log("  Mark startTime: " + mark1.startTime);
    console.log("  Mark duration: " + mark1.duration);

    // Do some work
    for (let i = 0; i < 10000; i++) {
        sum += i;
    }

    const mark2 = performance.mark("test-end");
    console.log("  Second mark created");

    // Test performance.measure()
    console.log("Testing performance.measure()...");
    const measure = performance.measure("test-measure", "test-start", "test-end");
    console.log("  Measure name: " + measure.name);
    console.log("  Measure entryType: " + measure.entryType);
    console.log("  Measure startTime: " + measure.startTime);
    console.log("  Measure duration: " + measure.duration);

    // Test performance.getEntries()
    console.log("Testing performance.getEntries()...");
    const entries = performance.getEntries();
    console.log("  Total entries: " + entries.length);

    // Test performance.getEntriesByName()
    console.log("Testing performance.getEntriesByName()...");
    const byName = performance.getEntriesByName("test-start");
    console.log("  Entries with name 'test-start': " + byName.length);

    // Test performance.getEntriesByType()
    console.log("Testing performance.getEntriesByType()...");
    const marks = performance.getEntriesByType("mark");
    console.log("  Mark entries: " + marks.length);
    const measures = performance.getEntriesByType("measure");
    console.log("  Measure entries: " + measures.length);

    // Test performance.clearMarks()
    console.log("Testing performance.clearMarks()...");
    performance.clearMarks("test-start");
    const afterClear = performance.getEntriesByName("test-start");
    console.log("  Entries after clearMarks: " + afterClear.length);

    // Test performance.clearMeasures()
    console.log("Testing performance.clearMeasures()...");
    performance.clearMeasures();
    const measuresAfter = performance.getEntriesByType("measure");
    console.log("  Measures after clear: " + measuresAfter.length);

    console.log("All perf_hooks tests passed!");
    return 0;
}
