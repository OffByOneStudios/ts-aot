// Truthiness in boolean context
console.log(!!0, !!1, !!"", !!"x", !!null, !!undefined, !!NaN, !!{});

// Boolean conversion
console.log(Boolean(0), Boolean(1), Boolean(""), Boolean("x"));
console.log(Boolean(null), Boolean(undefined), Boolean({}), Boolean([]));

// Number conversion
console.log(Number(""));
console.log(Number(" 42 "));
console.log(Number("abc"));
console.log(Number(true));
console.log(Number(false));
console.log(Number(null));
console.log(Number(undefined));
console.log(Number([]));
console.log(Number([42]));

// String conversion
console.log(String(123));
console.log(String(true));
console.log(String(null));
console.log(String(undefined));
console.log(String([1, 2, 3]));
console.log(String({}));
