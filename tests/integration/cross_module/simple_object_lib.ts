// Library for simple_object.ts integration test

export interface User {
    name: string;
    age: number;
}

export function getUserName(user: User): string {
    return user.name;
}

export function getUserAge(user: User): number {
    return user.age;
}

export function processUser(user: User): string {
    return user.name + ' is ' + user.age + ' years old';
}
