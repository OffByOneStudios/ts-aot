#pragma once

#include <memory>
#include <string>
#include <vector>

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/IRBuilder.h>

#include "../analysis/Analyzer.h"
#include "../analysis/Monomorphizer.h"

namespace ts {

class IRGenerator {
public:
    IRGenerator();

    void generate(ast::Program* program, const std::vector<Specialization>& specializations, const Analyzer& analyzer);
    void emitObjectCode(const std::string& filename);
    void dumpIR();

private:
    std::unique_ptr<llvm::LLVMContext> context;
    std::unique_ptr<llvm::Module> module;
    std::unique_ptr<llvm::IRBuilder<>> builder;

    llvm::Type* getLLVMType(const std::shared_ptr<Type>& type);
    void generateClasses(ast::Program* program, const Analyzer& analyzer);
    void generatePrototypes(const std::vector<Specialization>& specializations);
    void generateBodies(const std::vector<Specialization>& specializations);
    void generateEntryPoint();

    void visitBlockStatement(ast::BlockStatement* node);
    void visitVariableDeclaration(ast::VariableDeclaration* node);
    void visitReturnStatement(ast::ReturnStatement* node);
    void visitBinaryExpression(ast::BinaryExpression* node);
    void visitAssignmentExpression(ast::AssignmentExpression* node);
    void visitIdentifier(ast::Identifier* node);
    void visitNumericLiteral(ast::NumericLiteral* node);
    void visitBooleanLiteral(ast::BooleanLiteral* node);
    void visitArrowFunction(ast::ArrowFunction* node);
    void visitTemplateExpression(ast::TemplateExpression* node);
    void visitStringLiteral(ast::StringLiteral* node);
    void visitCallExpression(ast::CallExpression* node);
    void visitNewExpression(ast::NewExpression* node);
    void visitObjectLiteralExpression(ast::ObjectLiteralExpression* node);
    void visitArrayLiteralExpression(ast::ArrayLiteralExpression* node);
    void visitElementAccessExpression(ast::ElementAccessExpression* node);
    void visitPropertyAccessExpression(ast::PropertyAccessExpression* node);
    void visitAsExpression(ast::AsExpression* node);
    void visitExpressionStatement(ast::ExpressionStatement* node);
    void visitIfStatement(ast::IfStatement* node);
    void visitWhileStatement(ast::WhileStatement* node);
    void visitForStatement(ast::ForStatement* node);
    void visitForOfStatement(ast::ForOfStatement* node);
    void visitSwitchStatement(ast::SwitchStatement* node);
    void visitTryStatement(ast::TryStatement* node);
    void visitThrowStatement(ast::ThrowStatement* node);
    void visitBreakStatement(ast::BreakStatement* node);
    void visitContinueStatement(ast::ContinueStatement* node);
    void visitPrefixUnaryExpression(ast::PrefixUnaryExpression* node);
    void visitSuperExpression(ast::SuperExpression* node);
    void visit(ast::Node* node);

    llvm::Value* castValue(llvm::Value* val, llvm::Type* expectedType);

    llvm::AllocaInst* createEntryBlockAlloca(llvm::Function* function, const std::string& varName, llvm::Type* type);

    std::map<std::string, llvm::Value*> namedValues;
    llvm::Value* lastValue = nullptr;
    std::shared_ptr<Type> currentClass;

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

    llvm::Function* getRuntimeFunction(const std::string& name);
};

} // namespace ts
