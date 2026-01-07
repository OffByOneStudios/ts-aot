#pragma once

#include <memory>
#include <map>
#include <vector>
#include "../ast/AstNodes.h"
#include "SymbolTable.h"
#include "Module.h"
#include "ModuleResolver.h"

namespace ts {

struct CallSignature {
    std::vector<std::shared_ptr<Type>> argTypes;
    std::vector<std::shared_ptr<Type>> typeArguments;
};

class Analyzer : public ast::Visitor {
public:
    Analyzer();

    // Run the analysis pass on the program
    void analyze(ast::Program* program, const std::string& path = "main");

    // Dump inferred types to stdout as JSON
    void dumpTypes(ast::Program* program);

    // Run escape analysis to identify stack-allocatable objects
    void performEscapeAnalysis(ast::Program* program);

    // Get the symbol table (for later passes)
    SymbolTable& getSymbolTable() { return symbols; }
    const SymbolTable& getSymbolTable() const { return symbols; }

    // Get usage information (Function Name -> List of Argument Type Lists)
    const std::map<std::string, std::vector<CallSignature>>& getFunctionUsages() const { return functionUsages; }
    const std::map<std::string, std::vector<std::vector<std::shared_ptr<Type>>>>& getClassUsages() const { return classUsages; }

    int getErrorCount() const { return errorCount; }
    void setVerbose(bool v) { verbose = v; }

    // Load tsconfig.json for path aliases and baseUrl resolution
    bool loadTsConfig(const std::string& tsconfigPath);

    // Set the project root directory (for module resolution)
    void setProjectRoot(const std::string& rootPath);

    // Analyze a function body with specific argument types to determine return type
    std::shared_ptr<Type> analyzeFunctionBody(ast::FunctionDeclaration* node, const std::vector<std::shared_ptr<Type>>& argTypes, const std::vector<std::shared_ptr<Type>>& typeArguments = {});
    std::shared_ptr<ClassType> analyzeClassBody(ast::ClassDeclaration* node, const std::vector<std::shared_ptr<Type>>& typeArguments);
    std::shared_ptr<Type> analyzeMethodBody(ast::MethodDefinition* node, std::shared_ptr<ClassType> classType, const std::vector<std::shared_ptr<Type>>& typeArguments);

    void registerStdLib();
    void registerFS();
    void registerPath();
    void registerEvents();
    void registerStreams();
    void registerProcess();
    void registerBuffer();
    void registerNet();
    void registerHTTP();
    void registerHTTPS();
    void registerUtil();

    void reportError(const std::string& message);

    std::shared_ptr<Type> parseType(const std::string& typeName, SymbolTable& symbols);
    std::shared_ptr<Type> substitute(std::shared_ptr<Type> type, const std::map<std::string, std::shared_ptr<Type>>& env);

    static std::string manglePrivateName(const std::string& name, const std::string& className) {
        if (name.starts_with("#")) {
            return "__private_" + className + "_" + name.substr(1);
        }
        return name;
    }

    std::map<std::string, std::shared_ptr<Module>> modules;
    std::vector<std::string> moduleOrder;
    std::vector<ast::Expression*> expressions;
    std::vector<std::shared_ptr<Symbol>> topLevelVariables;

    // Synthetic functions (e.g., module init) are not part of any module AST body.
    // Track ownership so we can apply the correct module context during body analysis.
    std::map<std::string, std::string> syntheticFunctionOwners;

    ModuleType currentModuleType = ModuleType::TypeScript;

    // Module resolution for JSON imports (public for codegen access)
    ResolvedModule resolveModule(const std::string& specifier);

private:
    SymbolTable symbols;
    std::shared_ptr<Type> lastType; // Result of the last visited expression
    std::shared_ptr<Type> currentReturnType; // Inferred return type of the current function
    std::map<std::string, std::vector<CallSignature>> functionUsages;
    std::map<std::string, std::vector<std::vector<std::shared_ptr<Type>>>> classUsages;
    int errorCount = 0;
    bool suppressErrors = false; // Permissive mode for untyped JS
    int functionDepth = 0;
    bool verbose = false;
    bool skipUntypedSemantic = false; // Skip expensive semantic checks for raw JS

    // Contextual typing stack - used to propagate expected types to arrow functions
    std::vector<std::shared_ptr<Type>> contextualTypeStack;
    
    void pushContextualType(std::shared_ptr<Type> type) {
        contextualTypeStack.push_back(type);
    }
    
    void popContextualType() {
        if (!contextualTypeStack.empty()) {
            contextualTypeStack.pop_back();
        }
    }
    
    std::shared_ptr<Type> getContextualType() const {
        return contextualTypeStack.empty() ? nullptr : contextualTypeStack.back();
    }

    void visit(ast::Node* node);
    void visitProgram(ast::Program* node) override;
    void visitClassDeclaration(ast::ClassDeclaration* node) override;
    void visitInterfaceDeclaration(ast::InterfaceDeclaration* node);
    void visitFunctionDeclaration(ast::FunctionDeclaration* node) override;
    void visitVariableDeclaration(ast::VariableDeclaration* node) override;
    void visitExpressionStatement(ast::ExpressionStatement* node) override;
    void visitReturnStatement(ast::ReturnStatement* node) override;
    void visitCallExpression(ast::CallExpression* node) override;
    void visitNewExpression(ast::NewExpression* node) override;

