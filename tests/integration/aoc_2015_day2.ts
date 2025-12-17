let input = fs.readFileSync("E:\\src\\github.com\\cgrinker\\ts-aoc\\tests\\integration\\aoc_2015_day2.txt");
let lines = input.split("\n");
let total = 0;

for (let i = 0; i < lines.length; i = i + 1) {
    let line = lines[i];
    line = line.trim();
    if (line.length == 0) {
        continue;
    }
    
    let parts = line.split("x");
    let l = parseInt(parts[0]);
    let w = parseInt(parts[1]);
    let h = parseInt(parts[2]);
    
    let s1 = l * w;
    let s2 = w * h;
    let s3 = h * l;
    
    let slack = 0;
    if (s1 <= s2 && s1 <= s3) {
        slack = s1;
    } else if (s2 <= s1 && s2 <= s3) {
        slack = s2;
    } else {
        slack = s3;
    }
    
    let area = 2 * s1 + 2 * s2 + 2 * s3 + slack;
    total = total + area;
}
console.log(total);
