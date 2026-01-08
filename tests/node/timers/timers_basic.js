// Timer Module Basic Tests - JavaScript version

function user_main() {
  var failures = 0;
  console.log('=== Timers Module Tests (JS) ===\n');

  // Test 1: setTimeout basic
  console.log('Test 1: setTimeout - Starting');
  setTimeout(function() {
    console.log('PASS: setTimeout fired (100ms)');
  }, 100);

  // Test 2: Multiple setTimeout with different delays
  setTimeout(function() {
    console.log('PASS: setTimeout 2 fired (50ms)');
  }, 50);

  setTimeout(function() {
    console.log('PASS: setTimeout 3 fired (150ms)');
  }, 150);

  // Test 3: setInterval with clearInterval
  var count = 0;
  var interval = setInterval(function() {
    count = count + 1;
    console.log('PASS: setInterval iteration ' + count);
    if (count === 3) {
      clearInterval(interval);
      console.log('PASS: clearInterval called');
    }
  }, 30);

  console.log('All timer tests scheduled\n');
  console.log('Note: This test requires event loop to complete');

  return 0;
}
