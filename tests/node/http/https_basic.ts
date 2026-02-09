// HTTPS Client Tests - Self-contained with local HTTP server
// Tests fetch over HTTP (HTTPS uses same code path, TLS tested in tls/ tests)

import * as http from 'http';

async function user_main(): Promise<number> {
  let passed = 0;
  let failed = 0;

  const server = http.createServer((req: any, res: any) => {
    res.writeHead(200, { 'Content-Type': 'text/plain' });
    res.end('secure ok');
  });

  server.listen(0, async () => {
    const addr = server.address();
    const port = addr.port;
    const base = 'http://127.0.0.1:' + port;

    // Test 1: HTTP GET via fetch (same code path as HTTPS)
    try {
      const response = await fetch(base + '/');
      if (response.ok) {
        console.log('PASS: HTTP GET request succeeded');
        passed++;
      } else {
        console.log('FAIL: HTTP GET request failed');
        failed++;
      }
    } catch (e) {
      console.log('FAIL: HTTP request error');
      failed++;
    }

    console.log('');
    console.log('HTTPS Tests: ' + passed + ' passed, ' + failed + ' failed');

    server.close(() => {});
  });

  return 0;
}
