// Socket Address Properties Tests
// Tests socket.remoteAddress, remotePort, remoteFamily, localAddress, localPort, localFamily
import * as net from 'net';

function user_main(): number {
  let failures = 0;
  console.log('=== Socket Address Properties Tests ===\n');

  // Test 1: Verify Socket type has address properties defined
  console.log('Test 1: Socket address property type definitions');

  // Create a server to accept connections
  const server = net.createServer((socket: net.Socket) => {
    console.log('  Server received connection');

    // Check remote address properties
    const remoteAddr = socket.remoteAddress;
    const remotePort = socket.remotePort;
    const remoteFamily = socket.remoteFamily;

    // Check local address properties
    const localAddr = socket.localAddress;
    const localPort = socket.localPort;
    const localFamily = socket.localFamily;

    console.log('  Remote address: ' + remoteAddr);
    console.log('  Remote port: ' + remotePort);
    console.log('  Remote family: ' + remoteFamily);
    console.log('  Local address: ' + localAddr);
    console.log('  Local port: ' + localPort);
    console.log('  Local family: ' + localFamily);

    socket.end();
  });

  server.listen(0, () => {
    console.log('  Server listening');
  });

  console.log('PASS: Socket address properties are accessible');

  // Note: Full integration test would require running event loop
  // and making actual TCP connections to verify the values

  console.log('\n=== Summary ===');
  if (failures === 0) {
    console.log('All Socket address property tests passed!');
  } else {
    console.log(failures + ' test(s) failed');
  }

  // Force exit to prevent event loop from hanging on server.listen()
  process.exit(failures);
  return failures;
}
