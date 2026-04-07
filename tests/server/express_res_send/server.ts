const express = require('../../npm/express/node_modules/express');
import * as http from 'http';

const app = express();

// Test res.send() with a string body
app.get('/text', function(req: any, res: any) {
  res.send('Hello World');
});

// Test res.send() with an object (should auto-call res.json())
app.get('/object', function(req: any, res: any) {
  res.send({ key: 'value' });
});

// Test res.sendStatus()
app.get('/send-status', function(req: any, res: any) {
  res.sendStatus(202);
});

const server = http.createServer(app);
server.listen(13009, '127.0.0.1', function() {
  console.log('listening on http://localhost:13009');
});
