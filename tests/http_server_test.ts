import * as http from 'http';

const server = http.createServer((req, res) => {
    console.log('Request received: ' + req.url);
    res.writeHead(200, { 'Content-Type': 'text/plain' });
    res.write('Hello from ts-aot HTTP server!\n');
    res.end('Done.');
});

server.listen(8080, () => {
    console.log('Server listening on port 8080');
});

// For testing purposes, we'll just exit after a short delay or if we receive a request
// But since we don't have setTimeout yet, we'll just let it run.
// In a real test runner, we'd need a way to stop it.
