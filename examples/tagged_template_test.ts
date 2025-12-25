function tag(strings, ...values) {
    let result = "";
    for (let i = 0; i < strings.length; i++) {
        result += strings[i];
        if (i < values.length) {
            result += values[i];
        }
    }
    return result;
}

const name = "World";
const x = tag`Hello ${name}!`;
console.log(x);

const y = tag`1 + 2 = ${1 + 2}`;
console.log(y);
