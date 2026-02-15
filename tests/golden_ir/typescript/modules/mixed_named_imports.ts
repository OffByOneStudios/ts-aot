// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define {{.*}} @user_main
// OUTPUT: 30
// OUTPUT: 200

import { add, multiply } from './math_lib';

function user_main(): number {
    console.log(add(10, 20));
    console.log(multiply(10, 20));
    return 0;
}
