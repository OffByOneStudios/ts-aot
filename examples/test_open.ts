import * as fs from "fs";
async function main() {
    try {
        const handle = await fs.promises.open("test_simple.txt", "w");
        console.log("Success");
        await handle.close();
    } catch (e) {
        console.log("Error: " + e);
    }
}
main();
