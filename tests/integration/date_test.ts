// Date Test
const now = Date.now();
// ts_console_log("Now: " + now); // now is a large number, might be interpreted as double

const d = new Date();
ts_console_log("Year: " + d.getFullYear());
ts_console_log("Month: " + d.getMonth());
ts_console_log("Date: " + d.getDate());
// ts_console_log("Time: " + d.getTime());

const d2 = new Date(1600000000000);
ts_console_log("D2 Year: " + d2.getFullYear());
