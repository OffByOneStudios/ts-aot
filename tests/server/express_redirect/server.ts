const express = require('../../npm/express/node_modules/express');
import * as http from 'http';

const app = express();

// 302 redirect (default)
app.get('/old', function(req: any, res: any) {
  res.redirect('/new');
});

// 301 permanent redirect
app.get('/moved', function(req: any, res: any) {
  res.redirect(301, '/destination');
});

// Redirect target
app.get('/new', function(req: any, res: any) {
  var body = JSON.stringify({ page: 'new' });
  res.setHeader('Content-Type', 'application/json');
  res.setHeader('Content-Length', '' + body.length);
  res.end(body);
});

app.get('/destination', function(req: any, res: any) {
  var body = JSON.stringify({ page: 'destination' });
  res.setHeader('Content-Type', 'application/json');
  res.setHeader('Content-Length', '' + body.length);
  res.end(body);
});

const server = http.createServer(app);
server.listen(13011, function() {
  console.log('listening on http://localhost:13011');
});
