// Pattern Test: EventEmitter
// Tests EventEmitter with typed events across modules

import { EventEmitter } from 'events';
import { createCounter, Counter } from './event_emitter_lib';

function user_main(): number {
    console.log('Pattern Test: EventEmitter');
    console.log('==========================');
    console.log('');

    // Test 1: Basic event emission
    console.log('Test 1: Basic EventEmitter');
    const emitter = new EventEmitter();
    let receivedData = '';

    emitter.on('data', (msg: string) => {
        receivedData = msg;
    });

    emitter.emit('data', 'Hello');
    console.log('  Received: ' + receivedData);
    if (receivedData === 'Hello') {
        console.log('  PASS');
    } else {
        console.log('  FAIL');
        return 1;
    }
    console.log('');

    // Test 2: Counter from imported module
    console.log('Test 2: Counter with events');
    const counter = createCounter();
    let lastValue = -1;

    counter.on('change', (value: number) => {
        lastValue = value;
    });

    counter.increment();
    console.log('  After increment: ' + lastValue);

    counter.increment();
    console.log('  After second increment: ' + lastValue);

    counter.decrement();
    console.log('  After decrement: ' + lastValue);

    if (lastValue === 1) {
        console.log('  PASS');
    } else {
        console.log('  FAIL: Expected 1, got ' + lastValue);
        return 1;
    }
    console.log('');

    console.log('ALL TESTS PASSED');
    return 0;
}
