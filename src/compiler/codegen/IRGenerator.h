#pragma once

#include <memory>
#include <string>
#include <vector>
#include <map>
#include <set>

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/DIBuilder.h>
#include <nlohmann/json.hpp>

#include "../analysis/Analyzer.h"
#include "../analysis/Monomorphizer.h"
#include "../ast/AstNodes.h"
#include "BoxingPolicy.h"

namespace ts {

class IRGenerator : public ast::Visitor {
public:
    IRGenerator();

    void generate(ast::Program* program, const std::vector<Specialization>& specializations, Analyzer& analyzer, const std::string& sourceFile);
    void dumpIR();
    void setOptLevel(const std::string& level) { optLevel = level; }
    void setRuntimeBitcode(const std::string& path) { runtimeBitcodePath = path; }
    void setVerbose(bool v) { verbose = v; }
    void setDebug(bool d) { debug = d; }
    void setDebugRuntime(bool d) { debugRuntime = d; }

    llvm::Module* getModule() { return module.get(); }

private:
    std::unique_ptr<llvm::LLVMContext> context;
    std::unique_ptr<llvm::Module> module;
    std::unique_ptr<llvm::IRBuilder<>> builder;
    std::unique_ptr<llvm::DIBuilder> diBuilder;
    llvm::DICompileUnit* compileUnit = nullptr;
    std::vector<llvm::DIScope*> debugScopes;
    std::map<std::string, llvm::DIFile*> diFileCache;  // Cache of DIFile per source file path

    std::vector<Specialization> specializations;
    std::shared_ptr<Type> currentReturnType;
    Analyzer* analyzer = nullptr;
    std::string currentSourceFile;
    std::string optLevel = "0";
    std::string runtimeBitcodePath;
    bool verbose = false;
    bool debug = false;
    bool debugRuntime = false;

    void emitLocation(ast::Node* node);
    llvm::DIType* createDebugType(std::shared_ptr<Type> type);
    llvm::DIFile* getOrCreateDIFile(const std::string& filePath);  // Get or create DIFile for source file

    void visitParenthesizedExpression(ast::ParenthesizedExpression* node) override;

    // Inline boxing infrastructure - TsValue type and helpers
    llvm::StructType* tsValueType = nullptr;  // { i8 type, i64 value } - 16 bytes aligned
    void initTsValueType();
    
    // Inline boxing/unboxing - avoids function call overhead
    // ValueType enum values (must match runtime/include/TsObject.h):
    //   UNDEFINED=0, NUMBER_INT=1, NUMBER_DBL=2, BOOLEAN=3, STRING_PTR=4,
    //   OBJECT_PTR=5, PROMISE_PTR=6, ARRAY_PTR=7, BIGINT_PTR=8, SYMBOL_PTR=9, FUNCTION_PTR=10
    llvm::Value* emitInlineBox(llvm::Value* rawPtr, uint8_t valueType);
    llvm::Value* emitInlineUnbox(llvm::Value* boxedVal);
    llvm::Value* emitTypeCheck(llvm::Value* boxedVal, uint8_t expectedType);
    
    // Inline IR operations - scalar-based helpers to avoid struct passing
    // These generate LLVM IR that calls scalar helpers (__ts_map_find_bucket, etc.)
    llvm::Value* emitLoadTsValueType(llvm::Value* boxedPtr);      // Load type field as i8
    llvm::Value* emitLoadTsValueUnion(llvm::Value* boxedPtr);     // Load union field as i64
    void emitStoreTsValueFields(llvm::Value* boxedPtr, llvm::Value* type, llvm::Value* value);
    llvm::Value* emitInlineMapGet(llvm::Value* rawMap, llvm::Value* keyBoxed);
    void emitInlineMapSet(llvm::Value* rawMap, llvm::Value* keyBoxed, llvm::Value* valBoxed);
    llvm::Value* emitInlineArrayGet(llvm::Value* rawArr, llvm::Value* index);
    void emitInlineArraySet(llvm::Value* rawArr, llvm::Value* index, llvm::Value* valBoxed);
    llvm::Value* emitInlineObjectGetProp(llvm::Value* objBoxed, llvm::Value* keyBoxed);
    void emitInlineObjectSetProp(llvm::Value* objBoxed, llvm::Value* keyBoxed, llvm::Value* valBoxed);
    
