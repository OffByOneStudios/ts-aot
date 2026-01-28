// Pattern Test: Array Methods
// Tests array methods with callbacks across modules

import {
    filterUsers,
    mapUsers,
    reduceUsers,
    findUser,
    User
} from './array_methods_lib';

function user_main(): number {
    console.log('Pattern Test: Array Methods');
    console.log('===========================');
    console.log('');

    const users: User[] = [
        { id: 1, name: 'Alice', age: 30, active: true },
        { id: 2, name: 'Bob', age: 25, active: false },
        { id: 3, name: 'Charlie', age: 35, active: true },
        { id: 4, name: 'Diana', age: 28, active: true }
    ];

    // Test filter
    console.log('Test 1: filterUsers() - active users');
    const activeUsers = filterUsers(users, (u: User) => u.active);
    console.log('  Active users: ' + activeUsers.length);
    if (activeUsers.length === 3) {
        console.log('  PASS');
    } else {
        console.log('  FAIL');
        return 1;
    }
    console.log('');

    // Test map
    console.log('Test 2: mapUsers() - get names');
    const names = mapUsers(users, (u: User) => u.name);
    console.log('  Names: ' + names.join(', '));
    if (names.length === 4 && names[0] === 'Alice') {
        console.log('  PASS');
    } else {
        console.log('  FAIL');
        return 1;
    }
    console.log('');

    // Test reduce
    console.log('Test 3: reduceUsers() - sum ages');
    const totalAge = reduceUsers(users, (sum: number, u: User) => sum + u.age, 0);
    console.log('  Total age: ' + totalAge);
    if (totalAge === 118) {
        console.log('  PASS');
    } else {
        console.log('  FAIL: Expected 118');
        return 1;
    }
    console.log('');

    // Test find
    console.log('Test 4: findUser() - find by name');
    const found = findUser(users, (u: User) => u.name === 'Charlie');
    if (found && found.age === 35) {
        console.log('  Found Charlie, age: ' + found.age);
        console.log('  PASS');
    } else {
        console.log('  FAIL: Charlie not found');
        return 1;
    }
    console.log('');

    console.log('ALL TESTS PASSED');
    return 0;
}
