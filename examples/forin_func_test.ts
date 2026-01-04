// Test for-in on function object
function myFunc() { return 1; }
(myFunc as any).customProp = 'hello';

console.log("Before for-in");
for (var k in myFunc) {
    console.log("Key:", k);
}
console.log("After for-in");