    // Value-passing API helpers (_v variants) - DEPRECATED, use inline ops above
    llvm::Value* emitObjectGetPropV(llvm::Value* objBoxed, llvm::Value* keyBoxed);
    llvm::Value* emitMapGetV(llvm::Value* rawMap, llvm::Value* keyBoxed);
    llvm::Value* emitArrayGetV(llvm::Value* rawArr, llvm::Value* index);

    llvm::Value* lastConcreteType = nullptr;
    std::map<llvm::Value*, ClassType*> concreteTypes;
    std::set<llvm::Value*> boxedValues;
    std::set<std::string> boxedVariables;
    std::set<std::string> boxedElementArrayVars;  // Array variables where elements are boxed (e.g., rest parameters)
    std::set<std::string> cellVariables;  // Variables that need cells (captured and mutable)
    std::map<std::string, llvm::Value*> cellPointers;  // Maps var name -> cell pointer
    std::set<std::string> functionsWithClosures;  // Functions that capture outer scope variables
    std::map<llvm::Value*, std::string> lengthAliases; // Value* -> arrayVarName
    std::string lastLengthArray;
    
    // Boxing policy for deterministic boxing decisions
    BoxingPolicy boxingPolicy;
    
    // Helper to get runtime functions with enforcement
    // When enforceRegistration is true (default), throws if function not in BoxingPolicy registry
    llvm::FunctionCallee getRuntimeFunction(const std::string& name, llvm::FunctionType* ft, bool enforceRegistration = true);

