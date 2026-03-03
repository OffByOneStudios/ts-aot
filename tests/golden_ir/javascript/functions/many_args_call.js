// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// OUTPUT: call7: 28
// OUTPUT: call8: 36
// OUTPUT: method7: 28
// OUTPUT: method8: 36

function sum7(a, b, c, d, e, f, g) {
    return a + b + c + d + e + f + g;
}

function sum8(a, b, c, d, e, f, g, h) {
    return a + b + c + d + e + f + g + h;
}

console.log("call7: " + sum7(1, 2, 3, 4, 5, 6, 7));
console.log("call8: " + sum8(1, 2, 3, 4, 5, 6, 7, 8));

// Also test method calls with many args via .call()
var obj = {
    sum7: function(a, b, c, d, e, f, g) {
        return a + b + c + d + e + f + g;
    },
    sum8: function(a, b, c, d, e, f, g, h) {
        return a + b + c + d + e + f + g + h;
    }
};

console.log("method7: " + obj.sum7(1, 2, 3, 4, 5, 6, 7));
console.log("method8: " + obj.sum8(1, 2, 3, 4, 5, 6, 7, 8));
