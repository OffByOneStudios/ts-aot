const express = require('../../npm/express/node_modules/express');
import * as http from 'http';

const app = express();

// app.route() — chainable route definition
app.route('/resource')
  .get(function(req: any, res: any) {
    res.json({ method: 'GET', action: 'list' });
  })
  .post(function(req: any, res: any) {
    res.status(201).json({ method: 'POST', action: 'create' });
  })
  .put(function(req: any, res: any) {
    res.json({ method: 'PUT', action: 'update' });
  })
  .delete(function(req: any, res: any) {
    res.status(204).end();
  });

const server = http.createServer(app);
server.listen(13022, '127.0.0.1', function() {
  console.log('listening on http://localhost:13022');
});
