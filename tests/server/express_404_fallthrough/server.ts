const express = require('../../npm/express/node_modules/express');
import * as http from 'http';

const app = express();

app.get('/exists', function(req: any, res: any) {
  var body = JSON.stringify({ found: true });
  res.setHeader('Content-Type', 'application/json');
  res.setHeader('Content-Length', '' + body.length);
  res.end(body);
});

// Catch-all 404 handler — must be last
app.use(function(req: any, res: any) {
  res.statusCode = 404;
  var body = JSON.stringify({ error: 'Not Found', path: req.url });
  res.setHeader('Content-Type', 'application/json');
  res.setHeader('Content-Length', '' + body.length);
  res.end(body);
});

const server = http.createServer(app);
server.listen(13010, function() {
  console.log('listening on http://localhost:13010');
});