    llvm::Type* getLLVMType(const std::shared_ptr<Type>& type);
    void addStackProtection(llvm::Function* func);
    void emitCFICheck(llvm::Value* ptr, const std::string& typeId);
    void emitBoundsCheck(llvm::Value* index, llvm::Value* length);
    void emitNullCheck(llvm::Value* ptr);
    void emitNullCheckForExpression(ast::Expression* expr, llvm::Value* ptr);
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
    void visitBigIntLiteral(ast::BigIntLiteral* node);
    void visitBooleanLiteral(ast::BooleanLiteral* node);
    void visitNullLiteral(ast::NullLiteral* node);
    void visitUndefinedLiteral(ast::UndefinedLiteral* node);
    void visitStringLiteral(ast::StringLiteral* node);
    void visitRegularExpressionLiteral(ast::RegularExpressionLiteral* node);
    void visitAwaitExpression(ast::AwaitExpression* node);
    void visitYieldExpression(ast::YieldExpression* node);
    void visitArrowFunction(ast::ArrowFunction* node);
    void visitFunctionExpression(ast::FunctionExpression* node);
    void visitObjectBindingPattern(ast::ObjectBindingPattern* node);
    void visitArrayBindingPattern(ast::ArrayBindingPattern* node);
    void visitBindingElement(ast::BindingElement* node);
    void visitSpreadElement(ast::SpreadElement* node);
    void visitOmittedExpression(ast::OmittedExpression* node);
    void visitTemplateExpression(ast::TemplateExpression* node);
    void visitTaggedTemplateExpression(ast::TaggedTemplateExpression* node);
    void visitAsExpression(ast::AsExpression* node);
    void visitPrefixUnaryExpression(ast::PrefixUnaryExpression* node);
    void visitDeleteExpression(ast::DeleteExpression* node);
    void visitPostfixUnaryExpression(ast::PostfixUnaryExpression* node) override;
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
    void visitClassExpression(ast::ClassExpression* node);
    void visitInterfaceDeclaration(ast::InterfaceDeclaration* node);
    void visitImportDeclaration(ast::ImportDeclaration* node);
    void visitExportDeclaration(ast::ExportDeclaration* node);
    void visitExportAssignment(ast::ExportAssignment* node);
    void visitCallExpression(ast::CallExpression* node) override;
    void generateCall(ast::CallExpression* node);
    bool tryGenerateMemberCall(ast::CallExpression* node);
    bool tryGenerateBuiltinCall(ast::CallExpression* node, ast::PropertyAccessExpression* prop);
    bool tryGenerateFSCall(ast::CallExpression* node, ast::PropertyAccessExpression* prop);
    bool tryGeneratePathCall(ast::CallExpression* node, ast::PropertyAccessExpression* prop);
    bool tryGenerateProcessCall(ast::CallExpression* node, ast::PropertyAccessExpression* prop);
    bool tryGenerateBufferCall(ast::CallExpression* node, ast::PropertyAccessExpression* prop);
    bool tryGenerateEventsCall(ast::CallExpression* node, ast::PropertyAccessExpression* prop);
    bool tryGeneratePromiseCall(ast::CallExpression* node, ast::PropertyAccessExpression* prop);
    bool tryGenerateStreamCall(ast::CallExpression* node, ast::PropertyAccessExpression* prop);
    bool tryGenerateStreamModuleCall(ast::CallExpression* node, ast::PropertyAccessExpression* prop);
    bool tryGenerateStreamPropertyAccess(ast::PropertyAccessExpression* prop);
    bool tryGenerateNetCall(ast::CallExpression* node, ast::PropertyAccessExpression* prop);
    bool tryGenerateHTTPCall(ast::CallExpression* node, ast::PropertyAccessExpression* prop);
    bool tryGenerateTextEncodingCall(ast::CallExpression* node, ast::PropertyAccessExpression* prop);
    bool tryGenerateURLCall(ast::CallExpression* node, ast::PropertyAccessExpression* prop);
    bool tryGenerateURLModuleCall(ast::CallExpression* node, ast::PropertyAccessExpression* prop);
    bool tryGenerateUtilCall(ast::CallExpression* node, ast::PropertyAccessExpression* prop);
    bool tryGenerateQueryStringCall(ast::CallExpression* node, ast::PropertyAccessExpression* prop);
    bool tryGenerateOSCall(ast::CallExpression* node, ast::PropertyAccessExpression* prop);
    bool tryGenerateOSPropertyAccess(ast::PropertyAccessExpression* node);
    bool tryGenerateTimersCall(ast::CallExpression* node, ast::PropertyAccessExpression* prop);
    bool tryGenerateTimersPromisesCall(ast::CallExpression* node, ast::PropertyAccessExpression* prop);
    bool tryGenerateTimersSchedulerCall(ast::CallExpression* node, ast::PropertyAccessExpression* prop);
    bool tryGenerateVMCall(ast::CallExpression* node, ast::PropertyAccessExpression* prop);
    bool tryGenerateV8Call(ast::CallExpression* node, ast::PropertyAccessExpression* prop);
    bool tryGenerateAssertCall(ast::CallExpression* node, ast::PropertyAccessExpression* prop);
    bool tryGenerateStringDecoderCall(ast::CallExpression* node, ast::PropertyAccessExpression* prop);
    bool tryGenerateStringDecoderNew(ast::NewExpression* node);
    bool tryGeneratePerfHooksCall(ast::CallExpression* node, ast::PropertyAccessExpression* prop);
    bool tryGeneratePerfHooksPropertyAccess(ast::PropertyAccessExpression* prop);
    bool tryGeneratePerformanceEntryPropertyAccess(ast::PropertyAccessExpression* prop);
    bool tryGenerateOSConstantsPropertyAccess(ast::PropertyAccessExpression* node);
    bool tryGenerateUtilPropertyAccess(ast::PropertyAccessExpression* node);
    bool tryGenerateUtilInspectPropertyAccess(ast::PropertyAccessExpression* node);
    bool tryGenerateAsyncHooksCall(ast::CallExpression* node, ast::PropertyAccessExpression* prop);
    bool tryGenerateAsyncLocalStorageCall(ast::CallExpression* node, ast::PropertyAccessExpression* prop);
    bool tryGenerateAsyncResourceCall(ast::CallExpression* node, ast::PropertyAccessExpression* prop);
    bool tryGenerateAsyncHookMethodCall(ast::CallExpression* node, ast::PropertyAccessExpression* prop);
    bool tryGenerateAsyncLocalStorageNew(ast::NewExpression* node);
    bool tryGenerateAsyncResourceNew(ast::NewExpression* node);
    bool tryGenerateChildProcessCall(ast::CallExpression* node, ast::PropertyAccessExpression* prop);
    bool tryGenerateChildProcessMethodCall(ast::CallExpression* node, ast::PropertyAccessExpression* prop);
    bool tryGenerateChildProcessPropertyAccess(ast::PropertyAccessExpression* prop);
    bool tryGenerateClusterCall(ast::CallExpression* node, ast::PropertyAccessExpression* prop);
    bool tryGenerateWorkerCall(ast::CallExpression* node, ast::PropertyAccessExpression* prop);
    bool tryGenerateDNSCall(ast::CallExpression* node, ast::PropertyAccessExpression* prop);
    bool tryGenerateDNSPromisesCall(ast::CallExpression* node, const std::string& methodName);
    bool tryGenerateDNSPropertyAccess(ast::PropertyAccessExpression* node);
    bool tryGenerateDgramCall(ast::CallExpression* node, ast::PropertyAccessExpression* prop);
    bool tryGenerateInspectorCall(ast::CallExpression* node, ast::PropertyAccessExpression* prop);
    bool tryGenerateInspectorSessionNew(ast::NewExpression* node);
    bool tryGenerateInspectorSessionCall(ast::CallExpression* node, ast::PropertyAccessExpression* prop);
    void visitNewExpression(ast::NewExpression* node);
    void visitArrayLiteralExpression(ast::ArrayLiteralExpression* node);
    void visitElementAccessExpression(ast::ElementAccessExpression* node);
    void generateElementAccess(ast::ElementAccessExpression* node);
    void visitPropertyAccessExpression(ast::PropertyAccessExpression* node);
    void generatePropertyAccess(ast::PropertyAccessExpression* node);
    bool tryGenerateFSPropertyAccess(ast::PropertyAccessExpression* node);
    bool tryGeneratePathPropertyAccess(ast::PropertyAccessExpression* node);
    bool tryGenerateEventsPropertyAccess(ast::PropertyAccessExpression* node);
    bool tryGenerateBufferPropertyAccess(ast::PropertyAccessExpression* node);
    bool tryGenerateHTTPPropertyAccess(ast::PropertyAccessExpression* node);
    bool tryGenerateNetPropertyAccess(ast::PropertyAccessExpression* node);
    void visitObjectLiteralExpression(ast::ObjectLiteralExpression* node);
    void visitPropertyAssignment(ast::PropertyAssignment* node) override;
    void visitShorthandPropertyAssignment(ast::ShorthandPropertyAssignment* node) override;
    void visitComputedPropertyName(ast::ComputedPropertyName* node) override;
    void visitMethodDefinition(ast::MethodDefinition* node) override;
    void visitStaticBlock(ast::StaticBlock* node) override;
    void visitProgram(ast::Program* node);
    void visitTypeAliasDeclaration(ast::TypeAliasDeclaration* node);
    void visitEnumDeclaration(ast::EnumDeclaration* node);

