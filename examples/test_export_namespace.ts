// Test ES2020 export * as ns from 'module'
import { math } from './module_ns_export/index';

function user_main(): number {
    console.log("Export namespace test:");
    console.log(math.add(2, 3));      // 5
    console.log(math.multiply(4, 5)); // 20
    console.log(math.add(10, 20));    // 30
    return 0;
}
