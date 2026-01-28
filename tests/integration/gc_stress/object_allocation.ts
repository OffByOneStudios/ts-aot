// GC Stress Test: Object Allocation
// Creates many objects to trigger garbage collection
// Tests COMP-002: GC-unaware caches

interface Item {
    id: number;
    name: string;
    value: number;
}

function user_main(): number {
    console.log('GC Stress Test: Object Allocation');
    console.log('==================================');
    console.log('');

    const COUNT = 10000;
    const items: Item[] = [];

    console.log('Creating ' + COUNT + ' objects...');

    for (let i = 0; i < COUNT; i++) {
        const item: Item = {
            id: i,
            name: 'Item_' + i,
            value: i * 3.14
        };
        items.push(item);

        // Progress update every 2500
        if ((i + 1) % 2500 === 0) {
            console.log('  Created ' + (i + 1) + ' objects');
        }
    }

    console.log('');
    console.log('Verifying objects...');

    // Verify objects to ensure they weren't corrupted by GC
    let errors = 0;
    for (let i = 0; i < COUNT; i += 500) {
        const item = items[i];
        const expectedName = 'Item_' + i;

        if (item.id !== i) {
            console.log('  ERROR at index ' + i + ': id mismatch');
            errors++;
        }
        if (item.name !== expectedName) {
            console.log('  ERROR at index ' + i + ': name mismatch');
            errors++;
        }
    }

    console.log('');
    if (errors === 0) {
        console.log('All objects verified correctly');
        console.log('PASS');
        return 0;
    } else {
        console.log('FAIL: ' + errors + ' verification errors');
        return 1;
    }
}
