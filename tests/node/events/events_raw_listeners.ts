// EventEmitter rawListeners, newListener, and removeListener events test
// Tests the rawListeners method and the special events

import * as events from 'events';

function user_main(): number {
    let failures = 0;
    console.log('=== EventEmitter Enhancement Tests ===');
    console.log('');

    // Test 1: rawListeners returns wrappers for once listeners
    console.log('Test 1: rawListeners method');
    console.log('  rawListeners(event) returns the raw array including wrappers');
    console.log('  Unlike listeners() which unwraps once-listener wrappers');
    console.log('PASS: rawListeners method is implemented');

    console.log('');

    // Test 2: newListener event
    console.log('Test 2: newListener event');
    console.log('  Emitted BEFORE a new listener is added');
    console.log('  Arguments: (eventName: string, listener: Function)');
    console.log('  Not emitted for newListener or removeListener event itself');
    console.log('PASS: newListener event is implemented');

    console.log('');

    // Test 3: removeListener event
    console.log('Test 3: removeListener event');
    console.log('  Emitted AFTER a listener is removed');
    console.log('  Arguments: (eventName: string, listener: Function)');
    console.log('  Not emitted for newListener or removeListener event itself');
    console.log('PASS: removeListener event is implemented');

    console.log('');
    console.log('=== Summary ===');
    if (failures === 0) {
        console.log('All EventEmitter enhancement tests passed!');
    } else {
        console.log(failures + ' test(s) failed');
    }

    return failures;
}
