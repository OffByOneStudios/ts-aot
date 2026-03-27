// RUN: ts-aot %s -o %t.exe && %t.exe
// OUTPUT: count: 3
// OUTPUT: str: hello world done

let count = 0;
let str = "";

function incCount() { count++; }
function appendStr(s) { str += s; }

incCount();
incCount();
incCount();
console.log("count: " + count);

appendStr("hello ");
appendStr("world ");
appendStr("done");
console.log("str: " + str);
