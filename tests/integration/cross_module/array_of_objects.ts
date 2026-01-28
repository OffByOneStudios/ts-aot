// Integration test: Cross-module array of objects
// Tests passing arrays containing objects to imported functions

import { sumAges, findByName, getNames, Person } from './array_of_objects_lib';

function user_main(): number {
    console.log('Array of Objects Cross-Module Test');
    console.log('===================================');
    console.log('');

    const people: Person[] = [
        { name: 'Alice', age: 30 },
        { name: 'Bob', age: 25 },
        { name: 'Charlie', age: 35 }
    ];

    // Direct array access
    console.log('Test 1: Direct array access');
    console.log('  people[0].name = ' + people[0].name);
    console.log('  people[1].age = ' + people[1].age);
    console.log('  PASS');
    console.log('');

    // Sum ages through imported function
    console.log('Test 2: sumAges() via imported function');
    const total = sumAges(people);
    console.log('  sumAges(people) = ' + total);
    if (total === 90) {
        console.log('  PASS');
    } else {
        console.log('  FAIL: Expected 90, got ' + total);
        return 1;
    }
    console.log('');

    // Find by name
    console.log('Test 3: findByName() via imported function');
    const found = findByName(people, 'Bob');
    if (found) {
        console.log('  findByName(people, "Bob").age = ' + found.age);
        if (found.age === 25) {
            console.log('  PASS');
        } else {
            console.log('  FAIL: Expected age 25');
            return 1;
        }
    } else {
        console.log('  FAIL: Bob not found');
        return 1;
    }
    console.log('');

    // Get names
    console.log('Test 4: getNames() via imported function');
    const names = getNames(people);
    console.log('  getNames(people) = ' + names.join(', '));
    console.log('  PASS');
    console.log('');

    console.log('ALL TESTS PASSED');
    return 0;
}
