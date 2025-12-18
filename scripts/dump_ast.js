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

function getAccessModifier(node) {
    if (!node.modifiers) return null;
    for (const mod of node.modifiers) {
        if (mod.kind === ts.SyntaxKind.PrivateKeyword) return "private";
        if (mod.kind === ts.SyntaxKind.ProtectedKeyword) return "protected";
        if (mod.kind === ts.SyntaxKind.PublicKeyword) return "public";
    }
    return null;
}

function isStatic(node) {
    if (!node.modifiers) return false;
    for (const mod of node.modifiers) {
        if (mod.kind === ts.SyntaxKind.StaticKeyword) return true;
    }
    return false;
}

function isAbstract(node) {
    if (!node.modifiers) return false;
    for (const mod of node.modifiers) {
        if (mod.kind === ts.SyntaxKind.AbstractKeyword) return true;
    }
    return false;
}

function isReadonly(node) {
    if (!node.modifiers) return false;
    for (const mod of node.modifiers) {
        if (mod.kind === ts.SyntaxKind.ReadonlyKeyword) return true;
    }
    return false;
}

function isExported(node) {
    if (!node.modifiers) return false;
    for (const mod of node.modifiers) {
        if (mod.kind === ts.SyntaxKind.ExportKeyword) return true;
    }
    return false;
}

function isDefaultExport(node) {
    if (!node.modifiers) return false;
    for (const mod of node.modifiers) {
        if (mod.kind === ts.SyntaxKind.DefaultKeyword) return true;
    }
    return false;
}

function isAsync(node) {
    if (!node.modifiers) return false;
    for (const mod of node.modifiers) {
        if (mod.kind === ts.SyntaxKind.AsyncKeyword) return true;
    }
    return false;
}

function getTypeParameters(node) {
    if (!node.typeParameters) return [];
    return node.typeParameters.map(tp => ({
        name: tp.name.text,
        constraint: tp.constraint ? tp.constraint.getText(currentSourceFile) : null
    }));
}

function getTypeArguments(node) {
    if (!node.typeArguments) return [];
    return node.typeArguments.map(ta => ta.getText(currentSourceFile));
}

