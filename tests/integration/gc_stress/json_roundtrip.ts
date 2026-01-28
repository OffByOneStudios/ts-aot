// GC Stress Test: JSON Roundtrip
// Performs JSON parse/stringify at scale
// Tests COMP-002: GC-unaware caches

interface DataRecord {
    id: number;
    name: string;
    values: number[];
    nested: {
        flag: boolean;
        count: number;
    };
}

function createRecord(id: number): DataRecord {
    return {
        id: id,
        name: 'Record_' + id,
        values: [id, id * 2, id * 3],
        nested: {
            flag: id % 2 === 0,
            count: id * 10
        }
    };
}

function user_main(): number {
    console.log('GC Stress Test: JSON Roundtrip');
    console.log('==============================');
    console.log('');

    const COUNT = 1000;
    const records: DataRecord[] = [];

    console.log('Creating ' + COUNT + ' records...');
    for (let i = 0; i < COUNT; i++) {
        records.push(createRecord(i));
    }

    console.log('Performing JSON roundtrips...');
    console.log('');

    let errors = 0;

    for (let i = 0; i < COUNT; i++) {
        const original = records[i];

        // Stringify
        const json = JSON.stringify(original);

        // Parse
        const parsed = JSON.parse(json);

        // Verify
        if (parsed.id !== original.id) {
            console.log('  ERROR at ' + i + ': id mismatch');
            errors++;
        }
        if (parsed.name !== original.name) {
            console.log('  ERROR at ' + i + ': name mismatch');
            errors++;
        }
        if (parsed.nested.count !== original.nested.count) {
            console.log('  ERROR at ' + i + ': nested.count mismatch');
            errors++;
        }

        if ((i + 1) % 250 === 0) {
            console.log('  Processed ' + (i + 1) + ' records');
        }
    }

    console.log('');
    if (errors === 0) {
        console.log('All roundtrips verified correctly');
        console.log('PASS');
        return 0;
    } else {
        console.log('FAIL: ' + errors + ' verification errors');
        return 1;
    }
}
