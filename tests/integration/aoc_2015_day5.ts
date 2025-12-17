function isNice(s: string) {
    var vowels = 0;
    var hasDouble = false;
    var hasForbidden = false;
    
    for (var i = 0; i < s.length; i = i + 1) {
        var c = s.charCodeAt(i);
        
        // Vowels: a, e, i, o, u
        if (c == 97 || c == 101 || c == 105 || c == 111 || c == 117) {
            vowels = vowels + 1;
        }
        
        if (i > 0) {
            var prev = s.charCodeAt(i - 1);
            if (c == prev) {
                hasDouble = true;
            }
            
            // Forbidden: ab, cd, pq, xy
            // ab: 97, 98
            // cd: 99, 100
            // pq: 112, 113
            // xy: 120, 121
            if (prev == 97 && c == 98) hasForbidden = true;
            if (prev == 99 && c == 100) hasForbidden = true;
            if (prev == 112 && c == 113) hasForbidden = true;
            if (prev == 120 && c == 121) hasForbidden = true;
        }
    }
    
    if (hasForbidden) return false;
    if (vowels < 3) return false;
    if (!hasDouble) return false;
    
    return true;
}

var content = fs.readFileSync("tests/integration/aoc_2015_day5.input");
var lines = content.split("\n");
var niceCount = 0;

for (var i = 0; i < lines.length; i = i + 1) {
    var line = lines[i].trim();
    if (line.length > 0) {
        if (isNice(line)) {
            niceCount = niceCount + 1;
        }
    }
}

console.log(niceCount);
