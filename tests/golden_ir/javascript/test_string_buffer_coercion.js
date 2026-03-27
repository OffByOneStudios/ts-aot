// RUN: ts-aot %s -o %t.exe && %t.exe
// OUTPUT: hello
// OUTPUT: string
// OUTPUT: world

var buf = Buffer.from("hello");
console.log(String(buf));
console.log(typeof String(buf));

var buf2 = Buffer.from("world");
console.log("" + buf2);
