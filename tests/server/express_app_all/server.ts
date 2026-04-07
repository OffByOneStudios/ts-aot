const express = require('../../npm/express/node_modules/express');
import * as http from 'http';

const app = express();

// app.all() matches any HTTP method
app.all('/any', function(req: any, res: any) {
  var body = JSON.stringify({ method: req.method });
  res.setHeader('Content-Type', 'application/json');
  res.setHeader('Content-Length', '' + body.length);
  res.end(body);
});

const server = http.createServer(app);
server.listen(13013, '127.0.0.1', function() {
  console.log('listening on http://localhost:13013');
});
