// Test os.availableParallelism()
import * as os from 'os';

function user_main(): number {
    console.log("Testing os.availableParallelism()...");

    const parallelism = os.availableParallelism();
    console.log("Available parallelism: " + parallelism);

    // Should be at least 1
    console.log("Is positive: " + (parallelism > 0));

    // Should match cpus().length
    const cpuCount = os.cpus().length;
    console.log("Matches cpus().length: " + (parallelism === cpuCount));

    console.log("All os.availableParallelism() tests passed!");
    return 0;
}
