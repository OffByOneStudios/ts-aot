// Pattern Test: Callback in Object Array
// Tests storing function callbacks in objects, pushing to array, and calling them

function user_main(): number {
    console.log('Pattern Test: Callback in Object Array');
    console.log('======================================');
    console.log('');

    // Test 1: Simple object with callback
    console.log('Test 1: Simple callback in object');
    const obj1 = {
        name: 'test1',
        fn: () => {
            console.log('  callback1 called');
        }
    };
    obj1.fn();
    console.log('  PASS');
    console.log('');

    // Test 2: Push object with callback to array
    console.log('Test 2: Callback in object pushed to array');
    const items: Array<{ name: string; fn: () => void }> = [];

    items.push({
        name: 'item1',
        fn: () => {
            console.log('  item1 callback called');
        }
    });

    items.push({
        name: 'item2',
        fn: () => {
            console.log('  item2 callback called');
        }
    });

    // Access and call
    for (const item of items) {
        console.log('  Calling: ' + item.name);
        item.fn();
    }
    console.log('  PASS');
    console.log('');

    // Test 3: Callback with parameters stored in object array
    console.log('Test 3: Callback with closure');
    const tasks: Array<{ name: string; fn: () => number }> = [];

    for (let i = 0; i < 3; i++) {
        const index = i;
        tasks.push({
            name: 'task' + i,
            fn: () => {
                return index * 10;
            }
        });
    }

    let sum = 0;
    for (const task of tasks) {
        const result = task.fn();
        console.log('  ' + task.name + ' returned ' + result);
        sum = sum + result;
    }

    if (sum === 30) {  // 0 + 10 + 20
        console.log('  PASS');
    } else {
        console.log('  FAIL: Expected sum 30, got ' + sum);
        return 1;
    }
    console.log('');

    // Test 4: Class with array of callback objects (mimics BenchmarkSuite pattern)
    console.log('Test 4: Class with callback array (BenchmarkSuite pattern)');

    class Runner {
        private items: Array<{ name: string; fn: () => void }> = [];

        add(name: string, fn: () => void): void {
            this.items.push({ name: name, fn: fn });
        }

        run(): void {
            for (const item of this.items) {
                console.log('  Running: ' + item.name);
                item.fn();
            }
        }
    }

    const runner = new Runner();
    runner.add('task_a', () => {
        console.log('    task_a executed');
    });
    runner.add('task_b', () => {
        console.log('    task_b executed');
    });
    runner.run();
    console.log('  PASS');
    console.log('');

    console.log('ALL TESTS PASSED');
    return 0;
}
