const http = require('http');

const options = {
  hostname: 'localhost',
  port: 8080,
  path: '/test-url',
  method: 'GET'
};

console.log('Sending request...');
const req = http.request(options, (res) => {
  console.log('Got response!');
  
  res.on('data', (chunk) => {
    console.log('Got chunk: ' + chunk.toString());
  });
  
  res.on('end', () => {
    console.log('Response ended.');
  });
});

req.on('error', (e) => {
  console.log('Problem with request: ' + e.message);
});

req.end();
