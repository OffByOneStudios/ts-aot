// Pattern Test: Class Hierarchy
// Tests multi-level inheritance across modules

import { Animal, Dog, Cat, makeSound } from './class_hierarchy_lib';

function user_main(): number {
    console.log('Pattern Test: Class Hierarchy');
    console.log('=============================');
    console.log('');

    // Create instances
    const dog = new Dog('Buddy', 'Golden Retriever');
    const cat = new Cat('Whiskers', true);

    // Test inheritance
    console.log('Test 1: Dog instance');
    console.log('  name: ' + dog.name);
    console.log('  breed: ' + dog.breed);
    console.log('  speak(): ' + dog.speak());
    console.log('  describe(): ' + dog.describe());
    console.log('  PASS');
    console.log('');

    console.log('Test 2: Cat instance');
    console.log('  name: ' + cat.name);
    console.log('  indoor: ' + cat.indoor);
    console.log('  speak(): ' + cat.speak());
    console.log('  describe(): ' + cat.describe());
    console.log('  PASS');
    console.log('');

    // Test polymorphism through imported function
    console.log('Test 3: Polymorphism via makeSound()');
    console.log('  makeSound(dog): ' + makeSound(dog));
    console.log('  makeSound(cat): ' + makeSound(cat));
    console.log('  PASS');
    console.log('');

    console.log('ALL TESTS PASSED');
    return 0;
}
