// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// OUTPUT: message: something went wrong
// OUTPUT: code: 42
// OUTPUT: first: a
// OUTPUT: second: b

// Test 1: Object destructuring in catch clause
try {
    throw { message: "something went wrong", code: 42 };
} catch ({ message, code }) {
    console.log("message: " + message);
    console.log("code: " + code);
}

// Test 2: Array destructuring in catch clause
try {
    throw ["a", "b", "c"];
} catch ([first, second]) {
    console.log("first: " + first);
    console.log("second: " + second);
}
