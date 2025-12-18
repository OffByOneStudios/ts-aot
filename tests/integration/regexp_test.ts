// RegExp Test
const re = new RegExp("abc", "i");
const str = "ABC";

if (re.test(str)) {
    ts_console_log("Match found!");
} else {
    ts_console_log("No match.");
}

const re2 = new RegExp("(\\d+)");
const str2 = "The answer is 42";
const match = re2.exec(str2);

if (match) {
    ts_console_log("Found: " + match[0]);
    ts_console_log("Group 1: " + match[1]);
}
