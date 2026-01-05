// Minimal lodash load test
console.log('Loading lodash...');
const _ = require('lodash');
console.log('Lodash loaded!');
console.log('Lodash type:', typeof _);
console.log('Has chunk?', typeof _.chunk);
console.log('Done!');
