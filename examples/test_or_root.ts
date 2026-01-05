// Test the lodash root pattern
const freeGlobal = { type: 'global' };
const freeSelf = null;
const func = Function;

console.log('freeGlobal:', typeof freeGlobal);
console.log('freeSelf:', typeof freeSelf);

const root = freeGlobal || freeSelf || func;
console.log('root:', typeof root);
console.log('root value:', root);

// Test assignment like lodash does
interface RootType {
  _?: any;
}

const typedRoot = root as RootType;
typedRoot._ = { name: 'lodash' };
console.log('Assigned _.name:', typedRoot._.name);
