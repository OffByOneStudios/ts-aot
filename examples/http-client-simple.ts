import * as http from 'http';

console.log("Starting simple HTTP Client test...");

const options = {
    hostname: 'localhost',
    port: 8080,
    path: '/',
    method: 'GET'
};

console.log("About to call http.request...");
const req = http.request(options, (res) => {
    console.log("Response received!");
});
console.log("http.request returned, req is now set");

console.log("About to register error handler...");
req.on('error', (e) => {
    console.log("Request error: " + e.message);
});
console.log("Error handler registered");

console.log("About to call req.end()...");
req.end();
console.log("req.end() returned");
