let input = fs.readFileSync("tests/integration/aoc_2015_day3.txt");
let x = 0;
let y = 0;
let visited = new Map();
visited.set("0,0", 1);

for (let i = 0; i < input.length; i = i + 1) {
    let c = input.charCodeAt(i);
    if (c == 94) { // ^
        y = y + 1;
    } else if (c == 118) { // v
        y = y - 1;
    } else if (c == 62) { // >
        x = x + 1;
    } else if (c == 60) { // <
        x = x - 1;
    }
    
    let key = x + "," + y;
    visited.set(key, 1);
}

console.log(visited.size);
