type MyNumber = number;
type MyString = string;

enum Color {
    Red,
    Green,
    Blue = 10,
    Yellow
}

function printColor(c: Color) {
    if (c === Color.Red) {
        console.log("Red");
    } else if (c === Color.Green) {
        console.log("Green");
    } else if (c === Color.Blue) {
        console.log("Blue");
    } else if (c === Color.Yellow) {
        console.log("Yellow");
    }
}

const r: MyNumber = Color.Red;
const g: MyNumber = Color.Green;
const b: MyNumber = Color.Blue;
const y: MyNumber = Color.Yellow;

printColor(r);
printColor(g);
printColor(b);
printColor(y);

const s: MyString = "Hello Aliases";
console.log(s);
