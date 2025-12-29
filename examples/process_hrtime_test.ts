// Simple test for hrtime
console.log("Starting hrtime test...");

// Get current high-resolution time
const start = process.hrtime();
console.log("Got start time");

// Get elapsed time 
const end = process.hrtime(start);
console.log("Got end time");

console.log("Done!");
