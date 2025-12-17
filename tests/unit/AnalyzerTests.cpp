#include <catch2/catch_test_macros.hpp>
#include "analysis/Analyzer.h"
#include "ast/AstNodes.h"

using namespace ts;
using namespace ast;

TEST_CASE("Analyzer Type Inference", "[compiler][analyzer]") {
    SECTION("Numeric Literal Inference") {
        // Test Integer inference
        {
            auto num = std::make_unique<NumericLiteral>();
            num->value = 42.0;
            
            Analyzer analyzer;
            auto func = std::make_unique<FunctionDeclaration>();
            func->name = "test_int";
            
            auto returnStmt = std::make_unique<ReturnStatement>();
            returnStmt->expression = std::move(num);
            
            func->body.push_back(std::move(returnStmt));
            
            std::vector<std::shared_ptr<Type>> args;
            auto retType = analyzer.analyzeFunctionBody(func.get(), args);
            
            REQUIRE(retType->kind == TypeKind::Int);
        }

        // Test Double inference
        {
            auto num = std::make_unique<NumericLiteral>();
            num->value = 42.5;
            
            Analyzer analyzer;
            auto func = std::make_unique<FunctionDeclaration>();
            func->name = "test_dbl";
            
            auto returnStmt = std::make_unique<ReturnStatement>();
            returnStmt->expression = std::move(num);
            
            func->body.push_back(std::move(returnStmt));
            
            std::vector<std::shared_ptr<Type>> args;
            auto retType = analyzer.analyzeFunctionBody(func.get(), args);
            
            REQUIRE(retType->kind == TypeKind::Double);
        }
    }

    SECTION("String Literal Inference") {
        auto str = std::make_unique<StringLiteral>();
        str->value = "hello";
        
        Analyzer analyzer;
        auto func = std::make_unique<FunctionDeclaration>();
        func->name = "test";
        
        auto returnStmt = std::make_unique<ReturnStatement>();
        returnStmt->expression = std::move(str);
        
        func->body.push_back(std::move(returnStmt));
        
        std::vector<std::shared_ptr<Type>> args;
        auto retType = analyzer.analyzeFunctionBody(func.get(), args);
        
        REQUIRE(retType->kind == TypeKind::String);
    }

    SECTION("Parameter Type Propagation") {
        // function test(n: number) { return n; }
        auto func = std::make_unique<FunctionDeclaration>();
        func->name = "test";
        
        auto param = std::make_unique<Parameter>();
        param->name = "n";
        param->type = "number";
        func->parameters.push_back(std::move(param));
        
        auto id = std::make_unique<Identifier>();
        id->name = "n";
        
        auto returnStmt = std::make_unique<ReturnStatement>();
        returnStmt->expression = std::move(id);
        
        func->body.push_back(std::move(returnStmt));
        
        Analyzer analyzer;
        std::vector<std::shared_ptr<Type>> args = { std::make_shared<Type>(TypeKind::Double) };
        auto retType = analyzer.analyzeFunctionBody(func.get(), args);
        
        REQUIRE(retType->kind == TypeKind::Double);
    }
}
