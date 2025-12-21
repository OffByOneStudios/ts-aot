import * as http from 'http';

const server = http.createServer((req: IncomingMessage, res: ServerResponse) => {
    if (req.url === '/json') {
        res.writeHead(200, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ message: 'Hello, World!', timestamp: Date.now() }));
    } else {
        res.writeHead(404, { 'Content-Type': 'text/plain' });
        res.end("Not Found");
    }
});

console.log("Server listening on port 8080...");
server.listen(8080);
