const express = require('../../npm/express/node_modules/express');
import * as http from 'http';

const app = express();

// Route that succeeds
app.get('/ok', function(req: any, res: any) {
  var body = JSON.stringify({ ok: true });
  res.setHeader('Content-Type', 'application/json');
  res.setHeader('Content-Length', '' + body.length);
  res.end(body);
});

// Route that throws
app.get('/throw', function(req: any, res: any) {
  throw new Error('something broke');
});

// Route that passes error via next()
app.get('/next-error', function(req: any, res: any, next: any) {
  next(new Error('passed to next'));
});

// Error-handling middleware (4 arguments)
app.use(function(err: any, req: any, res: any, next: any) {
  res.statusCode = 500;
  var msg = err.message || 'Unknown error';
  var body = JSON.stringify({ error: msg });
  res.setHeader('Content-Type', 'application/json');
  res.setHeader('Content-Length', '' + body.length);
  res.end(body);
});

const server = http.createServer(app);
server.listen(13014, function() {
  console.log('listening on http://localhost:13014');
});
