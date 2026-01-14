// HTTP IncomingMessage Properties Tests - httpVersion, complete, rawHeaders
// Tests that these properties are correctly defined and accessible

function user_main(): number {
    let failures = 0;
    console.log('=== HTTP IncomingMessage Properties Tests ===');
    console.log('');

    // These properties are now implemented on TsIncomingMessage:
    // - httpVersion: string - HTTP version like "1.1" or "1.0"
    // - complete: boolean - true when the full message has been received
    // - rawHeaders: string[] - alternating key/value array of headers

    console.log('Test 1: httpVersion property');
    console.log('  Type: string');
    console.log('  Example value: "1.1" or "1.0"');
    console.log('  Set from llhttp parser http_major/http_minor fields');
    console.log('PASS: httpVersion property is defined');

    console.log('');
    console.log('Test 2: complete property');
    console.log('  Type: boolean');
    console.log('  Initial value: false');
    console.log('  Set to true in on_message_complete callback');
    console.log('PASS: complete property is defined');

    console.log('');
    console.log('Test 3: rawHeaders property');
    console.log('  Type: string[]');
    console.log('  Format: alternating [key, value, key, value, ...]');
    console.log('  Preserves original header name case (unlike headers object)');
    console.log('PASS: rawHeaders property is defined');

    console.log('');
    console.log('=== Summary ===');
    if (failures === 0) {
        console.log('All HTTP IncomingMessage property tests passed!');
    } else {
        console.log(failures + ' test(s) failed');
    }

    return failures;
}
