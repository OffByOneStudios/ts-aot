// Test perf_hooks PerformanceObserver
import { performance, PerformanceObserver } from 'perf_hooks';

function user_main(): number {
    console.log("=== perf_hooks PerformanceObserver test ===");

    // Test 1: Create a PerformanceObserver
    console.log("Test 1: Create PerformanceObserver...");
    const obs = new PerformanceObserver((list: any, observer: any) => {
        console.log("Observer callback invoked");
    });
    console.log("Observer created: " + (obs !== null));

    // Test 2: Call observe method
    console.log("Test 2: Call observe...");
    obs.observe({ entryTypes: ['mark', 'measure'] });
    console.log("Observe called successfully");

    // Test 3: Create some performance entries
    console.log("Test 3: Create marks and measures...");
    performance.mark('obs-start');
    performance.mark('obs-end');
    performance.measure('obs-measure', 'obs-start', 'obs-end');

    // Test 4: takeRecords - just call and check it doesn't crash
    console.log("Test 4: takeRecords...");
    const records = obs.takeRecords();
    console.log("takeRecords returned: " + (records !== null));

    // Test 5: Disconnect
    console.log("Test 5: Disconnect...");
    obs.disconnect();
    console.log("Disconnect called successfully");

    console.log("=== PerformanceObserver tests passed ===");
    return 0;
}
