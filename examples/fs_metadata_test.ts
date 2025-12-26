const fs = require('fs');

// Test constants
ts_console_log("F_OK: " + fs.constants.F_OK);
ts_console_log("R_OK: " + fs.constants.R_OK);
ts_console_log("W_OK: " + fs.constants.W_OK);
ts_console_log("X_OK: " + fs.constants.X_OK);

const testFile = 'examples/test_metadata.txt';
fs.writeFileSync(testFile, 'hello metadata');

// Test accessSync
try {
    fs.accessSync(testFile, fs.constants.R_OK);
    ts_console_log("accessSync OK");
} catch (e) {
    ts_console_log("accessSync failed");
}

// Test statSync
const stats = fs.statSync(testFile);
ts_console_log("File size: " + stats.size);
ts_console_log("Is file: " + stats.isFile());
ts_console_log("Is directory: " + stats.isDirectory());
ts_console_log("mtimeMs: " + stats.mtimeMs);

// Test chmodSync (0o444 = 292)
fs.chmodSync(testFile, 292);
ts_console_log("chmodSync OK");

// Test statfsSync
const fsStats = fs.statfsSync('.');
if (fsStats) {
    ts_console_log("statfs bsize: " + fsStats.bsize);
}

// Test promises
const promises = fs.promises;
promises.readFile(testFile).then((content) => {
    ts_console_log("Promise readFile content: " + content);
});

// Cleanup
// fs.unlinkSync(testFile);
