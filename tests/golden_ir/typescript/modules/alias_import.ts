// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define {{.*}} @user_main
// OUTPUT: 30

import { add as myAdd } from './math_lib';

function user_main(): number {
    console.log(myAdd(10, 20));
    return 0;
}
