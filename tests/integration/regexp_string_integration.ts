const s = "hello world 123";
const re = /[a-z]+/g;

// Test match
const matches = s.match(re);
if (matches) {
    ts_console_log("Matches found: " + matches.length);
    for (let i = 0; i < matches.length; i++) {
        ts_console_log("Match " + i + ": " + matches[i]);
    }
} else {
    ts_console_log("No matches found");
}

// Test replace
const replaced = s.replace(/[0-9]+/, "numbers");
ts_console_log("Replaced: " + replaced);

const replacedAll = s.replace(/[a-z]/g, "X");
ts_console_log("Replaced All: " + replacedAll);

// Test split
const parts = s.split(/\s+/);
ts_console_log("Parts length: " + parts.length);
for (let i = 0; i < parts.length; i++) {
    ts_console_log("Part " + i + ": '" + parts[i] + "'");
}

// Test split with capturing groups
const partsWithGroups = "a,b;c".split(/[,;]/);
ts_console_log("Parts with groups length: " + partsWithGroups.length);
for (let i = 0; i < partsWithGroups.length; i++) {
    ts_console_log("Part " + i + ": '" + partsWithGroups[i] + "'");
}
