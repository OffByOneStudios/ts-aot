// Test loading actual lodash from node_modules
const _ = require('lodash');

console.log('=== Testing lodash from node_modules ===');
console.log('typeof require("lodash"):', typeof _);
console.log('has _.chunk:', typeof _.chunk);
console.log('has _.lodash:', typeof _.lodash);
console.log('has _.default:', typeof _.default);
console.log('has _.exports:', typeof _.exports);

// If exports are wrong, avoid crashing early so we can see shape.
if (typeof _.chunk !== 'function') {
	console.log('ERROR: lodash API missing (expected _.chunk).');
	console.log('Hint: if _.lodash is present, lodash likely took the UMD global-attach path.');
	console.log('Skipping API tests.');
} else {

// Test 1: _.chunk
console.log('\nTest 1: _.chunk([1, 2, 3, 4, 5], 2)');
const chunked = _.chunk([1, 2, 3, 4, 5], 2);
console.log('Result:', chunked);
console.log('Length:', chunked.length);

// Test 2: _.compact
console.log('\nTest 2: _.compact([0, 1, false, 2, "", 3])');
const compacted = _.compact([0, 1, false, 2, '', 3]);
console.log('Result:', compacted);

// Test 3: _.difference
console.log('\nTest 3: _.difference([2, 1], [2, 3])');
const diff = _.difference([2, 1], [2, 3]);
console.log('Result:', diff);

// Test 4: _.isEqual
console.log('\nTest 4: _.isEqual({a: 1}, {a: 1})');
const isEq1 = _.isEqual({a: 1}, {a: 1});
console.log('Result:', isEq1);

console.log('\nTest 5: _.isEqual({a: 1}, {a: 2})');
const isEq2 = _.isEqual({a: 1}, {a: 2});
console.log('Result:', isEq2);

// Test 6: _.map
console.log('\nTest 6: _.map([1, 2, 3], function(n) { return n * 2; })');
const mapped = _.map([1, 2, 3], function(n: number) { return n * 2; });
console.log('Result:', mapped);

// Test 7: _.filter
console.log('\nTest 7: _.filter([1, 2, 3, 4], function(n) { return n % 2 === 0; })');
const filtered = _.filter([1, 2, 3, 4], function(n: number) { return n % 2 === 0; });
console.log('Result:', filtered);

// Test 8: _.cloneDeep
console.log('\nTest 8: _.cloneDeep({a: {b: 1}})');
const original = {a: {b: 1}};
const cloned = _.cloneDeep(original);
console.log('Original.a.b:', original.a.b);
console.log('Cloned.a.b:', cloned.a.b);
original.a.b = 999;
console.log('After modifying original:');
console.log('Original.a.b:', original.a.b);
console.log('Cloned.a.b:', cloned.a.b);
}

console.log('\n=== All tests complete ===');
