// Minimal lodash load test
console.log('Loading lodash...');
const _ = require('lodash');
console.log('Lodash loaded!');
console.log('Lodash type:', typeof _);
console.log('Has chunk?', typeof _.chunk);
if (typeof _.chunk === 'function') {
	const out = _.chunk([1, 2, 3, 4], 2);
	console.log('chunk([1,2,3,4],2)=', JSON.stringify(out));
}
console.log('Done!');
