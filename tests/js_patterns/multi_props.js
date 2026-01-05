// Test 105.11.5.9: Multiple Object Properties (Lodash typedArrayTags pattern)
// Pattern: Building lookup table with string keys

var tags = {};
tags['[object Float32Array]'] = true;
tags['[object Float64Array]'] = true;
tags['[object Int8Array]'] = true;
tags['[object Int16Array]'] = true;
tags['[object Int32Array]'] = true;
tags['[object Uint8Array]'] = true;
tags['[object Uint16Array]'] = true;
tags['[object Uint32Array]'] = true;

console.log("Float32Array tag:", tags['[object Float32Array]']);
console.log("Int16Array tag:", tags['[object Int16Array]']);
console.log("Unknown tag:", tags['[object Unknown]']);

console.log("PASS: multi_props");
