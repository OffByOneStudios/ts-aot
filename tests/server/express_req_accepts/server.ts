const express = require('../../npm/express/node_modules/express');
import * as http from 'http';

const app = express();

// req.accepts() — content negotiation
app.get('/negotiate', function(req: any, res: any) {
  var type = req.accepts('json', 'html', 'text');
  if (type === 'json') {
    res.json({ format: 'json' });
  } else if (type === 'html') {
    res.type('html').send('<p>html</p>');
  } else if (type === 'text') {
    res.type('text').send('text');
  } else {
    res.status(406).send('Not Acceptable');
  }
});

// req.is() — content type checking
app.post('/check-type', function(req: any, res: any) {
  if (req.is('application/json')) {
    res.json({ isJson: true });
  } else if (req.is('text/*')) {
    res.json({ isText: true });
  } else {
    res.json({ isJson: false, isText: false });
  }
});

const server = http.createServer(app);
server.listen(13025, function() {
  console.log('listening on http://localhost:13025');
});