    void visit(ast::Node* node);

    // JSON module support
    llvm::Value* generateJsonValue(const nlohmann::json& j);

    llvm::Value* boxValue(llvm::Value* val, std::shared_ptr<Type> type);
    llvm::Value* unboxValue(llvm::Value* val, std::shared_ptr<Type> type);
    llvm::Value* toInt32(llvm::Value* val);
    llvm::Value* toUint32(llvm::Value* val);
    void generateDestructuring(llvm::Value* value, std::shared_ptr<Type> type, ast::Node* pattern);

    llvm::Value* emitAwait(llvm::Value* promiseVal, std::shared_ptr<Type> type);
    void emitYieldStar(ast::YieldExpression* node);
    llvm::Value* createCall(llvm::FunctionType* ft, llvm::Value* callee, std::vector<llvm::Value*> args);
    llvm::Value* castValue(llvm::Value* val, llvm::Type* expectedType);
    llvm::Value* emitToBoolean(llvm::Value* val, std::shared_ptr<Type> type);
    llvm::Value* getUndefinedValue();
    llvm::Value* getNullValue();

    llvm::AllocaInst* createEntryBlockAlloca(llvm::Function* function, const std::string& varName, llvm::Type* type);

    struct VariableInfo {
        std::string name;
        std::shared_ptr<Type> type;
        llvm::Type* llvmType;
    };
    void collectVariables(ast::Node* node, std::vector<VariableInfo>& vars);
    
