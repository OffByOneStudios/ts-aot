const express = require('../../npm/express/node_modules/express');
import * as http from 'http';

const app = express();

app.get('/', function(req: any, res: any) {
  res.setHeader('X-Custom-Header', 'custom-value');
  res.setHeader('X-Request-Id', '12345');
  res.setHeader('Cache-Control', 'no-cache');
  res.setHeader('Content-Type', 'text/plain');
  var body = 'OK';
  res.setHeader('Content-Length', '' + body.length);
  res.end(body);
});

const server = http.createServer(app);
server.listen(13007, '127.0.0.1', function() {
  console.log('listening on http://localhost:13007');
});
