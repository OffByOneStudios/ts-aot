const express = require('../../npm/express/node_modules/express');
import * as http from 'http';

const app = express();

app.get('/search', function(req: any, res: any) {
  var q = req.query.q || '';
  var limit = req.query.limit || '10';
  var body = JSON.stringify({ q: q, limit: limit });
  res.setHeader('Content-Type', 'application/json');
  res.setHeader('Content-Length', '' + body.length);
  res.end(body);
});

app.get('/empty', function(req: any, res: any) {
  var body = JSON.stringify(req.query);
  res.setHeader('Content-Type', 'application/json');
  res.setHeader('Content-Length', '' + body.length);
  res.end(body);
});

const server = http.createServer(app);
server.listen(13002, function() {
  console.log('listening on http://localhost:13002');
});
