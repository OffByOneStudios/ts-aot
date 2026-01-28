// Library for array_of_objects.ts integration test

export interface Person {
    name: string;
    age: number;
}

export function sumAges(people: Person[]): number {
    let total = 0;
    for (let i = 0; i < people.length; i++) {
        total += people[i].age;
    }
    return total;
}

export function findByName(people: Person[], name: string): Person | null {
    for (let i = 0; i < people.length; i++) {
        if (people[i].name === name) {
            return people[i];
        }
    }
    return null;
}

export function getNames(people: Person[]): string[] {
    const names: string[] = [];
    for (let i = 0; i < people.length; i++) {
        names.push(people[i].name);
    }
    return names;
}
