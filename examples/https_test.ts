import * as https from 'https';

console.log("Starting HTTPS request to google.com...");

const options = {
    hostname: 'www.google.com',
    port: 443,
    path: '/',
    method: 'GET',
    rejectUnauthorized: false // For testing, though it should work with true if CA is set up
};

const req = https.request(options, (res) => {
    console.log(`Status Code: ${res.statusCode}`);
    
    res.on('data', (d) => {
        console.log(`Received ${d.length} bytes of data.`);
        if (d.length > 0) {
            console.log(`First byte: ${d[0]}`);
        }
    });
    
    res.on('end', () => {
        console.log("Response ended.");
    });
});

req.on('error', (e) => {
    console.error(`Problem with request: ${e.message}`);
});

req.end();
