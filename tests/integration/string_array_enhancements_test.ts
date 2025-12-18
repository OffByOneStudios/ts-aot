const s = "Hello World";
ts_console_log("Includes 'World': " + s.includes("World"));
ts_console_log("Includes 'world': " + s.includes("world"));
ts_console_log("IndexOf 'World': " + s.indexOf("World"));
ts_console_log("Lower: " + s.toLowerCase());
ts_console_log("Upper: " + s.toUpperCase());

const arr = [10, 20, 30, 40, 50];
ts_console_log("Array IndexOf 30: " + arr.indexOf(30));
ts_console_log("Array IndexOf 60: " + arr.indexOf(60));

const sliced = arr.slice(1, 4);
ts_console_log("Sliced length: " + sliced.length);
ts_console_log("Sliced[0]: " + sliced[0]);
ts_console_log("Sliced[1]: " + sliced[1]);
ts_console_log("Sliced[2]: " + sliced[2]);

const joined = arr.join("-");
ts_console_log("Joined: " + joined);

const strArr = ["a", "b", "c"];
ts_console_log("Joined strings: " + strArr.join("|"));
