const express = require('../../npm/express/node_modules/express');
import * as http from 'http';

const app = express();

// Test req.path, req.hostname, req.protocol, req.ip, req.method
app.get('/info', function(req: any, res: any) {
  var info: any = {};
  info.method = req.method;
  info.path = req.path;
  // req.hostname comes from Host header
  info.hasHostname = typeof req.hostname === 'string' && req.hostname.length > 0;
  // req.protocol is 'http' or 'https'
  info.protocol = req.protocol;
  // req.ip is the remote address
  info.hasIp = typeof req.ip === 'string' && req.ip.length > 0;
  var body = JSON.stringify(info);
  res.setHeader('Content-Type', 'application/json');
  res.setHeader('Content-Length', '' + body.length);
  res.end(body);
});

// Test req.path with query string (should exclude query)
app.get('/with-query', function(req: any, res: any) {
  var body = JSON.stringify({ path: req.path, url: req.url });
  res.setHeader('Content-Type', 'application/json');
  res.setHeader('Content-Length', '' + body.length);
  res.end(body);
});

const server = http.createServer(app);
server.listen(13016, '127.0.0.1', function() {
  console.log('listening on http://localhost:13016');
});
