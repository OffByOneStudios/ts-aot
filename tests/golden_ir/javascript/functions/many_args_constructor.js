// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// OUTPUT: sum4: 10
// OUTPUT: sum5: 15
// OUTPUT: sum6: 21
// OUTPUT: sum7: 28
// OUTPUT: sum8: 36

// Test: ES6 classes with 4-8 constructor arguments
// Exercises ts_new_from_constructor_4 through _8

class Box4 {
    constructor(a, b, c, d) {
        this.sum = a + b + c + d;
    }
}

class Box5 {
    constructor(a, b, c, d, e) {
        this.sum = a + b + c + d + e;
    }
}

class Box6 {
    constructor(a, b, c, d, e, f) {
        this.sum = a + b + c + d + e + f;
    }
}

class Box7 {
    constructor(a, b, c, d, e, f, g) {
        this.sum = a + b + c + d + e + f + g;
    }
}

class Box8 {
    constructor(a, b, c, d, e, f, g, h) {
        this.sum = a + b + c + d + e + f + g + h;
    }
}

var r4 = new Box4(1, 2, 3, 4);
console.log("sum4: " + r4.sum);

var r5 = new Box5(1, 2, 3, 4, 5);
console.log("sum5: " + r5.sum);

var r6 = new Box6(1, 2, 3, 4, 5, 6);
console.log("sum6: " + r6.sum);

var r7 = new Box7(1, 2, 3, 4, 5, 6, 7);
console.log("sum7: " + r7.sum);

var r8 = new Box8(1, 2, 3, 4, 5, 6, 7, 8);
console.log("sum8: " + r8.sum);
