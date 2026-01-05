// Test 105.11.5.18: typeof Checks
// Pattern: Runtime type detection

console.log("typeof undefined:", typeof undefined);
console.log("typeof null:", typeof null);
console.log("typeof 42:", typeof 42);
console.log("typeof 3.14:", typeof 3.14);
console.log("typeof 'hello':", typeof "hello");
console.log("typeof true:", typeof true);
console.log("typeof {}:", typeof {});
console.log("typeof []:", typeof []);
console.log("typeof function(){}:", typeof function(){});

// Variable tests
var undef;
var num = 42;
var str = "test";
var obj = {};
var arr = [];
var fn = function() {};

console.log("typeof undef:", typeof undef);
console.log("typeof num:", typeof num);
console.log("typeof str:", typeof str);
console.log("typeof obj:", typeof obj);
console.log("typeof arr:", typeof arr);
console.log("typeof fn:", typeof fn);

console.log("PASS: typeof");
