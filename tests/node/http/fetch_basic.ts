// Fetch API Tests - Self-contained with local HTTP server

import * as http from 'http';

async function user_main(): Promise<number> {
  let passed = 0;
  let failed = 0;

  const server = http.createServer((req: any, res: any) => {
    if (req.url === '/json') {
      res.writeHead(200, { 'Content-Type': 'application/json' });
      res.end('{"key":"value","num":42}');
    } else if (req.url === '/post') {
      let body = '';
      req.on('data', (chunk: any) => {
        body = body + chunk.toString();
      });
      req.on('end', () => {
        res.writeHead(200, { 'Content-Type': 'text/plain' });
        res.end('received: ' + body);
      });
    } else {
      res.writeHead(200, { 'Content-Type': 'text/plain' });
      res.end('hello world');
    }
  });

  server.listen(0, '127.0.0.1', async () => {
    const addr = server.address();
    const port = addr.port;
    const base = 'http://127.0.0.1:' + port;

    // Test 1: Basic fetch
    try {
      const response = await fetch(base + '/');
      if (response) {
        console.log('PASS: fetch creates request');
        passed++;
      } else {
        console.log('FAIL: fetch did not return response');
        failed++;
      }
    } catch (e) {
      console.log('FAIL: fetch threw exception');
      failed++;
    }

    // Test 2: response.ok
    try {
      const response = await fetch(base + '/');
      if (response.ok === true) {
        console.log('PASS: response.ok is true');
        passed++;
      } else {
        console.log('FAIL: response.ok is not true');
        failed++;
      }
    } catch (e) {
      console.log('FAIL: response.ok threw exception');
      failed++;
    }

    // Test 3: response.status
    try {
      const response = await fetch(base + '/');
      if (response.status === 200) {
        console.log('PASS: response.status is 200');
        passed++;
      } else {
        console.log('FAIL: response.status is ' + response.status);
        failed++;
      }
    } catch (e) {
      console.log('FAIL: response.status threw exception');
      failed++;
    }

    // Test 4: response.text()
    try {
      const response = await fetch(base + '/');
      const text = await response.text();
      if (text === 'hello world') {
        console.log('PASS: response.text() returns correct body');
        passed++;
      } else {
        console.log('FAIL: response.text() returned: ' + text);
        failed++;
      }
    } catch (e) {
      console.log('FAIL: response.text() threw exception');
      failed++;
    }

    // Test 5: response.json()
    try {
      const response = await fetch(base + '/json');
      const data = await response.json();
      if (data.key === 'value') {
        console.log('PASS: response.json() parses correctly');
        passed++;
      } else {
        console.log('FAIL: response.json() did not parse correctly');
        failed++;
      }
    } catch (e) {
      console.log('FAIL: response.json() threw exception');
      failed++;
    }

    // Test 6: fetch with POST method
    try {
      const response = await fetch(base + '/post', {
        method: 'POST',
        body: 'test data'
      });
      const text = await response.text();
      if (text === 'received: test data') {
        console.log('PASS: fetch POST with body');
        passed++;
      } else {
        console.log('FAIL: POST body was: ' + text);
        failed++;
      }
    } catch (e) {
      console.log('FAIL: fetch POST threw exception');
      failed++;
    }

    // Test 7: fetch with custom headers
    try {
      const response = await fetch(base + '/', {
        method: 'GET',
        headers: {
          'X-Custom': 'test-value'
        }
      });
      if (response.ok) {
        console.log('PASS: fetch with custom headers');
        passed++;
      } else {
        console.log('FAIL: fetch with headers not ok');
        failed++;
      }
    } catch (e) {
      console.log('FAIL: fetch with headers threw exception');
      failed++;
    }

    console.log('');
    console.log('Fetch Tests: ' + passed + ' passed, ' + failed + ' failed');

    server.close(() => {});
  });

  return 0;
}
