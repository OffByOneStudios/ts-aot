const express = require('../../npm/express/node_modules/express');
import * as http from 'http';

const app = express();

// Test res.json() — the Express convenience method
// This calls res.send() internally which calls this.json() for objects
app.get('/json', function(req: any, res: any) {
  res.json({ message: 'hello', count: 42 });
});

// Test res.status().json() chaining
app.get('/status-json', function(req: any, res: any) {
  res.status(201).json({ created: true });
});

const server = http.createServer(app);
server.listen(13008, '127.0.0.1', function() {
  console.log('listening on http://localhost:13008');
});
