const express = require('../../npm/express/node_modules/express');
import * as http from 'http';

const app = express();

app.get('/item', function(req: any, res: any) {
  var body = JSON.stringify({ method: 'GET' });
  res.setHeader('Content-Type', 'application/json');
  res.setHeader('Content-Length', '' + body.length);
  res.end(body);
});

app.post('/item', function(req: any, res: any) {
  res.statusCode = 201;
  var body = JSON.stringify({ method: 'POST' });
  res.setHeader('Content-Type', 'application/json');
  res.setHeader('Content-Length', '' + body.length);
  res.end(body);
});

app.put('/item', function(req: any, res: any) {
  var body = JSON.stringify({ method: 'PUT' });
  res.setHeader('Content-Type', 'application/json');
  res.setHeader('Content-Length', '' + body.length);
  res.end(body);
});

app.delete('/item', function(req: any, res: any) {
  res.statusCode = 204;
  res.end();
});

app.patch('/item', function(req: any, res: any) {
  var body = JSON.stringify({ method: 'PATCH' });
  res.setHeader('Content-Type', 'application/json');
  res.setHeader('Content-Length', '' + body.length);
  res.end(body);
});

const server = http.createServer(app);
server.listen(13003, function() {
  console.log('listening on http://localhost:13003');
});
