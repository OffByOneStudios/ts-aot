// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define {{.*}} @user_main
// OUTPUT: 30

import { sum } from './reexport_alias_barrel';

function user_main(): number {
    console.log(sum(10, 20));
    return 0;
}
