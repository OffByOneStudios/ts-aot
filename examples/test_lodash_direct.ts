// Direct lodash test - no wrapper function
const _ = require('lodash');

console.log('typeof _ =', typeof _);

console.log('Test: _.identity(2)');
const r1 = _.identity(2);
console.log('r1 =', r1);

console.log('Test: _.chunk([1,2,3,4], 2)');
const arr = [1, 2, 3, 4];
const r2 = _.chunk(arr, 2);
console.log('r2 =', r2);
