// EventEmitter Basic Tests - JavaScript version
var events = require('events');
var EventEmitter = events.EventEmitter;

function user_main() {
  var failures = 0;
  console.log('=== EventEmitter Basic Tests (JS) ===\n');

  var ee = new EventEmitter();

  // Test 1: EventEmitter constructor
  try {
    if (!ee) {
      console.log('FAIL: EventEmitter not created');
      failures = failures + 1;
    } else {
      console.log('PASS: EventEmitter constructor');
    }
  } catch (e) {
    console.log('FAIL: EventEmitter constructor - Exception');
    failures = failures + 1;
  }

  // Test 2: on() registers listener
  try {
    var called = false;
    ee.on('test', function() {
      called = true;
    });
    ee.emit('test');
    if (!called) {
      console.log('FAIL: on() listener not called');
      failures = failures + 1;
    } else {
      console.log('PASS: on() registers listener');
    }
  } catch (e) {
    console.log('FAIL: on() - Exception');
    failures = failures + 1;
  }

  // Test 3: emit() with multiple listeners
  try {
    var count = 0;
    var ee2 = new EventEmitter();
    ee2.on('multi', function() { count = count + 1; });
    ee2.on('multi', function() { count = count + 1; });
    ee2.emit('multi');
    if (count !== 2) {
      console.log('FAIL: Multiple listeners wrong count');
      failures = failures + 1;
    } else {
      console.log('PASS: emit() with multiple listeners');
    }
  } catch (e) {
    console.log('FAIL: Multiple listeners - Exception');
    failures = failures + 1;
  }

  // Test 4: once() listener called only once
  try {
    var count2 = 0;
    var ee3 = new EventEmitter();
    ee3.once('once-test', function() { count2 = count2 + 1; });
    ee3.emit('once-test');
    ee3.emit('once-test');
    if (count2 !== 1) {
      console.log('FAIL: once() listener called more than once');
      failures = failures + 1;
    } else {
      console.log('PASS: once() listener called only once');
    }
  } catch (e) {
    console.log('FAIL: once() - Exception');
    failures = failures + 1;
  }

  // Test 5: emit() with arguments
  try {
    var receivedArg = 0;
    var ee7 = new EventEmitter();
    ee7.on('args', function(arg) {
      receivedArg = arg;
    });
    ee7.emit('args', 42);
    if (receivedArg !== 42) {
      console.log('FAIL: emit() arguments not passed');
      failures = failures + 1;
    } else {
      console.log('PASS: emit() with arguments');
    }
  } catch (e) {
    console.log('FAIL: emit arguments - Exception');
    failures = failures + 1;
  }

  // Test 6: listenerCount()
  try {
    var ee8 = new EventEmitter();
    ee8.on('count', function() {});
    ee8.on('count', function() {});
    var lcount = ee8.listenerCount('count');
    if (lcount !== 2) {
      console.log('FAIL: listenerCount wrong value');
      failures = failures + 1;
    } else {
      console.log('PASS: listenerCount()');
    }
  } catch (e) {
    console.log('FAIL: listenerCount - Exception');
    failures = failures + 1;
  }

  console.log('\n=== Summary ===');
  if (failures === 0) {
    console.log('All tests passed!');
  } else {
    console.log(failures + ' test(s) failed');
  }

  return failures;
}
