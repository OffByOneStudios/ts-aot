const fs = require('fs');

console.log("Starting watch test...");

const testFile = "watch_test_file.txt";
fs.writeFileSync(testFile, "initial content");

console.log(`Watching ${testFile}...`);
const watcher = fs.watch(testFile, (eventType, filename) => {
    console.log(`Listener 1 - Event: ${eventType}, Filename: ${filename}`);
});

if (watcher) {
    console.log("Watcher created successfully");
}

watcher.on('change', (eventType, filename) => {
    console.log(`Watcher ON change: ${eventType}, ${filename}`);
});

setTimeout(() => {
    console.log("Modifying file...");
    fs.writeFileSync(testFile, "modified content");
}, 1000);

setTimeout(() => {
    console.log("Closing watcher...");
    watcher.close();
    console.log("Done.");
    fs.unlinkSync(testFile);
}, 3000);
