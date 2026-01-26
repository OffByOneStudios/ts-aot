// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define {{.*}} @user_main
// OUTPUT: |hello     |
// OUTPUT: |     world|
// OUTPUT: |hello     | and |     world|

// Bug: Using padEnd() or padStart() inside template literal interpolation
// causes a crash. The workaround is to call padEnd/padStart outside the
// template literal and store the result in a variable first.
//
// Works:
//   const padded = str.padEnd(10);
//   console.log(padded + " | " + other);
//
// Crashes:
//   console.log(`${str.padEnd(10)} | ${other}`);

function user_main(): number {
    const str1 = "hello";
    const str2 = "world";

    // These work - method call outside template literal
    const padded1 = str1.padEnd(10);
    const padded2 = str2.padStart(10);
    console.log("|" + padded1 + "|");
    console.log("|" + padded2 + "|");

    // This should work but crashes - method call inside template literal
    console.log(`|${str1.padEnd(10)}| and |${str2.padStart(10)}|`);

    return 0;
}
