const _ = require('./lodash_full.js');

// Test the functions lodash uses internally
const arr = [1, 2, 3, 4, 5, 6];
const size = 2;

console.log('Array:', JSON.stringify(arr));
console.log('size:', size);

// Test toInteger
console.log('\n=== Testing _.toInteger ===');
console.log('_.toInteger(2):', _.toInteger(2));
console.log('_.toInteger("2"):', _.toInteger("2"));

// Test Math.max
console.log('\n=== Testing Math.max ===');
console.log('Math.max(2, 0):', Math.max(2, 0));

// Test array == null check
console.log('\n=== Testing null checks ===');
console.log('arr == null:', arr == null);
console.log('arr === null:', arr === null);

// Test array.length
console.log('\n=== Testing array.length ===');
console.log('arr.length:', arr.length);

// Check if the condition that would cause [] is met
const length = arr == null ? 0 : arr.length;
const processedSize = Math.max(_.toInteger(size), 0);
console.log('\nlength =', length);
console.log('processedSize =', processedSize);
console.log('!length =', !length);
console.log('processedSize < 1 =', processedSize < 1);

if (!length || processedSize < 1) {
    console.log('WOULD RETURN [] (early exit)');
} else {
    console.log('WOULD CONTINUE (no early exit)');
}

// Now test chunk
console.log('\n=== Final _.chunk test ===');
const result = _.chunk(arr, 2);
console.log('_.chunk(arr, 2):', JSON.stringify(result));
