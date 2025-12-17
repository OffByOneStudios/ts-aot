let input = fs.readFileSync("E:\\src\\github.com\\cgrinker\\ts-aoc\\tests\\integration\\aoc_2015_day1.txt");
let floor = 0;
for (let i = 0; i < input.length; i = i + 1) {
    let c = input.charCodeAt(i);
    if (c == 40) {
        floor = floor + 1;
    } else if (c == 41) {
        floor = floor - 1;
    }
}
console.log(floor);
