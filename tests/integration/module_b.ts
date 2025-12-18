import { add, Point } from "./module_a";

function main() {
    const p: Point = { x: 10, y: 20 };
    const result = add(p.x, p.y);
    ts_console_log("Result of add: ");
    ts_console_log(result);
}
