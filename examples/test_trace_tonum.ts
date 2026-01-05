// Deep trace of toNumber
const _ = require('./lodash_full.js');

function trace(v: any): void {
    console.log('--- trace ---');
    console.log('v:', v);
    console.log('typeof v:', typeof v);
    console.log("typeof v == 'number':", typeof v == 'number');
    
    // If typeof v == 'number', toNumber should return v immediately
    // Let's see if that's what happens
    console.log('_.toNumber(v):', _.toNumber(v));
    console.log('--- end trace ---');
}

console.log('Test 1: trace(2)');
trace(2);

console.log('\nTest 2: Direct lodash call');
console.log('_.toNumber(2):', _.toNumber(2));

console.log('\nTest 3: Using a typed number');
const n: number = 2;
console.log('_.toNumber(n):', _.toNumber(n));