    // JavaScript function hoisting - pre-register function declarations before body execution
    void hoistFunctionDeclarations(const std::vector<ast::StmtPtr>& stmts, llvm::Function* enclosingFn);
    
    // JavaScript variable hoisting - pre-register var declarations for closure capture
    void hoistVariableDeclarations(const std::vector<ast::StmtPtr>& stmts, llvm::Function* enclosingFn);

    struct CapturedVariable {
        std::string name;
        llvm::Value* value;               // The LLVM value (pointer to local alloca)
        std::shared_ptr<Type> type;       // TypeScript type for boxing
    };
    void collectFreeVariables(ast::Node* node, 
                              const std::set<std::string>& localScope,
                              std::vector<CapturedVariable>& captured);

    // Recursively collect all variable names declared in a statement list (including nested blocks)
    void collectAllVariableNames(const std::vector<ast::StmtPtr>& statements,
                                 std::set<std::string>& varNames);

    // Pre-scan a function body to find all variables that will be captured by inner closures
    void collectCapturedVariableNames(ast::Node* node,
                                      const std::set<std::string>& outerScope,
                                      std::set<std::string>& capturedNames);

    std::map<std::string, llvm::Value*> namedValues;
    std::map<std::string, llvm::Type*> forcedVariableTypes;
    std::map<std::string, std::shared_ptr<Type>> variableTypes;  // TS types for local variables
    std::map<ast::Node*, llvm::Value*> valueOverrides;
    llvm::Value* lastValue = nullptr;
    std::shared_ptr<Type> currentClass;
    
    static std::string manglePrivateName(const std::string& name, const std::string& className) {
        if (name.starts_with("#")) {
            return "__private_" + className + "_" + name.substr(1);
        }
        return name;
    }

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
    llvm::SwitchInst* currentAsyncSwitch = nullptr;  // For adding state cases in yield/await
    bool currentIsGenerator = false;
    bool currentIsAsync = false;
    
    struct CatchInfo {
        llvm::BasicBlock* catchBB;
        llvm::Value* pendingExc;
    };
    std::vector<CatchInfo> catchStack;

    struct FinallyInfo {
        llvm::BasicBlock* finallyBB;
        llvm::Value* pendingExc;
        llvm::BasicBlock* nextFinallyBB;
        llvm::BasicBlock* nextBreakBB;
        llvm::BasicBlock* nextContinueBB;
        bool nextBreakIsFinally;
        bool nextContinueIsFinally;
    };
    std::vector<FinallyInfo> finallyStack;

    llvm::BasicBlock* currentBreakBB = nullptr;
    llvm::BasicBlock* currentContinueBB = nullptr;
    llvm::Value* currentReturnValueAlloca = nullptr;
    llvm::Value* currentShouldReturnAlloca = nullptr;
    llvm::BasicBlock* currentReturnBB = nullptr;

    llvm::Value* currentShouldBreakAlloca = nullptr;
    llvm::Value* currentShouldContinueAlloca = nullptr;
    llvm::Value* currentBreakTargetAlloca = nullptr;
    llvm::Value* currentContinueTargetAlloca = nullptr;

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
        size_t finallyStackDepth;
        std::string label;
        std::map<std::string, std::string> safeIndices; // indexVar -> arrayVar
    };
    std::vector<LoopInfo> loopStack;

    std::set<llvm::Value*> nonNullValues;
    std::set<llvm::Value*> checkedAllocas;

    int anonVarCounter = 0;

    llvm::Function* getRuntimeFunction(const std::string& name);
};

} // namespace ts
