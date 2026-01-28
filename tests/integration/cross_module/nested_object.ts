// Integration test: Cross-module nested property access
// Tests COMP-001: Passing objects with nested properties to imported functions

import { Result, getTimingMin, getTimingAvg, formatResult } from './nested_object_lib';

function user_main(): number {
    console.log('COMP-001: Cross-Module Nested Property Access Test');
    console.log('==================================================');
    console.log('');

    const result: Result = {
        name: 'test',
        iterations: 100,
        timing: {
            min: 0.5,
            max: 2.0,
            avg: 1.25
        }
    };

    // Direct access (should work)
    console.log('Test 1: Direct access');
    console.log('  result.timing.min = ' + result.timing.min);
    console.log('  result.timing.avg = ' + result.timing.avg);
    console.log('  PASS');
    console.log('');

    // Access through imported function (crashes in current compiler)
    console.log('Test 2: Access via imported getTimingMin()');
    const minVal = getTimingMin(result);
    console.log('  getTimingMin(result) = ' + minVal);
    console.log('  PASS');
    console.log('');

    console.log('Test 3: Access via imported getTimingAvg()');
    const avgVal = getTimingAvg(result);
    console.log('  getTimingAvg(result) = ' + avgVal);
    console.log('  PASS');
    console.log('');

    console.log('Test 4: Access via imported formatResult()');
    const formatted = formatResult(result);
    console.log('  formatResult(result) = ' + formatted);
    console.log('  PASS');
    console.log('');

    console.log('ALL TESTS PASSED');
    return 0;
}
