const ts = require("typescript");
const fs = require("fs");

let currentSourceFile;

function printAST(sourceFile) {
    currentSourceFile = sourceFile;
    const result = {
        kind: "Program",
        body: []
    };

    ts.forEachChild(sourceFile, (node) => {
        const stmt = visit(node);
        if (stmt) {
            result.body.push(stmt);
        }
    });

    return JSON.stringify(result, null, 2);
}

function visit(node) {
    switch (node.kind) {
        case ts.SyntaxKind.FunctionDeclaration:
            return {
                kind: "FunctionDeclaration",
                name: node.name ? node.name.text : "anonymous",
                returnType: node.type ? node.type.getText(currentSourceFile) : "any",
                parameters: node.parameters.map(p => ({
                    kind: "Parameter",
                    name: p.name.text,
                    type: p.type ? p.type.getText(currentSourceFile) : "any"
                })),
                body: visitBlock(node.body)
            };
        case ts.SyntaxKind.VariableStatement:
            // Simplified: assume one declaration per statement
            const decl = node.declarationList.declarations[0];
            return {
                kind: "VariableDeclaration",
                name: decl.name.text,
                initializer: decl.initializer ? visit(decl.initializer) : null
            };
        case ts.SyntaxKind.ExpressionStatement:
            return {
                kind: "ExpressionStatement",
                expression: visit(node.expression)
            };
        case ts.SyntaxKind.ReturnStatement:
            return {
                kind: "ReturnStatement",
                expression: node.expression ? visit(node.expression) : null
            };
        case ts.SyntaxKind.CallExpression:
            return {
                kind: "CallExpression",
                callee: visit(node.expression),
                arguments: node.arguments.map(visit)
            };
        case ts.SyntaxKind.BinaryExpression:
            return {
                kind: "BinaryExpression",
                operator: node.operatorToken.getText(),
                left: visit(node.left),
                right: visit(node.right)
            };
        case ts.SyntaxKind.StringLiteral:
            return {
                kind: "StringLiteral",
                value: node.text
            };
        case ts.SyntaxKind.NumericLiteral:
            return {
                kind: "NumericLiteral",
                value: Number(node.text)
            };
        case ts.SyntaxKind.Identifier:
            return {
                kind: "Identifier",
                name: node.text
            };
        case ts.SyntaxKind.PropertyAccessExpression:
            return {
                kind: "PropertyAccessExpression",
                expression: visit(node.expression),
                name: node.name.text
            };
        case ts.SyntaxKind.IfStatement:
            return {
                kind: "IfStatement",
                condition: visit(node.expression),
                thenStatement: visit(node.thenStatement),
                elseStatement: node.elseStatement ? visit(node.elseStatement) : null
            };
        case ts.SyntaxKind.Block:
            return {
                kind: "BlockStatement",
                statements: visitBlock(node)
            };
        case ts.SyntaxKind.EndOfFileToken:
            return null;
        default:
            // console.warn("Unhandled node kind:", ts.SyntaxKind[node.kind]);
            return null;
    }
}

function visitBlock(node) {
    if (!node) return [];
    const stmts = [];
    ts.forEachChild(node, (child) => {
        const res = visit(child);
        if (res) stmts.push(res);
    });
    return stmts;
}

const fileName = process.argv[2];
if (!fileName) {
    console.error("Usage: node dump_ast.js <file.ts>");
    process.exit(1);
}

const sourceCode = fs.readFileSync(fileName, "utf-8");
const sourceFile = ts.createSourceFile(
    fileName,
    sourceCode,
    ts.ScriptTarget.Latest,
    true
);

console.log(printAST(sourceFile));
