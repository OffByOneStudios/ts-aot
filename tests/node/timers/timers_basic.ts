// Timer Module Basic Tests

function user_main(): number {
  let failures = 0;
  console.log('=== Timers Module Tests ===\n');

  // Test 1: setTimeout basic
  console.log('Test 1: setTimeout - Starting');
  setTimeout(() => {
    console.log('PASS: setTimeout fired (100ms)');
  }, 100);

  // Test 2: Multiple setTimeout with different delays
  setTimeout(() => {
    console.log('PASS: setTimeout 2 fired (50ms)');
  }, 50);

  setTimeout(() => {
    console.log('PASS: setTimeout 3 fired (150ms)');
  }, 150);

  // Test 3: setInterval with clearInterval
  // Note: interval must be declared before the callback due to closure capture bug
  let count = 0;
  let interval: number = 0;
  interval = setInterval(() => {
    count++;
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
