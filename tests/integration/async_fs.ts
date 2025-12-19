async function main() {
    console.log("Reading file...");
    const content = await fs.promises.readFile("hello.txt");
    console.log("Content:", content);
}

main();
