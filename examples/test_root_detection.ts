// Mimic lodash root detection
const freeGlobal = typeof global == 'object' && global && global.Object === Object && global;
console.log('freeGlobal:', typeof freeGlobal, freeGlobal !== undefined);

const freeSelf = typeof self == 'object' && self && self.Object === Object && self;
console.log('freeSelf:', typeof freeSelf, freeSelf !== undefined);

const root = freeGlobal || freeSelf;
console.log('root:', typeof root, root !== undefined);

// Try to set a property on root
if (root) {
    console.log('Setting root.testProp');
    root.testProp = 'hello';
    console.log('root.testProp:', root.testProp);
} else {
    console.log('root is falsy!');
}
