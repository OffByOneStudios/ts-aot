function main() {
    let content = fs.readFileSync("tests/integration/data.txt");
    let num = parseInt(content);
    console.log(num + 1);
}
