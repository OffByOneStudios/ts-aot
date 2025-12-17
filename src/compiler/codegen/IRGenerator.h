#pragma once

#include <memory>
#include <string>
#include <vector>

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/IRBuilder.h>

#include "../analysis/Monomorphizer.h"

namespace ts {

class IRGenerator {
public:
    IRGenerator();

    void generate(const std::vector<Specialization>& specializations);
    void emitObjectCode(const std::string& filename);
    void dumpIR();

private:
    std::unique_ptr<llvm::LLVMContext> context;
    std::unique_ptr<llvm::Module> module;
    std::unique_ptr<llvm::IRBuilder<>> builder;

    llvm::Type* getLLVMType(const std::shared_ptr<Type>& type);
    void generatePrototypes(const std::vector<Specialization>& specializations);
    void generateBodies(const std::vector<Specialization>& specializations);
    void generateEntryPoint();

    void visit(ast::Node* node);
    void visitReturnStatement(ast::ReturnStatement* node);
    void visitBinaryExpression(ast::BinaryExpression* node);
    void visitIdentifier(ast::Identifier* node);
    void visitNumericLiteral(ast::NumericLiteral* node);
    void visitStringLiteral(ast::StringLiteral* node);
    void visitCallExpression(ast::CallExpression* node);
    void visitExpressionStatement(ast::ExpressionStatement* node);

    std::map<std::string, llvm::Value*> namedValues;
    llvm::Value* lastValue = nullptr;
};

} // namespace ts
