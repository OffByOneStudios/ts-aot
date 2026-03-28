const express = require('../../npm/express/node_modules/express');
import * as http from 'http';

const app = express();

// Path-scoped middleware: only fires for /api/*
app.use('/api', function(req: any, res: any, next: any) {
  req.isAPI = true;
  next();
});

app.get('/api/data', function(req: any, res: any) {
  var body = JSON.stringify({ isAPI: req.isAPI === true });
  res.setHeader('Content-Type', 'application/json');
  res.setHeader('Content-Length', '' + body.length);
  res.end(body);
});

app.get('/web', function(req: any, res: any) {
  var isAPI = req.isAPI === true;
  var body = JSON.stringify({ isAPI: isAPI });
  res.setHeader('Content-Type', 'application/json');
  res.setHeader('Content-Length', '' + body.length);
  res.end(body);
});

const server = http.createServer(app);
server.listen(13006, function() {
  console.log('listening on http://localhost:13006');
});
