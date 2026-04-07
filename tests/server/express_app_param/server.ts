const express = require('../../npm/express/node_modules/express');
import * as http from 'http';

const app = express();

// app.param() preprocesses route parameters before handler runs
app.param('userId', function(req: any, res: any, next: any, val: any) {
  // Convert to uppercase and attach to req
  req.userId = val.toUpperCase();
  next();
});

app.get('/user/:userId', function(req: any, res: any) {
  res.json({ id: req.params.userId, processed: req.userId });
});

// Multiple param handlers
app.param('itemId', function(req: any, res: any, next: any, val: any) {
  req.itemId = parseInt(val, 10);
  if (isNaN(req.itemId)) {
    res.status(400).json({ error: 'invalid item id' });
  } else {
    next();
  }
});

app.get('/item/:itemId', function(req: any, res: any) {
  res.json({ id: req.itemId, type: typeof req.itemId });
});

const server = http.createServer(app);
server.listen(13021, '127.0.0.1', function() {
  console.log('listening on http://localhost:13021');
});
