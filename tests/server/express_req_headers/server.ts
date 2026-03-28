const express = require('../../npm/express/node_modules/express');
import * as http from 'http';

const app = express();

// Test req.get() / req.header() — case-insensitive header access
app.get('/echo', function(req: any, res: any) {
  var ct = req.get('Content-Type') || 'none';
  var ua = req.get('user-agent') || 'none';
  var custom = req.get('X-Custom') || 'none';
  var body = JSON.stringify({ contentType: ct, userAgent: ua, custom: custom });
  res.setHeader('Content-Type', 'application/json');
  res.setHeader('Content-Length', '' + body.length);
  res.end(body);
});

// Test req.headers object directly
app.get('/raw-headers', function(req: any, res: any) {
  var host = req.headers['host'] || 'none';
  var body = JSON.stringify({ hasHost: host !== 'none' });
  res.setHeader('Content-Type', 'application/json');
  res.setHeader('Content-Length', '' + body.length);
  res.end(body);
});

const server = http.createServer(app);
server.listen(13012, function() {
  console.log('listening on http://localhost:13012');
});
