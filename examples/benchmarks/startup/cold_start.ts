/**
 * Cold Start Benchmark
 *
 * Measures the minimum startup time of the runtime.
 * This is a minimal program that just exits immediately.
 *
 * To benchmark externally:
 *   PowerShell: Measure-Command { ./cold_start.exe } | Select TotalMilliseconds
 *   Bash: time ./cold_start.exe
 *
 * Run 50+ times and average for accurate results.
 */

function user_main(): number {
    // Intentionally empty - we're measuring startup overhead
    return 0;
}
