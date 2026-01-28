// GC Stress Test: String Allocation
// Creates many strings to trigger garbage collection
// Tests COMP-002: GC-unaware caches

function user_main(): number {
    console.log('GC Stress Test: String Allocation');
    console.log('==================================');
    console.log('');

    const COUNT = 10000;
    const strings: string[] = [];

    console.log('Creating ' + COUNT + ' strings...');

    for (let i = 0; i < COUNT; i++) {
        // Create unique strings to prevent interning
        const str = 'string_' + i + '_' + (i * 7 % 1000);
        strings.push(str);

        // Progress update every 2500
        if ((i + 1) % 2500 === 0) {
            console.log('  Created ' + (i + 1) + ' strings');
        }
    }

    console.log('');
    console.log('Verifying strings...');

    // Verify some strings to ensure they weren't corrupted by GC
    let errors = 0;
    for (let i = 0; i < COUNT; i += 1000) {
        const expected = 'string_' + i + '_' + (i * 7 % 1000);
        if (strings[i] !== expected) {
            console.log('  ERROR at index ' + i + ': expected "' + expected + '", got "' + strings[i] + '"');
            errors++;
        }
    }

    console.log('');
    if (errors === 0) {
        console.log('All strings verified correctly');
        console.log('PASS');
        return 0;
    } else {
        console.log('FAIL: ' + errors + ' verification errors');
        return 1;
    }
}
