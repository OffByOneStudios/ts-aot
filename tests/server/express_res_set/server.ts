const express = require('../../npm/express/node_modules/express');
import * as http from 'http';

const app = express();

// Test res.set() — Express wrapper that adds charset for content-type
app.get('/set-header', function(req: any, res: any) {
  res.set('X-Custom', 'value1');
  res.set('X-Request-Id', 'abc-123');
  res.end('OK');
});

// Test res.type() — sets Content-Type with MIME lookup
app.get('/type-json', function(req: any, res: any) {
  res.type('json');
  res.end('{"typed":true}');
});

app.get('/type-html', function(req: any, res: any) {
  res.type('html');
  res.end('<p>hello</p>');
});

app.get('/type-text', function(req: any, res: any) {
  res.type('txt');
  res.end('plain text');
});

const server = http.createServer(app);
server.listen(13015, '127.0.0.1', function() {
  console.log('listening on http://localhost:13015');
});
