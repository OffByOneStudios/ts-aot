// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define {{.*}} @user_main
// OUTPUT: 50
// OUTPUT: 25

import { multiply, square } from './ambient_math_impl';

function user_main(): number {
    console.log(multiply(5, 10));
    console.log(square(5));
    return 0;
}
