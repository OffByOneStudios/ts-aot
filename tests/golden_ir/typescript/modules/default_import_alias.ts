// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// OUTPUT: 30

import myAdd from './math_default';

function user_main(): number {
    console.log(myAdd(10, 20));
    return 0;
}
