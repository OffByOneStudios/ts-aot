#include "IRGenerator.h"
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/TargetParser/Host.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/IR/LegacyPassManager.h>

namespace ts {

IRGenerator::IRGenerator() {
    context = std::make_unique<llvm::LLVMContext>();
    module = std::make_unique<llvm::Module>("ts_module", *context);
    builder = std::make_unique<llvm::IRBuilder<>>(*context);
}

void IRGenerator::generate(ast::Program* program, const std::vector<Specialization>& specializations, const Analyzer& analyzer) {
    generateClasses(program, analyzer);
    generatePrototypes(specializations);
    generateBodies(specializations);
    generateEntryPoint();
}

void IRGenerator::generateEntryPoint() {
    llvm::Function* userMain = module->getFunction("user_main");
    if (!userMain) return;

    // Declare ts_main
    std::vector<llvm::Type*> args = {
        llvm::Type::getInt32Ty(*context),
        llvm::PointerType::getUnqual(*context),
        llvm::PointerType::getUnqual(*context)
    };
    llvm::FunctionType* ft = llvm::FunctionType::get(
        llvm::Type::getInt32Ty(*context), args, false);
    llvm::Function* tsMain = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, "ts_main", module.get());

    // Define main
    std::vector<llvm::Type*> mainArgs = {
        llvm::Type::getInt32Ty(*context),
        llvm::PointerType::getUnqual(*context)
    };
    llvm::FunctionType* mainFt = llvm::FunctionType::get(
        llvm::Type::getInt32Ty(*context), mainArgs, false);
    llvm::Function* mainFn = llvm::Function::Create(mainFt, llvm::Function::ExternalLinkage, "main", module.get());

    llvm::BasicBlock* bb = llvm::BasicBlock::Create(*context, "entry", mainFn);
    builder->SetInsertPoint(bb);

    llvm::Value* argc = mainFn->getArg(0);
    llvm::Value* argv = mainFn->getArg(1);
    
    builder->CreateCall(tsMain, { argc, argv, userMain });
    
    builder->CreateRet(llvm::ConstantInt::get(*context, llvm::APInt(32, 0)));
}

llvm::Type* IRGenerator::getLLVMType(const std::shared_ptr<Type>& type) {
    switch (type->kind) {
        case TypeKind::Void: return llvm::Type::getVoidTy(*context);
        case TypeKind::Int: return llvm::Type::getInt64Ty(*context);
        case TypeKind::Double: return llvm::Type::getDoubleTy(*context);
        case TypeKind::Boolean: return llvm::Type::getInt1Ty(*context);
        case TypeKind::Object: {
            auto objType = std::static_pointer_cast<ObjectType>(type);
            std::vector<llvm::Type*> fieldTypes;
            for (auto& [name, fieldType] : objType->fields) {
                fieldTypes.push_back(getLLVMType(fieldType));
            }
            llvm::StructType* structType = llvm::StructType::get(*context, fieldTypes);
            return llvm::PointerType::getUnqual(structType);
        }
        case TypeKind::Class: {
            auto classType = std::static_pointer_cast<ClassType>(type);
            llvm::StructType* structType = llvm::StructType::getTypeByName(*context, classType->name);
            if (!structType) {
                // Should have been created in generateClasses, but fallback just in case
                structType = llvm::StructType::create(*context, classType->name);
            }
            return llvm::PointerType::getUnqual(structType);
        }
        case TypeKind::String:
        case TypeKind::Any:
        case TypeKind::Array:
        case TypeKind::Union:
        case TypeKind::Intersection:
            return llvm::PointerType::getUnqual(*context);
        default: return llvm::Type::getVoidTy(*context);
    }
}

llvm::AllocaInst* IRGenerator::createEntryBlockAlloca(llvm::Function* function, const std::string& varName, llvm::Type* type) {
    llvm::IRBuilder<> tmpBuilder(&function->getEntryBlock(), function->getEntryBlock().begin());
    return tmpBuilder.CreateAlloca(type, nullptr, varName);
}

