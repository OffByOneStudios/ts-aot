const express = require('../../../tests/npm/express/node_modules/express');
import * as http from 'http';

const app = express();

app.use(function(req: any, res: any, next: any) {
  console.log(req.method + ' ' + req.url);
  next();
});

app.get('/', function(req: any, res: any) {
  res.writeHead(200, {'Content-Type': 'application/json'});
  res.end('{"message":"Hello from ts-aot Express!","status":"ok"}');
});

app.get('/health', function(req: any, res: any) {
  res.writeHead(200, {'Content-Type': 'application/json'});
  res.end('{"healthy":true}');
});

const server = http.createServer(app);
server.listen(3000, function() {
  console.log('Express listening on http://localhost:3000');
});
