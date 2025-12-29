// Test for extended process module APIs (Milestones 102.5 - 102.10)

console.log("=== Process Extended APIs Test ===\n");

// ============================================================================
// Milestone 102.5: Process Info
// ============================================================================
console.log("--- Milestone 102.5: Process Info ---");
console.log("process.pid:", process.pid);
console.log("process.ppid:", process.ppid);
console.log("process.version:", process.version);
console.log("process.versions:", process.versions);
console.log("process.argv0:", process.argv0);
console.log("process.execPath:", process.execPath);
console.log("process.execArgv:", process.execArgv);
console.log("process.title:", process.title);

// ============================================================================
// Milestone 102.6: High-Resolution Time & Resource Usage
// ============================================================================
console.log("\n--- Milestone 102.6: High-Res Time & Resources ---");

// Test hrtime
const hrStart = process.hrtime();
console.log("hrtime start:", hrStart);

const hrEnd = process.hrtime(hrStart);
console.log("hrtime elapsed:", hrEnd);

// Test uptime
console.log("process.uptime():", process.uptime(), "seconds");

// Test memoryUsage
const memUsage = process.memoryUsage();
console.log("process.memoryUsage():", memUsage);

// Test cpuUsage
const cpuStart = process.cpuUsage();
console.log("process.cpuUsage():", cpuStart);

// Test resourceUsage
const resUsage = process.resourceUsage();
console.log("process.resourceUsage().userCPUTime:", resUsage.userCPUTime);
console.log("process.resourceUsage().maxRSS:", resUsage.maxRSS);

// ============================================================================
// Milestone 102.7: Process Control (some tests)
// ============================================================================
console.log("\n--- Milestone 102.7: Process Control ---");

// Test umask (get current)
const currentUmask = process.umask(0);
console.log("Current umask:", currentUmask);
// Restore it
process.umask(currentUmask);

// Test emitWarning
process.emitWarning("This is a test warning");

// ============================================================================
// Milestone 102.10: Configuration & Features
// ============================================================================
console.log("\n--- Milestone 102.10: Configuration & Features ---");

console.log("process.config:", process.config);
console.log("process.features:", process.features);
console.log("process.release:", process.release);
console.log("process.debugPort:", process.debugPort);

// ============================================================================
// Done!
// ============================================================================
console.log("\n=== All Extended Process API Tests Complete ===");
