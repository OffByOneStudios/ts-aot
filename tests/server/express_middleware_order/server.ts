const express = require('../../npm/express/node_modules/express');
import * as http from 'http';

const app = express();

// First middleware: add a tag
app.use(function(req: any, res: any, next: any) {
  req.tags = ['first'];
  next();
});

// Second middleware: add another tag
app.use(function(req: any, res: any, next: any) {
  req.tags.push('second');
  next();
});

// Third middleware: add another tag
app.use(function(req: any, res: any, next: any) {
  req.tags.push('third');
  next();
});

app.get('/', function(req: any, res: any) {
  var body = JSON.stringify({ tags: req.tags });
  res.setHeader('Content-Type', 'application/json');
  res.setHeader('Content-Length', '' + body.length);
  res.end(body);
});

const server = http.createServer(app);
server.listen(13005, function() {
  console.log('listening on http://localhost:13005');
});
