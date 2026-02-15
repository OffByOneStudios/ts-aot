// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define {{.*}} @user_main
// OUTPUT: 42

// This test verifies that declare module 'string-literal' in .d.ts files
// can be parsed without error. The actual ambient module declarations provide
// type info only. We test with a real implementation module.
import { multiply, square } from './ambient_math_impl';

function user_main(): number {
    const result = multiply(6, 7);
    console.log(result);
    return 0;
}