llvm::Function* IRGenerator::getRuntimeFunction(const std::string& name) {
    if (auto* f = module->getFunction(name)) return f;

    if (name == "ts_push_exception_handler") {
        llvm::FunctionType* ft = llvm::FunctionType::get(
            llvm::PointerType::getUnqual(*context), {}, false);
        return llvm::Function::Create(ft, llvm::Function::ExternalLinkage, name, module.get());
    } else if (name == "ts_pop_exception_handler") {
        llvm::FunctionType* ft = llvm::FunctionType::get(
            llvm::Type::getVoidTy(*context), {}, false);
        return llvm::Function::Create(ft, llvm::Function::ExternalLinkage, name, module.get());
    } else if (name == "ts_throw") {
        llvm::FunctionType* ft = llvm::FunctionType::get(
            llvm::Type::getVoidTy(*context), {llvm::PointerType::getUnqual(*context)}, false);
        return llvm::Function::Create(ft, llvm::Function::ExternalLinkage, name, module.get());
    } else if (name == "ts_get_exception") {
        llvm::FunctionType* ft = llvm::FunctionType::get(
            llvm::PointerType::getUnqual(*context), {}, false);
        return llvm::Function::Create(ft, llvm::Function::ExternalLinkage, name, module.get());
    } else if (name == "_setjmp") {
        // Windows x64 _setjmp(jmp_buf, frame_ptr)
        llvm::FunctionType* ft = llvm::FunctionType::get(
            llvm::Type::getInt32Ty(*context), 
            {llvm::PointerType::getUnqual(*context), llvm::PointerType::getUnqual(*context)}, 
            false);
        return llvm::Function::Create(ft, llvm::Function::ExternalLinkage, name, module.get());
    }

    return nullptr;
}

void IRGenerator::visit(ast::Node* node) {
    if (!node) return;
    if (auto ret = dynamic_cast<ast::ReturnStatement*>(node)) visitReturnStatement(ret);
    else if (auto bin = dynamic_cast<ast::BinaryExpression*>(node)) visitBinaryExpression(bin);
    else if (auto assign = dynamic_cast<ast::AssignmentExpression*>(node)) visitAssignmentExpression(assign);
    else if (auto id = dynamic_cast<ast::Identifier*>(node)) visitIdentifier(id);
    else if (auto num = dynamic_cast<ast::NumericLiteral*>(node)) visitNumericLiteral(num);
    else if (auto boolean = dynamic_cast<ast::BooleanLiteral*>(node)) visitBooleanLiteral(boolean);
    else if (auto str = dynamic_cast<ast::StringLiteral*>(node)) visitStringLiteral(str);
    else if (auto call = dynamic_cast<ast::CallExpression*>(node)) visitCallExpression(call);
    else if (auto obj = dynamic_cast<ast::ObjectLiteralExpression*>(node)) visitObjectLiteralExpression(obj);
    else if (auto prop = dynamic_cast<ast::PropertyAccessExpression*>(node)) visitPropertyAccessExpression(prop);
    else if (auto as = dynamic_cast<ast::AsExpression*>(node)) visitAsExpression(as);
    else if (auto exprStmt = dynamic_cast<ast::ExpressionStatement*>(node)) visitExpressionStatement(exprStmt);
    else if (auto ifStmt = dynamic_cast<ast::IfStatement*>(node)) visitIfStatement(ifStmt);
    else if (auto whileStmt = dynamic_cast<ast::WhileStatement*>(node)) visitWhileStatement(whileStmt);
    else if (auto forStmt = dynamic_cast<ast::ForStatement*>(node)) visitForStatement(forStmt);
    else if (auto forOfStmt = dynamic_cast<ast::ForOfStatement*>(node)) visitForOfStatement(forOfStmt);
    else if (auto* n = dynamic_cast<ast::SwitchStatement*>(node)) {
        visitSwitchStatement(n);
    } else if (auto* n = dynamic_cast<ast::TryStatement*>(node)) {
        visitTryStatement(n);
    } else if (auto* n = dynamic_cast<ast::ThrowStatement*>(node)) {
        visitThrowStatement(n);
    } else if (auto* n = dynamic_cast<ast::BreakStatement*>(node)) {
        visitBreakStatement(n);
    } else if (auto* n = dynamic_cast<ast::ContinueStatement*>(node)) {
        visitContinueStatement(n);
    } else if (auto block = dynamic_cast<ast::BlockStatement*>(node)) visitBlockStatement(block);
    else if (auto pre = dynamic_cast<ast::PrefixUnaryExpression*>(node)) visitPrefixUnaryExpression(pre);
    else if (auto varDecl = dynamic_cast<ast::VariableDeclaration*>(node)) visitVariableDeclaration(varDecl);
    else if (auto elem = dynamic_cast<ast::ElementAccessExpression*>(node)) visitElementAccessExpression(elem);
    else if (auto arr = dynamic_cast<ast::ArrayLiteralExpression*>(node)) visitArrayLiteralExpression(arr);
    else if (auto newExpr = dynamic_cast<ast::NewExpression*>(node)) visitNewExpression(newExpr);
    else if (auto sup = dynamic_cast<ast::SuperExpression*>(node)) visitSuperExpression(sup);
    else if (auto arrow = dynamic_cast<ast::ArrowFunction*>(node)) visitArrowFunction(arrow);
    else if (auto tmpl = dynamic_cast<ast::TemplateExpression*>(node)) visitTemplateExpression(tmpl);
}

