// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// OUTPUT: 42
// OUTPUT: 42
// OUTPUT: 3.14
// OUTPUT: 3.14
// OUTPUT: val=42
// OUTPUT: str=42
// OUTPUT: 16
// OUTPUT: done

// Tests that number.toString() returns the correct string
// (was returning undefined when the runtime returned the string
//  directly instead of a callable native function wrapper)

function user_main(): number {
    const a = 42;
    console.log(a);               // direct number print
    console.log(a.toString());    // should print "42"

    const b: number = 3.14;
    console.log(b);               // direct float print
    console.log(b.toString());    // should print "3.14"

    console.log("val=" + a);      // string concat with number
    const s = a.toString();
    console.log("str=" + s);      // concat with toString result

    // toString with radix
    const hex = (22).toString(16);
    console.log(hex);             // should print "16" (22 in hex)

    console.log("done");
    return 0;
}
