// GC Stress Test: Nested Object Allocation
// Creates deeply nested objects to stress GC
// Tests COMP-002: GC-unaware caches

interface Node {
    id: number;
    value: string;
    children: Node[];
}

function createTree(depth: number, breadth: number, prefix: string): Node {
    const node: Node = {
        id: depth * 1000 + breadth,
        value: prefix + '_' + depth + '_' + breadth,
        children: []
    };

    if (depth > 0) {
        for (let i = 0; i < 3; i++) {
            node.children.push(createTree(depth - 1, i, prefix + '_' + i));
        }
    }

    return node;
}

function countNodes(node: Node): number {
    let count = 1;
    for (let i = 0; i < node.children.length; i++) {
        count += countNodes(node.children[i]);
    }
    return count;
}

function verifyTree(node: Node, expectedPrefix: string): boolean {
    if (!node.value.startsWith(expectedPrefix)) {
        return false;
    }
    for (let i = 0; i < node.children.length; i++) {
        if (!verifyTree(node.children[i], expectedPrefix + '_' + i)) {
            return false;
        }
    }
    return true;
}

function user_main(): number {
    console.log('GC Stress Test: Nested Allocation');
    console.log('==================================');
    console.log('');

    const TREE_COUNT = 100;
    const DEPTH = 4;
    const trees: Node[] = [];

    console.log('Creating ' + TREE_COUNT + ' trees of depth ' + DEPTH + '...');

    for (let i = 0; i < TREE_COUNT; i++) {
        const tree = createTree(DEPTH, 0, 'tree' + i);
        trees.push(tree);

        if ((i + 1) % 25 === 0) {
            console.log('  Created ' + (i + 1) + ' trees');
        }
    }

    console.log('');

    // Count total nodes
    let totalNodes = 0;
    for (let i = 0; i < trees.length; i++) {
        totalNodes += countNodes(trees[i]);
    }
    console.log('Total nodes created: ' + totalNodes);

    console.log('');
    console.log('Verifying trees...');

    let errors = 0;
    for (let i = 0; i < TREE_COUNT; i += 10) {
        if (!verifyTree(trees[i], 'tree' + i)) {
            console.log('  ERROR: Tree ' + i + ' verification failed');
            errors++;
        }
    }

    console.log('');
    if (errors === 0) {
        console.log('All trees verified correctly');
        console.log('PASS');
        return 0;
    } else {
        console.log('FAIL: ' + errors + ' verification errors');
        return 1;
    }
}
