const fs = require('fs');

const testFile = 'test_fs.txt';
const content = 'Hello from ts-aot!';

console.log('Writing file...');
fs.writeFileSync(testFile, content);

console.log('Checking if file exists...');
if (fs.existsSync(testFile)) {
    console.log('File exists!');
} else {
    console.log('File does NOT exist!');
}

console.log('Reading file...');
const readContent = fs.readFileSync(testFile);
console.log('Read content:', readContent);

console.log('Cleaning up...');
fs.unlinkSync(testFile);

console.log('Testing mkdirSync...');
const testDir = 'test_dir';
fs.mkdirSync(testDir);
if (fs.existsSync(testDir)) {
    console.log('Directory created!');
}

console.log('Testing rmdirSync...');
fs.rmdirSync(testDir);
if (!fs.existsSync(testDir)) {
    console.log('Directory removed!');
}

console.log('Done!');
