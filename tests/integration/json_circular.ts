// JSON Circular Reference Test

const a: any[] = [1];
a.push(a);

// We don't have full try/catch support that catches C++ exceptions yet in the generated code
// but ts_json_stringify catches it internally and returns "null" or similar if it fails.
// Actually, I implemented it to return TsString::Create("null") on catch (...)

const s = JSON.stringify(a);
if (s === "null") {
    ts_console_log("Caught expected circular reference");
} else {
    ts_console_log("Stringify success: " + s);
}
