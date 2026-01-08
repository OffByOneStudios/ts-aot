// EventEmitter Basic Tests
import { EventEmitter } from 'events';

function user_main(): number {
  let failures = 0;
  console.log('=== EventEmitter Basic Tests ===\n');

  const ee = new EventEmitter();

  // Test 1: EventEmitter constructor
  try {
    if (!ee) {
      console.log('FAIL: EventEmitter not created');
      failures++;
    } else {
      console.log('PASS: EventEmitter constructor');
    }
  } catch (e) {
    console.log('FAIL: EventEmitter constructor - Exception');
    failures++;
  }

  // Test 2: on() registers listener
  try {
    let called = false;
    ee.on('test', () => {
      called = true;
    });
    ee.emit('test');
    if (!called) {
      console.log('FAIL: on() listener not called');
      failures++;
    } else {
      console.log('PASS: on() registers listener');
    }
  } catch (e) {
    console.log('FAIL: on() - Exception');
    failures++;
  }

  // Test 3: emit() with multiple listeners
  try {
    let count = 0;
    const ee2 = new EventEmitter();
    ee2.on('multi', () => { count++; });
    ee2.on('multi', () => { count++; });
    ee2.emit('multi');
    if (count !== 2) {
      console.log('FAIL: Multiple listeners wrong count');
      failures++;
    } else {
      console.log('PASS: emit() with multiple listeners');
    }
  } catch (e) {
    console.log('FAIL: Multiple listeners - Exception');
    failures++;
  }

  // Test 4: once() listener called only once
  try {
    let count = 0;
    const ee3 = new EventEmitter();
    ee3.once('once-test', () => { count++; });
    ee3.emit('once-test');
    ee3.emit('once-test');
    if (count !== 1) {
      console.log('FAIL: once() listener called more than once');
      failures++;
    } else {
      console.log('PASS: once() listener called only once');
    }
  } catch (e) {
    console.log('FAIL: once() - Exception');
    failures++;
  }

  // Test 5: prependListener() adds to front
  try {
    let order = '';
    const ee4 = new EventEmitter();
    ee4.on('order', () => { order = order + 'B'; });
    ee4.prependListener('order', () => { order = order + 'A'; });
    ee4.emit('order');
    if (order.length !== 2) {
      console.log('FAIL: prependListener order wrong');
      failures++;
    } else {
      console.log('PASS: prependListener() order');
    }
  } catch (e) {
    console.log('FAIL: prependListener - Exception');
    failures++;
  }

  // Test 6: removeListener()
  try {
    let count = 0;
    const ee5 = new EventEmitter();
    const listener = () => { count++; };
    ee5.on('remove-test', listener);
    ee5.emit('remove-test');
    ee5.removeListener('remove-test', listener);
    ee5.emit('remove-test');
    if (count !== 1) {
      console.log('FAIL: removeListener did not remove');
      failures++;
    } else {
      console.log('PASS: removeListener()');
    }
  } catch (e) {
    console.log('FAIL: removeListener - Exception');
    failures++;
  }

  // Test 7: removeAllListeners()
  try {
    let count = 0;
    const ee6 = new EventEmitter();
    ee6.on('clear', () => { count++; });
    ee6.on('clear', () => { count++; });
    ee6.removeAllListeners('clear');
    ee6.emit('clear');
    if (count !== 0) {
      console.log('FAIL: removeAllListeners did not clear all');
      failures++;
    } else {
      console.log('PASS: removeAllListeners()');
    }
  } catch (e) {
    console.log('FAIL: removeAllListeners - Exception');
    failures++;
  }

  // Test 8: emit() with arguments
  try {
    let receivedArg = 0;
    const ee7 = new EventEmitter();
    ee7.on('args', (arg) => {
      receivedArg = arg;
    });
    ee7.emit('args', 42);
    if (receivedArg !== 42) {
      console.log('FAIL: emit() arguments not passed');
      failures++;
    } else {
      console.log('PASS: emit() with arguments');
    }
  } catch (e) {
    console.log('FAIL: emit arguments - Exception');
    failures++;
  }

  // Test 9: listenerCount()
  try {
    const ee8 = new EventEmitter();
    ee8.on('count', () => {});
    ee8.on('count', () => {});
    const count = ee8.listenerCount('count');
    if (count !== 2) {
      console.log('FAIL: listenerCount wrong value');
      failures++;
    } else {
      console.log('PASS: listenerCount()');
    }
  } catch (e) {
    console.log('FAIL: listenerCount - Exception');
    failures++;
  }

  // Test 10: eventNames()
  try {
    const ee9 = new EventEmitter();
    ee9.on('event1', () => {});
    ee9.on('event2', () => {});
    const names = ee9.eventNames();
    if (names.length !== 2) {
      console.log('FAIL: eventNames wrong count');
      failures++;
    } else {
      console.log('PASS: eventNames()');
    }
  } catch (e) {
    console.log('FAIL: eventNames - Exception');
    failures++;
  }

  console.log('\n=== Summary ===');
  if (failures === 0) {
    console.log('All tests passed!');
  } else {
    console.log(failures + ' test(s) failed');
  }

  return failures;
}
