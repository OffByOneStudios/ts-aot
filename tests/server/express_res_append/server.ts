const express = require('../../npm/express/node_modules/express');
import * as http from 'http';

const app = express();

// Test res.append() — accumulate multi-value headers
app.get('/multi', function(req: any, res: any) {
  res.append('X-Custom', 'one');
  res.append('X-Custom', 'two');
  res.json({ ok: true });
});

// Test res.append() with Set-Cookie (most common real-world use)
app.get('/cookies', function(req: any, res: any) {
  res.append('Set-Cookie', 'a=1; Path=/');
  res.append('Set-Cookie', 'b=2; Path=/');
  res.json({ ok: true });
});

// Test res.append() with Vary header
app.get('/vary', function(req: any, res: any) {
  res.append('Vary', 'Accept');
  res.append('Vary', 'Accept-Encoding');
  res.json({ ok: true });
});

const server = http.createServer(app);
server.listen(13020, '127.0.0.1', function() {
  console.log('listening on http://localhost:13020');
});