function visit(node) {
    switch (node.kind) {
        case ts.SyntaxKind.ImportDeclaration:
            return {
                kind: "ImportDeclaration",
                moduleSpecifier: node.moduleSpecifier.text,
                importClause: node.importClause ? {
                    name: node.importClause.name ? node.importClause.name.text : null,
                    namedBindings: node.importClause.namedBindings ? (
                        node.importClause.namedBindings.kind === ts.SyntaxKind.NamespaceImport ?
                        { kind: "NamespaceImport", name: node.importClause.namedBindings.name.text } :
                        {
                            kind: "NamedImports",
                            elements: node.importClause.namedBindings.elements.map(e => ({
                                name: e.name.text,
                                propertyName: e.propertyName ? e.propertyName.text : null
                            }))
                        }
                    ) : null
                } : null
            };
        case ts.SyntaxKind.ExportDeclaration:
            return {
                kind: "ExportDeclaration",
                moduleSpecifier: node.moduleSpecifier ? node.moduleSpecifier.text : null,
                isStarExport: !node.exportClause && !!node.moduleSpecifier,
                namedExports: node.exportClause && node.exportClause.kind === ts.SyntaxKind.NamedExports ? 
                    node.exportClause.elements.map(e => ({
                        name: e.name.text,
                        propertyName: e.propertyName ? e.propertyName.text : null
                    })) : []
            };
        case ts.SyntaxKind.FunctionDeclaration:
            return {
                kind: "FunctionDeclaration",
                name: node.name ? node.name.text : "anonymous",
                isExported: isExported(node),
                isDefaultExport: isDefaultExport(node),
                isAsync: isAsync(node),
                typeParameters: getTypeParameters(node),
                returnType: node.type ? node.type.getText(currentSourceFile) : "any",
                parameters: node.parameters.map(visitParameter),
                body: visitBlock(node.body)
            };
        case ts.SyntaxKind.ClassDeclaration:
            let baseClass = "";
            let implementsInterfaces = [];
            if (node.heritageClauses) {
                for (const clause of node.heritageClauses) {
                    if (clause.token === ts.SyntaxKind.ExtendsKeyword) {
                        baseClass = clause.types[0].expression.getText(currentSourceFile);
                    } else if (clause.token === ts.SyntaxKind.ImplementsKeyword) {
                        implementsInterfaces = clause.types.map(t => t.expression.getText(currentSourceFile));
                    }
                }
            }
            return {
                kind: "ClassDeclaration",
                name: node.name ? node.name.text : "anonymous",
                isExported: isExported(node),
                isDefaultExport: isDefaultExport(node),
                typeParameters: getTypeParameters(node),
                baseClass: baseClass,
                implementsInterfaces: implementsInterfaces,
                members: node.members.map(visit).filter(m => m),
                isAbstract: isAbstract(node)
            };
        case ts.SyntaxKind.InterfaceDeclaration:
            let baseInterfaces = [];
            if (node.heritageClauses) {
                for (const clause of node.heritageClauses) {
                    if (clause.token === ts.SyntaxKind.ExtendsKeyword) {
                        baseInterfaces = clause.types.map(t => t.expression.getText(currentSourceFile));
                    }
                }
            }
            return {
                kind: "InterfaceDeclaration",
                name: node.name.text,
                isExported: isExported(node),
                isDefaultExport: isDefaultExport(node),
                typeParameters: getTypeParameters(node),
                baseInterfaces: baseInterfaces,
                members: node.members.map(visit).filter(m => m)
            };
        case ts.SyntaxKind.PropertyDeclaration:
        case ts.SyntaxKind.PropertySignature:
            return {
                kind: "PropertyDefinition",
                name: node.name.text,
                type: node.type ? node.type.getText(currentSourceFile) : "any",
                initializer: node.initializer ? visit(node.initializer) : null,
                access: getAccessModifier(node),
                isStatic: isStatic(node),
                isReadonly: isReadonly(node)
            };
        case ts.SyntaxKind.MethodDeclaration:
        case ts.SyntaxKind.MethodSignature:
        case ts.SyntaxKind.GetAccessor:
        case ts.SyntaxKind.SetAccessor:
            return {
                kind: "MethodDefinition",
                name: node.name.text,
                isAsync: isAsync(node),
                typeParameters: getTypeParameters(node),
                parameters: node.parameters.map(visitParameter),
                returnType: node.type ? node.type.getText(currentSourceFile) : "any",
                body: node.body ? visitBlock(node.body) : [],
                hasBody: !!node.body,
                access: getAccessModifier(node),
                isStatic: isStatic(node),
                isAbstract: isAbstract(node),
                isGetter: node.kind === ts.SyntaxKind.GetAccessor,
                isSetter: node.kind === ts.SyntaxKind.SetAccessor
            };
        case ts.SyntaxKind.Constructor:
             return {
                kind: "MethodDefinition",
                name: "constructor",
                parameters: node.parameters.map(visitParameter),
                returnType: "void",
                body: node.body ? visitBlock(node.body) : [],
                hasBody: !!node.body,
                access: "public"
            };
        case ts.SyntaxKind.VariableStatement:
            // Simplified: assume one declaration per statement
            const decl = node.declarationList.declarations[0];
            return {
                kind: "VariableDeclaration",
                name: visit(decl.name),
                isExported: isExported(node),
                type: decl.type ? decl.type.getText(currentSourceFile) : "any",
                initializer: decl.initializer ? visit(decl.initializer) : null
            };
        case ts.SyntaxKind.ObjectBindingPattern:
        case ts.SyntaxKind.ArrayBindingPattern:
            return {
                kind: node.kind === ts.SyntaxKind.ObjectBindingPattern ? "ObjectBindingPattern" : "ArrayBindingPattern",
                elements: node.elements.map(visit)
            };
        case ts.SyntaxKind.BindingElement:
            return {
                kind: "BindingElement",
                name: visit(node.name),
                propertyName: node.propertyName ? node.propertyName.text : null,
                initializer: node.initializer ? visit(node.initializer) : null,
                isSpread: !!node.dotDotDotToken
            };
        case ts.SyntaxKind.OmittedExpression:
            return { kind: "OmittedExpression" };
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
                arguments: node.arguments.map(visit),
                typeArguments: getTypeArguments(node)
            };
        case ts.SyntaxKind.NewExpression:
            return {
                kind: "NewExpression",
                expression: visit(node.expression),
                arguments: node.arguments ? node.arguments.map(visit) : [],
                typeArguments: getTypeArguments(node)
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
        case ts.SyntaxKind.AwaitExpression:
            return {
                kind: "AwaitExpression",
                expression: visit(node.expression)
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
        case ts.SyntaxKind.ThisKeyword:
            return {
                kind: "Identifier",
                name: "this"
            };
        case ts.SyntaxKind.SuperKeyword:
            return {
                kind: "SuperExpression"
            };
        case ts.SyntaxKind.SpreadElement:
        case ts.SyntaxKind.SpreadAssignment:
            return {
                kind: "SpreadElement",
                expression: visit(node.expression)
            };
        case ts.SyntaxKind.PrefixUnaryExpression:
        case ts.SyntaxKind.TypeOfExpression:
            return {
                kind: "PrefixUnaryExpression",
                operator: node.kind === ts.SyntaxKind.TypeOfExpression ? "typeof" : ts.tokenToString(node.operator),
                operand: visit(node.kind === ts.SyntaxKind.TypeOfExpression ? node.expression : node.operand)
            };
        case ts.SyntaxKind.PostfixUnaryExpression:
            return {
                kind: "PostfixUnaryExpression",
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
                        name: visit(decl.name),
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
                    name: visit(decl.name),
                    initializer: null // No initializer in for-of
                };
            }
            return {
                kind: "ForOfStatement",
                initializer: forOfInit,
                expression: visit(node.expression),
                body: visit(node.statement)
            };
        case ts.SyntaxKind.ForInStatement:
            let forInInit = null;
            if (node.initializer.kind === ts.SyntaxKind.VariableDeclarationList) {
                const decl = node.initializer.declarations[0];
                forInInit = {
                    kind: "VariableDeclaration",
                    name: visit(decl.name),
                    initializer: null
                };
            }
            return {
                kind: "ForInStatement",
                initializer: forInInit,
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
        case ts.SyntaxKind.ArrowFunction:
            return {
                kind: "ArrowFunction",
                isAsync: isAsync(node),
                parameters: node.parameters.map(visitParameter),
                body: visit(node.body) // Body can be Block or Expression
            };
        case ts.SyntaxKind.FunctionExpression:
            return {
                kind: "FunctionExpression",
                name: node.name ? node.name.text : null,
                isAsync: isAsync(node),
                typeParameters: getTypeParameters(node),
                returnType: node.type ? node.type.getText(currentSourceFile) : "any",
                parameters: node.parameters.map(visitParameter),
                body: visitBlock(node.body)
            };
        case ts.SyntaxKind.TemplateExpression:
            return {
                kind: "TemplateExpression",
                head: node.head.text,
                templateSpans: node.templateSpans.map(span => ({
                    expression: visit(span.expression),
                    literal: span.literal.text
                }))
            };
        case ts.SyntaxKind.NoSubstitutionTemplateLiteral:
            return {
                kind: "StringLiteral",
                value: node.text
            };
        case ts.SyntaxKind.AsExpression:
            return {
                kind: "AsExpression",
                expression: visit(node.expression),
                type: node.type.getText(currentSourceFile)
            };
        case ts.SyntaxKind.TryStatement:
            return {
                kind: "TryStatement",
                tryBlock: visitBlock(node.tryBlock),
                catchClause: node.catchClause ? {
                    kind: "CatchClause",
                    variable: node.catchClause.variableDeclaration ? visit(node.catchClause.variableDeclaration.name) : null,
                    block: visitBlock(node.catchClause.block)
                } : null,
                finallyBlock: node.finallyBlock ? visitBlock(node.finallyBlock) : null
            };
        case ts.SyntaxKind.ThrowStatement:
            return {
                kind: "ThrowStatement",
                expression: visit(node.expression)
            };
        case ts.SyntaxKind.ExportAssignment:
            return {
                kind: "ExportAssignment",
                expression: visit(node.expression),
                isExportEquals: !!node.isExportEquals
            };
        case ts.SyntaxKind.TypeAliasDeclaration:
            return {
                kind: "TypeAliasDeclaration",
                name: node.name.text,
                type: node.type.getText(currentSourceFile),
                typeParameters: getTypeParameters(node),
                isExported: isExported(node)
            };
        case ts.SyntaxKind.EnumDeclaration:
            return {
                kind: "EnumDeclaration",
                name: node.name.text,
                members: node.members.map(m => ({
                    name: m.name.getText(currentSourceFile),
                    initializer: m.initializer ? visit(m.initializer) : null
                })),
                isExported: isExported(node)
            };
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

function visitParameter(p) {
        return {
            kind: "Parameter",
            name: visit(p.name),
            type: p.type ? p.type.getText(currentSourceFile) : "any",
            isOptional: !!p.questionToken || !!p.initializer,
            isRest: !!p.dotDotDotToken,
            initializer: p.initializer ? visit(p.initializer) : null,
            access: getAccessModifier(p),
            isReadonly: isReadonly(p)
        };
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

// console.error("ObjectLiteralExpression kind:", ts.SyntaxKind.ObjectLiteralExpression);

const outputFileName = process.argv[3];
const astJson = printAST(sourceFile);

if (outputFileName) {
    fs.writeFileSync(outputFileName, astJson, "utf-8");
} else {
    console.log(astJson);
}
