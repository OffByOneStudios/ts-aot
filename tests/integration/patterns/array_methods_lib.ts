// Library for array_methods.ts pattern test

export interface User {
    id: number;
    name: string;
    age: number;
    active: boolean;
}

export function filterUsers(users: User[], predicate: (u: User) => boolean): User[] {
    const result: User[] = [];
    for (let i = 0; i < users.length; i++) {
        if (predicate(users[i])) {
            result.push(users[i]);
        }
    }
    return result;
}

export function mapUsers<T>(users: User[], mapper: (u: User) => T): T[] {
    const result: T[] = [];
    for (let i = 0; i < users.length; i++) {
        result.push(mapper(users[i]));
    }
    return result;
}

export function reduceUsers<T>(users: User[], reducer: (acc: T, u: User) => T, initial: T): T {
    let result = initial;
    for (let i = 0; i < users.length; i++) {
        result = reducer(result, users[i]);
    }
    return result;
}

export function findUser(users: User[], predicate: (u: User) => boolean): User | null {
    for (let i = 0; i < users.length; i++) {
        if (predicate(users[i])) {
            return users[i];
        }
    }
    return null;
}