void IRGenerator::dumpIR() {
    module->print(llvm::outs(), nullptr);
}

void IRGenerator::emitObjectCode(const std::string& filename) {
    // Initialize the target registry etc.
    llvm::InitializeAllTargetInfos();
    llvm::InitializeAllTargets();
    llvm::InitializeAllTargetMCs();
    llvm::InitializeAllAsmParsers();
    llvm::InitializeAllAsmPrinters();

    auto targetTriple = llvm::sys::getDefaultTargetTriple();
    module->setTargetTriple(targetTriple);

    std::string error;
    auto target = llvm::TargetRegistry::lookupTarget(targetTriple, error);

    if (!target) {
        llvm::errs() << error;
        return;
    }

    auto cpu = "generic";
    auto features = "";

    llvm::TargetOptions opt;
    auto rm = std::optional<llvm::Reloc::Model>();
    auto targetMachine = target->createTargetMachine(targetTriple, cpu, features, opt, rm);

    module->setDataLayout(targetMachine->createDataLayout());

    std::error_code ec;
    llvm::raw_fd_ostream dest(filename, ec, llvm::sys::fs::OF_None);

    if (ec) {
        llvm::errs() << "Could not open file: " << ec.message();
        return;
    }

    llvm::legacy::PassManager pass;
    auto fileType = llvm::CodeGenFileType::ObjectFile;

    if (targetMachine->addPassesToEmitFile(pass, dest, nullptr, fileType)) {
        llvm::errs() << "TargetMachine can't emit a file of this type";
        return;
    }

    pass.run(*module);
    dest.flush();
}

llvm::Value* IRGenerator::castValue(llvm::Value* val, llvm::Type* expectedType) {
    if (!val) return nullptr;
    llvm::Type* valType = val->getType();
    if (valType == expectedType) return val;

    if (expectedType->isDoubleTy() && valType->isIntegerTy()) {
        return builder->CreateSIToFP(val, expectedType);
    }
    if (expectedType->isIntegerTy() && valType->isDoubleTy()) {
        return builder->CreateFPToSI(val, expectedType);
    }

    // Handle 'any' (pointers)
    if (expectedType->isPointerTy()) {
        if (valType->isDoubleTy()) {
            // Bitcast double to i64 then inttoptr
            llvm::Value* i64Val = builder->CreateBitCast(val, llvm::Type::getInt64Ty(*context));
            return builder->CreateIntToPtr(i64Val, expectedType);
        }
        if (valType->isIntegerTy()) {
            return builder->CreateIntToPtr(val, expectedType);
        }
    }

    if (valType->isPointerTy()) {
        if (expectedType->isDoubleTy()) {
            // ptrtoint then bitcast to double
            llvm::Value* i64Val = builder->CreatePtrToInt(val, llvm::Type::getInt64Ty(*context));
            return builder->CreateBitCast(i64Val, expectedType);
        }
        if (expectedType->isIntegerTy()) {
            return builder->CreatePtrToInt(val, expectedType);
        }
    }

    return builder->CreateBitCast(val, expectedType);
}

} // namespace ts
