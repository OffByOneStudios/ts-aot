// Fetch API Tests (JavaScript) - Self-contained with local HTTP server

var http = require('http');

async function user_main() {
  var passed = 0;
  var failed = 0;

  var server = http.createServer(function(req, res) {
    if (req.url === '/json') {
      res.writeHead(200, { 'Content-Type': 'application/json' });
      res.end('{"key":"value"}');
    } else {
      res.writeHead(200, { 'Content-Type': 'text/plain' });
      res.end('hello');
    }
  });

  server.listen(0, '127.0.0.1', async function() {
    var addr = server.address();
    var port = addr.port;
    var base = 'http://127.0.0.1:' + port;

    // Test 1: Basic fetch
    try {
      var response = await fetch(base + '/');
      if (response.ok) {
        console.log('PASS: fetch creates request');
        passed = passed + 1;
      } else {
        console.log('FAIL: fetch request failed');
        failed = failed + 1;
      }
    } catch (e) {
      console.log('FAIL: fetch threw exception');
      failed = failed + 1;
    }

    // Test 2: response.ok is boolean
    try {
      var response2 = await fetch(base + '/');
      if (typeof response2.ok === 'boolean') {
        console.log('PASS: response.ok is boolean');
        passed = passed + 1;
      } else {
        console.log('FAIL: response.ok not a boolean');
        failed = failed + 1;
      }
    } catch (e) {
      console.log('FAIL: response.ok threw exception');
      failed = failed + 1;
    }

    console.log('');
    console.log('Fetch JS Tests: ' + passed + ' passed, ' + failed + ' failed');

    server.close(function() {});
  });

  return 0;
}
