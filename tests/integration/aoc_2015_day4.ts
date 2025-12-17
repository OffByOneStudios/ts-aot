var key = "abcdef";
var i = 0;
while (true) {
    i = i + 1;
    var hash = crypto.md5(key + i);
    if (hash.startsWith("00000")) {
        console.log(i);
        break;
    }
}
