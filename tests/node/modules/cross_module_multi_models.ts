// Library file: User model that imports from utils

import { formatName } from './cross_module_multi_utils';

export class User {
    name: string;

    constructor(first: string, last: string) {
        this.name = formatName(first, last);
    }

    greet(): string {
        return "Hello, " + this.name;
    }
}
