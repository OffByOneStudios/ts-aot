const express = require('../../npm/express/node_modules/express');
import * as http from 'http';

const app = express();

app.get('/ok', function(req: any, res: any) {
  res.statusCode = 200;
  res.end('OK');
});

app.get('/created', function(req: any, res: any) {
  res.statusCode = 201;
  res.end('Created');
});

app.get('/no-content', function(req: any, res: any) {
  res.statusCode = 204;
  res.end();
});

app.get('/bad-request', function(req: any, res: any) {
  res.statusCode = 400;
  res.end('Bad Request');
});

app.get('/not-found', function(req: any, res: any) {
  res.statusCode = 404;
  res.end('Not Found');
});

app.get('/server-error', function(req: any, res: any) {
  res.statusCode = 500;
  res.end('Internal Server Error');
});

const server = http.createServer(app);
server.listen(13004, '127.0.0.1', function() {
  console.log('listening on http://localhost:13004');
});
