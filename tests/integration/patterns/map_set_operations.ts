// Pattern Test: Map/Set Operations
// Tests Map and Set with object values

function user_main(): number {
    console.log('Pattern Test: Map/Set Operations');
    console.log('=================================');
    console.log('');

    // Test Map with string keys and object values
    console.log('Test 1: Map with object values');
    const userMap = new Map<string, { name: string; score: number }>();

    userMap.set('user1', { name: 'Alice', score: 100 });
    userMap.set('user2', { name: 'Bob', score: 85 });
    userMap.set('user3', { name: 'Charlie', score: 92 });

    console.log('  Map size: ' + userMap.size);

    const user1 = userMap.get('user1');
    if (user1 && user1.name === 'Alice' && user1.score === 100) {
        console.log('  user1: ' + user1.name + ' with score ' + user1.score);
        console.log('  PASS');
    } else {
        console.log('  FAIL: user1 not found or incorrect');
        return 1;
    }
    console.log('');

    // Test Map iteration
    console.log('Test 2: Map forEach');
    let totalScore = 0;
    userMap.forEach((value, key) => {
        totalScore += value.score;
    });
    console.log('  Total score: ' + totalScore);
    if (totalScore === 277) {
        console.log('  PASS');
    } else {
        console.log('  FAIL: Expected 277');
        return 1;
    }
    console.log('');

    // Test Set
    console.log('Test 3: Set operations');
    const numberSet = new Set<number>();

    numberSet.add(1);
    numberSet.add(2);
    numberSet.add(3);
    numberSet.add(2); // Duplicate

    console.log('  Set size: ' + numberSet.size);
    if (numberSet.size === 3) {
        console.log('  PASS (duplicates ignored)');
    } else {
        console.log('  FAIL');
        return 1;
    }
    console.log('');

    // Test Set has
    console.log('Test 4: Set has()');
    if (numberSet.has(2) && !numberSet.has(5)) {
        console.log('  has(2): true, has(5): false');
        console.log('  PASS');
    } else {
        console.log('  FAIL');
        return 1;
    }
    console.log('');

    console.log('ALL TESTS PASSED');
    return 0;
}
