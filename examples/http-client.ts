import * as http from 'http';

console.log("Starting HTTP Client test...");

const options = {
    hostname: 'localhost',
    port: 8080,
    path: '/',
    method: 'GET'
};

const req = http.request(options, (res) => {
    console.log("Response received!");
    console.log("Status: " + res.statusCode);
    
    res.on('data', (chunk) => {
        console.log("Data: " + chunk.toString());
    });
    
    res.on('end', () => {
        console.log("Response ended.");
    });
});

req.on('error', (e) => {
    console.log("Request error: " + e.message);
});

req.end();
