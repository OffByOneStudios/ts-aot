const express = require('../../npm/express/node_modules/express');
import * as http from 'http';

const app = express();

// res.cookie() — sets Set-Cookie header
app.get('/set', function(req: any, res: any) {
  res.cookie('name', 'value');
  res.json({ ok: true });
});

// res.cookie() with options
app.get('/set-opts', function(req: any, res: any) {
  res.cookie('session', 'abc123', { httpOnly: true, path: '/api' });
  res.json({ ok: true });
});

// res.clearCookie() — sets expired cookie
app.get('/clear', function(req: any, res: any) {
  res.clearCookie('name');
  res.json({ ok: true });
});

// Multiple cookies
app.get('/multi', function(req: any, res: any) {
  res.cookie('a', '1');
  res.cookie('b', '2');
  res.json({ ok: true });
});

const server = http.createServer(app);
server.listen(13024, '127.0.0.1', function() {
  console.log('listening on http://localhost:13024');
});
