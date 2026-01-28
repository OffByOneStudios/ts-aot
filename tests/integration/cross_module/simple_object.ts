// Integration test: Cross-module simple object passing
// Tests basic object passing to imported functions (no nesting)

import { processUser, getUserName, getUserAge } from './simple_object_lib';

interface User {
    name: string;
    age: number;
}

function user_main(): number {
    console.log('Simple Object Cross-Module Test');
    console.log('================================');
    console.log('');

    const user: User = {
        name: 'Alice',
        age: 30
    };

    // Direct access (should work)
    console.log('Test 1: Direct property access');
    console.log('  user.name = ' + user.name);
    console.log('  user.age = ' + user.age);
    console.log('  PASS');
    console.log('');

    // Access through imported function
    console.log('Test 2: Access via imported getUserName()');
    const name = getUserName(user);
    console.log('  getUserName(user) = ' + name);
    console.log('  PASS');
    console.log('');

    console.log('Test 3: Access via imported getUserAge()');
    const age = getUserAge(user);
    console.log('  getUserAge(user) = ' + age);
    console.log('  PASS');
    console.log('');

    console.log('Test 4: Access via imported processUser()');
    const processed = processUser(user);
    console.log('  processUser(user) = ' + processed);
    console.log('  PASS');
    console.log('');

    console.log('ALL TESTS PASSED');
    return 0;
}
