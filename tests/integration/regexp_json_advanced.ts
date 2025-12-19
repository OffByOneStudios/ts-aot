// RegExp and JSON Advanced Test

// 1. RegExp lastIndex and global flag
const re = new RegExp("a", "g");
const str = "aba";

ts_console_log("Test 1: Global RegExp");
ts_console_log("lastIndex before 1: " + re.lastIndex);
ts_console_log("test 1: " + re.test(str));
ts_console_log("lastIndex after 1: " + re.lastIndex);
ts_console_log("test 2: " + re.test(str));
ts_console_log("lastIndex after 2: " + re.lastIndex);
ts_console_log("test 3: " + re.test(str));
ts_console_log("lastIndex after 3: " + re.lastIndex);

re.lastIndex = 0;
ts_console_log("lastIndex reset: " + re.lastIndex);
const exec1 = re.exec(str);
ts_console_log("exec 1: " + exec1[0]);
ts_console_log("lastIndex after exec 1: " + re.lastIndex);

// 2. RegExp source and flags
ts_console_log("Test 2: RegExp properties");
ts_console_log("source: " + re.source);
ts_console_log("flags: " + re.flags);

// 3. JSON stringify with space
const obj = ["a", 2];
ts_console_log("Test 3: JSON stringify with space");
ts_console_log("Normal: " + JSON.stringify(obj));
ts_console_log("Space 2:\n" + JSON.stringify(obj, null, 2));

// 4. JSON stringify with replacer array
// Note: Object literals are currently structs and not fully supported by JSON.stringify
// but we can test that it doesn't crash.
ts_console_log("Test 4: JSON stringify with replacer array");
const replacer = ["a"];
ts_console_log("Replacer ['a']: " + JSON.stringify(obj, replacer));
