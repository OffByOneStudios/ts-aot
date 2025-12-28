import { EventEmitter } from 'events';

const ee = new EventEmitter();

let count = 0;
ee.on('test', () => {
    count++;
    console.log('on test');
});

ee.once('once-test', () => {
    count++;
    console.log('once test');
});

console.log('Emitting test...');
ee.emit('test');
ee.emit('test');

console.log('Emitting once-test...');
ee.emit('once-test');
ee.emit('once-test');

console.log('Count:', count);

if (count !== 3) {
    console.log('FAILED: count should be 3');
    process.exit(1);
}

// Test prependListener
let order = '';
ee.on('order', () => { order += 'B'; });
ee.prependListener('order', () => { order += 'A'; });
ee.emit('order');
console.log('Order:', order);
if (order !== 'AB') {
    console.log('FAILED: order should be AB');
    process.exit(1);
}

// Test removeAllListeners
ee.removeAllListeners('test');
count = 0;
ee.emit('test');
if (count !== 0) {
    console.log('FAILED: count should be 0 after removeAllListeners');
    process.exit(1);
}

// Test error event
try {
    ee.emit('error', 'test error');
    console.log('FAILED: should have thrown');
    process.exit(1);
} catch (e) {
    console.log('Caught expected error:', e);
}

import * as events from 'events';
const p = events.once(ee, 'static-once');
p.then((args: any[]) => {
    console.log('static once resolved: ' + args[0]);
    if (args[0] === 'hello') {
        console.log('PASSED STATIC ONCE');
    } else {
        console.log('FAILED STATIC ONCE');
        process.exit(1);
    }
});
ee.emit('static-once', 'hello');

console.log('PASSED');
