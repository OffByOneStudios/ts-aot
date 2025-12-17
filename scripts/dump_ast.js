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
        case ts.SyntaxKind.NewExpression:
            return {
                kind: "NewExpression",
                expression: visit(node.expression),
                arguments: node.arguments ? node.arguments.map(visit) : []
            };
        case ts.SyntaxKind.ArrayLiteralExpression:
            return {
                kind: "ArrayLiteralExpression",
                elements: node.elements.map(visit)
            };
        case ts.SyntaxKind.ElementAccessExpression:
            return {
                kind: "ElementAccessExpression",
                expression: visit(node.expression),
                argumentExpression: visit(node.argumentExpression)
            };
        case ts.SyntaxKind.BinaryExpression:
            if (node.operatorToken.kind === ts.SyntaxKind.EqualsToken) {
                return {
                    kind: "AssignmentExpression",
                    left: visit(node.left),
                    right: visit(node.right)
                };
            }
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
        case ts.SyntaxKind.TrueKeyword:
            return {
                kind: "BooleanLiteral",
                value: true
            };
        case ts.SyntaxKind.FalseKeyword:
            return {
                kind: "BooleanLiteral",
                value: false
            };
        case ts.SyntaxKind.ObjectLiteralExpression:
            return {
                kind: "ObjectLiteralExpression",
                properties: node.properties.map(p => ({
                    name: p.name.text,
                    initializer: visit(p.initializer)
                }))
            };
        case ts.SyntaxKind.Identifier:
            return {
                kind: "Identifier",
                name: node.text
            };
        case ts.SyntaxKind.PrefixUnaryExpression:
            return {
                kind: "PrefixUnaryExpression",
                operator: ts.tokenToString(node.operator),
                operand: visit(node.operand)
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
        case ts.SyntaxKind.WhileStatement:
            return {
                kind: "WhileStatement",
                condition: visit(node.expression),
                statement: visit(node.statement)
            };
        case ts.SyntaxKind.ForStatement:
            let init = null;
            if (node.initializer) {
                if (node.initializer.kind === ts.SyntaxKind.VariableDeclarationList) {
                    const decl = node.initializer.declarations[0];
                    init = {
                        kind: "VariableDeclaration",
                        name: decl.name.text,
                        initializer: decl.initializer ? visit(decl.initializer) : null
                    };
                } else {
                    init = {
                        kind: "ExpressionStatement",
                        expression: visit(node.initializer)
                    };
                }
            }
            return {
                kind: "ForStatement",
                initializer: init,
                condition: node.condition ? visit(node.condition) : null,
                incrementor: node.incrementor ? visit(node.incrementor) : null,
                body: visit(node.statement)
            };
        case ts.SyntaxKind.ForOfStatement:
            let forOfInit = null;
            if (node.initializer.kind === ts.SyntaxKind.VariableDeclarationList) {
                const decl = node.initializer.declarations[0];
                forOfInit = {
                    kind: "VariableDeclaration",
                    name: decl.name.text,
                    initializer: null // No initializer in for-of
                };
            }
            return {
                kind: "ForOfStatement",
                initializer: forOfInit,
                expression: visit(node.expression),
                body: visit(node.statement)
            };
        case ts.SyntaxKind.SwitchStatement:
            return {
                kind: "SwitchStatement",
                expression: visit(node.expression),
                clauses: node.caseBlock.clauses.map(visit)
            };
        case ts.SyntaxKind.CaseClause:
            return {
                kind: "CaseClause",
                expression: visit(node.expression),
                statements: node.statements.map(visit)
            };
        case ts.SyntaxKind.DefaultClause:
            return {
                kind: "DefaultClause",
                statements: node.statements.map(visit)
            };
        case ts.SyntaxKind.BreakStatement:
            return { kind: "BreakStatement" };
        case ts.SyntaxKind.ContinueStatement:
            return { kind: "ContinueStatement" };
        case ts.SyntaxKind.Block:
            return {
                kind: "BlockStatement",
                statements: node.statements.map(visit).filter(s => s)
            };
        case ts.SyntaxKind.EndOfFileToken:
            return null;
        default:
            console.error("Unhandled node kind:", node.kind);
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

console.error("ObjectLiteralExpression kind:", ts.SyntaxKind.ObjectLiteralExpression);

console.log(printAST(sourceFile));
