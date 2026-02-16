// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// OUTPUT: hello world, you are 42
// OUTPUT: just one arg
// OUTPUT: 1 2 3
// OUTPUT: a b c

function user_main(): number {
    console.log('hello %s, you are %d', 'world', 42);
    console.log('just one arg');
    console.log(1, 2, 3);
    console.log('a', 'b', 'c');
    return 0;
}
