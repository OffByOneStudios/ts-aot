import { add, Point } from "./module_e";

function main() {
    const p: Point = { x: 5, y: 5 };
    ts_console_log(add(p.x, p.y));
}
