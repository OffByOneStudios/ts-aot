// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK-NOT: PathLike
// OUTPUT: true

import type { PathLike } from 'fs';
import { existsSync } from 'fs';

function user_main(): number {
    console.log(existsSync('.'));
    return 0;
}
