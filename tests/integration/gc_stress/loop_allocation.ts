// GC Stress Test: Loop Allocation
// Creates and discards objects in tight loops
// Tests COMP-002: GC-unaware caches

function user_main(): number {
    console.log('GC Stress Test: Loop Allocation');
    console.log('================================');
    console.log('');

    const OUTER_ITERATIONS = 100;
    const INNER_ITERATIONS = 1000;

    console.log('Running ' + OUTER_ITERATIONS + ' outer iterations...');
    console.log('Each creates ' + INNER_ITERATIONS + ' temporary objects');
    console.log('');

    let totalSum = 0;

    for (let outer = 0; outer < OUTER_ITERATIONS; outer++) {
        // Create temporary objects that should be collected
        let localSum = 0;

        for (let inner = 0; inner < INNER_ITERATIONS; inner++) {
            // Create object that will be discarded
            const temp = {
                x: inner,
                y: outer,
                label: 'temp_' + inner
            };
            localSum += temp.x + temp.y;
        }

        totalSum += localSum;

        if ((outer + 1) % 25 === 0) {
            console.log('  Completed iteration ' + (outer + 1));
        }
    }

    console.log('');

    // Verify the sum is correct
    // Sum of 0..999 = 999*1000/2 = 499500
    // For each outer iteration: 499500 + outer*1000
    // Total: 100*499500 + sum(0..99)*1000 = 49950000 + 4950*1000 = 54900000
    const expectedSum = 54900000;

    console.log('Total sum: ' + totalSum);
    console.log('Expected:  ' + expectedSum);

    if (totalSum === expectedSum) {
        console.log('');
        console.log('PASS');
        return 0;
    } else {
        console.log('');
        console.log('FAIL: Sum mismatch');
        return 1;
    }
}