    // Helper functions for Promise type inference
    std::shared_ptr<Type> inferPromiseTypeFromExecutor(ast::Node* body, const std::string& resolveParamName);
    std::shared_ptr<Type> inferPromiseTypeFromFunctionBody(const std::vector<ast::StmtPtr>& statements, const std::string& resolveParamName);

    void visitParenthesizedExpression(ast::ParenthesizedExpression* node) override;
    void visitObjectLiteralExpression(ast::ObjectLiteralExpression* node) override;
    void visitPropertyAssignment(ast::PropertyAssignment* node) override;
    void visitShorthandPropertyAssignment(ast::ShorthandPropertyAssignment* node) override;
    void visitComputedPropertyName(ast::ComputedPropertyName* node) override;
    void visitMethodDefinition(ast::MethodDefinition* node) override;
    void visitStaticBlock(ast::StaticBlock* node) override;
    void visitArrayLiteralExpression(ast::ArrayLiteralExpression* node) override;
    void visitElementAccessExpression(ast::ElementAccessExpression* node) override;
    void visitPropertyAccessExpression(ast::PropertyAccessExpression* node) override;
    void visitAsExpression(ast::AsExpression* node);
    void visitBinaryExpression(ast::BinaryExpression* node) override;
    void visitConditionalExpression(ast::ConditionalExpression* node) override;
    void visitAssignmentExpression(ast::AssignmentExpression* node) override;
    void visitIfStatement(ast::IfStatement* node) override;
    void visitWhileStatement(ast::WhileStatement* node) override;
    void visitForStatement(ast::ForStatement* node) override;
    void visitForOfStatement(ast::ForOfStatement* node) override;
    void visitForInStatement(ast::ForInStatement* node) override;
    void visitSwitchStatement(ast::SwitchStatement* node) override;
    void visitTryStatement(ast::TryStatement* node) override;
    void visitThrowStatement(ast::ThrowStatement* node) override;
    void visitImportDeclaration(ast::ImportDeclaration* node) override;
    void visitExportDeclaration(ast::ExportDeclaration* node) override;
    void visitExportAssignment(ast::ExportAssignment* node) override;
    void visitBreakStatement(ast::BreakStatement* node) override;
    void visitContinueStatement(ast::ContinueStatement* node) override;
    void visitTemplateExpression(ast::TemplateExpression* node);
    void visitTaggedTemplateExpression(ast::TaggedTemplateExpression* node);
    void visitPrefixUnaryExpression(ast::PrefixUnaryExpression* node) override;
    void visitDeleteExpression(ast::DeleteExpression* node) override;
    void visitPostfixUnaryExpression(ast::PostfixUnaryExpression* node) override;
    void visitBlockStatement(ast::BlockStatement* node) override;
    void visitIdentifier(ast::Identifier* node) override;
    void visitSuperExpression(ast::SuperExpression* node) override;
    void visitStringLiteral(ast::StringLiteral* node) override;
    void visitRegularExpressionLiteral(ast::RegularExpressionLiteral* node) override;
    void visitNumericLiteral(ast::NumericLiteral* node) override;
    void visitBigIntLiteral(ast::BigIntLiteral* node) override;
    void visitBooleanLiteral(ast::BooleanLiteral* node) override;
    void visitNullLiteral(ast::NullLiteral* node) override;
    void visitUndefinedLiteral(ast::UndefinedLiteral* node) override;
    void visitAwaitExpression(ast::AwaitExpression* node) override;
    void visitYieldExpression(ast::YieldExpression* node) override;
    void visitArrowFunction(ast::ArrowFunction* node);
    void visitFunctionExpression(ast::FunctionExpression* node);
    void visitObjectBindingPattern(ast::ObjectBindingPattern* node);
    void visitArrayBindingPattern(ast::ArrayBindingPattern* node);
    void visitBindingElement(ast::BindingElement* node);
    void visitSpreadElement(ast::SpreadElement* node);
    void visitOmittedExpression(ast::OmittedExpression* node);
    void visitTypeAliasDeclaration(ast::TypeAliasDeclaration* node) override;
    void visitEnumDeclaration(ast::EnumDeclaration* node) override;

    void declareBindingPattern(ast::Node* pattern, std::shared_ptr<Type> type);

    std::shared_ptr<FunctionType> resolveOverload(const std::vector<std::shared_ptr<FunctionType>>& overloads, const std::vector<std::shared_ptr<Type>>& argTypes);

    void visitMethodDefinition(ast::MethodDefinition* node, std::shared_ptr<ClassType> classType);
    void visitPropertyDefinition(ast::PropertyDefinition* node, std::shared_ptr<ClassType> classType);

    void checkInterfaceImplementation(std::shared_ptr<ClassType> classType, std::shared_ptr<InterfaceType> interfaceType);

    std::vector<std::shared_ptr<Type>> inferTypeArguments(
        const std::vector<std::shared_ptr<TypeParameterType>>& typeParams,
        const std::vector<std::shared_ptr<Type>>& paramTypes,
        const std::vector<std::shared_ptr<Type>>& argTypes);

    std::shared_ptr<Module> currentModule;
    std::string currentFilePath;
    ModuleResolver moduleResolver;

    void analyzeModule(std::shared_ptr<Module> module);
    std::shared_ptr<Module> loadModule(const std::string& specifier);

    std::shared_ptr<ClassType> currentClass;
    std::string currentMethodName;
};

} // namespace ts
