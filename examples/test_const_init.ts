// Test const division at module level vs local
const modLevel = 1 / 0;

function test(): void {
    const localLevel = 1 / 0;
    console.log("localLevel:", localLevel);
    console.log("modLevel:", modLevel);
}

test();
console.log("direct 1/0:", 1 / 0);
