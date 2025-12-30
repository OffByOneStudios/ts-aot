// Test http.Agent and https.Agent

import * as http from 'http';
import * as https from 'https';

console.log("=== Testing http.Agent ===");

// Test http.globalAgent
console.log("Accessing http.globalAgent...");
const globalAgent = http.globalAgent;
console.log("globalAgent exists:", globalAgent !== null && globalAgent !== undefined);

// Test new http.Agent()
console.log("\nCreating new http.Agent with options...");
const agent = new http.Agent({
    keepAlive: true,
    maxSockets: 10,
    maxFreeSockets: 5
});
console.log("agent created:", agent !== null && agent !== undefined);

// Test Agent.destroy()
console.log("\nTesting agent.destroy()...");
agent.destroy();
console.log("agent destroyed");

// Test https.globalAgent
console.log("\n=== Testing https.Agent ===");
console.log("Accessing https.globalAgent...");
const httpsGlobalAgent = https.globalAgent;
console.log("httpsGlobalAgent exists:", httpsGlobalAgent !== null && httpsGlobalAgent !== undefined);

// Test new https.Agent()
console.log("\nCreating new https.Agent...");
const httpsAgent = new https.Agent({
    keepAlive: true,
    maxSockets: 5
});
console.log("httpsAgent created:", httpsAgent !== null && httpsAgent !== undefined);

// Test setMaxIdleHTTPParsers
console.log("\n=== Testing setMaxIdleHTTPParsers ===");
console.log("Getting current max...");
const currentMax = http.getMaxIdleHTTPParsers();
console.log("Current max idle HTTP parsers:", currentMax);

console.log("Setting to 500...");
http.setMaxIdleHTTPParsers(500);

const newMax = http.getMaxIdleHTTPParsers();
console.log("New max idle HTTP parsers:", newMax);
console.log("setMaxIdleHTTPParsers works:", newMax === 500);

console.log("\n=== All Agent tests passed! ===");
