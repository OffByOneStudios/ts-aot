// Test EventEmitter.listenerCount static method (deprecated but still available)
import { EventEmitter } from 'events';
import * as events from 'events';

function user_main(): number {
    let passed = 0;
    let failed = 0;

    const emitter = new EventEmitter();

    // Test static method with no listeners
    const count0 = events.EventEmitter.listenerCount(emitter, 'test');
    if (count0 === 0) {
        console.log('PASS: static listenerCount returns 0 for no listeners');
        passed++;
    } else {
        console.log('FAIL: static listenerCount should return 0, got ' + count0);
        failed++;
    }

    // Add a listener
    emitter.on('test', () => {});

    // Test static method with one listener
    const count1 = events.EventEmitter.listenerCount(emitter, 'test');
    if (count1 === 1) {
        console.log('PASS: static listenerCount returns 1 after adding one listener');
        passed++;
    } else {
        console.log('FAIL: static listenerCount should return 1, got ' + count1);
        failed++;
    }

    console.log('Tests: ' + passed + ' passed, ' + failed + ' failed');
    return failed;
}
