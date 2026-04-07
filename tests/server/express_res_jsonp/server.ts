const express = require('../../npm/express/node_modules/express');
import * as http from 'http';

const app = express();

// res.jsonp() without callback — should behave like res.json()
app.get('/data', function(req: any, res: any) {
  res.jsonp({ value: 123 });
});

// res.jsonp() with callback query param
// GET /wrapped?callback=myFunc should return: /**/ typeof myFunc === 'function' && myFunc({"value":123});
app.get('/wrapped', function(req: any, res: any) {
  res.jsonp({ value: 123 });
});

const server = http.createServer(app);
server.listen(13023, '127.0.0.1', function() {
  console.log('listening on http://localhost:13023');
});
