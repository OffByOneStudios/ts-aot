import * as https from 'https';
import * as fs from 'fs';

const options = {
  key: fs.readFileSync('key.pem'),
  cert: fs.readFileSync('cert.pem')
};

const server = https.createServer(options, (req, res) => {
  console.log('Request received:', req.method, req.url);
  res.writeHead(200, { 'Content-Type': 'text/plain' });
  res.end('Hello from HTTPS server!\n');
});

server.on('error', (err) => {
  console.error('Server error:', err);
});

server.listen(8443, () => {
  console.log('HTTPS server listening on port 8443');
  
  // After a short delay, try to fetch from it
  // Note: We need to disable certificate verification for self-signed certs
  setTimeout(() => {
    console.log('Sending request to HTTPS server...');
    const req = https.request({
      hostname: 'localhost',
      port: 8443,
      path: '/',
      method: 'GET',
      rejectUnauthorized: false // Allow self-signed cert
    }, (res) => {
      console.log('Response status:', res.statusCode);
      res.on('data', (d) => {
        console.log('Response data:', d.toString());
        process.exit(0);
      });
    });
    
    req.on('error', (e) => {
      console.error('Request error:', e);
      process.exit(1);
    });
    
    req.end();
  }, 1000);
});
