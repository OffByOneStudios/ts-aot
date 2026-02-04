// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// OUTPUT: Found 3 matches
// OUTPUT: apple
// OUTPUT: banana
// OUTPUT: cherry

function user_main(): number {
    const text = "apple, banana, cherry";
    const pattern = /\w+/g;

    const matches: string[] = [];
    let match: RegExpExecArray | null;

    while ((match = pattern.exec(text)) !== null) {
        matches.push(match[0]);
    }

    console.log("Found " + matches.length + " matches");
    for (const m of matches) {
        console.log(m);
    }

    return 0;
}
