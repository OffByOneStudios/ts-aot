const pi = Math.PI;
ts_console_log("PI: " + pi);

const r = Math.random();
ts_console_log("Random: " + r);

const s = Math.sqrt(16);
ts_console_log("Sqrt(16): " + s);

const p = Math.pow(2, 3);
ts_console_log("Pow(2, 3): " + p);

const c = Math.ceil(4.2);
ts_console_log("Ceil(4.2): " + c);

const rd = Math.round(4.6);
ts_console_log("Round(4.6): " + rd);

const f = Math.floor(4.8);
ts_console_log("Floor(4.8): " + f);

const path = "test_write.txt";
const content = "Hello from ts-aot!";
fs.writeFileSync(path, content);

const readBack = fs.readFileSync(path);
ts_console_log("Read back: " + readBack);
