#pragma once

#include <memory>
#include <string>
#include <vector>

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/IRBuilder.h>

#include "../analysis/Analyzer.h"
#include "../analysis/Monomorphizer.h"
#include "../ast/AstNodes.h"

namespace ts {

class IRGenerator : public ast::Visitor {
public:
    IRGenerator();

    void generate(ast::Program* program, const std::vector<Specialization>& specializations, const Analyzer& analyzer);
    void emitObjectCode(const std::string& filename);
    void dumpIR();
    void setOptLevel(const std::string& level) { optLevel = level; }
    void setRuntimeBitcode(const std::string& path) { runtimeBitcodePath = path; }

private:
    std::unique_ptr<llvm::LLVMContext> context;
    std::unique_ptr<llvm::Module> module;
    std::unique_ptr<llvm::IRBuilder<>> builder;
    std::vector<Specialization> specializations;
    std::shared_ptr<Type> currentReturnType;
    std::string optLevel = "0";
    std::string runtimeBitcodePath;

    llvm::Type* getLLVMType(const std::shared_ptr<Type>& type);
    void generateGlobals(const Analyzer& analyzer);
    void generateClasses(const Analyzer& analyzer, const std::vector<Specialization>& specializations);
    void generatePrototypes(const std::vector<Specialization>& specializations);
    void generateBodies(const std::vector<Specialization>& specializations);
    void generateAsyncFunctionBody(llvm::Function* entryFunc, ast::Node* node, const std::vector<std::shared_ptr<Type>>& argTypes, std::shared_ptr<Type> classType, const std::string& specializedName);
    void generateEntryPoint();

    void visitBlockStatement(ast::BlockStatement* node);
    void visitVariableDeclaration(ast::VariableDeclaration* node);
    void visitReturnStatement(ast::ReturnStatement* node);
    void visitBinaryExpression(ast::BinaryExpression* node);
    void visitConditionalExpression(ast::ConditionalExpression* node);
    void visitAssignmentExpression(ast::AssignmentExpression* node);
    void visitIdentifier(ast::Identifier* node);
    void visitNumericLiteral(ast::NumericLiteral* node);
    void visitBooleanLiteral(ast::BooleanLiteral* node);
    void visitNullLiteral(ast::NullLiteral* node);
    void visitUndefinedLiteral(ast::UndefinedLiteral* node);
    void visitStringLiteral(ast::StringLiteral* node);
    void visitRegularExpressionLiteral(ast::RegularExpressionLiteral* node);
    void visitAwaitExpression(ast::AwaitExpression* node);
    void visitArrowFunction(ast::ArrowFunction* node);
    void visitFunctionExpression(ast::FunctionExpression* node);
    void visitObjectBindingPattern(ast::ObjectBindingPattern* node);
    void visitArrayBindingPattern(ast::ArrayBindingPattern* node);
    void visitBindingElement(ast::BindingElement* node);
    void visitSpreadElement(ast::SpreadElement* node);
    void visitOmittedExpression(ast::OmittedExpression* node);
    void visitTemplateExpression(ast::TemplateExpression* node);
    void visitAsExpression(ast::AsExpression* node);
    void visitPrefixUnaryExpression(ast::PrefixUnaryExpression* node);
    void visitPostfixUnaryExpression(ast::PostfixUnaryExpression* node);
    void visitSuperExpression(ast::SuperExpression* node);
    void visitExpressionStatement(ast::ExpressionStatement* node);
    void visitIfStatement(ast::IfStatement* node);
    void visitWhileStatement(ast::WhileStatement* node);
    void visitForStatement(ast::ForStatement* node);
    void visitForOfStatement(ast::ForOfStatement* node);
    void visitForInStatement(ast::ForInStatement* node);
    void visitSwitchStatement(ast::SwitchStatement* node);
    void visitBreakStatement(ast::BreakStatement* node);
    void visitContinueStatement(ast::ContinueStatement* node);
    void visitThrowStatement(ast::ThrowStatement* node);
    void visitTryStatement(ast::TryStatement* node);
    void visitFunctionDeclaration(ast::FunctionDeclaration* node);
    void visitClassDeclaration(ast::ClassDeclaration* node);
    void visitInterfaceDeclaration(ast::InterfaceDeclaration* node);
    void visitImportDeclaration(ast::ImportDeclaration* node);
    void visitExportDeclaration(ast::ExportDeclaration* node);
    void visitExportAssignment(ast::ExportAssignment* node);
    void visitCallExpression(ast::CallExpression* node) override;
    bool tryGenerateMemberCall(ast::CallExpression* node);
    bool tryGenerateBuiltinCall(ast::CallExpression* node, ast::PropertyAccessExpression* prop);
    void visitNewExpression(ast::NewExpression* node);
    void visitArrayLiteralExpression(ast::ArrayLiteralExpression* node);
    void visitElementAccessExpression(ast::ElementAccessExpression* node);
    void visitPropertyAccessExpression(ast::PropertyAccessExpression* node);
    void visitObjectLiteralExpression(ast::ObjectLiteralExpression* node);
    void visitProgram(ast::Program* node);
    void visitTypeAliasDeclaration(ast::TypeAliasDeclaration* node);
    void visitEnumDeclaration(ast::EnumDeclaration* node);

    void visit(ast::Node* node);

    llvm::Value* boxValue(llvm::Value* val, std::shared_ptr<Type> type);
    llvm::Value* unboxValue(llvm::Value* val, std::shared_ptr<Type> type);
    void generateDestructuring(llvm::Value* value, std::shared_ptr<Type> type, ast::Node* pattern);

    llvm::Value* createCall(llvm::FunctionType* ft, llvm::Value* callee, std::vector<llvm::Value*> args);
    llvm::Value* castValue(llvm::Value* val, llvm::Type* expectedType);

    llvm::AllocaInst* createEntryBlockAlloca(llvm::Function* function, const std::string& varName, llvm::Type* type);

    struct VariableInfo {
        std::string name;
        std::shared_ptr<Type> type;
        llvm::Type* llvmType;
    };
    void collectVariables(ast::Node* node, std::vector<VariableInfo>& vars);

    std::map<std::string, llvm::Value*> namedValues;
    llvm::Value* lastValue = nullptr;
    std::shared_ptr<Type> currentClass;
    std::map<std::string, std::shared_ptr<Type>> typeEnvironment;
    llvm::Value* currentContext = nullptr;

    // Async support
    llvm::Value* currentAsyncContext = nullptr;
    llvm::Value* currentAsyncResumedValue = nullptr;
    llvm::Value* currentAsyncFrame = nullptr;
    llvm::StructType* currentAsyncFrameType = nullptr;
    llvm::StructType* asyncContextType = nullptr;
    std::map<std::string, int> currentAsyncFrameMap;
    llvm::BasicBlock* asyncDispatcherBB = nullptr;
    std::vector<llvm::BasicBlock*> asyncStateBlocks;
    std::vector<llvm::BasicBlock*> catchStack;

    struct ClassLayout {
        std::vector<std::pair<std::string, std::shared_ptr<Type>>> allFields;
        std::vector<std::pair<std::string, std::shared_ptr<FunctionType>>> allMethods;
        std::map<std::string, int> fieldIndices;
        std::map<std::string, int> methodIndices;
    };
    std::map<std::string, ClassLayout> classLayouts;

    struct LoopInfo {
        llvm::BasicBlock* continueBlock;
        llvm::BasicBlock* breakBlock;
    };
    std::vector<LoopInfo> loopStack;

    int anonVarCounter = 0;

    llvm::Function* getRuntimeFunction(const std::string& name);
};

} // namespace ts
