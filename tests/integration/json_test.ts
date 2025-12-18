// JSON Test
const jsonStr = '{"name": "John", "age": 30, "isStudent": false}';
const obj = JSON.parse(jsonStr);

ts_console_log("Name: " + obj.name);

const backToStr = JSON.stringify(obj);
ts_console_log("Stringified: " + backToStr);

const simpleObj = { a: 1, b: "hello" };
// ts_console_log("Simple Object: " + JSON.stringify(simpleObj)); // Object literals might not have magic numbers yet
