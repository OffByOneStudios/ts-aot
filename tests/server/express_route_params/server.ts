const express = require('../../npm/express/node_modules/express');
import * as http from 'http';

const app = express();

app.get('/user/:id', function(req: any, res: any) {
  var body = JSON.stringify({ id: req.params.id });
  res.setHeader('Content-Type', 'application/json');
  res.setHeader('Content-Length', '' + body.length);
  res.end(body);
});

app.get('/post/:postId/comment/:commentId', function(req: any, res: any) {
  var body = JSON.stringify({ postId: req.params.postId, commentId: req.params.commentId });
  res.setHeader('Content-Type', 'application/json');
  res.setHeader('Content-Length', '' + body.length);
  res.end(body);
});

const server = http.createServer(app);
server.listen(13001, function() {
  console.log('listening on http://localhost:13001');
});
