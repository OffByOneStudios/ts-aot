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

function isGenerator(node) {
    return !!node.asteriskToken;
}

function getDecorators(node) {
    const decorators = ts.canHaveDecorators(node) ? ts.getDecorators(node) : (node.decorators);
    if (!decorators) {
        // console.error("No decorators for", node.kind);
        return [];
    }
    return decorators.map(d => {
        const expr = d.expression;
        if (ts.isIdentifier(expr)) return expr.text;
        if (ts.isCallExpression(expr)) {
            if (ts.isIdentifier(expr.expression)) return expr.expression.text;
            return expr.expression.getText(currentSourceFile);
        }
        if (ts.isPropertyAccessExpression(expr)) {
            return expr.getText(currentSourceFile);
        }
        return expr.getText(currentSourceFile);
    });
}

function hasDecorator(node, name) {
    // Check for actual decorators
    const decorators = ts.canHaveDecorators(node) ? ts.getDecorators(node) : (node.decorators);
    if (decorators) {
        const found = decorators.some(d => {
            const expr = d.expression;
            if (ts.isIdentifier(expr)) return expr.text === name;
            if (ts.isCallExpression(expr) && ts.isIdentifier(expr.expression)) return expr.expression.text === name;
            return false;
        });
        if (found) return true;
    }

    // Check for JSDoc @struct
    const jsDoc = node.jsDoc;
    if (jsDoc) {
        for (const doc of jsDoc) {
            if (doc.tags) {
                for (const tag of doc.tags) {
                    if (tag.tagName.text === name) return true;
                }
            }
        }
    }

    // Check for comment-based @struct in the text if jsDoc is missing
    const text = node.getFullText();
    if (text.includes("@" + name)) {
        return true;
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
    let result = visitInternal(node);
    if (result && typeof result === 'object' && !Array.isArray(result)) {
        const { line, character } = currentSourceFile.getLineAndCharacterOfPosition(node.getStart());
        result.line = line + 1;
        result.column = character + 1;
        result.sourceFile = currentSourceFile.fileName;
    }
    return result;
}

function visitInternal(node) {
    if (!node) return null;
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
                isGenerator: isGenerator(node),
                typeParameters: getTypeParameters(node),
                returnType: node.type ? node.type.getText(currentSourceFile) : "",
                parameters: node.parameters.map(visitParameter),
                body: visitBlock(node.body),
                decorators: getDecorators(node)
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
                isAbstract: isAbstract(node),
                isStruct: hasDecorator(node, "struct"),
                decorators: getDecorators(node)
            };

        case ts.SyntaxKind.ClassStaticBlockDeclaration:
            return {
                kind: "StaticBlock",
                body: visitBlock(node.body)
            };
        case ts.SyntaxKind.ClassExpression: {
            let classExprBaseClass = "";
            let classExprImplements = [];
            if (node.heritageClauses) {
                for (const clause of node.heritageClauses) {
                    if (clause.token === ts.SyntaxKind.ExtendsKeyword) {
                        classExprBaseClass = clause.types[0].expression.getText(currentSourceFile);
                    } else if (clause.token === ts.SyntaxKind.ImplementsKeyword) {
                        classExprImplements = clause.types.map(t => t.expression.getText(currentSourceFile));
                    }
                }
            }
            return {
                kind: "ClassExpression",
                name: node.name ? node.name.text : "",
                typeParameters: getTypeParameters(node),
                baseClass: classExprBaseClass,
                implementsInterfaces: classExprImplements,
                members: node.members.map(visit).filter(m => m)
            };
        }
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
                name: node.name ? (node.name.text || node.name.getText(currentSourceFile)) : "",
                type: node.type ? node.type.getText(currentSourceFile) : "",
                initializer: node.initializer ? visit(node.initializer) : null,
                access: getAccessModifier(node),
                isStatic: isStatic(node),
                isReadonly: isReadonly(node),
                decorators: getDecorators(node)
            };
        case ts.SyntaxKind.MethodDeclaration:
        case ts.SyntaxKind.MethodSignature:
        case ts.SyntaxKind.GetAccessor:
        case ts.SyntaxKind.SetAccessor:
            return {
                kind: "MethodDefinition",
                name: node.name ? (node.name.text || node.name.getText(currentSourceFile)) : "",
                nameNode: visit(node.name),
                isAsync: isAsync(node),
                isGenerator: !!node.asteriskToken,
                typeParameters: getTypeParameters(node),
                parameters: node.parameters.map(visitParameter),
                returnType: node.type ? node.type.getText(currentSourceFile) : "",
                body: node.body ? visitBlock(node.body) : [],
                hasBody: !!node.body,
                access: getAccessModifier(node),
                isStatic: isStatic(node),
                isAbstract: isAbstract(node),
                isGetter: node.kind === ts.SyntaxKind.GetAccessor,
                isSetter: node.kind === ts.SyntaxKind.SetAccessor,
                decorators: getDecorators(node)
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
            // Handle all declarations in the statement (e.g., var a = 1, b = 2, c = 3)
            const declarations = node.declarationList.declarations.map(decl => ({
                kind: "VariableDeclaration",
                name: visit(decl.name),
                isExported: isExported(node),
                type: decl.type ? decl.type.getText(currentSourceFile) : "",
                initializer: decl.initializer ? visit(decl.initializer) : null
            }));
            // If there's only one declaration, return it directly; otherwise return an array marker
            // that the AST loader will expand
            if (declarations.length === 1) {
                return declarations[0];
            } else {
                // Return a special node that signals multiple declarations
                // The AST loader will need to handle this and expand it into separate statements
                return {
                    kind: "MultipleVariableDeclarations",
                    declarations: declarations
                };
            }
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
                typeArguments: getTypeArguments(node),
                isOptional: !!node.questionDotToken
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
                argumentExpression: visit(node.argumentExpression),
                isOptional: !!node.questionDotToken
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
        case ts.SyntaxKind.ConditionalExpression:
            return {
                kind: "ConditionalExpression",
                condition: visit(node.condition),
                whenTrue: visit(node.whenTrue),
                whenFalse: visit(node.whenFalse)
            };
        case ts.SyntaxKind.AwaitExpression:
            return {
                kind: "AwaitExpression",
                expression: visit(node.expression)
            };
        case ts.SyntaxKind.YieldExpression:
            return {
                kind: "YieldExpression",
                expression: node.expression ? visit(node.expression) : null,
                isAsterisk: !!node.asteriskToken
            };
        case ts.SyntaxKind.StringLiteral:
            return {
                kind: "StringLiteral",
                value: node.text
            };
        case ts.SyntaxKind.RegularExpressionLiteral:
            return {
                kind: "RegularExpressionLiteral",
                text: node.text
            };
        case ts.SyntaxKind.NumericLiteral:
            return {
                kind: "NumericLiteral",
                value: Number(node.text)
            };
        case ts.SyntaxKind.BigIntLiteral:
            return {
                kind: "BigIntLiteral",
                value: node.text
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
        case ts.SyntaxKind.NullKeyword:
            return {
                kind: "NullLiteral"
            };
        case ts.SyntaxKind.UndefinedKeyword:
            return {
                kind: "UndefinedLiteral"
            };
        case ts.SyntaxKind.ObjectLiteralExpression:
            return {
                kind: "ObjectLiteralExpression",
                properties: node.properties.map(p => {
                    if (p.kind === ts.SyntaxKind.PropertyAssignment) {
                        return {
                            kind: "PropertyAssignment",
                            name: p.name.getText(currentSourceFile),
                            nameNode: visit(p.name),
                            initializer: visit(p.initializer)
                        };
                    } else if (p.kind === ts.SyntaxKind.ShorthandPropertyAssignment) {
                        return {
                            kind: "ShorthandPropertyAssignment",
                            name: p.name.getText(currentSourceFile),
                            nameNode: visit(p.name)
                        };
                    } else {
                        return visit(p);
                    }
                }).filter(p => p !== null)
            };
        case ts.SyntaxKind.ParenthesizedExpression:
            return {
                kind: "ParenthesizedExpression",
                expression: visit(node.expression)
            };
        case ts.SyntaxKind.Identifier:
            return {
                kind: "Identifier",
                name: node.text
            };
        case ts.SyntaxKind.PrivateIdentifier:
            return {
                kind: "Identifier",
                name: node.text,
                isPrivate: true
            };
        case ts.SyntaxKind.Parameter:
            return visitParameter(node);
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
        case ts.SyntaxKind.DeleteExpression:
            return {
                kind: "DeleteExpression",
                expression: visit(node.expression)
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
                name: node.name.text,
                isOptional: !!node.questionDotToken
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
                body: visit(node.statement),
                isAwait: !!node.awaitModifier
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
                body: visitBlock(node)
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
                isGenerator: !!node.asteriskToken,
                typeParameters: getTypeParameters(node),
                returnType: node.type ? node.type.getText(currentSourceFile) : "",
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
        case ts.SyntaxKind.TaggedTemplateExpression:
            return {
                kind: "TaggedTemplateExpression",
                tag: visit(node.tag),
                template: visit(node.template)
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
        case ts.SyntaxKind.ComputedPropertyName:
            return {
                kind: "ComputedPropertyName",
                expression: visit(node.expression)
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
        case ts.SyntaxKind.EmptyStatement:
            // No-op statement; omit from AST
            return null;
        case ts.SyntaxKind.DoStatement:
            // Lower do { body } while (test) into an equivalent block:
            // { body; while (test) { body; } }
            const bodyOnce = visit(node.statement);
            const bodyLoop = visit(node.statement);
            const testExpr = visit(node.expression);
            return {
                kind: "BlockStatement",
                body: [
                    bodyOnce,
                    {
                        kind: "WhileStatement",
                        condition: testExpr,
                        statement: bodyLoop
                    }
                ]
            };
        case ts.SyntaxKind.LabeledStatement:
            // Drop the label and keep the inner statement to avoid label handling downstream.
            return visit(node.statement);
        case ts.SyntaxKind.Decorator:
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

function visitParameter(p) {
    const access = getAccessModifier(p);
    const readonly = isReadonly(p);
    const { line, character } = currentSourceFile.getLineAndCharacterOfPosition(p.getStart());
    return {
        kind: "Parameter",
        name: visit(p.name),
        type: p.type ? p.type.getText(currentSourceFile) : "",
        isOptional: !!p.questionToken || !!p.initializer,
        isRest: !!p.dotDotDotToken,
        initializer: p.initializer ? visit(p.initializer) : null,
        access: access,
        isReadonly: readonly,
        isParameterProperty: !!access || readonly,
        line: line + 1,
        column: character + 1,
        sourceFile: currentSourceFile.fileName
    };
}

const fileName = process.argv[2];
const outputFileName = process.argv[3];

if (!fileName || !outputFileName) {
    console.error("Usage: node dump_ast.js <input.ts> <output.json>");
    process.exit(1);
}

const sourceCode = fs.readFileSync(fileName, "utf-8");
// Determine script kind based on file extension
const ext = fileName.toLowerCase();
let scriptKind = ts.ScriptKind.TS;
if (ext.endsWith('.js') || ext.endsWith('.jsx') || ext.endsWith('.mjs') || ext.endsWith('.cjs')) {
    scriptKind = ts.ScriptKind.JS;
} else if (ext.endsWith('.ts')) {
    scriptKind = ts.ScriptKind.TS;
} else if (ext.endsWith('.tsx') || ext.endsWith('.jsx')) {
    scriptKind = ts.ScriptKind.TSX;
}
const sourceFile = ts.createSourceFile(
    fileName,
    sourceCode,
    ts.ScriptTarget.Latest,
    true,
    scriptKind
);

const astJson = printAST(sourceFile);
fs.writeFileSync(outputFileName, astJson, "utf-8");
