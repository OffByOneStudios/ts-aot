// Integration test: Cross-module callback receiving object
// Tests callback functions that receive object parameters

import { processWithCallback, transformData, Data } from './callback_with_object_lib';

function user_main(): number {
    console.log('Callback with Object Cross-Module Test');
    console.log('======================================');
    console.log('');

    const data: Data = {
        id: 42,
        value: 'test-value'
    };

    // Test 1: Callback receives object directly
    console.log('Test 1: Callback receives object');
    let callbackResult = '';
    processWithCallback(data, (d: Data) => {
        callbackResult = 'id=' + d.id + ', value=' + d.value;
    });
    console.log('  Callback result: ' + callbackResult);
    if (callbackResult === 'id=42, value=test-value') {
        console.log('  PASS');
    } else {
        console.log('  FAIL');
        return 1;
    }
    console.log('');

    // Test 2: Callback transforms object
    console.log('Test 2: Callback transforms object');
    const transformed = transformData(data, (d: Data) => {
        return d.value + '-' + d.id;
    });
    console.log('  transformData result: ' + transformed);
    if (transformed === 'test-value-42') {
        console.log('  PASS');
    } else {
        console.log('  FAIL');
        return 1;
    }
    console.log('');

    console.log('ALL TESTS PASSED');
    return 0;
}
