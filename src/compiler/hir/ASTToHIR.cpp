#include "ASTToHIR.h"
#include "../extensions/ExtensionLoader.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>
#include <spdlog/spdlog.h>

namespace ts::hir {

//==============================================================================
// Helper: Convert ext::TypeReference to HIR type
//==============================================================================

static std::shared_ptr<HIRType> extTypeRefToHIR(const ext::TypeReference& typeRef) {
    const auto& name = typeRef.name;
    if (name == "string") return HIRType::makeString();
    if (name == "number" || name == "int" || name == "i64") return HIRType::makeInt64();
    if (name == "double" || name == "f64" || name == "float") return HIRType::makeFloat64();
    if (name == "boolean" || name == "bool") return HIRType::makeBool();
    if (name == "void") return HIRType::makeVoid();
    if (name == "Array") return HIRType::makeArray(HIRType::makeAny());
    if (name == "Map") return HIRType::makeMap();
    if (name == "Set") return HIRType::makeSet();
    // Check if this is a known extension class type
    if (ext::ExtensionRegistry::instance().isExtensionType(name)) {
        return HIRType::makeClass(name, 0);
    }
    // For unknown types, use Any (the LoweringRegistry handles LLVM types)
    return HIRType::makeAny();
}

//==============================================================================
// Helper: Scan constructor body for this.propName = expr assignments
//==============================================================================

static void scanConstructorBodyForProperties(
    const std::vector<ast::StmtPtr>& body,
    std::shared_ptr<HIRShape>& shape,
    uint32_t& propertyOffset)
{
    for (auto& stmtPtr : body) {
        // Only scan top-level ExpressionStatements (conservative: skip if/else/loops)
        auto* exprStmt = dynamic_cast<ast::ExpressionStatement*>(stmtPtr.get());
        if (!exprStmt || !exprStmt->expression) continue;

        auto* assign = dynamic_cast<ast::AssignmentExpression*>(exprStmt->expression.get());
        if (!assign || !assign->left) continue;

        auto* propAccess = dynamic_cast<ast::PropertyAccessExpression*>(assign->left.get());
        if (!propAccess || !propAccess->expression) continue;

        auto* thisIdent = dynamic_cast<ast::Identifier*>(propAccess->expression.get());
        if (!thisIdent || thisIdent->name != "this") continue;

        const std::string& propName = propAccess->name;
        if (propName.empty()) continue;

        // Skip if already in shape (from PropertyDefinition or base class)
        if (shape->propertyOffsets.count(propName)) continue;

        shape->propertyOffsets[propName] = propertyOffset;
        shape->propertyTypes[propName] = HIRType::makeAny();
        propertyOffset++;
    }
}

//==============================================================================
// Helper: Check if an AST expression contains a function/arrow (for var hoisting)
//==============================================================================

// Helper: Check if a function body uses the 'arguments' identifier.
// Does NOT recurse into nested FunctionDeclaration/FunctionExpression (they have own arguments).
// DOES recurse into ArrowFunction (arrow functions inherit outer arguments).
static bool containsArgumentsIdentifier(ast::Node* node) {
    if (!node) return false;
    // Check if this is an Identifier named "arguments"
    if (auto* ident = dynamic_cast<ast::Identifier*>(node)) {
        return ident->name == "arguments";
    }
    // Do NOT recurse into FunctionExpression or FunctionDeclaration - they have their own arguments
    if (dynamic_cast<ast::FunctionExpression*>(node)) return false;
    if (dynamic_cast<ast::FunctionDeclaration*>(node)) return false;
    // DO recurse into ArrowFunction (arrow functions don't have own arguments)
    if (auto* arrow = dynamic_cast<ast::ArrowFunction*>(node)) {
        return containsArgumentsIdentifier(arrow->body.get());
    }
    // Statements
    if (auto* block = dynamic_cast<ast::BlockStatement*>(node)) {
        for (auto& s : block->statements) if (containsArgumentsIdentifier(s.get())) return true;
        return false;
    }
    if (auto* expr = dynamic_cast<ast::ExpressionStatement*>(node)) {
        return containsArgumentsIdentifier(expr->expression.get());
    }
    if (auto* ret = dynamic_cast<ast::ReturnStatement*>(node)) {
        return containsArgumentsIdentifier(ret->expression.get());
    }
    if (auto* ifStmt = dynamic_cast<ast::IfStatement*>(node)) {
        return containsArgumentsIdentifier(ifStmt->condition.get()) ||
               containsArgumentsIdentifier(ifStmt->thenStatement.get()) ||
               containsArgumentsIdentifier(ifStmt->elseStatement.get());
    }
    if (auto* whileStmt = dynamic_cast<ast::WhileStatement*>(node)) {
        return containsArgumentsIdentifier(whileStmt->condition.get()) ||
               containsArgumentsIdentifier(whileStmt->body.get());
    }
    if (auto* forStmt = dynamic_cast<ast::ForStatement*>(node)) {
        return containsArgumentsIdentifier(forStmt->initializer.get()) ||
               containsArgumentsIdentifier(forStmt->condition.get()) ||
               containsArgumentsIdentifier(forStmt->incrementor.get()) ||
               containsArgumentsIdentifier(forStmt->body.get());
    }
    if (auto* forOf = dynamic_cast<ast::ForOfStatement*>(node)) {
        return containsArgumentsIdentifier(forOf->expression.get()) ||
               containsArgumentsIdentifier(forOf->body.get());
    }
    if (auto* forIn = dynamic_cast<ast::ForInStatement*>(node)) {
        return containsArgumentsIdentifier(forIn->expression.get()) ||
               containsArgumentsIdentifier(forIn->body.get());
    }
    if (auto* sw = dynamic_cast<ast::SwitchStatement*>(node)) {
        if (containsArgumentsIdentifier(sw->expression.get())) return true;
        for (auto& cl : sw->clauses) {
            if (auto* cc = dynamic_cast<ast::CaseClause*>(cl.get())) {
                if (containsArgumentsIdentifier(cc->expression.get())) return true;
                for (auto& s : cc->statements) if (containsArgumentsIdentifier(s.get())) return true;
            }
            if (auto* dc = dynamic_cast<ast::DefaultClause*>(cl.get())) {
                for (auto& s : dc->statements) if (containsArgumentsIdentifier(s.get())) return true;
            }
        }
        return false;
    }
    if (auto* tryStmt = dynamic_cast<ast::TryStatement*>(node)) {
        for (auto& s : tryStmt->tryBlock) if (containsArgumentsIdentifier(s.get())) return true;
        if (tryStmt->catchClause) {
            for (auto& s : tryStmt->catchClause->block) if (containsArgumentsIdentifier(s.get())) return true;
        }
        for (auto& s : tryStmt->finallyBlock) if (containsArgumentsIdentifier(s.get())) return true;
        return false;
    }
    if (auto* throwStmt = dynamic_cast<ast::ThrowStatement*>(node)) {
        return containsArgumentsIdentifier(throwStmt->expression.get());
    }
    if (auto* varDecl = dynamic_cast<ast::VariableDeclaration*>(node)) {
        return containsArgumentsIdentifier(varDecl->initializer.get());
    }
    if (auto* labeled = dynamic_cast<ast::LabeledStatement*>(node)) {
        return containsArgumentsIdentifier(labeled->statement.get());
    }
    // Expressions
    if (auto* call = dynamic_cast<ast::CallExpression*>(node)) {
        if (containsArgumentsIdentifier(call->callee.get())) return true;
        for (auto& arg : call->arguments) if (containsArgumentsIdentifier(arg.get())) return true;
        return false;
    }
    if (auto* newExpr = dynamic_cast<ast::NewExpression*>(node)) {
        if (containsArgumentsIdentifier(newExpr->expression.get())) return true;
        for (auto& arg : newExpr->arguments) if (containsArgumentsIdentifier(arg.get())) return true;
        return false;
    }
    if (auto* bin = dynamic_cast<ast::BinaryExpression*>(node)) {
        return containsArgumentsIdentifier(bin->left.get()) ||
               containsArgumentsIdentifier(bin->right.get());
    }
    if (auto* assign = dynamic_cast<ast::AssignmentExpression*>(node)) {
        return containsArgumentsIdentifier(assign->left.get()) ||
               containsArgumentsIdentifier(assign->right.get());
    }
    if (auto* cond = dynamic_cast<ast::ConditionalExpression*>(node)) {
        return containsArgumentsIdentifier(cond->condition.get()) ||
               containsArgumentsIdentifier(cond->whenTrue.get()) ||
               containsArgumentsIdentifier(cond->whenFalse.get());
    }
    if (auto* prefix = dynamic_cast<ast::PrefixUnaryExpression*>(node)) {
        return containsArgumentsIdentifier(prefix->operand.get());
    }
    if (auto* postfix = dynamic_cast<ast::PostfixUnaryExpression*>(node)) {
        return containsArgumentsIdentifier(postfix->operand.get());
    }
    if (auto* prop = dynamic_cast<ast::PropertyAccessExpression*>(node)) {
        return containsArgumentsIdentifier(prop->expression.get());
    }
    if (auto* elem = dynamic_cast<ast::ElementAccessExpression*>(node)) {
        return containsArgumentsIdentifier(elem->expression.get()) ||
               containsArgumentsIdentifier(elem->argumentExpression.get());
    }
    if (auto* arr = dynamic_cast<ast::ArrayLiteralExpression*>(node)) {
        for (auto& e : arr->elements) if (containsArgumentsIdentifier(e.get())) return true;
        return false;
    }
    if (auto* obj = dynamic_cast<ast::ObjectLiteralExpression*>(node)) {
        for (auto& p : obj->properties) {
            if (auto* pa = dynamic_cast<ast::PropertyAssignment*>(p.get())) {
                if (containsArgumentsIdentifier(pa->initializer.get())) return true;
            }
        }
        return false;
    }
    if (auto* tmpl = dynamic_cast<ast::TemplateExpression*>(node)) {
        for (auto& span : tmpl->spans) if (containsArgumentsIdentifier(span.expression.get())) return true;
        return false;
    }
    if (auto* paren = dynamic_cast<ast::ParenthesizedExpression*>(node)) {
        return containsArgumentsIdentifier(paren->expression.get());
    }
    if (auto* spread = dynamic_cast<ast::SpreadElement*>(node)) {
        return containsArgumentsIdentifier(spread->expression.get());
    }
    if (auto* del = dynamic_cast<ast::DeleteExpression*>(node)) {
        return containsArgumentsIdentifier(del->expression.get());
    }
    if (auto* await_ = dynamic_cast<ast::AwaitExpression*>(node)) {
        return containsArgumentsIdentifier(await_->expression.get());
    }
    if (auto* yield_ = dynamic_cast<ast::YieldExpression*>(node)) {
        return containsArgumentsIdentifier(yield_->expression.get());
    }
    if (auto* asExpr = dynamic_cast<ast::AsExpression*>(node)) {
        return containsArgumentsIdentifier(asExpr->expression.get());
    }
    if (auto* nonNull = dynamic_cast<ast::NonNullExpression*>(node)) {
        return containsArgumentsIdentifier(nonNull->expression.get());
    }
    return false;
}

static bool containsClosureExpression(ast::Node* node) {
    if (!node) return false;
    std::string kind = node->getKind();
    if (kind == "FunctionExpression" || kind == "ArrowFunction") return true;
    // Check CallExpression arguments (e.g., setInterval(function() {...}, ...))
    if (auto* call = dynamic_cast<ast::CallExpression*>(node)) {
        for (auto& arg : call->arguments) {
            if (containsClosureExpression(arg.get())) return true;
        }
        if (containsClosureExpression(call->callee.get())) return true;
    }
    // Check NewExpression arguments
    if (auto* newExpr = dynamic_cast<ast::NewExpression*>(node)) {
        for (auto& arg : newExpr->arguments) {
            if (containsClosureExpression(arg.get())) return true;
        }
    }
    // Check array literals
    if (auto* arr = dynamic_cast<ast::ArrayLiteralExpression*>(node)) {
        for (auto& elem : arr->elements) {
            if (containsClosureExpression(elem.get())) return true;
        }
    }
    // Check object literals
    if (auto* obj = dynamic_cast<ast::ObjectLiteralExpression*>(node)) {
        for (auto& prop : obj->properties) {
            if (auto* p = dynamic_cast<ast::PropertyAssignment*>(prop.get())) {
                if (containsClosureExpression(p->initializer.get())) return true;
            }
        }
    }
    // Check ternary / binary
    if (auto* cond = dynamic_cast<ast::ConditionalExpression*>(node)) {
        if (containsClosureExpression(cond->whenTrue.get())) return true;
        if (containsClosureExpression(cond->whenFalse.get())) return true;
    }
    if (auto* bin = dynamic_cast<ast::BinaryExpression*>(node)) {
        if (containsClosureExpression(bin->left.get())) return true;
        if (containsClosureExpression(bin->right.get())) return true;
    }
    return false;
}

//==============================================================================
// Constructor / Entry Point
//==============================================================================

ASTToHIR::ASTToHIR() : builder_(nullptr) {}

std::unique_ptr<HIRModule> ASTToHIR::lower(ast::Program* program, const std::string& moduleName) {
    module_ = std::make_unique<HIRModule>(moduleName);
    module_->sourcePath = program->sourceFile;
    builder_ = HIRBuilder(module_.get());

    valueCounter_ = 0;
    blockCounter_ = 0;
    scopes_.clear();
    pushScope();  // Global scope

    // Check if we need a module init function for top-level executable code
    // (VariableDeclarations with initializers, ExpressionStatements, etc.)
    bool needsModuleInit = false;
    for (auto& stmt : program->body) {
        std::string kind = stmt->getKind();
        if (kind == "VariableDeclaration" || kind == "ExpressionStatement" || kind == "BlockStatement") {
            needsModuleInit = true;
            break;
        }
    }

    // Create module initialization function for top-level code
    HIRFunction* moduleInitFunc = nullptr;
    if (needsModuleInit) {
        auto initFunc = std::make_unique<HIRFunction>("__module_init");
        initFunc->returnType = HIRType::makeVoid();
        moduleInitFunc = initFunc.get();
        currentFunction_ = moduleInitFunc;

        // Create entry block
        auto entryBlock = initFunc->createBlock("entry");
        builder_.setInsertPoint(entryBlock);
        currentBlock_ = entryBlock;

        module_->functions.push_back(std::move(initFunc));
    }

    // Visit all statements in the program
    for (auto& stmt : program->body) {
        lowerStatement(stmt.get());
    }

    // Add terminator to module init function if it was created
    if (moduleInitFunc && currentBlock_ && !hasTerminator()) {
        builder_.createReturnVoid();
    }

    popScope();
    return std::move(module_);
}

std::unique_ptr<HIRModule> ASTToHIR::lower(ast::Program* program,
                                           const std::vector<Specialization>& specializations,
                                           const std::string& moduleName) {
    module_ = std::make_unique<HIRModule>(moduleName);
    module_->sourcePath = program->sourceFile;
    builder_ = HIRBuilder(module_.get());

    // Store specializations for lookup during call generation
    specializations_ = &specializations;

    valueCounter_ = 0;
    blockCounter_ = 0;
    scopes_.clear();
    pushScope();  // Global scope

    // First pass: visit all statements in the program to process classes and globals
    // This ensures class definitions and other declarations are available
    for (auto& stmt : program->body) {
        std::string kind = stmt->getKind();
        // Process class declarations, enum declarations, and imports
        // Skip function declarations as they'll be processed via specializations
        if (kind == "ClassDeclaration" || kind == "EnumDeclaration" ||
            kind == "ImportDeclaration" || kind == "ExportDeclaration") {
            lowerStatement(stmt.get());
        }
    }

    // Determine the main source file for distinguishing imported modules.
    // The main file's statements come LAST in program->body (after all imports).
    // So we scan backwards to find the last unique sourceFile.
    mainSourceFile_.clear();
    for (auto it = program->body.rbegin(); it != program->body.rend(); ++it) {
        if (!(*it)->sourceFile.empty()) {
            mainSourceFile_ = (*it)->sourceFile;
            break;
        }
    }

    // Scan for module-scoped VariableDeclarations from imported modules.
    // Functions from imported modules may reference these variables (e.g., defaultOptions
    // in benchmark.ts referenced by benchmark()). We register them as module globals
    // so they can be resolved in visitIdentifier.
    moduleVarDecls_.clear();
    moduleGlobalVarsByModule_.clear();
    // Scan module init specializations for VariableDeclarations.
    // The Monomorphizer moves VariableDeclarations from imported modules into
    // __module_init_<hash> specialization functions. We need to find these and
    // register them as module globals so other functions from the same module
    // can reference them via LoadGlobal/StoreGlobal.
    for (const auto& spec : specializations) {
        if (spec.originalName.find("__module_init_") != 0) continue;
        auto* funcNode = dynamic_cast<ast::FunctionDeclaration*>(spec.node);
        if (!funcNode) continue;
        // Set currentModulePath_ so modVarName() generates unique globals per module
        currentModulePath_ = spec.modulePath;
        // Note: We include all module init functions (including the main file)
        // because file-level variables need to be shared across functions.
        // Helper to register a single name as a module global
        auto registerModuleGlobalName = [&](const std::string& name, std::shared_ptr<HIRType> globalType) {
            if (name.find("__") == 0 || name == "exports") return;
            moduleGlobalVarsByModule_[name].insert(currentModulePath_);
            module_->globals[modVarName(name)] = globalType;
        };

        // Helper to extract all binding names from a destructuring pattern
        std::function<void(ast::Node*, std::shared_ptr<HIRType>)> registerBindingNames;
        registerBindingNames = [&](ast::Node* node, std::shared_ptr<HIRType> globalType) {
            if (auto* ident = dynamic_cast<ast::Identifier*>(node)) {
                registerModuleGlobalName(ident->name, globalType);
            } else if (auto* objPat = dynamic_cast<ast::ObjectBindingPattern*>(node)) {
                for (auto& elem : objPat->elements) {
                    if (auto* binding = dynamic_cast<ast::BindingElement*>(elem.get())) {
                        registerBindingNames(binding->name.get(), globalType);
                    }
                }
            } else if (auto* arrPat = dynamic_cast<ast::ArrayBindingPattern*>(node)) {
                for (auto& elem : arrPat->elements) {
                    if (auto* binding = dynamic_cast<ast::BindingElement*>(elem.get())) {
                        registerBindingNames(binding->name.get(), globalType);
                    }
                }
            }
        };

        // Helper lambda to register a VariableDeclaration as a module global
        auto registerModuleVar = [&](ast::VariableDeclaration* varDecl) {
            // Infer type from initializer to preserve Object vs Any distinction.
            // This is critical for method dispatch: without it, object literal methods
            // like "add" or "info" collide with Set.add() or console.info().
            auto globalType = HIRType::makeAny();
            if (varDecl->initializer) {
                auto kind = varDecl->initializer->getKind();
                if (kind == "ObjectLiteralExpression") {
                    globalType = HIRType::makeObject();
                } else if (kind == "ArrayLiteralExpression") {
                    globalType = HIRType::makeArray(HIRType::makeAny());
                }
            }

            if (auto* ident = dynamic_cast<ast::Identifier*>(varDecl->name.get())) {
                // Simple variable: const x = ...
                if (ident->name.find("__") == 0 || ident->name == "exports") return;
                moduleVarDecls_[ident->name] = varDecl;
                registerModuleGlobalName(ident->name, globalType);
            } else {
                // Destructuring pattern: const { a, b } = ... or const [a, b] = ...
                // For destructured require(), each extracted variable is Any type
                registerBindingNames(varDecl->name.get(), HIRType::makeAny());
            }
        };

        for (auto& stmt : funcNode->body) {
            if (stmt->getKind() == "VariableDeclaration") {
                auto* varDecl = dynamic_cast<ast::VariableDeclaration*>(stmt.get());
                if (varDecl) registerModuleVar(varDecl);
            } else if (stmt->getKind() == "BlockStatement") {
                // Multi-variable declarations (e.g., "let a, b") get wrapped in a
                // BlockStatement by the parser. Unwrap and check each child.
                auto* block = dynamic_cast<ast::BlockStatement*>(stmt.get());
                if (block) {
                    for (auto& inner : block->statements) {
                        auto* varDecl = dynamic_cast<ast::VariableDeclaration*>(inner.get());
                        if (varDecl) registerModuleVar(varDecl);
                    }
                }
            } else if (auto* funcDecl = dynamic_cast<ast::FunctionDeclaration*>(stmt.get())) {
                // Function declarations must also be registered as module globals.
                // Without this, functions captured by closures in the same module
                // use closure cells, which fail when the captured function is declared
                // after the capturing function (capture gets null due to source ordering).
                if (!funcDecl->name.empty() && funcDecl->name.find("__") != 0) {
                    moduleGlobalVarsByModule_[funcDecl->name].insert(currentModulePath_);
                    module_->globals[modVarName(funcDecl->name)] = HIRType::makeAny();
                }
            }
        }
    }

    // Pre-pass: create HIRClass objects for imported classes that aren't in the main program.
    // The first pass only processes ClassDeclarations from the main file's AST, so classes
    // defined in imported modules don't get HIRClass objects. We detect these from the
    // specializations (which include methods from all classes).
    // We use the ClassDeclaration's sourceFile to determine if a class is from the main
    // program (will be handled by visitClassDeclaration) or from an imported module.
    std::string mainSourceFile;
    if (!program->body.empty() && !program->body[0]->sourceFile.empty()) {
        mainSourceFile = program->body[0]->sourceFile;
    }

    std::set<std::string> classesCreatedFromSpecs;
    for (const auto& spec : specializations) {
        if (!spec.classType) continue;
        auto classType = std::dynamic_pointer_cast<ts::ClassType>(spec.classType);
        if (!classType || !classType->node) continue;
        std::string className = classType->name;

        // Skip classes from the main source file - they will be handled by
        // visitClassDeclaration during normal statement processing
        if (!mainSourceFile.empty() && classType->node->sourceFile == mainSourceFile) continue;

        // Check if this class already exists (from the first pass / main file)
        bool alreadyExists = false;
        for (auto& cls : module_->classes) {
            if (cls->name == className) {
                alreadyExists = true;
                break;
            }
        }
        if (alreadyExists) continue;
        if (classesCreatedFromSpecs.count(className)) continue;
        classesCreatedFromSpecs.insert(className);

        // Create HIRClass for this imported class
        auto* hirClass = builder_.createClass(className);
        if (!hirClass) continue;

        // Build shape from the ClassDeclaration's property definitions
        ast::ClassDeclaration* classDecl = classType->node;
        auto shape = std::make_shared<HIRShape>();
        shape->className = className;
        uint32_t propertyOffset = 0;

        // Handle base class
        if (!classDecl->baseClass.empty()) {
            for (auto& cls : module_->classes) {
                if (cls->name == classDecl->baseClass) {
                    hirClass->baseClass = cls.get();
                    break;
                }
            }
            if (hirClass->baseClass && hirClass->baseClass->shape) {
                auto baseShape = hirClass->baseClass->shape;
                shape->parent = baseShape.get();
                for (const auto& [name, offset] : baseShape->propertyOffsets) {
                    shape->propertyOffsets[name] = offset;
                }
                for (const auto& [name, type] : baseShape->propertyTypes) {
                    shape->propertyTypes[name] = type;
                }
                propertyOffset = baseShape->size;
            }
        }

        for (auto& memberPtr : classDecl->members) {
            if (auto* propDef = dynamic_cast<ast::PropertyDefinition*>(memberPtr.get())) {
                if (!propDef->isStatic) {
                    auto propType = propDef->type.empty()
                        ? HIRType::makeAny()
                        : convertTypeFromString(propDef->type);
                    shape->propertyOffsets[propDef->name] = propertyOffset;
                    shape->propertyTypes[propDef->name] = propType;
                    propertyOffset++;
                }
            }
        }

        // Scan constructor body for this.x = expr assignments
        for (auto& memberPtr : classDecl->members) {
            if (auto* method = dynamic_cast<ast::MethodDefinition*>(memberPtr.get())) {
                if (method->name == "constructor" && method->hasBody) {
                    scanConstructorBodyForProperties(method->body, shape, propertyOffset);
                    break;
                }
            }
        }

        shape->size = propertyOffset;
        hirClass->shape = shape;

        // Register class shape for flat object codegen if it has properties or instance methods.
        // Classes with methods but no PropertyDefinition fields (e.g., JS classes where properties
        // are assigned in the constructor body) still need flat objects for vtable method dispatch.
        bool hasInstanceMethods_prepass = false;
        for (auto& memberPtr2 : classDecl->members) {
            if (auto* md = dynamic_cast<ast::MethodDefinition*>(memberPtr2.get())) {
                if (md->name != "constructor" && !md->isStatic && !md->isAbstract && md->hasBody) {
                    hasInstanceMethods_prepass = true;
                    break;
                }
            }
        }
        if (!shape->propertyOffsets.empty() || hasInstanceMethods_prepass) {
            shape->id = nextShapeId_++;
            module_->shapes.push_back(shape);
        }

        // Generate default constructor for imported classes with field initializers
        // but no explicit constructor (mirrors visitClassDeclaration behavior)
        bool hasPropertyInitializers = false;
        for (auto& memberPtr2 : classDecl->members) {
            if (auto* propDef = dynamic_cast<ast::PropertyDefinition*>(memberPtr2.get())) {
                if (!propDef->isStatic && propDef->initializer) {
                    hasPropertyInitializers = true;
                    break;
                }
            }
        }

        // Also check if there's an explicit constructor in the class
        bool hasExplicitConstructor = false;
        for (auto& memberPtr2 : classDecl->members) {
            if (auto* method = dynamic_cast<ast::MethodDefinition*>(memberPtr2.get())) {
                if (method->name == "constructor" && method->hasBody) {
                    hasExplicitConstructor = true;
                    break;
                }
            }
        }

        if (hasPropertyInitializers && !hasExplicitConstructor) {
            std::string ctorName = className + "_constructor";
            auto defaultCtor = std::make_unique<HIRFunction>(ctorName);
            defaultCtor->params.push_back({"this", HIRType::makeClass(className, 0)});
            defaultCtor->returnType = HIRType::makeVoid();
            defaultCtor->nextValueId = 1;

            HIRBlock* ctorBlock = defaultCtor->createBlock("entry");
            HIRFunction* savedFunc = currentFunction_;
            currentFunction_ = defaultCtor.get();
            builder_.setInsertPoint(ctorBlock);
            currentBlock_ = ctorBlock;
            pushScope();

            auto thisValue = std::make_shared<HIRValue>(0, HIRType::makeClass(className, 0), "this");
            defineVariable("this", thisValue);

            // Initialize property defaults from AST
            for (auto& memberPtr2 : classDecl->members) {
                if (auto* propDef = dynamic_cast<ast::PropertyDefinition*>(memberPtr2.get())) {
                    if (!propDef->isStatic && propDef->initializer) {
                        auto initVal = lowerExpression(propDef->initializer.get());
                        builder_.createSetPropStatic(thisValue, propDef->name, initVal);
                    }
                }
            }

            builder_.createReturnVoid();
            popScope();
            currentFunction_ = savedFunc;

            hirClass->constructor = defaultCtor.get();
            module_->functions.push_back(std::move(defaultCtor));
        }

        SPDLOG_DEBUG("Created HIRClass for imported class: {} with {} properties",
            className, propertyOffset);
    }

    // Pre-register methods for imported JS slow-path classes on their HIRClass objects.
    // When a module init function compiles new ClassName(...).method(...), it needs to
    // know the method exists on the class. For typed TS classes, visitClassDeclaration
    // handles this in the first pass. For JS classes, the ClassDeclaration is inside
    // the module init body, so methods aren't registered until the init is processed.
    // We pre-register placeholder entries from the AST to enable direct VTable dispatch.
    for (auto& cls : module_->classes) {
        if (!cls->methods.empty() || cls->constructor) continue;  // Already has methods
        // Find the ClassDeclaration AST node for this class
        ast::ClassDeclaration* classDecl = nullptr;
        for (const auto& spec : specializations) {
            if (!spec.classType) continue;
            auto ct = std::dynamic_pointer_cast<ts::ClassType>(spec.classType);
            if (ct && ct->name == cls->name && ct->node) {
                classDecl = ct->node;
                break;
            }
        }
        if (!classDecl) continue;
        // Only pre-register for JS files (slow-path). TS classes are handled by
        // visitClassDeclaration in the first pass with real method registrations.
        if (classDecl->sourceFile.size() < 3 ||
            classDecl->sourceFile.substr(classDecl->sourceFile.size() - 3) != ".js") continue;
        for (auto& memberPtr : classDecl->members) {
            auto* md = dynamic_cast<ast::MethodDefinition*>(memberPtr.get());
            if (!md || md->name == "constructor" || md->isAbstract || !md->hasBody) continue;
            std::string methodKey = md->name;
            if (md->isGetter) methodKey = "__getter_" + md->name;
            else if (md->isSetter) methodKey = "__setter_" + md->name;
            cls->methods[methodKey] = nullptr;  // Placeholder, real ptr set later
        }
    }

    // Second pass: generate functions from specializations
    SPDLOG_WARN("[ASTToHIR] Generating {} specializations...", specializations.size());
    size_t specIdx = 0;
    for (const auto& spec : specializations) {
        specIdx++;
        if (specIdx % 20 == 0) {
            SPDLOG_WARN("[ASTToHIR] spec {}/{}: {}", specIdx, specializations.size(), spec.specializedName);
        }
        if (spec.specializedName.find("lambda") != std::string::npos) {
            // Skip lambda specializations - they'll be generated when encountered
            continue;
        }

        // Track current module path for cross-module function name disambiguation
        currentModulePath_ = spec.modulePath;

        // Get the node - could be FunctionDeclaration or MethodDefinition
        if (auto* funcNode = dynamic_cast<ast::FunctionDeclaration*>(spec.node)) {
            // Create HIR function with the specialized name
            auto func = std::make_unique<HIRFunction>(spec.specializedName);
            func->isAsync = funcNode->isAsync;
            func->isGenerator = funcNode->isGenerator;
            func->sourceLine = funcNode->line;
            func->sourceFile = funcNode->sourceFile;
            func->displayName = funcNode->name;

            // Collect destructured parameter patterns for later extraction
            struct SpecDestructuredParam {
                size_t paramIndex;
                ast::ObjectBindingPattern* objPattern = nullptr;
                ast::ArrayBindingPattern* arrPattern = nullptr;
            };
            std::vector<SpecDestructuredParam> specDestructuredParams;

            // Handle parameters
            for (size_t paramIdx = 0; paramIdx < funcNode->parameters.size(); ++paramIdx) {
                auto& param = funcNode->parameters[paramIdx];
                // Use specialized type from spec.argTypes if available
                std::shared_ptr<HIRType> paramType;
                if (paramIdx < spec.argTypes.size() && spec.argTypes[paramIdx]) {
                    paramType = convertType(spec.argTypes[paramIdx]);
                } else if (!param->type.empty()) {
                    paramType = convertTypeFromString(param->type);
                } else {
                    paramType = HIRType::makeAny();
                }

                // If parameter has a default value, it must be Any type to receive undefined
                if (param->initializer) {
                    paramType = HIRType::makeAny();
                }

                // Get parameter name
                std::string paramName;
                if (auto* ident = dynamic_cast<ast::Identifier*>(param->name.get())) {
                    paramName = ident->name;
                } else if (auto* objPat = dynamic_cast<ast::ObjectBindingPattern*>(param->name.get())) {
                    paramName = "param" + std::to_string(func->params.size());
                    paramType = HIRType::makeAny();
                    specDestructuredParams.push_back({func->params.size(), objPat, nullptr});
                } else if (auto* arrPat = dynamic_cast<ast::ArrayBindingPattern*>(param->name.get())) {
                    paramName = "param" + std::to_string(func->params.size());
                    paramType = HIRType::makeAny();
                    specDestructuredParams.push_back({func->params.size(), nullptr, arrPat});
                } else {
                    paramName = "param" + std::to_string(func->params.size());
                }

                if (param->isRest) {
                    func->hasRestParam = true;
                }

                func->params.push_back({paramName, paramType});
            }

            // Set return type from specialization
            if (spec.returnType) {
                func->returnType = convertType(spec.returnType);
            } else if (!funcNode->returnType.empty()) {
                func->returnType = convertTypeFromString(funcNode->returnType);
            } else {
                func->returnType = HIRType::makeAny();
            }

            // Push function to module BEFORE lowering body so recursive calls
            // can find this function (e.g., fib calling fib). Without this,
            // the recursive call's return type defaults to Any, causing
            // ts_value_add instead of native fadd for arithmetic.
            auto* funcPtr = func.get();
            module_->functions.push_back(std::move(func));

            // Create entry block and set up for lowering
            auto entryBlock = funcPtr->createBlock("entry");
            currentFunction_ = funcPtr;
            currentBlock_ = entryBlock;
            builder_.setInsertPoint(entryBlock);

            // Push function scope and bind parameters
            pushFunctionScope(funcPtr);
            funcPtr->nextValueId = static_cast<uint32_t>(funcPtr->params.size());
            for (size_t i = 0; i < funcPtr->params.size(); ++i) {
                const auto& [paramName, paramType] = funcPtr->params[i];
                auto paramValue = std::make_shared<HIRValue>(static_cast<uint32_t>(i), paramType, paramName);

                // Check if this parameter has a default value
                ast::Parameter* astParam = (i < funcNode->parameters.size()) ? funcNode->parameters[i].get() : nullptr;
                if (astParam && astParam->initializer) {
                    // Parameter has a default value - need to check if undefined and use default
                    auto allocaVal = builder_.createAlloca(paramType);

                    // Check if param is undefined using runtime function
                    auto isUndefined = builder_.createCall("ts_value_is_undefined",
                        {paramValue}, HIRType::makeBool());

                    // Create basic blocks for the conditional
                    auto defaultBB = funcPtr->createBlock("default_param");
                    auto usedBB = funcPtr->createBlock("use_param");
                    auto mergeBB = funcPtr->createBlock("param_merge");

                    // Branch based on undefined check
                    builder_.createCondBranch(isUndefined, defaultBB, usedBB);

                    // Default block - evaluate default expression and store
                    builder_.setInsertPoint(defaultBB);
                    currentBlock_ = defaultBB;
                    auto* initExpr = dynamic_cast<ast::Expression*>(astParam->initializer.get());
                    auto defaultVal = initExpr ? lowerExpression(initExpr) : builder_.createConstUndefined();
                    // Force box the default value if parameter type is Any
                    // We use forceBoxValue because the expression might be a function call
                    // that gets inlined later, changing its type from Any to a concrete type
                    if (paramType->kind == HIRTypeKind::Any) {
                        defaultVal = forceBoxValue(defaultVal);
                    }
                    builder_.createStore(defaultVal, allocaVal);
                    builder_.createBranch(mergeBB);

                    // Use param block - store the passed parameter value
                    builder_.setInsertPoint(usedBB);
                    currentBlock_ = usedBB;
                    builder_.createStore(paramValue, allocaVal);
                    builder_.createBranch(mergeBB);

                    // Merge block - continue execution
                    builder_.setInsertPoint(mergeBB);
                    currentBlock_ = mergeBB;

                    // Register the alloca as the variable
                    defineVariableAlloca(paramName, allocaVal, paramType);
                } else {
                    // No default value - store into an alloca so reassignment works
                    // (LLVM's mem2reg will eliminate the alloca for params that are never reassigned)
                    auto allocaVal = builder_.createAlloca(paramType);
                    builder_.createStore(paramValue, allocaVal);
                    defineVariableAlloca(paramName, allocaVal, paramType);
                }
            }

            // Emit destructuring extraction for parameters with binding patterns
            for (auto& dp : specDestructuredParams) {
                auto paramValue = std::make_shared<HIRValue>(
                    static_cast<uint32_t>(dp.paramIndex),
                    HIRType::makeAny(),
                    funcPtr->params[dp.paramIndex].first);
                if (dp.objPattern) {
                    lowerObjectBindingPattern(dp.objPattern, paramValue);
                } else if (dp.arrPattern) {
                    lowerArrayBindingPattern(dp.arrPattern, paramValue);
                }
            }

            // JavaScript function hoisting: pre-declare nested function names as variables
            // This allows functions to be called before they appear in source order.
            // We create allocas for function names, which will be filled when the function
            // declaration is processed. Calls to these names will use indirect call.
            for (auto& stmt : funcNode->body) {
                if (auto* nestedFunc = dynamic_cast<ast::FunctionDeclaration*>(stmt.get())) {
                    // Create a function type for the closure
                    auto nestedFuncType = std::make_shared<HIRType>(HIRTypeKind::Function);
                    for (auto& param : nestedFunc->parameters) {
                        std::shared_ptr<HIRType> paramType = HIRType::makeAny();
                        if (!param->type.empty()) {
                            paramType = convertTypeFromString(param->type);
                        }
                        nestedFuncType->paramTypes.push_back(paramType);
                    }
                    nestedFuncType->returnType = nestedFunc->returnType.empty()
                        ? HIRType::makeAny()
                        : convertTypeFromString(nestedFunc->returnType);

                    // Create an alloca for the function variable (will hold closure or function ptr)
                    auto allocaVal = builder_.createAlloca(nestedFuncType, nestedFunc->name);
                    // Initialize with null - will be set when the function is processed
                    builder_.createStore(builder_.createConstNull(), allocaVal);
                    defineVariableAlloca(nestedFunc->name, allocaVal, nestedFuncType);
                }
            }

            // Variable hoisting for shared closure cells.
            // When the function body has nested function declarations, pre-declare ALL
            // vars so the first pass (function decl lowering) can detect them as captures.
            // Without this, inner function declarations can't see outer variables that
            // are declared after them in source order, breaking closure capture.
            {
                bool hasNestedFuncDecls = false;
                for (auto& stmt : funcNode->body) {
                    if (dynamic_cast<ast::FunctionDeclaration*>(stmt.get())) {
                        hasNestedFuncDecls = true;
                        break;
                    }
                }
                bool hoistAllVars = hasNestedFuncDecls;

                for (auto& stmt : funcNode->body) {
                    if (auto* block = dynamic_cast<ast::BlockStatement*>(stmt.get())) {
                        if (hoistAllVars) {
                            for (auto& inner : block->statements) {
                                if (auto* vd = dynamic_cast<ast::VariableDeclaration*>(inner.get())) {
                                    if (auto* ident = dynamic_cast<ast::Identifier*>(vd->name.get())) {
                                        if (!lookupVariableInfoInCurrentFunction(ident->name)) {
                                            auto allocaVal = builder_.createAlloca(HIRType::makeAny(), ident->name);
                                            builder_.createStore(builder_.createConstUndefined(), allocaVal);
                                            defineVariableAlloca(ident->name, allocaVal, HIRType::makeAny());
                                        }
                                    }
                                }
                            }
                        }
                        continue;
                    }
                    if (auto* varDecl = dynamic_cast<ast::VariableDeclaration*>(stmt.get())) {
                        if (auto* ident = dynamic_cast<ast::Identifier*>(varDecl->name.get())) {
                            if (hoistAllVars) {
                                if (!lookupVariableInfoInCurrentFunction(ident->name)) {
                                    auto allocaVal = builder_.createAlloca(HIRType::makeAny(), ident->name);
                                    builder_.createStore(builder_.createConstUndefined(), allocaVal);
                                    defineVariableAlloca(ident->name, allocaVal, HIRType::makeAny());
                                }
                            } else if (!lookupVariableInfoInCurrentFunction(ident->name) &&
                                       varDecl->initializer && containsClosureExpression(varDecl->initializer.get())) {
                                auto varType = HIRType::makeAny();
                                auto allocaVal = builder_.createAlloca(varType, ident->name);
                                builder_.createStore(builder_.createConstUndefined(), allocaVal, varType);
                                defineVariableAlloca(ident->name, allocaVal, varType);
                            }
                        }
                    }
                }
            }

            // Lower function body in two passes for proper JavaScript function hoisting:
            // FIRST PASS: Process FunctionDeclarations to create closures
            // This ensures nested functions are available before any other code runs.
            for (auto& stmt : funcNode->body) {
                if (dynamic_cast<ast::FunctionDeclaration*>(stmt.get())) {
                    lowerStatement(stmt.get());
                }
            }
            emitMutualRecursionFixup();

            // SECOND PASS: Process non-FunctionDeclaration statements in order
            SPDLOG_DEBUG("[SPEC] Body pass 2: {} stmts in {}", funcNode->body.size(), spec.specializedName);
            for (auto& stmt : funcNode->body) {
                if (!dynamic_cast<ast::FunctionDeclaration*>(stmt.get())) {
                    SPDLOG_DEBUG("[SPEC]   stmt kind={}", stmt ? stmt->getKind() : "null");
                    lowerStatement(stmt.get());
                    if (builder_.isBlockTerminated()) {
                        break;
                    }
                }
            }

            // Add implicit return if needed
            if (!hasTerminator()) {
                if (funcPtr->returnType->kind == HIRTypeKind::Void) {
                    builder_.createReturnVoid();
                } else {
                    // Return undefined for non-void functions without explicit return
                    auto undef = builder_.createConstUndefined();
                    builder_.createReturn(undef);
                }
            }

            popScope();
            // func already pushed to module_->functions before body lowering
        } else if (auto* methodNode = dynamic_cast<ast::MethodDefinition*>(spec.node)) {
            // Handle method definitions (similar to above)
            if (methodNode->isAbstract || !methodNode->hasBody) continue;

            auto func = std::make_unique<HIRFunction>(spec.specializedName);
            func->isAsync = methodNode->isAsync;
            func->isGenerator = methodNode->isGenerator;
            func->sourceLine = methodNode->line;
            func->sourceFile = methodNode->sourceFile;
            func->displayName = methodNode->name;

            // Add 'this' parameter first for instance methods
            // spec.argTypes[0] is the class type for 'this' (set by Monomorphizer)
            size_t argTypeOffset = 0;
            if (!methodNode->isStatic) {
                auto thisType = (spec.argTypes.size() > 0 && spec.argTypes[0])
                    ? convertType(spec.argTypes[0])
                    : HIRType::makeAny();
                func->params.push_back({"this", thisType});
                argTypeOffset = 1;  // Skip 'this' in spec.argTypes for regular params
            }

            // Handle regular parameters
            for (size_t paramIdx = 0; paramIdx < methodNode->parameters.size(); ++paramIdx) {
                auto& param = methodNode->parameters[paramIdx];
                std::shared_ptr<HIRType> paramType;
                size_t specIdx = paramIdx + argTypeOffset;
                if (specIdx < spec.argTypes.size() && spec.argTypes[specIdx]) {
                    paramType = convertType(spec.argTypes[specIdx]);
                } else if (!param->type.empty()) {
                    paramType = convertTypeFromString(param->type);
                } else {
                    paramType = HIRType::makeAny();
                }

                std::string paramName;
                if (auto* ident = dynamic_cast<ast::Identifier*>(param->name.get())) {
                    paramName = ident->name;
                } else {
                    paramName = "param" + std::to_string(func->params.size());
                }

                func->params.push_back({paramName, paramType});
            }

            // Set return type
            if (spec.returnType) {
                func->returnType = convertType(spec.returnType);
            } else if (!methodNode->returnType.empty()) {
                func->returnType = convertTypeFromString(methodNode->returnType);
            } else {
                func->returnType = HIRType::makeAny();
            }

            // Push method to module BEFORE lowering body (same reason as functions above)
            auto* methPtr = func.get();
            module_->functions.push_back(std::move(func));

            auto entryBlock = methPtr->createBlock("entry");
            currentFunction_ = methPtr;
            currentBlock_ = entryBlock;
            builder_.setInsertPoint(entryBlock);

            // Set currentClass_ so that property type resolution works for 'this' access
            // (e.g., this.items resolves to array<string> instead of any)
            HIRClass* savedClass = currentClass_;
            if (spec.classType) {
                auto classType = std::dynamic_pointer_cast<ts::ClassType>(spec.classType);
                if (classType) {
                    for (auto& cls : module_->classes) {
                        if (cls->name == classType->name) {
                            currentClass_ = cls.get();
                            break;
                        }
                    }
                }
            }

            pushFunctionScope(methPtr);
            methPtr->nextValueId = static_cast<uint32_t>(methPtr->params.size());
            for (size_t i = 0; i < methPtr->params.size(); ++i) {
                const auto& [paramName, paramType] = methPtr->params[i];
                auto paramValue = std::make_shared<HIRValue>(static_cast<uint32_t>(i), paramType, paramName);
                auto allocaVal = builder_.createAlloca(paramType);
                builder_.createStore(paramValue, allocaVal);
                defineVariableAlloca(paramName, allocaVal, paramType);
            }

            // For constructors of imported classes, emit field initializers
            // before the constructor body (mirrors visitClassDeclaration behavior)
            if (methodNode->name == "constructor" && spec.classType) {
                auto classType = std::dynamic_pointer_cast<ts::ClassType>(spec.classType);
                if (classType && classType->node) {
                    auto thisValue = lookupVariable("this");
                    if (thisValue) {
                        for (auto& member : classType->node->members) {
                            if (auto* propDef = dynamic_cast<ast::PropertyDefinition*>(member.get())) {
                                if (!propDef->isStatic && propDef->initializer) {
                                    auto initVal = lowerExpression(propDef->initializer.get());
                                    builder_.createSetPropStatic(thisValue, propDef->name, initVal);
                                }
                            }
                        }
                    }
                }
            }

            // Two-pass for function hoisting: FIRST process FunctionDeclarations
            for (auto& stmt : methodNode->body) {
                if (dynamic_cast<ast::FunctionDeclaration*>(stmt.get())) {
                    lowerStatement(stmt.get());
                }
            }
            // SECOND pass: process non-FunctionDeclaration statements
            for (auto& stmt : methodNode->body) {
                if (!dynamic_cast<ast::FunctionDeclaration*>(stmt.get())) {
                    lowerStatement(stmt.get());
                    if (builder_.isBlockTerminated()) {
                        break;
                    }
                }
            }

            if (!hasTerminator()) {
                if (methPtr->returnType->kind == HIRTypeKind::Void) {
                    builder_.createReturnVoid();
                } else {
                    auto undef = builder_.createConstUndefined();
                    builder_.createReturn(undef);
                }
            }

            popScope();
            currentClass_ = savedClass;  // Restore after method body lowering

            // Link method to its HIRClass (for constructor calls and method resolution)
            if (spec.classType) {
                auto classType = std::dynamic_pointer_cast<ts::ClassType>(spec.classType);
                if (classType) {
                    std::string className = classType->name;
                    HIRClass* hirClass = nullptr;
                    for (auto& cls : module_->classes) {
                        if (cls->name == className) {
                            hirClass = cls.get();
                            break;
                        }
                    }
                    if (hirClass) {
                        if (methodNode->name == "constructor") {
                            hirClass->constructor = methPtr;
                        } else if (methodNode->isStatic) {
                            hirClass->staticMethods[methodNode->name] = methPtr;
                        } else {
                            std::string methodKey = methodNode->name;
                            if (methodNode->isGetter) methodKey = "__getter_" + methodNode->name;
                            else if (methodNode->isSetter) methodKey = "__setter_" + methodNode->name;
                            hirClass->methods[methodKey] = methPtr;
                            hirClass->vtable.push_back({methodKey, methPtr});
                        }
                    }
                }
            }

            // func already pushed to module_ before body lowering
        }
    }

    popScope();
    specializations_ = nullptr;  // Clear to avoid dangling pointer
    return std::move(module_);
}

//==============================================================================
// SSA Helpers
//==============================================================================

std::shared_ptr<HIRValue> ASTToHIR::createValue(std::shared_ptr<HIRType> type) {
    // Delegate to builder to ensure we use the same value counter as HIRFunction
    return builder_.createValue(type);
}

HIRBlock* ASTToHIR::createBlock(const std::string& hint) {
    std::ostringstream ss;
    ss << hint << blockCounter_++;
    return currentFunction_->createBlock(ss.str());
}

void ASTToHIR::pushScope() {
    Scope scope;
    scope.isFunctionBoundary = false;
    scope.owningFunction = currentFunction_;
    scopes_.push_back(scope);
}

void ASTToHIR::pushFunctionScope(HIRFunction* func) {
    Scope scope;
    scope.isFunctionBoundary = true;
    scope.owningFunction = func;
    scopes_.push_back(scope);
}

void ASTToHIR::popScope() {
    if (!scopes_.empty()) {
        SPDLOG_DEBUG("[SCOPE] pop depth={} isFuncBoundary={} owner={}",
            scopes_.size(),
            scopes_.back().isFunctionBoundary,
            scopes_.back().owningFunction ? scopes_.back().owningFunction->name : "null");
        scopes_.pop_back();
    } else {
        SPDLOG_ERROR("[SCOPE] popScope called on EMPTY scope stack!");
    }
}

void ASTToHIR::emitMutualRecursionFixup() {
    if (innerFuncClosures_.size() <= 1) {
        innerFuncClosures_.clear();
        return;
    }

    // Collect the set of inner function names in this scope
    std::set<std::string> innerFuncNames;
    for (const auto& info : innerFuncClosures_) {
        innerFuncNames.insert(info.funcName);
    }

    // For each closure, update cells that reference sibling functions
    for (const auto& info : innerFuncClosures_) {
        for (const auto& [capName, capIdx] : info.captureNamesAndIndices) {
            // Skip self-references (handled by existing LLVM-level fix)
            if (capName == info.funcName) continue;

            // If this capture names a sibling inner function, update the cell
            if (innerFuncNames.count(capName)) {
                auto* siblingInfo = lookupVariableInfo(capName);
                if (siblingInfo && siblingInfo->isAlloca) {
                    auto currentVal = builder_.createLoad(
                        siblingInfo->elemType ? siblingInfo->elemType : HIRType::makeAny(),
                        siblingInfo->value);
                    builder_.createStoreCaptureFromClosure(
                        info.closureValue, capIdx, currentVal);
                }
            }
        }
    }
    innerFuncClosures_.clear();
}

void ASTToHIR::defineVariable(const std::string& name, std::shared_ptr<HIRValue> value) {
    if (!scopes_.empty()) {
        VariableInfo info;
        info.value = value;
        info.isAlloca = false;
        info.elemType = nullptr;
        scopes_.back().variables[name] = info;
    }
}

void ASTToHIR::defineVariableAlloca(const std::string& name, std::shared_ptr<HIRValue> allocaPtr,
                                     std::shared_ptr<HIRType> elemType) {
    if (!scopes_.empty()) {
        VariableInfo info;
        info.value = allocaPtr;
        info.isAlloca = true;
        info.elemType = elemType;
        scopes_.back().variables[name] = info;
    }
}

ASTToHIR::VariableInfo* ASTToHIR::lookupVariableInfo(const std::string& name) {
    // Search from innermost to outermost scope
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
        auto found = it->variables.find(name);
        if (found != it->variables.end()) {
            return &found->second;
        }
    }
    return nullptr;
}

ASTToHIR::VariableInfo* ASTToHIR::lookupVariableInfoInCurrentFunction(const std::string& name) {
    // Search scopes only within the current function (stop at function boundaries
    // that belong to a different function). This prevents a `var` declaration in a
    // nested function from finding and overwriting an outer function's alloca.
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
        auto found = it->variables.find(name);
        if (found != it->variables.end()) {
            // Safety: if we found the variable but it's in a scope owned by a
            // different function, don't return it. This prevents a local `var`
            // declaration from finding a same-named variable from an outer
            // function's scope (e.g., `var url` in parseurl shadowing module-level
            // `var url = require('url')`). Without this check, the function-local
            // var stores to the outer function's alloca, which can be null or
            // point to a destroyed stack frame.
            if (it->isFunctionBoundary && it->owningFunction && it->owningFunction != currentFunction_) {
                return nullptr;
            }
            return &found->second;
        }
        // Stop at function boundaries belonging to a different function
        if (it->isFunctionBoundary && it->owningFunction != currentFunction_) {
            break;
        }
    }
    return nullptr;
}

std::shared_ptr<HIRValue> ASTToHIR::lookupVariable(const std::string& name) {
    // Legacy method - looks up and emits load if needed
    auto* info = lookupVariableInfo(name);
    if (!info) return nullptr;

    // If this variable is captured by a nested closure, we need to read from the cell
    if (info->isCapturedByNested && info->closurePtr && info->captureIndex >= 0) {
        // Use cell-based access: ts_closure_get_cell(closure, index) -> ts_cell_get(cell)
        auto type = info->elemType ? info->elemType : HIRType::makeAny();
        // closurePtr is an alloca - load the closure pointer first to ensure dominance
        auto closureVal = builder_.createLoad(HIRType::makeAny(), info->closurePtr);
        // Pass the original variable value as fallback for paths where the closure
        // was never created (e.g., closure only in one branch of if/else)
        std::shared_ptr<HIRValue> fallback = nullptr;
        if (info->isAlloca && info->value) {
            fallback = builder_.createLoad(info->elemType ? info->elemType : type, info->value);
        } else if (info->value) {
            fallback = info->value;
        }
        return builder_.createLoadCaptureFromClosure(closureVal, info->captureIndex, type, fallback);
    }

    if (info->isAlloca && info->elemType) {
        // Emit a load for alloca-stored variables
        return builder_.createLoad(info->elemType, info->value);
    }
    return info->value;
}

bool ASTToHIR::isCapturedVariable(const std::string& name, size_t* outScopeIndex) {
    // Search from innermost to outermost scope
    // A variable is captured if it's defined in a scope that belongs to a DIFFERENT function.
    // We use owningFunction to check this, rather than counting function boundaries,
    // because block scopes (if/for/etc.) within a function are not function boundaries
    // but still belong to the outer function.
    size_t scopeIndex = scopes_.size();

    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it, --scopeIndex) {
        auto found = it->variables.find(name);
        if (found != it->variables.end()) {
            // Found the variable - is it from a different function?
            if (it->owningFunction != currentFunction_) {
                if (outScopeIndex) *outScopeIndex = scopeIndex - 1;
                return true;
            }
            return false;
        }
    }

    return false;  // Variable not found
}

void ASTToHIR::registerCapture(const std::string& name, std::shared_ptr<HIRType> type, size_t scopeIndex) {
    // Check if already registered
    for (const auto& cap : pendingCaptures_) {
        if (cap.name == name) return;  // Already captured
    }

    CaptureInfo info;
    info.name = name;
    info.type = type;
    info.outerScopeIndex = scopeIndex;
    pendingCaptures_.push_back(info);
}

//==============================================================================
// Control Flow Helpers
//==============================================================================

void ASTToHIR::emitBranchIfNeeded(HIRBlock* target) {
    if (!hasTerminator()) {
        builder_.createBranch(target);
    }
}

bool ASTToHIR::hasTerminator() {
    HIRBlock* block = builder_.getInsertBlock();
    if (!block || block->instructions.empty()) {
        return false;
    }
    auto& last = block->instructions.back();
    // Check if last instruction is a terminator
    auto op = last->opcode;
    return op == HIROpcode::Branch || op == HIROpcode::CondBranch ||
           op == HIROpcode::Return || op == HIROpcode::ReturnVoid ||
           op == HIROpcode::Throw || op == HIROpcode::Unreachable;
}

std::shared_ptr<HIRValue> ASTToHIR::boxValueIfNeeded(std::shared_ptr<HIRValue> value) {
    // If value is already Any/ptr type, no boxing needed
    if (!value->type || value->type->kind == HIRTypeKind::Any ||
        value->type->kind == HIRTypeKind::Ptr) {
        return value;
    }

    // Box based on value type
    switch (value->type->kind) {
        case HIRTypeKind::Int64:
            return builder_.createBoxInt(value);
        case HIRTypeKind::Float64:
            return builder_.createBoxFloat(value);
        case HIRTypeKind::Bool:
            return builder_.createBoxBool(value);
        case HIRTypeKind::String:
            return builder_.createBoxString(value);
        case HIRTypeKind::Object:
        case HIRTypeKind::Array:
        case HIRTypeKind::Function:
        case HIRTypeKind::Class:
            return builder_.createBoxObject(value);
        default:
            // Already a ptr-like type, return as is
            return value;
    }
}

std::shared_ptr<HIRValue> ASTToHIR::forceBoxValue(std::shared_ptr<HIRValue> value) {
    // Force boxing regardless of the current type
    // This is needed for cases where the type at HIR level might be Any
    // but after inlining the actual value could be an unboxed primitive
    if (!value->type) {
        return value;  // No type info, return as-is
    }

    switch (value->type->kind) {
        case HIRTypeKind::Int64:
            return builder_.createBoxInt(value);
        case HIRTypeKind::Float64:
            return builder_.createBoxFloat(value);
        case HIRTypeKind::Bool:
            return builder_.createBoxBool(value);
        case HIRTypeKind::String:
            return builder_.createBoxString(value);
        case HIRTypeKind::Object:
        case HIRTypeKind::Array:
        case HIRTypeKind::Function:
        case HIRTypeKind::Class:
            return builder_.createBoxObject(value);
        case HIRTypeKind::Any:
        case HIRTypeKind::Ptr:
            // Type says it's already a pointer, but after inlining it might not be
            // Use runtime check: ts_ensure_boxed will check and box if needed
            return builder_.createCall("ts_ensure_boxed", {value}, HIRType::makeAny());
        default:
            return value;
    }
}

//==============================================================================
// Type Conversion
//==============================================================================

std::shared_ptr<HIRType> ASTToHIR::convertTypeFromString(const std::string& typeStr) {
    if (typeStr.empty()) {
        return HIRType::makeAny();
    }

    // Handle basic TypeScript type names
    if (typeStr == "number") {
        // In TypeScript, 'number' is always IEEE 754 double-precision float
        return HIRType::makeFloat64();
    } else if (typeStr == "string") {
        return HIRType::makeString();
    } else if (typeStr == "boolean") {
        return HIRType::makeBool();
    } else if (typeStr == "void") {
        return HIRType::makeVoid();
    } else if (typeStr == "null") {
        return HIRType::makePtr();
    } else if (typeStr == "undefined") {
        return HIRType::makePtr();
    } else if (typeStr == "any") {
        return HIRType::makeAny();
    } else if (typeStr == "unknown") {
        return HIRType::makeAny();
    } else if (typeStr == "object") {
        return HIRType::makeObject();
    } else if (typeStr == "never") {
        return HIRType::makeVoid();
    } else if (typeStr.find("[]") != std::string::npos) {
        // Array type like "number[]"
        std::string elemType = typeStr.substr(0, typeStr.length() - 2);
        return HIRType::makeArray(convertTypeFromString(elemType));
    } else if (typeStr.find("Array<") == 0) {
        // Array<T> syntax
        size_t start = 6;  // Length of "Array<"
        size_t end = typeStr.rfind('>');
        if (end != std::string::npos && end > start) {
            std::string elemType = typeStr.substr(start, end - start);
            return HIRType::makeArray(convertTypeFromString(elemType));
        }
        return HIRType::makeArray(HIRType::makeAny());
    } else if (typeStr.find("Promise<") == 0) {
        // Promise<T> - treat as ptr for now
        return HIRType::makePtr();
    } else if (typeStr.find("=>") != std::string::npos) {
        // Arrow function type syntax like "() => number" or "(x: number) => number"
        // These are function types, represented as pointers (closures)
        auto funcType = std::make_shared<HIRType>(HIRTypeKind::Function);
        // Parse the return type after "=>"
        size_t arrowPos = typeStr.find("=>");
        if (arrowPos != std::string::npos) {
            std::string retTypeStr = typeStr.substr(arrowPos + 2);
            // Trim leading whitespace
            while (!retTypeStr.empty() && (retTypeStr[0] == ' ' || retTypeStr[0] == '\t')) {
                retTypeStr = retTypeStr.substr(1);
            }
            funcType->returnType = convertTypeFromString(retTypeStr);
        } else {
            funcType->returnType = HIRType::makeAny();
        }
        return funcType;
    }

    // Unknown type - preserve class name for property resolution
    return HIRType::makeClass(typeStr, 0);
}

std::shared_ptr<HIRType> ASTToHIR::convertType(const std::shared_ptr<ts::Type>& type) {
    if (!type) {
        return HIRType::makeAny();
    }

    switch (type->kind) {
        case ts::TypeKind::Void:
            return HIRType::makeVoid();
        case ts::TypeKind::Boolean:
            return HIRType::makeBool();
        case ts::TypeKind::Int:
            return HIRType::makeInt64();
        case ts::TypeKind::Double:
            return HIRType::makeFloat64();
        case ts::TypeKind::String:
            return HIRType::makeString();
        case ts::TypeKind::Any:
        case ts::TypeKind::Unknown:
            return HIRType::makeAny();
        case ts::TypeKind::Null:
        case ts::TypeKind::Undefined:
            return HIRType::makePtr();  // null/undefined are ptr type
        case ts::TypeKind::Array:
            if (auto arrType = std::dynamic_pointer_cast<ts::ArrayType>(type)) {
                return HIRType::makeArray(convertType(arrType->elementType));
            }
            return HIRType::makeArray(HIRType::makeAny());
        case ts::TypeKind::Object:
            return HIRType::makeObject();
        case ts::TypeKind::Class: {
            // Preserve class type information including the class name
            if (auto classType = std::dynamic_pointer_cast<ts::ClassType>(type)) {
                // If the class name comes from a user-imported module (not a real HIR class),
                // use Any instead of Class to prevent extension dispatch from intercepting
                // user-defined classes that happen to share names with built-in types
                // (e.g., eventemitter3's EventEmitter vs the built-in events EventEmitter).
                if (isModuleGlobalVar(classType->name)) {
                    bool isRealHIRClass = false;
                    for (auto& cls : module_->classes) {
                        if (cls->name == classType->name) {
                            isRealHIRClass = true;
                            break;
                        }
                    }
                    if (!isRealHIRClass) {
                        return HIRType::makeAny();
                    }
                }
                return HIRType::makeClass(classType->name, 0);
            }
            return HIRType::makeObject();  // Fallback to generic object
        }
        case ts::TypeKind::BigInt:
            return HIRType::makeObject();  // BigInt is a heap-allocated object
        case ts::TypeKind::Function: {
            // Preserve function type information for closures
            auto funcType = std::dynamic_pointer_cast<ts::FunctionType>(type);
            if (funcType) {
                auto hirFuncType = std::make_shared<HIRType>(HIRTypeKind::Function);
                for (const auto& paramType : funcType->paramTypes) {
                    hirFuncType->paramTypes.push_back(convertType(paramType));
                }
                if (funcType->returnType) {
                    hirFuncType->returnType = convertType(funcType->returnType);
                } else {
                    hirFuncType->returnType = HIRType::makeAny();
                }
                return hirFuncType;
            }
            return HIRType::makePtr();  // Fallback to generic pointer
        }
        default:
            return HIRType::makeAny();
    }
}

//==============================================================================
// Deferred Static Initialization
//==============================================================================

void ASTToHIR::emitDeferredStaticInits() {
    // Emit static property initializations
    for (auto& init : deferredStaticInits_) {
        auto initVal = lowerExpression(init.initExpr);
        builder_.createStore(initVal, init.globalPtr, init.propType);
    }
    deferredStaticInits_.clear();  // Only emit once

    // Emit static blocks
    for (auto* staticBlock : deferredStaticBlocks_) {
        for (auto& stmt : staticBlock->body) {
            lowerStatement(stmt.get());
        }
    }
    deferredStaticBlocks_.clear();  // Only emit once
}

void ASTToHIR::generateClassDecoratorStaticInit(const std::string& className,
                                                 const std::vector<ast::Decorator>& classDecorators,
                                                 const std::vector<ast::NodePtr>& members) {
    // Check if there are any decorators (class, method, property, or parameter)
    bool hasDecorators = !classDecorators.empty();
    if (!hasDecorators) {
        for (const auto& member : members) {
            if (auto* method = dynamic_cast<ast::MethodDefinition*>(member.get())) {
                if (!method->decorators.empty()) { hasDecorators = true; break; }
                for (const auto& param : method->parameters) {
                    if (!param->decorators.empty()) { hasDecorators = true; break; }
                }
            } else if (auto* prop = dynamic_cast<ast::PropertyDefinition*>(member.get())) {
                if (!prop->decorators.empty()) { hasDecorators = true; break; }
            }
            if (hasDecorators) break;
        }
    }
    if (!hasDecorators) return;

    SPDLOG_DEBUG("Generating decorator static init for class: {}", className);

    // Create a static init function for this class
    std::string initFuncName = className + "___static_init";
    auto initFunc = std::make_unique<HIRFunction>(initFuncName);

    // Function takes a context parameter (void*) for consistency with legacy
    initFunc->params.push_back({"ctx", HIRType::makePtr()});
    initFunc->returnType = HIRType::makeVoid();
    initFunc->nextValueId = 1;

    // Save current function and set up for the init function
    HIRFunction* savedFunc = currentFunction_;
    currentFunction_ = initFunc.get();

    // Create entry block
    auto entryBlock = initFunc->createBlock("entry");
    builder_.setInsertPoint(entryBlock);
    currentBlock_ = entryBlock;

    pushScope();

    // Create a class descriptor object with 'name' property
    // 1. Create map: ts_map_create() -> map_ptr
    auto classDescriptor = builder_.createCall("ts_map_create", {}, HIRType::makeMap());

    // 2. Create the class name string: ts_string_create("ClassName")
    auto classNameStr = builder_.createConstString(className);

    // 3. Set the 'name' property: ts_map_set_cstr_string(map, "name", classNameStr)
    // Note: key is a C string pointer, value is a TsString*
    auto nameKey = builder_.createConstCString("name");
    builder_.createCall("ts_map_set_cstr_string", {classDescriptor, nameKey, classNameStr}, HIRType::makeVoid());

    // 4. Box the class descriptor: ts_value_make_object(map) -> boxed_descriptor
    auto boxedDescriptor = builder_.createCall("ts_value_make_object", {classDescriptor}, HIRType::makeAny());

    // 5. Call each decorator in reverse order (innermost first, per TypeScript spec)
    for (auto it = classDecorators.rbegin(); it != classDecorators.rend(); ++it) {
        const auto& decorator = *it;

        if (!decorator.expression) continue;

        // If it's a simple identifier (not a factory), call it directly
        if (auto* id = dynamic_cast<ast::Identifier*>(decorator.expression.get())) {
            // Decorator function takes (target) and returns target
            // The mangled name is "decoratorName_any" for class decorators
            // Note: Regular functions in HIR don't have an implicit context parameter
            std::string mangledDecoratorName = id->name + "_any";

            SPDLOG_DEBUG("  Calling class decorator: {} (mangled: {})", decorator.name, mangledDecoratorName);

            // Call the decorator: result = decorator_any(boxedDescriptor)
            auto result = builder_.createCall(mangledDecoratorName, {boxedDescriptor}, HIRType::makeAny());

            // For now, we ignore the return value since we can't replace the class in AOT
            (void)result;
        }
        // Handle decorator factories @decorator(args)
        else if (auto* call = dynamic_cast<ast::CallExpression*>(decorator.expression.get())) {
            // Get the factory function name
            auto* factoryIdent = dynamic_cast<ast::Identifier*>(call->callee.get());
            if (!factoryIdent) {
                SPDLOG_DEBUG("  Decorator factory with complex callee not supported: {}", decorator.name);
                continue;
            }

            std::string factoryName = factoryIdent->name;
            SPDLOG_DEBUG("  Calling decorator factory: {}", factoryName);

            // Lower each argument - box them for the _any variant
            std::vector<std::shared_ptr<HIRValue>> factoryArgs;
            for (auto& arg : call->arguments) {
                auto argVal = lowerExpression(arg.get());
                // Box the argument since we're calling the _any variant
                auto boxedArg = boxValueIfNeeded(argVal);
                factoryArgs.push_back(boxedArg);
            }

            // Decorator factories are called with _any suffix since monomorphizer
            // doesn't track decorator usage and generates the _any variant
            std::string mangledFactoryName = factoryName + "_any";
            SPDLOG_DEBUG("    Mangled factory name: {}", mangledFactoryName);

            auto decoratorFunc = builder_.createCall(mangledFactoryName, factoryArgs, HIRType::makeAny());

            // Call the returned decorator with the class descriptor
            auto result = builder_.createCallIndirect(decoratorFunc, {boxedDescriptor}, HIRType::makeAny());
            (void)result;
        }
    }

    // Process property decorators: @decorator on class properties
    // Property decorators receive (target, propertyKey)
    for (const auto& member : members) {
        auto* prop = dynamic_cast<ast::PropertyDefinition*>(member.get());
        if (!prop || prop->decorators.empty()) continue;

        SPDLOG_DEBUG("  Processing property decorators for: {}", prop->name);

        // For _any_str, pass raw TsString* (not boxed)
        auto propertyKey = builder_.createConstString(prop->name);

        for (auto it = prop->decorators.rbegin(); it != prop->decorators.rend(); ++it) {
            const auto& decorator = *it;
            if (!decorator.expression) continue;

            if (auto* id = dynamic_cast<ast::Identifier*>(decorator.expression.get())) {
                // Property decorator: (target: any, propertyKey: string) -> _any_str
                std::string mangledName = id->name + "_any_str";
                SPDLOG_DEBUG("    Calling property decorator: {} (mangled: {})", id->name, mangledName);
                builder_.createCall(mangledName, {boxedDescriptor, propertyKey}, HIRType::makeVoid());
            }
        }
    }

    // Process method decorators: @decorator on class methods
    // Method decorators receive (target, propertyKey, descriptor)
    for (const auto& member : members) {
        auto* method = dynamic_cast<ast::MethodDefinition*>(member.get());
        if (!method || method->decorators.empty()) continue;

        SPDLOG_DEBUG("  Processing method decorators for: {}", method->name);

        // For _any_str_any, pass raw TsString* for propertyKey (not boxed)
        auto propertyKey = builder_.createConstString(method->name);

        // Create a PropertyDescriptor object appropriate for the member type
        auto descriptorMap = builder_.createCall("ts_map_create", {}, HIRType::makeMap());
        auto trueVal = builder_.createConstInt(1);
        auto boxedTrue = builder_.createCall("ts_value_make_bool", {trueVal}, HIRType::makeAny());

        if (method->isGetter || method->isSetter) {
            // Accessor descriptor: set 'get' and/or 'set' properties
            // For a getter, set 'get' to true; for a setter, set 'set' to true
            // Since getters/setters on the same property share a descriptor, set both
            auto getKey = builder_.createConstCString("get");
            builder_.createCall("ts_map_set_cstr", {descriptorMap, getKey, boxedTrue}, HIRType::makeVoid());
            auto setKey = builder_.createConstCString("set");
            builder_.createCall("ts_map_set_cstr", {descriptorMap, setKey, boxedTrue}, HIRType::makeVoid());
        } else {
            // Data descriptor: set 'value' property
            auto valueKey = builder_.createConstCString("value");
            builder_.createCall("ts_map_set_cstr", {descriptorMap, valueKey, boxedTrue}, HIRType::makeVoid());
        }
        auto boxedDescriptorMap = builder_.createCall("ts_value_make_object", {descriptorMap}, HIRType::makeAny());

        for (auto it = method->decorators.rbegin(); it != method->decorators.rend(); ++it) {
            const auto& decorator = *it;
            if (!decorator.expression) continue;

            if (auto* id = dynamic_cast<ast::Identifier*>(decorator.expression.get())) {
                // Method decorator: (target: any, propertyKey: string, descriptor: PropertyDescriptor) -> _any_str_any
                std::string mangledName = id->name + "_any_str_any";
                SPDLOG_DEBUG("    Calling method decorator: {} (mangled: {})", id->name, mangledName);
                builder_.createCall(mangledName, {boxedDescriptor, propertyKey, boxedDescriptorMap}, HIRType::makeVoid());
            }
        }
    }

    // Process parameter decorators: @decorator on method parameters
    // Parameter decorators receive (target, propertyKey, parameterIndex)
    for (const auto& member : members) {
        auto* method = dynamic_cast<ast::MethodDefinition*>(member.get());
        if (!method) continue;

        for (size_t paramIdx = 0; paramIdx < method->parameters.size(); ++paramIdx) {
            const auto& param = method->parameters[paramIdx];
            if (param->decorators.empty()) continue;

            SPDLOG_DEBUG("  Processing parameter decorators for: {}[{}]", method->name, paramIdx);

            // For _any_str_int, pass raw TsString* and raw int (not boxed)
            auto propertyKey = builder_.createConstString(method->name);
            auto paramIndex = builder_.createConstInt(static_cast<int64_t>(paramIdx));

            for (auto it = param->decorators.rbegin(); it != param->decorators.rend(); ++it) {
                const auto& decorator = *it;
                if (!decorator.expression) continue;

                if (auto* id = dynamic_cast<ast::Identifier*>(decorator.expression.get())) {
                    // Parameter decorator: (target: any, propertyKey: string, parameterIndex: number) -> _any_str_int
                    std::string mangledName = id->name + "_any_str_int";
                    SPDLOG_DEBUG("    Calling parameter decorator: {} (mangled: {})", id->name, mangledName);
                    builder_.createCall(mangledName, {boxedDescriptor, propertyKey, paramIndex}, HIRType::makeVoid());
                }
            }
        }
    }

    // Return void
    builder_.createReturnVoid();

    popScope();

    // Restore saved function
    currentFunction_ = savedFunc;
    if (savedFunc) {
        auto* savedBlock = savedFunc->getEntryBlock();
        if (savedBlock) {
            builder_.setInsertPoint(savedBlock);
            currentBlock_ = savedBlock;
        }
    }

    // Add the static init function to the module
    module_->functions.push_back(std::move(initFunc));

    SPDLOG_DEBUG("Generated decorator static init function: {}", initFuncName);
}

//==============================================================================
// Statement Lowering
//==============================================================================

void ASTToHIR::lowerStatement(ast::Statement* stmt) {
    stmt->accept(this);
}

std::shared_ptr<HIRValue> ASTToHIR::lowerExpression(ast::Expression* expr) {
    lastValue_ = nullptr;
    expr->accept(this);
    return lastValue_;
}

void ASTToHIR::visitProgram(ast::Program* node) {
    setSourceLine(node);
    for (auto& stmt : node->body) {
        lowerStatement(stmt.get());
    }
}

void ASTToHIR::visitFunctionDeclaration(ast::FunctionDeclaration* node) {
    setSourceLine(node);
    SPDLOG_DEBUG("[FD] ENTER name={} scopes={} currentFunc={} bodySize={}",
        node->name, scopes_.size(),
        currentFunction_ ? currentFunction_->name : "null",
        node->body.size());
    // Declaration-only function (from .d.ts or overload signature) — no code to generate
    if (node->body.empty()) {
        return;
    }

    // Create HIR function - HIRFunction constructor requires a name
    // Add module hash suffix for cross-module disambiguation when inside a
    // module init function (JS modules may define functions with the same name).
    // Generate a unique name for the function declaration. Inner function
    // declarations can collide across modules (e.g., `function next()` inside
    // `handle()` in both route.js and router/index.js). Use a counter to ensure
    // uniqueness, similar to how function expressions use funcExprCounter_.
    std::string funcName = node->name;
    if (currentFunction_) {
        // Nested function declaration — always disambiguate with a counter
        funcName += "_" + std::to_string(funcExprCounter_++);
    } else if (!currentModulePath_.empty()) {
        // Top-level module function — disambiguate with module hash
        std::hash<std::string> hasher;
        auto hash = hasher(currentModulePath_) % 999999;
        funcName += "_m" + std::to_string(hash);
    }
    auto func = std::make_unique<HIRFunction>(funcName);
    func->isAsync = node->isAsync;
    func->isGenerator = node->isGenerator;
    func->sourceLine = node->line;
    func->sourceFile = node->sourceFile;
    func->displayName = node->name;

    // Collect destructured parameter patterns for later extraction
    struct DestructuredParam {
        size_t paramIndex;
        ast::ObjectBindingPattern* objPattern = nullptr;
        ast::ArrayBindingPattern* arrPattern = nullptr;
    };
    std::vector<DestructuredParam> destructuredParams;

    // Handle parameters
    for (size_t paramIdx = 0; paramIdx < node->parameters.size(); ++paramIdx) {
        auto& param = node->parameters[paramIdx];
        // Convert parameter type from string if available
        auto paramType = param->type.empty()
            ? HIRType::makeAny()
            : convertTypeFromString(param->type);

        // If parameter has a default value, it must be Any type to receive undefined
        if (param->initializer) {
            paramType = HIRType::makeAny();
        }

        // Get parameter name from NodePtr (it's a unique_ptr<Node>)
        std::string paramName;
        if (auto* ident = dynamic_cast<ast::Identifier*>(param->name.get())) {
            paramName = ident->name;
        } else if (auto* objPat = dynamic_cast<ast::ObjectBindingPattern*>(param->name.get())) {
            paramName = "param" + std::to_string(func->params.size());
            // Force Any type for destructured params (we extract properties at function entry)
            paramType = HIRType::makeAny();
            destructuredParams.push_back({func->params.size(), objPat, nullptr});
        } else if (auto* arrPat = dynamic_cast<ast::ArrayBindingPattern*>(param->name.get())) {
            paramName = "param" + std::to_string(func->params.size());
            paramType = HIRType::makeAny();
            destructuredParams.push_back({func->params.size(), nullptr, arrPat});
        } else {
            paramName = "param" + std::to_string(func->params.size());
        }

        // Check if this is a rest parameter (...args)
        if (param->isRest) {
            func->hasRestParam = true;
            func->restParamIndex = paramIdx;
            // Rest parameter should be an array type
            if (paramType->kind != HIRTypeKind::Array) {
                // Wrap in array type if not already
                paramType = HIRType::makeArray(paramType, false);
            }
        }

        func->params.push_back({paramName, paramType});
    }

    // Use declared return type if available, otherwise default to Any
    func->returnType = node->returnType.empty()
        ? HIRType::makeAny()
        : convertTypeFromString(node->returnType);

    // Save current function AND current block (needed for nested functions in try/catch)
    HIRFunction* savedFunc = currentFunction_;
    HIRBlock* savedBlock = currentBlock_;
    auto savedCaptures = pendingCaptures_;  // Save outer function's pending captures
    auto savedInnerFuncClosures = std::move(innerFuncClosures_);
    innerFuncClosures_.clear();
    // Save loop/switch/label stacks - nested functions must not see parent's break/continue targets
    auto savedLoopStack = loopStack_;
    auto savedSwitchStack = switchStack_;
    auto savedLabeledLoops = labeledLoops_;
    loopStack_ = {};
    switchStack_ = {};
    labeledLoops_ = {};

    currentFunction_ = func.get();
    clearPendingCaptures();  // Start fresh for this function

    // Create entry block
    auto entryBlock = func->createBlock("entry");
    builder_.setInsertPoint(entryBlock);
    currentBlock_ = entryBlock;

    // Enter function scope (marks function boundary for capture detection)
    pushFunctionScope(func.get());

    // Update function's value counter to start after parameters BEFORE the loop
    // This ensures values created during parameter processing (allocas, etc.)
    // don't conflict with parameter IDs 0, 1, 2, ...
    func->nextValueId = static_cast<uint32_t>(func->params.size());

    // Register parameters in the scope so they can be looked up
    // Parameter values have IDs 0, 1, 2, ... matching their index in HIRToLLVM
    for (size_t i = 0; i < func->params.size(); ++i) {
        const auto& [paramName, paramType] = func->params[i];
        // Create a value representing this parameter with specific ID
        auto paramValue = std::make_shared<HIRValue>(static_cast<uint32_t>(i), paramType, paramName);

        // Check if this parameter has a default value
        ast::Parameter* astParam = (i < node->parameters.size()) ? node->parameters[i].get() : nullptr;
        if (astParam && astParam->initializer) {
            // Parameter has a default value - need to check if undefined and use default
            // Create an alloca to store the potentially-defaulted value
            auto allocaVal = builder_.createAlloca(paramType);

            // Check if param is undefined using runtime function
            // We can't use pointer comparison because ts_value_make_undefined() creates
            // a new TsValue* each time, so pointers won't match. Instead use the
            // runtime's ts_value_is_undefined() which checks the type field.
            auto isUndefined = builder_.createCall("ts_value_is_undefined",
                {paramValue}, HIRType::makeBool());

            // Create basic blocks for the conditional
            auto defaultBB = func->createBlock("default_param");
            auto usedBB = func->createBlock("use_param");
            auto mergeBB = func->createBlock("param_merge");

            // Branch based on undefined check
            builder_.createCondBranch(isUndefined, defaultBB, usedBB);

            // Default block - evaluate default expression and store
            builder_.setInsertPoint(defaultBB);
            currentBlock_ = defaultBB;
            auto* initExpr = dynamic_cast<ast::Expression*>(astParam->initializer.get());
            auto defaultVal = initExpr ? lowerExpression(initExpr) : builder_.createConstUndefined();
            // Force box the default value if parameter type is Any
            // We use forceBoxValue because the expression might be a function call
            // that gets inlined later, changing its type from Any to a concrete type
            if (paramType->kind == HIRTypeKind::Any) {
                defaultVal = forceBoxValue(defaultVal);
            }
            builder_.createStore(defaultVal, allocaVal);
            builder_.createBranch(mergeBB);

            // Use param block - store the passed parameter value
            builder_.setInsertPoint(usedBB);
            currentBlock_ = usedBB;
            builder_.createStore(paramValue, allocaVal);
            builder_.createBranch(mergeBB);

            // Merge block - continue execution
            builder_.setInsertPoint(mergeBB);
            currentBlock_ = mergeBB;

            // Register the alloca as the variable (loads will get the correct value)
            defineVariableAlloca(paramName, allocaVal, paramType);
        } else {
            // No default value - store into an alloca so reassignment works
            auto allocaVal = builder_.createAlloca(paramType);
            builder_.createStore(paramValue, allocaVal);
            defineVariableAlloca(paramName, allocaVal, paramType);
        }
    }

    // Emit destructuring extraction for parameters with binding patterns
    for (auto& dp : destructuredParams) {
        // Get the parameter value by its index
        auto paramValue = std::make_shared<HIRValue>(
            static_cast<uint32_t>(dp.paramIndex),
            HIRType::makeAny(),
            func->params[dp.paramIndex].first);
        if (dp.objPattern) {
            lowerObjectBindingPattern(dp.objPattern, paramValue);
        } else if (dp.arrPattern) {
            lowerArrayBindingPattern(dp.arrPattern, paramValue);
        }
    }

    // Create 'arguments' array-like object if the function body references 'arguments'.
    // Must be done at function entry (before any other code) because inner calls
    // will overwrite ts_last_call_argc, making lazy creation incorrect.
    // Arrow functions don't have their own 'arguments' (they inherit from enclosing).
    {
        bool usesArguments = false;
        for (auto& stmt : node->body) {
            if (containsArgumentsIdentifier(stmt.get())) {
                usesArguments = true;
                break;
            }
        }
        if (usesArguments) {
            // Build args for ts_create_arguments_from_params(p0..p9)
            // The runtime uses ts_last_call_argc to know how many were actually passed.
            // Include both declared params AND hidden __argN__ params so that
            // functions with fewer formal params than call args still capture all args.
            std::vector<std::shared_ptr<HIRValue>> callArgs;

            // Pass each user parameter (up to 10), padding with undefined
            size_t userIdx = 0;
            for (size_t i = 0; i < func->params.size() && userIdx < 10; ++i) {
                if (func->params[i].first == "__closure__") continue;
                auto paramVal = lookupVariable(func->params[i].first);
                if (!paramVal) {
                    paramVal = builder_.createConstUndefined();
                }
                callArgs.push_back(paramVal);
                userIdx++;
            }
            // Pad remaining slots with undefined (up to 10 total params)
            while (userIdx < 10) {
                callArgs.push_back(builder_.createConstUndefined());
                userIdx++;
            }

            // Call runtime to create arguments array
            auto argsArray = builder_.createCall("ts_create_arguments_from_params",
                callArgs, HIRType::makeAny());

            // Store as local variable "arguments"
            auto allocaVal = builder_.createAlloca(HIRType::makeAny(), "arguments");
            builder_.createStore(argsArray, allocaVal, HIRType::makeAny());
            defineVariableAlloca("arguments", allocaVal, HIRType::makeAny());
        }
    }

    // If this is user_main, emit deferred static property initializations
    if (node->name == "user_main") {
        emitDeferredStaticInits();
    }

    // JavaScript function hoisting: pre-declare nested function names as variables
    // This allows functions to be called before they appear in source order.
    // We create allocas for function names, which will be filled when the function
    // declaration is processed. Calls to these names will use indirect call.
    for (auto& stmt : node->body) {
        if (auto* funcDecl = dynamic_cast<ast::FunctionDeclaration*>(stmt.get())) {
            // Create a function type for the closure
            auto funcType = std::make_shared<HIRType>(HIRTypeKind::Function);
            for (auto& param : funcDecl->parameters) {
                std::shared_ptr<HIRType> paramType = HIRType::makeAny();
                if (!param->type.empty()) {
                    paramType = convertTypeFromString(param->type);
                }
                funcType->paramTypes.push_back(paramType);
            }
            funcType->returnType = funcDecl->returnType.empty()
                ? HIRType::makeAny()
                : convertTypeFromString(funcDecl->returnType);

            // Create an alloca for the function variable (will hold closure or function ptr)
            auto allocaVal = builder_.createAlloca(funcType, funcDecl->name);
            // Initialize with null - will be set when the function is processed
            builder_.createStore(builder_.createConstNull(), allocaVal);
            defineVariableAlloca(funcDecl->name, allocaVal, funcType);
        }
    }

    // Variable hoisting for shared closure cells.
    // When the function body has nested function declarations, pre-declare ALL
    // vars so the first pass (function decl lowering) can detect them as captures.
    // Without this, inner function declarations can't see outer variables that
    // are declared after them in source order, breaking closure capture.
    {
        bool hasNestedFuncDecls = false;
        for (auto& stmt : node->body) {
            if (dynamic_cast<ast::FunctionDeclaration*>(stmt.get())) {
                hasNestedFuncDecls = true;
                break;
            }
            // Also check inside BlockStatements (function body may be wrapped)
            if (auto* block = dynamic_cast<ast::BlockStatement*>(stmt.get())) {
                for (auto& inner : block->statements) {
                    if (dynamic_cast<ast::FunctionDeclaration*>(inner.get())) {
                        hasNestedFuncDecls = true;
                        break;
                    }
                }
                if (hasNestedFuncDecls) break;
            }
        }
        bool hoistAllVars = hasNestedFuncDecls;

        for (auto& stmt : node->body) {
            if (auto* block = dynamic_cast<ast::BlockStatement*>(stmt.get())) {
                if (hoistAllVars) {
                    for (auto& inner : block->statements) {
                        if (auto* vd = dynamic_cast<ast::VariableDeclaration*>(inner.get())) {
                            if (auto* ident = dynamic_cast<ast::Identifier*>(vd->name.get())) {
                                if (!lookupVariableInfoInCurrentFunction(ident->name)) {
                                    auto allocaVal = builder_.createAlloca(HIRType::makeAny(), ident->name);
                                    builder_.createStore(builder_.createConstUndefined(), allocaVal);
                                    defineVariableAlloca(ident->name, allocaVal, HIRType::makeAny());
                                }
                            }
                        }
                    }
                }
                continue;
            }
            if (auto* varDecl = dynamic_cast<ast::VariableDeclaration*>(stmt.get())) {
                if (auto* ident = dynamic_cast<ast::Identifier*>(varDecl->name.get())) {
                    if (hoistAllVars) {
                        if (!lookupVariableInfoInCurrentFunction(ident->name)) {
                            auto allocaVal = builder_.createAlloca(HIRType::makeAny(), ident->name);
                            builder_.createStore(builder_.createConstUndefined(), allocaVal);
                            defineVariableAlloca(ident->name, allocaVal, HIRType::makeAny());
                        }
                    } else if (!lookupVariableInfo(ident->name) &&
                               varDecl->initializer && containsClosureExpression(varDecl->initializer.get())) {
                        auto varType = HIRType::makeAny();
                        auto allocaVal = builder_.createAlloca(varType, ident->name);
                        builder_.createStore(builder_.createConstUndefined(), allocaVal, varType);
                        defineVariableAlloca(ident->name, allocaVal, varType);
                    }
                }
            }
        }
    }

    // Lower function body in two passes for proper JavaScript function hoisting:
    // FIRST PASS: Process FunctionDeclarations to create closures
    // This ensures nested functions are available before any other code runs,
    // matching JavaScript semantics where function declarations are hoisted.
    for (size_t i = 0; i < node->body.size(); ++i) {
        auto& stmt = node->body[i];
        if (dynamic_cast<ast::FunctionDeclaration*>(stmt.get())) {
            lowerStatement(stmt.get());
        }
    }
    emitMutualRecursionFixup();

    // SECOND PASS: Process non-FunctionDeclaration statements in order
    for (size_t i = 0; i < node->body.size(); ++i) {
        auto& stmt = node->body[i];
        if (!dynamic_cast<ast::FunctionDeclaration*>(stmt.get())) {
            lowerStatement(stmt.get());
            // Stop processing statements after a terminator (return, throw, etc.)
            // This prevents dead code from being emitted after control flow ends
            if (builder_.isBlockTerminated()) {
                break;
            }
        }
    }

    // Add implicit return if no terminator
    if (!hasTerminator()) {
        builder_.createReturnVoid();
    }

    // Copy pending captures to the function's captures list
    for (const auto& cap : pendingCaptures_) {
        func->captures.push_back({cap.name, cap.type});
    }
    bool hasClosure = !pendingCaptures_.empty();

    // Save the captures list for later use (after we restore context)
    std::vector<std::pair<std::string, std::shared_ptr<HIRType>>> innerCaptures = func->captures;

    popScope();

    // Get the function pointer before we move it
    HIRFunction* funcPtr = func.get();

    // Restore saved function and block
    currentFunction_ = savedFunc;
    currentBlock_ = savedBlock;
    pendingCaptures_ = savedCaptures;  // Restore outer function's pending captures
    innerFuncClosures_ = std::move(savedInnerFuncClosures);
    loopStack_ = savedLoopStack;
    switchStack_ = savedSwitchStack;
    labeledLoops_ = savedLabeledLoops;
    if (savedBlock) {
        builder_.setInsertPoint(savedBlock);
    }

    // Add function to module
    module_->functions.push_back(std::move(func));

    // For nested functions (when savedFunc != nullptr), we need to handle closures
    // If this is a nested function with captures, create a closure and define
    // the function name as a closure variable in the outer scope
    if (savedFunc && hasClosure) {
        // Build function type for the closure
        auto closureFuncType = std::make_shared<HIRType>(HIRTypeKind::Function);
        for (const auto& [paramName, paramType] : funcPtr->params) {
            closureFuncType->paramTypes.push_back(paramType);
        }
        closureFuncType->returnType = funcPtr->returnType;

        // Create a closure with captured values
        std::vector<std::shared_ptr<HIRValue>> captureValues;
        for (const auto& cap : innerCaptures) {
            const std::string& capName = cap.first;
            const auto& capType = cap.second;

            // Check if this variable requires capture propagation
            size_t scopeIndex = 0;
            bool needsCapturePropagation = isCapturedVariable(capName, &scopeIndex);

            if (needsCapturePropagation) {
                // Variable is in an outer function's scope - propagate the capture
                registerCapture(capName, capType, scopeIndex);
                currentFunction_->hasClosure = true;
                bool alreadyInCaptures = false;
                for (const auto& existingCap : currentFunction_->captures) {
                    if (existingCap.first == capName) {
                        alreadyInCaptures = true;
                        break;
                    }
                }
                if (!alreadyInCaptures) {
                    currentFunction_->captures.push_back({capName, capType});
                }
                auto val = builder_.createLoadCapture(capName, capType);
                captureValues.push_back(val);
            } else {
                // Variable is directly accessible in the current function's scope
                auto* info = lookupVariableInfo(capName);
                if (info) {
                    std::shared_ptr<HIRValue> val;
                    if (info->isAlloca && info->elemType) {
                        val = builder_.createLoad(info->elemType, info->value);
                    } else {
                        val = info->value;
                    }
                    captureValues.push_back(val);
                } else {
                    captureValues.push_back(builder_.createConstNull());
                }
            }
        }

        auto closureVal = builder_.createMakeClosure(funcName, captureValues, closureFuncType);

        // Mark captured variables as "captured by nested"
        int captureIdx = 0;
        for (const auto& cap : innerCaptures) {
            const std::string& capName = cap.first;
            size_t scopeIndex = 0;
            if (!isCapturedVariable(capName, &scopeIndex)) {
                auto* info = lookupVariableInfo(capName);
                if (info && !info->isCapturedByNested) {
                    info->isCapturedByNested = true;
                    // Store closure pointer in an alloca to ensure SSA dominance
                    // (closure may be created in try block but accessed from catch block)
                    auto closureAlloca = builder_.createAlloca(HIRType::makeAny(), capName + "$closure");
                    builder_.createStore(closureVal, closureAlloca);
                    info->closurePtr = closureAlloca;
                    info->captureIndex = captureIdx;
                }
            }
            captureIdx++;
        }

        // Store the closure into the pre-created alloca (if it exists)
        // This enables function hoisting - the alloca was created before processing statements
        auto* existingInfo = lookupVariableInfo(node->name);
        if (existingInfo && existingInfo->isAlloca) {
            builder_.createStore(closureVal, existingInfo->value);
        } else {
            // No pre-created alloca, define the function name as a closure variable
            defineVariable(node->name, closureVal);
        }

        // Record closure info for mutual recursion post-sweep
        {
            InnerFuncClosureInfo closureInfo;
            closureInfo.funcName = node->name;
            closureInfo.closureValue = closureVal;
            int idx = 0;
            for (const auto& cap : innerCaptures) {
                closureInfo.captureNamesAndIndices.push_back({cap.first, idx++});
            }
            innerFuncClosures_.push_back(std::move(closureInfo));
        }

        // Also store to module global if this is a module-level function declaration.
        // This allows other functions in the module to access it via __modvar_ globals
        // instead of closure cells, fixing ordering issues where a capturing function
        // is declared before the captured function.
        if (isModuleGlobalVar(node->name)) {
            builder_.createStoreGlobal(modVarName(node->name), closureVal);
        }
    } else if (savedFunc) {
        // Nested function without captures - still store it so it can be called
        // Build function type
        auto funcType = std::make_shared<HIRType>(HIRTypeKind::Function);
        for (const auto& [paramName, paramType] : funcPtr->params) {
            funcType->paramTypes.push_back(paramType);
        }
        funcType->returnType = funcPtr->returnType;

        // Create a closure with no captures (for call_indirect compatibility)
        std::vector<std::shared_ptr<HIRValue>> emptyCaptureValues;
        auto closureVal = builder_.createMakeClosure(funcName, emptyCaptureValues, funcType);

        // Store into pre-created alloca or define new variable
        auto* existingInfo = lookupVariableInfo(node->name);
        if (existingInfo && existingInfo->isAlloca) {
            builder_.createStore(closureVal, existingInfo->value);
        } else {
            defineVariable(node->name, closureVal);
        }

        // Also store to module global for module-level function declarations
        if (isModuleGlobalVar(node->name)) {
            builder_.createStoreGlobal(modVarName(node->name), closureVal);
        }
    }
}

void ASTToHIR::visitVariableDeclaration(ast::VariableDeclaration* node) {
    setSourceLine(node);
    // VariableDeclaration has name (NodePtr) and initializer (ExprPtr)
    // name can be Identifier, ObjectBindingPattern, or ArrayBindingPattern

    // Lower the initializer first (if any)
    std::shared_ptr<HIRValue> initValue;
    if (node->initializer) {
        // Set pending display name for arrow functions / function expressions
        // so they can use the variable name for .name property (ES2019)
        if (auto* ident = dynamic_cast<ast::Identifier*>(node->name.get())) {
            if (dynamic_cast<ast::ArrowFunction*>(node->initializer.get()) ||
                dynamic_cast<ast::FunctionExpression*>(node->initializer.get())) {
                pendingClosureDisplayName_ = ident->name;
            }
        }

        // Pre-register variable for self-referencing function expressions
        // (e.g., `const create = str => { create(match[1]); }` - recursive self-call).
        // The arrow function body is processed inline below, but the variable isn't
        // registered until after lowerExpression returns. Pre-register an alloca so
        // that isCapturedVariable/lookupVariableInfo can find it during body lowering.
        std::shared_ptr<HIRValue> preAllocaPtr;
        if (auto* ident = dynamic_cast<ast::Identifier*>(node->name.get())) {
            if (dynamic_cast<ast::ArrowFunction*>(node->initializer.get()) ||
                dynamic_cast<ast::FunctionExpression*>(node->initializer.get())) {
                // Only pre-register if not already in scope (avoid duplicates)
                if (!lookupVariableInfo(ident->name)) {
                    preAllocaPtr = builder_.createAlloca(HIRType::makeAny(), ident->name);
                    defineVariableAlloca(ident->name, preAllocaPtr, HIRType::makeAny());
                }
            }
        }

        SPDLOG_DEBUG("[VD] initializer kind={} for var={}", node->initializer->getKind(),
            dynamic_cast<ast::Identifier*>(node->name.get()) ? dynamic_cast<ast::Identifier*>(node->name.get())->name : "?");
        initValue = lowerExpression(node->initializer.get());
        pendingClosureDisplayName_.clear();

        // Track class expression assignments: const MyClass = class { ... }
        if (dynamic_cast<ast::ClassExpression*>(node->initializer.get())) {
            if (auto* ident = dynamic_cast<ast::Identifier*>(node->name.get())) {
                // Map the variable name to the generated class name
                variableToClassName_[ident->name] = lastGeneratedClassName_;
            }
        }
    } else {
        initValue = builder_.createConstUndefined();
    }

    // Handle the binding pattern
    if (auto* ident = dynamic_cast<ast::Identifier*>(node->name.get())) {
        // Simple identifier - create variable directly
        std::shared_ptr<HIRType> varType = HIRType::makeAny();
        if (node->initializer && node->initializer->inferredType) {
            varType = convertType(node->initializer->inferredType);
        }
        if (!node->type.empty() && varType->kind == HIRTypeKind::Any) {
            varType = convertTypeFromString(node->type);
        }
        // Fallback: if type is still Any but initValue has a more specific type, use that
        // This handles cases like `const map = new Map()` where the lowered expression knows the type
        if (varType->kind == HIRTypeKind::Any && initValue && initValue->type && initValue->type->kind != HIRTypeKind::Any) {
            varType = initValue->type;
        }
        // Override: if initValue is a Generator (from generator method call), use that type
        // The analyzer may infer the wrong class type (e.g., NumberRange instead of Generator)
        if (initValue && initValue->type && initValue->type->kind == HIRTypeKind::Class &&
            initValue->type->className == "Generator") {
            varType = initValue->type;
        }

        // Check if this variable was already pre-hoisted in the CURRENT function -
        // if so, reuse its alloca. We must only match allocas from the current
        // function scope, not from outer functions, because a `var` declaration
        // inside a nested function should shadow outer variables, not overwrite them.
        auto* existingInfo = lookupVariableInfoInCurrentFunction(ident->name);
        if (existingInfo && existingInfo->isAlloca) {
            // Variable was pre-hoisted: just store the init value into the existing alloca
            builder_.createStore(initValue, existingInfo->value, varType);
            // Update the type info if we have a more specific type now
            if (varType->kind != HIRTypeKind::Any) {
                existingInfo->elemType = varType;
            }
            // If this variable is captured by a nested closure, also update the cell
            // so the closure sees the new value (e.g., var interval = setInterval(...))
            if (existingInfo->isCapturedByNested && existingInfo->closurePtr && existingInfo->captureIndex >= 0) {
                auto closureVal = builder_.createLoad(HIRType::makeAny(), existingInfo->closurePtr);
                builder_.createStoreCaptureFromClosure(closureVal, existingInfo->captureIndex, initValue);
            }
        } else {
            auto allocaPtr = builder_.createAlloca(varType, ident->name);
            builder_.createStore(initValue, allocaPtr, varType);
            defineVariableAlloca(ident->name, allocaPtr, varType);
        }

        // If this variable is a module-scoped global (from an imported module),
        // also store the value to the LLVM global variable so other functions
        // from the same module can access it via LoadGlobal.
        // Only do this in the module init function itself — a `var` declaration
        // inside a nested function expression (e.g., forEach callback) with the
        // same name should shadow the module global, not overwrite it.
        if (isModuleGlobalVar(ident->name) &&
            (!currentFunction_ || currentFunction_->name.find("__module_init_") == 0)) {
            std::string globalName = modVarName(ident->name);
            builder_.createStoreGlobal(globalName, initValue);
        }
    } else if (auto* objPattern = dynamic_cast<ast::ObjectBindingPattern*>(node->name.get())) {
        // Object destructuring: const { a, b } = obj
        lowerObjectBindingPattern(objPattern, initValue);
    } else if (auto* arrPattern = dynamic_cast<ast::ArrayBindingPattern*>(node->name.get())) {
        // Array destructuring: const [a, b] = arr
        lowerArrayBindingPattern(arrPattern, initValue);
    }
}

void ASTToHIR::lowerObjectBindingPattern(ast::ObjectBindingPattern* pattern,
                                          std::shared_ptr<HIRValue> sourceValue) {
    for (auto& elem : pattern->elements) {
        if (auto* binding = dynamic_cast<ast::BindingElement*>(elem.get())) {
            lowerBindingElement(binding, sourceValue, true /* isObjectPattern */);
        }
    }
}

void ASTToHIR::lowerArrayBindingPattern(ast::ArrayBindingPattern* pattern,
                                         std::shared_ptr<HIRValue> sourceValue) {
    int64_t index = 0;
    for (auto& elem : pattern->elements) {
        if (auto* binding = dynamic_cast<ast::BindingElement*>(elem.get())) {
            if (binding->isSpread) {
                // Rest element: ...rest - get remaining elements
                lowerRestElement(binding, sourceValue, index);
            } else {
                // Regular element - extract by index
                lowerBindingElementByIndex(binding, sourceValue, index);
            }
            index++;
        } else if (dynamic_cast<ast::OmittedExpression*>(elem.get())) {
            // Hole in array pattern: [a, , b] - skip this index
            index++;
        }
    }
}

void ASTToHIR::lowerBindingElement(ast::BindingElement* binding,
                                    std::shared_ptr<HIRValue> sourceValue,
                                    bool isObjectPattern) {
    // Determine the property name to extract
    std::string propName;
    if (!binding->propertyName.empty()) {
        // { propName: varName } - use explicit property name
        propName = binding->propertyName;
    } else if (auto* ident = dynamic_cast<ast::Identifier*>(binding->name.get())) {
        // { varName } - shorthand, property name is same as variable name
        propName = ident->name;
    }

    // Get the property value from source object
    auto propNameValue = builder_.createConstString(propName);
    auto extractedValue = builder_.createGetPropDynamic(sourceValue, propNameValue);

    // Handle default value if present
    if (binding->initializer) {
        // Check if extracted value is undefined using runtime function
        auto isUndefined = builder_.createIsUndefined(extractedValue);
        auto defaultValue = lowerExpression(binding->initializer.get());

        // Box the default value to match extractedValue type (Any/ptr)
        defaultValue = boxValueIfNeeded(defaultValue);

        // Select: isUndefined ? defaultValue : extractedValue
        extractedValue = builder_.createSelect(isUndefined, defaultValue, extractedValue);
    }

    // Bind to variable(s)
    if (auto* ident = dynamic_cast<ast::Identifier*>(binding->name.get())) {
        // Simple variable binding
        auto varType = HIRType::makeAny();
        auto allocaPtr = builder_.createAlloca(varType, ident->name);
        builder_.createStore(extractedValue, allocaPtr, varType);
        defineVariableAlloca(ident->name, allocaPtr, varType);

        // If this variable is a module-scoped global (e.g. from destructured require()),
        // also store to the __modvar_ LLVM global so other functions can access it.
        if (isModuleGlobalVar(ident->name)) {
            builder_.createStoreGlobal(modVarName(ident->name), extractedValue);
        }
    } else if (auto* nestedObj = dynamic_cast<ast::ObjectBindingPattern*>(binding->name.get())) {
        // Nested object destructuring: { a: { b, c } }
        lowerObjectBindingPattern(nestedObj, extractedValue);
    } else if (auto* nestedArr = dynamic_cast<ast::ArrayBindingPattern*>(binding->name.get())) {
        // Nested array destructuring: { a: [b, c] }
        lowerArrayBindingPattern(nestedArr, extractedValue);
    }
}

void ASTToHIR::lowerBindingElementByIndex(ast::BindingElement* binding,
                                           std::shared_ptr<HIRValue> sourceValue,
                                           int64_t index) {
    // Get the element at index from source array
    // Force result type to Any so the value stays boxed (won't be unboxed in HIRToLLVM)
    auto indexValue = builder_.createConstInt(index);
    auto extractedValue = builder_.createGetElem(sourceValue, indexValue, HIRType::makeAny());

    // Handle default value if present
    if (binding->initializer) {
        // Use bounds check instead of ts_value_is_undefined, because type propagation
        // may unbox the extracted value to a primitive (double/i64), making the undefined
        // check impossible. Bounds check: index >= array.length means out-of-bounds → use default.
        auto arrayLength = builder_.createCall("ts_array_length", {sourceValue}, HIRType::makeInt64());
        auto idxConst = builder_.createConstInt(index);
        auto inBounds = builder_.createCmpLtI64(idxConst, arrayLength);

        auto defaultValue = lowerExpression(binding->initializer.get());

        // Box the default value to match extractedValue (Any/ptr)
        defaultValue = boxValueIfNeeded(defaultValue);
        // Also box the extracted value if it was unboxed by type propagation
        extractedValue = boxValueIfNeeded(extractedValue);

        // Select: inBounds ? extractedValue : defaultValue
        extractedValue = builder_.createSelect(inBounds, extractedValue, defaultValue);
    }

    // Bind to variable(s)
    if (auto* ident = dynamic_cast<ast::Identifier*>(binding->name.get())) {
        auto varType = HIRType::makeAny();
        auto allocaPtr = builder_.createAlloca(varType, ident->name);
        builder_.createStore(extractedValue, allocaPtr, varType);
        defineVariableAlloca(ident->name, allocaPtr, varType);

        // If this variable is a module-scoped global (e.g. from destructured require()),
        // also store to the __modvar_ LLVM global so other functions can access it.
        if (isModuleGlobalVar(ident->name)) {
            builder_.createStoreGlobal(modVarName(ident->name), extractedValue);
        }
    } else if (auto* nestedObj = dynamic_cast<ast::ObjectBindingPattern*>(binding->name.get())) {
        lowerObjectBindingPattern(nestedObj, extractedValue);
    } else if (auto* nestedArr = dynamic_cast<ast::ArrayBindingPattern*>(binding->name.get())) {
        lowerArrayBindingPattern(nestedArr, extractedValue);
    }
}

void ASTToHIR::lowerRestElement(ast::BindingElement* binding,
                                 std::shared_ptr<HIRValue> sourceValue,
                                 int64_t startIndex) {
    // Create a new array with remaining elements using array.slice(startIndex)
    auto startIndexValue = builder_.createConstInt(startIndex);
    std::vector<std::shared_ptr<HIRValue>> sliceArgs = { startIndexValue };
    auto restValue = builder_.createCallMethod(sourceValue, "slice", sliceArgs, HIRType::makeAny());

    // Bind to variable
    if (auto* ident = dynamic_cast<ast::Identifier*>(binding->name.get())) {
        auto varType = HIRType::makeAny();
        auto allocaPtr = builder_.createAlloca(varType, ident->name);
        builder_.createStore(restValue, allocaPtr, varType);
        defineVariableAlloca(ident->name, allocaPtr, varType);
    }
}

void ASTToHIR::visitExpressionStatement(ast::ExpressionStatement* node) {
    setSourceLine(node);
    if (node->expression) {
        lowerExpression(node->expression.get());
    }
}

void ASTToHIR::visitBlockStatement(ast::BlockStatement* node) {
    setSourceLine(node);
    // Synthetic blocks (from multi-var declarations like "var a = 1, b = 2;")
    // should NOT create a new scope - variables need to be visible in the
    // enclosing scope, just like individual var declarations would be.
    if (!node->isSynthetic) {
        pushScope();
    }
    for (auto& stmt : node->statements) {
        lowerStatement(stmt.get());
        // Stop processing statements after a terminator (return, throw, etc.)
        // This prevents dead code from being emitted after control flow ends
        if (builder_.isBlockTerminated()) {
            break;
        }
    }
    if (!node->isSynthetic) {
        popScope();
    }
}

void ASTToHIR::visitReturnStatement(ast::ReturnStatement* node) {
    setSourceLine(node);
    // Pop all active exception handlers before returning from inside try blocks.
    // Without this, a tail-call return destroys the stack frame but leaves the
    // handler on exceptionStack, creating a "zombie frame" that longjmp can
    // jump back to — causing stack corruption and crashes.
    // IMPORTANT: evaluate the return expression FIRST (while handler is still
    // active), then pop handlers. This ensures try/catch still protects the
    // expression evaluation (e.g., `return parseUrl(req).pathname` must be
    // caught if parseUrl throws).
    if (node->expression) {
        auto retVal = lowerExpression(node->expression.get());
        for (int i = 0; i < tryDepth_; i++) {
            builder_.createPopHandler();
        }
        builder_.createReturn(retVal);
    } else {
        for (int i = 0; i < tryDepth_; i++) {
            builder_.createPopHandler();
        }
        builder_.createReturnVoid();
    }
}

void ASTToHIR::visitIfStatement(ast::IfStatement* node) {
    setSourceLine(node);
    auto cond = lowerExpression(node->condition.get());

    auto* thenBlock = createBlock("if.then");
    auto* elseBlock = createBlock("if.else");
    auto* mergeBlock = createBlock("if.end");

    builder_.createCondBranch(cond, thenBlock, elseBlock);

    // Then block
    builder_.setInsertPoint(thenBlock);
    currentBlock_ = thenBlock;
    lowerStatement(node->thenStatement.get());
    emitBranchIfNeeded(mergeBlock);

    // Else block
    builder_.setInsertPoint(elseBlock);
    currentBlock_ = elseBlock;
    if (node->elseStatement) {
        lowerStatement(node->elseStatement.get());
    }
    emitBranchIfNeeded(mergeBlock);

    // Continue in merge block
    builder_.setInsertPoint(mergeBlock);
    currentBlock_ = mergeBlock;
}

void ASTToHIR::visitWhileStatement(ast::WhileStatement* node) {
    setSourceLine(node);
    auto* condBlock = createBlock("while.cond");
    auto* bodyBlock = createBlock("while.body");
    auto* endBlock = createBlock("while.end");

    // Push loop context for break/continue
    LoopContext ctx = {condBlock, endBlock};
    loopStack_.push(ctx);
    breakTargetStack_.push(endBlock);

    // Register with label if this loop is labeled
    std::string myLabel;
    if (!pendingLabel_.empty()) {
        myLabel = pendingLabel_;
        labeledLoops_[myLabel] = ctx;
        pendingLabel_.clear();  // Clear so nested loops don't also register
    }

    // For do-while, jump to body first (body executes before condition).
    // For while, jump to condition first.
    builder_.createBranch(node->isDoWhile ? bodyBlock : condBlock);

    // Condition block
    builder_.setInsertPoint(condBlock);
    currentBlock_ = condBlock;
    auto cond = lowerExpression(node->condition.get());
    builder_.createCondBranch(cond, bodyBlock, endBlock);

    // Body block
    builder_.setInsertPoint(bodyBlock);
    currentBlock_ = bodyBlock;
    lowerStatement(node->body.get());
    emitBranchIfNeeded(condBlock);

    loopStack_.pop();
    breakTargetStack_.pop();
    if (!myLabel.empty()) {
        labeledLoops_.erase(myLabel);
    }

    // Continue in end block
    builder_.setInsertPoint(endBlock);
    currentBlock_ = endBlock;
}

void ASTToHIR::visitForStatement(ast::ForStatement* node) {
    setSourceLine(node);
    auto* condBlock = createBlock("for.cond");
    auto* bodyBlock = createBlock("for.body");
    auto* updateBlock = createBlock("for.update");
    auto* endBlock = createBlock("for.end");

    // Push loop context (continue -> update, break -> end)
    LoopContext ctx = {updateBlock, endBlock};
    loopStack_.push(ctx);
    breakTargetStack_.push(endBlock);

    // Register with label if this loop is labeled
    std::string myLabel;
    if (!pendingLabel_.empty()) {
        myLabel = pendingLabel_;
        labeledLoops_[myLabel] = ctx;
        pendingLabel_.clear();  // Clear so nested loops don't also register
    }

    pushScope();

    // Initializer
    if (node->initializer) {
        lowerStatement(node->initializer.get());
    }

    builder_.createBranch(condBlock);

    // Condition block
    builder_.setInsertPoint(condBlock);
    currentBlock_ = condBlock;
    if (node->condition) {
        auto cond = lowerExpression(node->condition.get());
        builder_.createCondBranch(cond, bodyBlock, endBlock);
    } else {
        // Infinite loop without condition
        builder_.createBranch(bodyBlock);
    }

    // Body block
    builder_.setInsertPoint(bodyBlock);
    currentBlock_ = bodyBlock;
    lowerStatement(node->body.get());
    emitBranchIfNeeded(updateBlock);

    // Update block
    builder_.setInsertPoint(updateBlock);
    currentBlock_ = updateBlock;
    if (node->incrementor) {
        lowerExpression(node->incrementor.get());
    }
    builder_.createBranch(condBlock);

    loopStack_.pop();
    breakTargetStack_.pop();
    if (!myLabel.empty()) {
        labeledLoops_.erase(myLabel);
    }
    popScope();

    // Continue in end block
    builder_.setInsertPoint(endBlock);
    currentBlock_ = endBlock;
}

void ASTToHIR::visitForOfStatement(ast::ForOfStatement* node) {
    setSourceLine(node);
    // For-of loop: iterate over iterable (arrays or generators)
    auto* condBlock = createBlock("forof.cond");
    auto* bodyBlock = createBlock("forof.body");
    auto* updateBlock = createBlock("forof.update");
    auto* endBlock = createBlock("forof.end");

    // Push loop context (continue -> update, break -> end)
    LoopContext ctx = {updateBlock, endBlock};
    loopStack_.push(ctx);
    breakTargetStack_.push(endBlock);

    // Register with label if this loop is labeled
    std::string myLabel;
    if (!pendingLabel_.empty()) {
        myLabel = pendingLabel_;
        labeledLoops_[myLabel] = ctx;
        pendingLabel_.clear();  // Clear so nested loops don't also register
    }

    pushScope();

    // Get the iterable
    auto* iterExpr = dynamic_cast<ast::Expression*>(node->expression.get());
    auto iterable = iterExpr ? lowerExpression(iterExpr) : createValue(HIRType::makeAny());

    // Check if this is a Generator/AsyncGenerator/Iterator - use iterator protocol instead of array indexing
    bool isGenerator = iterable->type && iterable->type->kind == HIRTypeKind::Class &&
        (iterable->type->className == "Generator" || iterable->type->className == "AsyncGenerator");
    // Object-typed iterables (e.g., Map.keys() returns an iterator object) also use .next()
    bool isIteratorObject = !isGenerator && iterable->type &&
        iterable->type->kind == HIRTypeKind::Object;

    // Detect iterator-returning method calls: map.keys(), map.values(), map.entries(),
    // arr.entries(), arr.keys(), arr.values() - these return iterator objects with .next()
    // even though the TypeScript type analyzer may report them as Any before MethodResolutionPass
    if (!isGenerator && !isIteratorObject) {
        if (auto* callExpr = dynamic_cast<ast::CallExpression*>(iterExpr)) {
            if (auto* propAccess = dynamic_cast<ast::PropertyAccessExpression*>(callExpr->callee.get())) {
                const auto& methodName = propAccess->name;
                if (methodName == "keys" || methodName == "values" || methodName == "entries") {
                    // Check if the object is a Map, Set, or Array by looking up its variable type
                    if (auto* objIdent = dynamic_cast<ast::Identifier*>(propAccess->expression.get())) {
                        auto* varInfo = lookupVariableInfo(objIdent->name);
                        if (varInfo) {
                            // For alloca variables, elemType holds the actual type;
                            // for direct variables, value->type holds the type
                            auto varTypePtr = varInfo->isAlloca ? varInfo->elemType :
                                (varInfo->value ? varInfo->value->type : nullptr);
                            if (varTypePtr) {
                                auto kind = varTypePtr->kind;
                                if (kind == HIRTypeKind::Map || kind == HIRTypeKind::Set ||
                                    kind == HIRTypeKind::Array) {
                                    isIteratorObject = true;
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    isGenerator = isGenerator || isIteratorObject;

    if (isGenerator) {
        // Generator iteration: call .next() in a loop, check .done, get .value
        // Store result in an alloca so we can access it in both cond and body blocks
        auto resultAlloca = builder_.createAlloca(HIRType::makeObject(), "forof.result");

        builder_.createBranch(condBlock);

        // Condition: call gen.next(), check if result.done is true
        builder_.setInsertPoint(condBlock);
        currentBlock_ = condBlock;
        auto nextResult = builder_.createCallMethod(iterable, "next", {}, HIRType::makeObject());
        builder_.createStore(nextResult, resultAlloca);
        auto doneVal = builder_.createGetPropStatic(nextResult, "done", HIRType::makeAny());
        // condBranch handles boxed value -> bool conversion via ts_value_to_bool
        builder_.createCondBranch(doneVal, endBlock, bodyBlock);

        // Body: get value and execute body
        builder_.setInsertPoint(bodyBlock);
        currentBlock_ = bodyBlock;
        auto resultVal = builder_.createLoad(HIRType::makeObject(), resultAlloca);
        auto elemVal = builder_.createGetPropStatic(resultVal, "value", HIRType::makeAny());

        // Bind to loop variable (supports simple, array destructuring, object destructuring)
        if (node->initializer) {
            auto* varDecl = dynamic_cast<ast::VariableDeclaration*>(node->initializer.get());
            if (varDecl) {
                if (auto* ident = dynamic_cast<ast::Identifier*>(varDecl->name.get())) {
                    defineVariable(ident->name, elemVal);
                } else if (auto* arrPat = dynamic_cast<ast::ArrayBindingPattern*>(varDecl->name.get())) {
                    lowerArrayBindingPattern(arrPat, elemVal);
                } else if (auto* objPat = dynamic_cast<ast::ObjectBindingPattern*>(varDecl->name.get())) {
                    lowerObjectBindingPattern(objPat, elemVal);
                }
            }
        }

        lowerStatement(node->body.get());

        // Branch to update (if not already terminated)
        emitBranchIfNeeded(updateBlock);

        // Update block: just jump back to cond (next call happens there)
        builder_.setInsertPoint(updateBlock);
        currentBlock_ = updateBlock;
        builder_.createBranch(condBlock);
    } else {
        // Array iteration: use index-based access
        auto lenVal = builder_.createArrayLength(iterable);

        // Create index variable (alloca for SSA)
        auto indexAlloca = builder_.createAlloca(HIRType::makeInt64(), "forof.idx");
        auto zero = builder_.createConstInt(0);
        builder_.createStore(zero, indexAlloca);

        builder_.createBranch(condBlock);

        // Condition: index < length
        builder_.setInsertPoint(condBlock);
        currentBlock_ = condBlock;
        auto indexVal = builder_.createLoad(HIRType::makeInt64(), indexAlloca);
        auto cond = builder_.createCmpLtI64(indexVal, lenVal);
        builder_.createCondBranch(cond, bodyBlock, endBlock);

        // Body: get element and execute body
        builder_.setInsertPoint(bodyBlock);
        currentBlock_ = bodyBlock;

        // Get current element
        auto currentIndex = builder_.createLoad(HIRType::makeInt64(), indexAlloca);
        auto elemVal = builder_.createGetElem(iterable, currentIndex);

        // Bind to loop variable (supports simple, array destructuring, object destructuring)
        if (node->initializer) {
            auto* varDecl = dynamic_cast<ast::VariableDeclaration*>(node->initializer.get());
            if (varDecl) {
                if (auto* ident = dynamic_cast<ast::Identifier*>(varDecl->name.get())) {
                    defineVariable(ident->name, elemVal);
                } else if (auto* arrPat = dynamic_cast<ast::ArrayBindingPattern*>(varDecl->name.get())) {
                    lowerArrayBindingPattern(arrPat, elemVal);
                } else if (auto* objPat = dynamic_cast<ast::ObjectBindingPattern*>(varDecl->name.get())) {
                    lowerObjectBindingPattern(objPat, elemVal);
                }
            }
        }

        lowerStatement(node->body.get());

        // Branch to update (if not already terminated)
        emitBranchIfNeeded(updateBlock);

        // Update block: increment index
        builder_.setInsertPoint(updateBlock);
        currentBlock_ = updateBlock;
        auto idxForInc = builder_.createLoad(HIRType::makeInt64(), indexAlloca);
        auto one = builder_.createConstInt(1);
        auto newIndex = builder_.createAddI64(idxForInc, one);
        builder_.createStore(newIndex, indexAlloca);
        builder_.createBranch(condBlock);
    }

    loopStack_.pop();
    breakTargetStack_.pop();
    if (!myLabel.empty()) {
        labeledLoops_.erase(myLabel);
    }
    popScope();

    builder_.setInsertPoint(endBlock);
    currentBlock_ = endBlock;
}

void ASTToHIR::visitForInStatement(ast::ForInStatement* node) {
    setSourceLine(node);
    // For-in loop: iterate over object keys
    // Implementation: Get Object.keys(obj), then iterate over the array
    auto* condBlock = createBlock("forin.cond");
    auto* bodyBlock = createBlock("forin.body");
    auto* updateBlock = createBlock("forin.update");
    auto* endBlock = createBlock("forin.end");

    // Push loop context (continue -> update, break -> end)
    LoopContext ctx = {updateBlock, endBlock};
    loopStack_.push(ctx);
    breakTargetStack_.push(endBlock);

    // Register with label if this loop is labeled
    std::string myLabel;
    if (!pendingLabel_.empty()) {
        myLabel = pendingLabel_;
        labeledLoops_[myLabel] = ctx;
        pendingLabel_.clear();  // Clear so nested loops don't also register
    }

    pushScope();

    // Get the object to iterate
    auto* objExpr = dynamic_cast<ast::Expression*>(node->expression.get());
    auto obj = objExpr ? lowerExpression(objExpr) : createValue(HIRType::makeObject());

    // Get keys array: Object.keys(obj)
    auto keys = builder_.createCall("ts_object_keys", {obj}, HIRType::makeArray(HIRType::makeString()));

    // Get array length
    auto length = builder_.createArrayLength(keys);

    // Create index variable (alloca for SSA)
    auto indexAlloca = builder_.createAlloca(HIRType::makeInt64(), "forin.idx");
    auto zero = builder_.createConstInt(0);
    builder_.createStore(zero, indexAlloca);

    // Branch to condition
    builder_.createBranch(condBlock);

    // Condition block: check index < length
    builder_.setInsertPoint(condBlock);
    currentBlock_ = condBlock;
    auto indexVal = builder_.createLoad(HIRType::makeInt64(), indexAlloca);
    auto cond = builder_.createCmpLtI64(indexVal, length);
    builder_.createCondBranch(cond, bodyBlock, endBlock);

    // Body block
    builder_.setInsertPoint(bodyBlock);
    currentBlock_ = bodyBlock;

    // Get current key: keys[index]
    auto currentIndex = builder_.createLoad(HIRType::makeInt64(), indexAlloca);
    auto key = builder_.createGetElem(keys, currentIndex);

    // Bind to loop variable
    if (node->initializer) {
        auto* varDecl = dynamic_cast<ast::VariableDeclaration*>(node->initializer.get());
        if (varDecl) {
            auto* ident = dynamic_cast<ast::Identifier*>(varDecl->name.get());
            if (ident) {
                defineVariable(ident->name, key);
            }
        }
    }

    // Lower body
    lowerStatement(node->body.get());

    // Branch to update (if not already terminated)
    emitBranchIfNeeded(updateBlock);

    // Update block: increment index
    builder_.setInsertPoint(updateBlock);
    currentBlock_ = updateBlock;
    auto idxForInc = builder_.createLoad(HIRType::makeInt64(), indexAlloca);
    auto one = builder_.createConstInt(1);
    auto newIndex = builder_.createAddI64(idxForInc, one);
    builder_.createStore(newIndex, indexAlloca);
    builder_.createBranch(condBlock);

    loopStack_.pop();
    breakTargetStack_.pop();
    if (!myLabel.empty()) {
        labeledLoops_.erase(myLabel);
    }
    popScope();

    builder_.setInsertPoint(endBlock);
    currentBlock_ = endBlock;
}

void ASTToHIR::visitBreakStatement(ast::BreakStatement* node) {
    setSourceLine(node);
    for (int i = 0; i < tryDepth_; i++) {
        builder_.createPopHandler();
    }
    if (!node->label.empty()) {
        auto it = labeledLoops_.find(node->label);
        if (it != labeledLoops_.end()) {
            builder_.createBranch(it->second.breakTarget);
        }
    } else if (!breakTargetStack_.empty()) {
        builder_.createBranch(breakTargetStack_.top());
    }
}

void ASTToHIR::visitContinueStatement(ast::ContinueStatement* node) {
    setSourceLine(node);
    for (int i = 0; i < tryDepth_; i++) {
        builder_.createPopHandler();
    }
    if (!node->label.empty()) {
        auto it = labeledLoops_.find(node->label);
        if (it != labeledLoops_.end()) {
            builder_.createBranch(it->second.continueTarget);
        }
    } else if (!loopStack_.empty()) {
        builder_.createBranch(loopStack_.top().continueTarget);
    }
}

void ASTToHIR::visitLabeledStatement(ast::LabeledStatement* node) {
    setSourceLine(node);
    // Set the pending label - the next loop will register itself with this label
    std::string savedLabel = pendingLabel_;
    pendingLabel_ = node->label;

    // Lower the statement (the loop will pick up pendingLabel_)
    lowerStatement(node->statement.get());

    // Clean up the label registration (in case the loop registered it)
    labeledLoops_.erase(node->label);

    // Restore any outer pending label
    pendingLabel_ = savedLabel;
}

void ASTToHIR::visitSwitchStatement(ast::SwitchStatement* node) {
    setSourceLine(node);
    auto switchVal = lowerExpression(node->expression.get());

    auto* endBlock = createBlock("switch.end");
    switchStack_.push({endBlock, {}, nullptr});
    breakTargetStack_.push(endBlock);

    std::vector<HIRBlock*> caseBlocks;
    HIRBlock* defaultBlock = endBlock;

    // Create blocks for each case
    for (auto& clause : node->clauses) {
        auto* caseClause = dynamic_cast<ast::CaseClause*>(clause.get());
        auto* defaultClause = dynamic_cast<ast::DefaultClause*>(clause.get());

        if (caseClause) {
            auto* caseBlock = createBlock("switch.case");
            caseBlocks.push_back(caseBlock);
        } else if (defaultClause) {
            defaultBlock = createBlock("switch.default");
            caseBlocks.push_back(defaultBlock);
        }
    }

    // Detect if any case uses string literals
    bool hasStringCases = false;
    for (auto& clause : node->clauses) {
        auto* caseClause = dynamic_cast<ast::CaseClause*>(clause.get());
        if (caseClause && caseClause->expression) {
            if (dynamic_cast<ast::StringLiteral*>(caseClause->expression.get())) {
                hasStringCases = true;
                break;
            }
        }
    }

    if (hasStringCases) {
        // Lower string switch to if-else chain with string comparisons.
        // switchVal may be boxed (any/TsValue*) from get_prop.static.
        // HIRToLLVM's ts_string_eq handler already extracts raw TsString*
        // from boxed values via ts_value_get_string, so this is safe.
        size_t blockIdx = 0;
        for (auto& clause : node->clauses) {
            auto* caseClause = dynamic_cast<ast::CaseClause*>(clause.get());

            if (caseClause && caseClause->expression && blockIdx < caseBlocks.size()) {
                auto* strLit = dynamic_cast<ast::StringLiteral*>(caseClause->expression.get());
                if (strLit) {
                    auto caseStr = builder_.createConstString(strLit->value);
                    auto cmpResult = builder_.createCall("ts_string_eq",
                        {switchVal, caseStr}, HIRType::makeBool());

                    // Determine the "next check" block
                    HIRBlock* nextCheckBlock = nullptr;
                    for (size_t j = blockIdx + 1; j < node->clauses.size(); ++j) {
                        auto* nextCase = dynamic_cast<ast::CaseClause*>(node->clauses[j].get());
                        if (nextCase && nextCase->expression) {
                            nextCheckBlock = createBlock("switch.check");
                            break;
                        }
                    }
                    if (!nextCheckBlock) nextCheckBlock = defaultBlock;

                    builder_.createCondBranch(cmpResult, caseBlocks[blockIdx], nextCheckBlock);

                    // Continue emitting checks from the next check block
                    builder_.setInsertPoint(nextCheckBlock);
                    currentBlock_ = nextCheckBlock;
                }
            }
            blockIdx++;
        }

        // If we're in a check block (not the default block itself) without
        // a terminator, branch to default. The last condbr's false branch
        // already points to defaultBlock, so this only applies if some case
        // had a non-string expression and was skipped.
        if (!hasTerminator() && currentBlock_ != defaultBlock) {
            builder_.createBranch(defaultBlock);
        }
    } else {
        // Build integer switch cases
        std::vector<std::pair<int64_t, HIRBlock*>> cases;
        size_t blockIdx = 0;
        for (auto& clause : node->clauses) {
            auto* caseClause = dynamic_cast<ast::CaseClause*>(clause.get());
            if (caseClause && caseClause->expression) {
                // Try to get constant value
                auto* numLit = dynamic_cast<ast::NumericLiteral*>(caseClause->expression.get());
                if (numLit && blockIdx < caseBlocks.size()) {
                    cases.push_back({static_cast<int64_t>(numLit->value), caseBlocks[blockIdx]});
                }
            }
            blockIdx++;
        }

        builder_.createSwitch(switchVal, defaultBlock, cases);
    }

    // Generate code for each case
    size_t blockIdx = 0;
    for (auto& clause : node->clauses) {
        auto* caseClause = dynamic_cast<ast::CaseClause*>(clause.get());
        auto* defaultClause = dynamic_cast<ast::DefaultClause*>(clause.get());

        HIRBlock* block = (blockIdx < caseBlocks.size()) ? caseBlocks[blockIdx] : nullptr;
        if (!block) continue;

        builder_.setInsertPoint(block);
        currentBlock_ = block;

        if (caseClause) {
            for (auto& stmt : caseClause->statements) {
                lowerStatement(stmt.get());
            }
        } else if (defaultClause) {
            for (auto& stmt : defaultClause->statements) {
                lowerStatement(stmt.get());
            }
        }

        // Fall through to next case or end
        if (!hasTerminator()) {
            if (blockIdx + 1 < caseBlocks.size()) {
                builder_.createBranch(caseBlocks[blockIdx + 1]);
            } else {
                builder_.createBranch(endBlock);
            }
        }

        blockIdx++;
    }

    switchStack_.pop();
    breakTargetStack_.pop();

    builder_.setInsertPoint(endBlock);
    currentBlock_ = endBlock;
}

void ASTToHIR::visitTryStatement(ast::TryStatement* node) {
    setSourceLine(node);
    // Create basic blocks for exception handling control flow
    // Use createBlock (with unique numbering) to handle nested try statements
    auto tryBB = createBlock("try");
    auto catchBB = node->catchClause ? createBlock("catch") : nullptr;
    auto finallyBB = !node->finallyBlock.empty() ? createBlock("finally") : nullptr;
    auto mergeBB = createBlock("try.merge");

    // When there's finally but no catch, we need an intermediate block to store the exception
    HIRBlock* exceptionStoreBB = nullptr;
    if (finallyBB && !catchBB) {
        exceptionStoreBB = createBlock("try.store_exception");
    }

    // Determine where to go after try/catch
    HIRBlock* afterTryDest = finallyBB ? finallyBB : mergeBB;
    HIRBlock* afterCatchDest = finallyBB ? finallyBB : mergeBB;

    // Determine where to go on exception
    HIRBlock* exceptionDest = catchBB ? catchBB : (exceptionStoreBB ? exceptionStoreBB : afterTryDest);

    // Create alloca for pending exception (for finally rethrow)
    std::shared_ptr<HIRValue> pendingExc = nullptr;
    if (finallyBB) {
        pendingExc = builder_.createAlloca(HIRType::makeAny());
        builder_.createStore(builder_.createConstNull(), pendingExc);
    }

    // Setup try: push handler and call setjmp
    // Returns true if we're coming from an exception, false on normal entry
    auto isException = builder_.createSetupTry(exceptionDest);
    builder_.createCondBranch(isException, exceptionDest, tryBB);

    // --- Try Block ---
    builder_.setInsertPoint(tryBB);
    currentBlock_ = tryBB;

    tryDepth_++;
    for (auto& stmt : node->tryBlock) {
        if (hasTerminator()) break;  // Stop if block already terminated (e.g., by throw)
        lowerStatement(stmt.get());
    }
    tryDepth_--;

    // Pop exception handler and branch to finally/merge
    bool tryReachedMerge = false;
    if (currentBlock_->getTerminator() == nullptr) {
        builder_.createPopHandler();
        builder_.createBranch(afterTryDest);
        tryReachedMerge = true;
    }

    // --- Catch Block ---
    bool catchReachedMerge = false;
    if (catchBB && node->catchClause) {
        builder_.setInsertPoint(catchBB);
        currentBlock_ = catchBB;

        // Get and clear the exception
        auto exception = builder_.createGetException();
        builder_.createClearException();

        // Bind exception to catch variable if present
        if (node->catchClause->variable) {
            // The variable could be an Identifier or a binding pattern
            if (auto* id = dynamic_cast<ast::Identifier*>(node->catchClause->variable.get())) {
                defineVariable(id->name, exception);
            } else if (auto* objPat = dynamic_cast<ast::ObjectBindingPattern*>(node->catchClause->variable.get())) {
                lowerObjectBindingPattern(objPat, exception);
            } else if (auto* arrPat = dynamic_cast<ast::ArrayBindingPattern*>(node->catchClause->variable.get())) {
                lowerArrayBindingPattern(arrPat, exception);
            }
        }

        // Execute catch block statements
        for (auto& stmt : node->catchClause->block) {
            if (hasTerminator()) break;  // Stop if block already terminated
            lowerStatement(stmt.get());
        }

        // Branch to finally/merge
        if (currentBlock_->getTerminator() == nullptr) {
            builder_.createBranch(afterCatchDest);
            catchReachedMerge = true;
        }
    }

    // --- Exception Store Block (for try-finally without catch) ---
    if (exceptionStoreBB) {
        builder_.setInsertPoint(exceptionStoreBB);
        currentBlock_ = exceptionStoreBB;

        // Get the exception and store it for later rethrow
        auto exception = builder_.createGetException();
        builder_.createStore(exception, pendingExc);
        builder_.createBranch(finallyBB);
    }

    // --- Finally Block ---
    if (finallyBB) {
        builder_.setInsertPoint(finallyBB);
        currentBlock_ = finallyBB;

        // Execute finally block statements
        for (auto& stmt : node->finallyBlock) {
            if (hasTerminator()) break;  // Stop if block already terminated
            lowerStatement(stmt.get());
        }

        // Check for pending exception to rethrow
        if (currentBlock_->getTerminator() == nullptr) {
            if (pendingExc) {
                auto exc = builder_.createLoad(HIRType::makeAny(), pendingExc);
                auto isNull = builder_.createCmpEqPtr(exc, builder_.createConstNull());

                auto rethrowBB = createBlock("try.rethrow");
                builder_.createCondBranch(isNull, mergeBB, rethrowBB);

                builder_.setInsertPoint(rethrowBB);
                currentBlock_ = rethrowBB;
                builder_.createThrow(exc);
            } else {
                builder_.createBranch(mergeBB);
            }
        }
    }

    // --- Merge Block ---
    builder_.setInsertPoint(mergeBB);
    currentBlock_ = mergeBB;

    // If both try and catch terminated early (return/throw/break), no branches
    // reach the merge block. Emit a dummy return so LLVM has a valid terminator
    // (using unreachable here can cause SimplifyCFG to propagate traps into
    // reachable code paths in some edge cases).
    bool finallyReachedMerge = (finallyBB != nullptr);
    if (!tryReachedMerge && !catchReachedMerge && !finallyReachedMerge) {
        builder_.createReturn(builder_.createConstUndefined());
    }
}

void ASTToHIR::visitThrowStatement(ast::ThrowStatement* node) {
    setSourceLine(node);
    // Lower the throw expression
    std::shared_ptr<HIRValue> exception;
    if (node->expression) {
        exception = lowerExpression(node->expression.get());
        // Box the value if needed (throw can accept any value)
        if (exception->type && exception->type->kind != HIRTypeKind::Any) {
            exception = builder_.createBoxObject(exception);
        }
    } else {
        // throw; without expression - rethrow current exception
        exception = builder_.createGetException();
    }

    builder_.createThrow(exception);
}

void ASTToHIR::visitImportDeclaration(ast::ImportDeclaration* node) {
    setSourceLine(node);
    // Type-only imports are erased entirely - no runtime effect
    if (node->isTypeOnly) return;

    // Track named imports from extension modules so we can route their calls
    // through the extension registry instead of treating them as user functions.
    // E.g., `import { join } from 'path'` -> extensionImports_["join"] = {"path", "join"}
    auto& registry = ext::ExtensionRegistry::instance();
    std::string modSpec = node->moduleSpecifier;
    // Strip "node:" prefix
    if (modSpec.size() > 5 && modSpec.substr(0, 5) == "node:") {
        modSpec = modSpec.substr(5);
    }

    if (registry.isRegisteredModule(modSpec) || registry.isRegisteredObject(modSpec)) {
        for (const auto& spec : node->namedImports) {
            // Skip per-specifier type-only imports: import { type Foo, bar } from '...'
            if (spec.isTypeOnly) continue;
            std::string exportedName = spec.propertyName.empty() ? spec.name : spec.propertyName;
            extensionImports_[spec.name] = { modSpec, exportedName };
        }
        if (!node->defaultImport.empty()) {
            extensionImports_[node->defaultImport] = { modSpec, "default" };
        }
    }
}

void ASTToHIR::visitExportDeclaration(ast::ExportDeclaration* node) {
    setSourceLine(node);
    // Exports are handled at module resolution time
    // ExportDeclaration has moduleSpecifier, namedExports, isStarExport, namespaceExport
    // but no direct declaration - nothing to lower at HIR level
}

void ASTToHIR::visitExportAssignment(ast::ExportAssignment* node) {
    setSourceLine(node);
    // Lower the expression
    if (node->expression) {
        lowerExpression(node->expression.get());
    }
}

void ASTToHIR::visitNamespaceDeclaration(ast::NamespaceDeclaration* node) {
    setSourceLine(node);
    // Namespaces have no runtime code — type-only construct
}

void ASTToHIR::visitImportEqualsDeclaration(ast::ImportEqualsDeclaration* node) {
    setSourceLine(node);
    // Import equals has no runtime code — resolved at analysis time
}

//==============================================================================
// Expression Lowering
//==============================================================================

void ASTToHIR::visitBinaryExpression(ast::BinaryExpression* node) {
    setSourceLine(node);
    const std::string& op = node->op;

    // Handle nullish coalescing with short-circuit semantics
    if (op == "??") {
        // Lower left side first
        auto lhs = lowerExpression(node->left.get());

        // Box lhs to Any if needed (for consistent phi node type)
        auto boxedLhs = boxValueIfNeeded(lhs);

        // Check if lhs is nullish
        auto isNullish = builder_.createCall("ts_value_is_nullish", {boxedLhs}, HIRType::makeBool());

        // Create unique block names
        int blockId = blockCounter_++;
        auto* rhsBlock = builder_.createBlock("nullish_rhs_" + std::to_string(blockId));
        auto* mergeBlock = builder_.createBlock("nullish_merge_" + std::to_string(blockId));

        auto* lhsBlock = builder_.getInsertBlock();

        // If nullish, evaluate rhs; otherwise use lhs
        builder_.createCondBranch(isNullish, rhsBlock, mergeBlock);

        // Evaluate rhs
        builder_.setInsertPoint(rhsBlock);
        currentBlock_ = rhsBlock;  // Keep ASTToHIR's currentBlock_ in sync
        auto rhs = lowerExpression(node->right.get());
        // Box rhs to Any if needed (for consistent phi node type)
        auto boxedRhs = boxValueIfNeeded(rhs);
        auto* finalRhsBlock = builder_.getInsertBlock();
        builder_.createBranch(mergeBlock);

        // Merge with phi node - both values should now be Any/ptr type
        builder_.setInsertPoint(mergeBlock);
        currentBlock_ = mergeBlock;  // Keep ASTToHIR's currentBlock_ in sync
        std::vector<std::pair<std::shared_ptr<HIRValue>, HIRBlock*>> phiIncoming;
        phiIncoming.push_back(std::make_pair(boxedLhs, lhsBlock));
        phiIncoming.push_back(std::make_pair(boxedRhs, finalRhsBlock));
        lastValue_ = builder_.createPhi(HIRType::makeAny(), phiIncoming);
        return;
    }

    // Handle instanceof operator - need to handle rhs specially as class reference
    if (op == "instanceof") {
        auto lhs = lowerExpression(node->left.get());

        // rhs should be a class identifier - don't evaluate it as an expression
        auto* ident = dynamic_cast<ast::Identifier*>(node->right.get());
        if (ident) {
            // Check for built-in types like Array
            if (ident->name == "Array") {
                // Arrays have a specific check using ts_array_is_array
                lastValue_ = builder_.createCall("ts_array_is_array", {lhs}, HIRType::makeBool());
                return;
            }

            // Check if identifier refers to a compiler-known class with a vtable
            bool isKnownClass = false;
            if (module_) {
                for (auto& shape : module_->shapes) {
                    if (shape->className == ident->name) {
                        isKnownClass = true;
                        break;
                    }
                }
            }
            if (isKnownClass) {
                // Known class: use fast vtable comparison
                std::string vtableGlobalName = ident->name + "_VTable_Global";
                auto vtablePtr = builder_.createLoadGlobal(vtableGlobalName);
                lastValue_ = builder_.createInstanceOf(lhs, vtablePtr);
            } else {
                // Unknown class (dynamic constructor, e.g., from require()):
                // use JS-spec prototype-chain instanceof
                auto rhs = lowerExpression(node->right.get());
                auto boxedRhs = boxValueIfNeeded(rhs);
                auto boxedLhs = boxValueIfNeeded(lhs);
                lastValue_ = builder_.createCall("ts_instanceof_dynamic",
                    {boxedLhs, boxedRhs}, HIRType::makeBool());
            }
        } else {
            // RHS is an expression (not a simple identifier) - use dynamic instanceof
            auto rhs = lowerExpression(node->right.get());
            auto boxedRhs = boxValueIfNeeded(rhs);
            auto boxedLhs = boxValueIfNeeded(lhs);
            lastValue_ = builder_.createCall("ts_instanceof_dynamic",
                {boxedLhs, boxedRhs}, HIRType::makeBool());
        }
        return;
    }

    // Handle 'in' operator - check if property exists in object
    if (op == "in") {
        auto lhs = lowerExpression(node->left.get());  // property key
        auto rhs = lowerExpression(node->right.get()); // object
        lastValue_ = builder_.createCall("ts_object_has_property", {rhs, lhs}, HIRType::makeBool());
        return;
    }

    // Handle logical AND with short-circuit semantics
    // Must be before general lhs/rhs evaluation to avoid eagerly evaluating RHS
    if (op == "&&") {
        auto lhs = lowerExpression(node->left.get());
        auto boxedLhs = boxValueIfNeeded(lhs);

        // Convert LHS to boolean for branching
        std::shared_ptr<HIRValue> lhsCond;
        if (lhs->type && lhs->type->kind == HIRTypeKind::Bool) {
            lhsCond = lhs;
        } else {
            lhsCond = builder_.createCall("ts_value_to_bool", {boxedLhs}, HIRType::makeBool());
        }

        int blockId = blockCounter_++;
        auto* rhsBlock = builder_.createBlock("land_rhs_" + std::to_string(blockId));
        auto* mergeBlock = builder_.createBlock("land_merge_" + std::to_string(blockId));
        auto* lhsBlock = builder_.getInsertBlock();

        // && short-circuit: if truthy → eval RHS, if falsy → skip to merge with LHS
        builder_.createCondBranch(lhsCond, rhsBlock, mergeBlock);

        // RHS block
        builder_.setInsertPoint(rhsBlock);
        currentBlock_ = rhsBlock;  // Keep ASTToHIR's currentBlock_ in sync
        auto rhs = lowerExpression(node->right.get());
        auto boxedRhs = boxValueIfNeeded(rhs);
        auto* finalRhsBlock = builder_.getInsertBlock();
        builder_.createBranch(mergeBlock);

        // Move mergeBlock to end of block list so it's lowered AFTER any blocks
        // created during RHS evaluation (e.g., nested || creates lor_rhs/lor_merge
        // blocks that must be lowered before the merge block's phi can see them
        // as predecessors).
        {
            auto& blocks = currentFunction_->blocks;
            auto it = std::find_if(blocks.begin(), blocks.end(),
                [mergeBlock](const auto& b) { return b.get() == mergeBlock; });
            if (it != blocks.end() && std::next(it) != blocks.end()) {
                std::rotate(it, std::next(it), blocks.end());
            }
        }

        // Merge with phi
        builder_.setInsertPoint(mergeBlock);
        currentBlock_ = mergeBlock;  // Keep ASTToHIR's currentBlock_ in sync
        std::vector<std::pair<std::shared_ptr<HIRValue>, HIRBlock*>> phiIncoming;
        phiIncoming.push_back(std::make_pair(boxedLhs, lhsBlock));
        phiIncoming.push_back(std::make_pair(boxedRhs, finalRhsBlock));
        lastValue_ = builder_.createPhi(HIRType::makeAny(), phiIncoming);
        return;
    }

    // Handle logical OR with short-circuit semantics
    if (op == "||") {
        auto lhs = lowerExpression(node->left.get());
        auto boxedLhs = boxValueIfNeeded(lhs);

        // Convert LHS to boolean for branching
        std::shared_ptr<HIRValue> lhsCond;
        if (lhs->type && lhs->type->kind == HIRTypeKind::Bool) {
            lhsCond = lhs;
        } else {
            lhsCond = builder_.createCall("ts_value_to_bool", {boxedLhs}, HIRType::makeBool());
        }

        int blockId = blockCounter_++;
        auto* rhsBlock = builder_.createBlock("lor_rhs_" + std::to_string(blockId));
        auto* mergeBlock = builder_.createBlock("lor_merge_" + std::to_string(blockId));
        auto* lhsBlock = builder_.getInsertBlock();

        // || short-circuit: if truthy → skip to merge with LHS, if falsy → eval RHS
        builder_.createCondBranch(lhsCond, mergeBlock, rhsBlock);

        // RHS block
        builder_.setInsertPoint(rhsBlock);
        currentBlock_ = rhsBlock;  // Keep ASTToHIR's currentBlock_ in sync
        auto rhs = lowerExpression(node->right.get());
        auto boxedRhs = boxValueIfNeeded(rhs);
        auto* finalRhsBlock = builder_.getInsertBlock();
        builder_.createBranch(mergeBlock);

        // Move mergeBlock to end of block list (same reason as && above)
        {
            auto& blocks = currentFunction_->blocks;
            auto it = std::find_if(blocks.begin(), blocks.end(),
                [mergeBlock](const auto& b) { return b.get() == mergeBlock; });
            if (it != blocks.end() && std::next(it) != blocks.end()) {
                std::rotate(it, std::next(it), blocks.end());
            }
        }

        // Merge with phi
        builder_.setInsertPoint(mergeBlock);
        currentBlock_ = mergeBlock;  // Keep ASTToHIR's currentBlock_ in sync
        std::vector<std::pair<std::shared_ptr<HIRValue>, HIRBlock*>> phiIncoming;
        phiIncoming.push_back(std::make_pair(boxedLhs, lhsBlock));
        phiIncoming.push_back(std::make_pair(boxedRhs, finalRhsBlock));
        lastValue_ = builder_.createPhi(HIRType::makeAny(), phiIncoming);
        return;
    }

    auto lhs = lowerExpression(node->left.get());
    auto rhs = lowerExpression(node->right.get());

    // Helper to check if an operand is a string type
    auto isString = [](const std::shared_ptr<HIRValue>& val, ast::Expression* astNode) {
        if (val && val->type && val->type->kind == HIRTypeKind::String) return true;
        if (astNode && astNode->inferredType && astNode->inferredType->kind == ts::TypeKind::String) return true;
        return false;
    };

    // Helper to check if an operand is Float64 type
    auto isFloat64 = [](const std::shared_ptr<HIRValue>& val, ast::Expression* astNode) {
        if (val && val->type && val->type->kind == HIRTypeKind::Float64) return true;
        if (astNode && astNode->inferredType && astNode->inferredType->kind == ts::TypeKind::Double) return true;
        return false;
    };

    // Helper to check if an operand is BigInt type
    auto isBigInt = [](ast::Expression* astNode) {
        return astNode && astNode->inferredType &&
               astNode->inferredType->kind == ts::TypeKind::BigInt;
    };

    // Helper to check if an operand is a number type (Int or Double)
    auto isNumber = [&isFloat64](const std::shared_ptr<HIRValue>& val, ast::Expression* astNode) {
        // Check HIR type
        if (val && val->type) {
            if (val->type->kind == HIRTypeKind::Int64 ||
                val->type->kind == HIRTypeKind::Float64) return true;
        }
        // Check AST inferred type
        if (astNode && astNode->inferredType) {
            if (astNode->inferredType->kind == ts::TypeKind::Int ||
                astNode->inferredType->kind == ts::TypeKind::Double) return true;
        }
        return false;
    };

    // Helper to check if an operand is a boolean type
    auto isBoolean = [](const std::shared_ptr<HIRValue>& val, ast::Expression* astNode) {
        if (val && val->type && val->type->kind == HIRTypeKind::Bool) return true;
        if (astNode && astNode->inferredType && astNode->inferredType->kind == ts::TypeKind::Boolean) return true;
        return false;
    };

    // Helper to check if an operand could be Any, Null, or Undefined type
    // (requires runtime type checking for strict equality)
    auto isAnyOrNullish = [](const std::shared_ptr<HIRValue>& val, ast::Expression* astNode) {
        // Check HIR type
        if (val && val->type && val->type->kind == HIRTypeKind::Any) return true;
        // Check AST inferred type
        if (astNode && astNode->inferredType) {
            auto kind = astNode->inferredType->kind;
            if (kind == ts::TypeKind::Any || kind == ts::TypeKind::Undefined ||
                kind == ts::TypeKind::Null || kind == ts::TypeKind::Unknown) return true;
        }
        return false;
    };

    // Helper to check if an expression is the literal `undefined` keyword
    auto isUndefinedLiteral = [](ast::Expression* astNode) {
        if (auto* id = dynamic_cast<ast::Identifier*>(astNode)) {
            return id->name == "undefined";
        }
        return false;
    };

    // Helper to check if an expression is the literal `null` keyword
    auto isNullLiteral = [](ast::Expression* astNode) {
        if (auto* nullLit = dynamic_cast<ast::NullLiteral*>(astNode)) {
            return true;
        }
        if (auto* id = dynamic_cast<ast::Identifier*>(astNode)) {
            return id->name == "null";
        }
        return false;
    };

    // For strict equality (===), check if types are incompatible
    // Returns true if types are definitely different and === should return false
    auto typesIncompatibleForStrictEqual = [&isString, &isNumber, &isBoolean, &isBigInt](
            const std::shared_ptr<HIRValue>& lhsVal, ast::Expression* lhsAst,
            const std::shared_ptr<HIRValue>& rhsVal, ast::Expression* rhsAst) {
        bool lhsIsString = isString(lhsVal, lhsAst);
        bool rhsIsString = isString(rhsVal, rhsAst);
        bool lhsIsNumber = !lhsIsString && ((lhsVal && lhsVal->type &&
            (lhsVal->type->kind == HIRTypeKind::Int64 || lhsVal->type->kind == HIRTypeKind::Float64))
            || (lhsAst && lhsAst->inferredType &&
            (lhsAst->inferredType->kind == ts::TypeKind::Int ||
             lhsAst->inferredType->kind == ts::TypeKind::Double)));
        bool rhsIsNumber = !rhsIsString && ((rhsVal && rhsVal->type &&
            (rhsVal->type->kind == HIRTypeKind::Int64 || rhsVal->type->kind == HIRTypeKind::Float64))
            || (rhsAst && rhsAst->inferredType &&
            (rhsAst->inferredType->kind == ts::TypeKind::Int ||
             rhsAst->inferredType->kind == ts::TypeKind::Double)));
        bool lhsIsBoolean = isBoolean(lhsVal, lhsAst);
        bool rhsIsBoolean = isBoolean(rhsVal, rhsAst);
        bool lhsIsBigInt = isBigInt(lhsAst);
        bool rhsIsBigInt = isBigInt(rhsAst);

        // If both are the same type category, compatible
        if (lhsIsString && rhsIsString) return false;
        if (lhsIsNumber && rhsIsNumber) return false;
        if (lhsIsBoolean && rhsIsBoolean) return false;
        if (lhsIsBigInt && rhsIsBigInt) return false;

        // If one has a known type and the other has a different known type, incompatible
        if (lhsIsString && (rhsIsNumber || rhsIsBoolean || rhsIsBigInt)) return true;
        if (lhsIsNumber && (rhsIsString || rhsIsBoolean || rhsIsBigInt)) return true;
        if (lhsIsBoolean && (rhsIsString || rhsIsNumber || rhsIsBigInt)) return true;
        if (lhsIsBigInt && (rhsIsString || rhsIsNumber || rhsIsBoolean)) return true;

        // If types are unknown (Any), can't determine incompatibility at compile time
        return false;
    };

    // Determine if we should use Float64 operations (if either operand is Float64)
    bool useFloat = isFloat64(lhs, node->left.get()) || isFloat64(rhs, node->right.get());
    bool useBigInt = isBigInt(node->left.get()) || isBigInt(node->right.get());

    if (op == "+") {
        // Check if either operand is a string - if so, use string concatenation
        if (isString(lhs, node->left.get()) || isString(rhs, node->right.get())) {
            lastValue_ = builder_.createStringConcat(lhs, rhs);
        } else if (useBigInt) {
            // BigInt addition via runtime call
            lastValue_ = builder_.createCall("ts_bigint_add", {lhs, rhs}, HIRType::makeObject());
        } else if (isAnyOrNullish(lhs, node->left.get()) && isAnyOrNullish(rhs, node->right.get())) {
            // Dynamic dispatch for any-typed operands - runtime handles number vs string
            // Only when BOTH operands are any to avoid breaking concrete typed cases
            lastValue_ = builder_.createCall("ts_value_add", {lhs, rhs}, HIRType::makeAny());
        } else if (useFloat) {
            lastValue_ = builder_.createAddF64(lhs, rhs);
        } else {
            lastValue_ = builder_.createAddI64(lhs, rhs);
        }
    } else if (op == "-") {
        if (useBigInt) {
            lastValue_ = builder_.createCall("ts_bigint_sub", {lhs, rhs}, HIRType::makeObject());
        } else if (isAnyOrNullish(lhs, node->left.get()) || isAnyOrNullish(rhs, node->right.get())) {
            lastValue_ = builder_.createCall("ts_value_sub", {lhs, rhs}, HIRType::makeAny());
        } else {
            lastValue_ = useFloat ? builder_.createSubF64(lhs, rhs) : builder_.createSubI64(lhs, rhs);
        }
    } else if (op == "*") {
        if (useBigInt) {
            lastValue_ = builder_.createCall("ts_bigint_mul", {lhs, rhs}, HIRType::makeObject());
        } else if (isAnyOrNullish(lhs, node->left.get()) || isAnyOrNullish(rhs, node->right.get())) {
            lastValue_ = builder_.createCall("ts_value_mul", {lhs, rhs}, HIRType::makeAny());
        } else {
            lastValue_ = useFloat ? builder_.createMulF64(lhs, rhs) : builder_.createMulI64(lhs, rhs);
        }
    } else if (op == "/") {
        if (useBigInt) {
            lastValue_ = builder_.createCall("ts_bigint_div", {lhs, rhs}, HIRType::makeObject());
        } else if (isAnyOrNullish(lhs, node->left.get()) || isAnyOrNullish(rhs, node->right.get())) {
            lastValue_ = builder_.createCall("ts_value_div", {lhs, rhs}, HIRType::makeAny());
        } else {
            lastValue_ = useFloat ? builder_.createDivF64(lhs, rhs) : builder_.createDivI64(lhs, rhs);
        }
    } else if (op == "%") {
        if (useBigInt) {
            lastValue_ = builder_.createCall("ts_bigint_mod", {lhs, rhs}, HIRType::makeObject());
        } else if (isAnyOrNullish(lhs, node->left.get()) || isAnyOrNullish(rhs, node->right.get())) {
            lastValue_ = builder_.createCall("ts_value_mod", {lhs, rhs}, HIRType::makeAny());
        } else {
            lastValue_ = useFloat ? builder_.createModF64(lhs, rhs) : builder_.createModI64(lhs, rhs);
        }
    } else if (op == "<") {
        if (useBigInt) {
            lastValue_ = builder_.createCall("ts_bigint_lt", {lhs, rhs}, HIRType::makeBool());
        } else if (isAnyOrNullish(lhs, node->left.get()) || isAnyOrNullish(rhs, node->right.get())) {
            lastValue_ = builder_.createCall("ts_value_lt", {lhs, rhs}, HIRType::makeAny());
        } else {
            lastValue_ = useFloat ? builder_.createCmpLtF64(lhs, rhs) : builder_.createCmpLtI64(lhs, rhs);
        }
    } else if (op == "<=") {
        if (useBigInt) {
            lastValue_ = builder_.createCall("ts_bigint_le", {lhs, rhs}, HIRType::makeBool());
        } else if (isAnyOrNullish(lhs, node->left.get()) || isAnyOrNullish(rhs, node->right.get())) {
            lastValue_ = builder_.createCall("ts_value_lte", {lhs, rhs}, HIRType::makeAny());
        } else {
            lastValue_ = useFloat ? builder_.createCmpLeF64(lhs, rhs) : builder_.createCmpLeI64(lhs, rhs);
        }
    } else if (op == ">") {
        if (useBigInt) {
            lastValue_ = builder_.createCall("ts_bigint_gt", {lhs, rhs}, HIRType::makeBool());
        } else if (isAnyOrNullish(lhs, node->left.get()) || isAnyOrNullish(rhs, node->right.get())) {
            lastValue_ = builder_.createCall("ts_value_gt", {lhs, rhs}, HIRType::makeAny());
        } else {
            lastValue_ = useFloat ? builder_.createCmpGtF64(lhs, rhs) : builder_.createCmpGtI64(lhs, rhs);
        }
    } else if (op == ">=") {
        if (useBigInt) {
            lastValue_ = builder_.createCall("ts_bigint_ge", {lhs, rhs}, HIRType::makeBool());
        } else if (isAnyOrNullish(lhs, node->left.get()) || isAnyOrNullish(rhs, node->right.get())) {
            lastValue_ = builder_.createCall("ts_value_gte", {lhs, rhs}, HIRType::makeAny());
        } else {
            lastValue_ = useFloat ? builder_.createCmpGeF64(lhs, rhs) : builder_.createCmpGeI64(lhs, rhs);
        }
    } else if (op == "==") {
        // Loose equality - use coercing comparison
        if (useBigInt) {
            lastValue_ = builder_.createCall("ts_bigint_eq", {lhs, rhs}, HIRType::makeBool());
        } else if (isAnyOrNullish(lhs, node->left.get()) || isAnyOrNullish(rhs, node->right.get())) {
            lastValue_ = builder_.createCall("ts_value_eq", {lhs, rhs}, HIRType::makeAny());
        } else {
            lastValue_ = useFloat ? builder_.createCmpEqF64(lhs, rhs) : builder_.createCmpEqI64(lhs, rhs);
        }
    } else if (op == "===") {
        // Strict equality - if types are incompatible, return false directly
        if (typesIncompatibleForStrictEqual(lhs, node->left.get(), rhs, node->right.get())) {
            lastValue_ = builder_.createConstBool(false);
        } else if (useBigInt) {
            lastValue_ = builder_.createCall("ts_bigint_eq", {lhs, rhs}, HIRType::makeBool());
        } else if (isString(lhs, node->left.get()) && isString(rhs, node->right.get())) {
            // String comparison using ts_string_eq
            lastValue_ = builder_.createCall("ts_string_eq", {lhs, rhs}, HIRType::makeBool());
        } else if (isUndefinedLiteral(node->right.get()) && isAnyOrNullish(lhs, node->left.get())) {
            // x === undefined where x is Any type: use ts_value_is_undefined(x)
            // This correctly checks if a TsValue* has type == UNDEFINED
            lastValue_ = builder_.createCall("ts_value_is_undefined", {lhs}, HIRType::makeBool());
        } else if (isUndefinedLiteral(node->left.get()) && isAnyOrNullish(rhs, node->right.get())) {
            // undefined === x where x is Any type: use ts_value_is_undefined(x)
            lastValue_ = builder_.createCall("ts_value_is_undefined", {rhs}, HIRType::makeBool());
        } else if (isNullLiteral(node->right.get()) && isAnyOrNullish(lhs, node->left.get())) {
            // x === null where x is Any type: use ts_value_is_null(x)
            lastValue_ = builder_.createCall("ts_value_is_null", {lhs}, HIRType::makeBool());
        } else if (isNullLiteral(node->left.get()) && isAnyOrNullish(rhs, node->right.get())) {
            // null === x where x is Any type: use ts_value_is_null(x)
            lastValue_ = builder_.createCall("ts_value_is_null", {rhs}, HIRType::makeBool());
        } else if (isUndefinedLiteral(node->left.get()) && isUndefinedLiteral(node->right.get())) {
            // undefined === undefined is always true
            lastValue_ = builder_.createConstBool(true);
        } else if (isNullLiteral(node->left.get()) && isNullLiteral(node->right.get())) {
            // null === null is always true
            lastValue_ = builder_.createConstBool(true);
        } else if (isAnyOrNullish(lhs, node->left.get()) || isAnyOrNullish(rhs, node->right.get())) {
            // When either operand is Any type, use runtime strict equality which
            // checks types first (e.g., undefined === true must be false, not coerced)
            // Return boxed TsValue* to preserve ptr typing for variables that may be
            // reassigned later with non-boolean values (e.g., match = regex.exec(...))
            lastValue_ = builder_.createCall("ts_value_strict_eq", {lhs, rhs}, HIRType::makeAny());
        } else {
            lastValue_ = useFloat ? builder_.createCmpEqF64(lhs, rhs) : builder_.createCmpEqI64(lhs, rhs);
        }
    } else if (op == "!=") {
        // Loose inequality - use coercing comparison
        if (useBigInt) {
            lastValue_ = builder_.createCall("ts_bigint_ne", {lhs, rhs}, HIRType::makeBool());
        } else if (isAnyOrNullish(lhs, node->left.get()) || isAnyOrNullish(rhs, node->right.get())) {
            // Use ts_value_eq and negate for != with any operands
            auto eq = builder_.createCall("ts_value_eq", {lhs, rhs}, HIRType::makeAny());
            lastValue_ = builder_.createLogicalNot(eq);
        } else {
            lastValue_ = useFloat ? builder_.createCmpNeF64(lhs, rhs) : builder_.createCmpNeI64(lhs, rhs);
        }
    } else if (op == "!==") {
        // Strict inequality - if types are incompatible, return true directly
        if (typesIncompatibleForStrictEqual(lhs, node->left.get(), rhs, node->right.get())) {
            lastValue_ = builder_.createConstBool(true);
        } else if (useBigInt) {
            lastValue_ = builder_.createCall("ts_bigint_ne", {lhs, rhs}, HIRType::makeBool());
        } else if (isString(lhs, node->left.get()) && isString(rhs, node->right.get())) {
            // String comparison using ts_string_eq, then negate
            auto eq = builder_.createCall("ts_string_eq", {lhs, rhs}, HIRType::makeBool());
            lastValue_ = builder_.createLogicalNot(eq);
        } else if (isUndefinedLiteral(node->right.get()) && isAnyOrNullish(lhs, node->left.get())) {
            // x !== undefined where x is Any type: negate ts_value_is_undefined(x)
            auto eq = builder_.createCall("ts_value_is_undefined", {lhs}, HIRType::makeBool());
            lastValue_ = builder_.createLogicalNot(eq);
        } else if (isUndefinedLiteral(node->left.get()) && isAnyOrNullish(rhs, node->right.get())) {
            // undefined !== x where x is Any type: negate ts_value_is_undefined(x)
            auto eq = builder_.createCall("ts_value_is_undefined", {rhs}, HIRType::makeBool());
            lastValue_ = builder_.createLogicalNot(eq);
        } else if (isNullLiteral(node->right.get()) && isAnyOrNullish(lhs, node->left.get())) {
            // x !== null where x is Any type: negate ts_value_is_null(x)
            auto eq = builder_.createCall("ts_value_is_null", {lhs}, HIRType::makeBool());
            lastValue_ = builder_.createLogicalNot(eq);
        } else if (isNullLiteral(node->left.get()) && isAnyOrNullish(rhs, node->right.get())) {
            // null !== x where x is Any type: negate ts_value_is_null(x)
            auto eq = builder_.createCall("ts_value_is_null", {rhs}, HIRType::makeBool());
            lastValue_ = builder_.createLogicalNot(eq);
        } else if (isUndefinedLiteral(node->left.get()) && isUndefinedLiteral(node->right.get())) {
            // undefined !== undefined is always false
            lastValue_ = builder_.createConstBool(false);
        } else if (isNullLiteral(node->left.get()) && isNullLiteral(node->right.get())) {
            // null !== null is always false
            lastValue_ = builder_.createConstBool(false);
        } else if (isAnyOrNullish(lhs, node->left.get()) || isAnyOrNullish(rhs, node->right.get())) {
            // When either operand is Any type, use runtime strict equality and negate
            // Return boxed TsValue* to preserve ptr typing
            auto eq = builder_.createCall("ts_value_strict_eq", {lhs, rhs}, HIRType::makeAny());
            lastValue_ = builder_.createLogicalNot(eq);
        } else {
            lastValue_ = useFloat ? builder_.createCmpNeF64(lhs, rhs) : builder_.createCmpNeI64(lhs, rhs);
        }
    } else if (op == "&&") {
        lastValue_ = builder_.createLogicalAnd(lhs, rhs);
    } else if (op == "||") {
        lastValue_ = builder_.createLogicalOr(lhs, rhs);
    } else if (op == "&") {
        lastValue_ = builder_.createAndI64(lhs, rhs);
    } else if (op == "|") {
        lastValue_ = builder_.createOrI64(lhs, rhs);
    } else if (op == "^") {
        lastValue_ = builder_.createXorI64(lhs, rhs);
    } else if (op == "<<") {
        lastValue_ = builder_.createShlI64(lhs, rhs);
    } else if (op == ">>") {
        lastValue_ = builder_.createShrI64(lhs, rhs);
    } else if (op == ">>>") {
        lastValue_ = builder_.createUShrI64(lhs, rhs);
    } else if (op == ",") {
        // Comma operator: evaluate both sides for side effects, return right
        // lhs is already evaluated above, rhs is already evaluated above
        lastValue_ = rhs;
    } else if (op == "+=" || op == "-=" || op == "*=" || op == "/=" || op == "%=" ||
               op == "&=" || op == "|=" || op == "^=" || op == "<<=" || op == ">>=" || op == ">>>=") {
        // Compound assignment operators
        // lhs already contains the loaded current value
        // rhs contains the value to add/subtract/etc.
        // We need to:
        // 1. Compute the new value
        // 2. Store it back to the LHS location
        // 3. Return the new value

        std::shared_ptr<HIRValue> result;

        // Compute the operation
        bool eitherAny = isAnyOrNullish(lhs, node->left.get()) || isAnyOrNullish(rhs, node->right.get());
        if (op == "+=") {
            if (isString(lhs, node->left.get()) || isString(rhs, node->right.get())) {
                result = builder_.createStringConcat(lhs, rhs);
            } else if (useBigInt) {
                result = builder_.createCall("ts_bigint_add", {lhs, rhs}, HIRType::makeObject());
            } else if (eitherAny) {
                result = builder_.createCall("ts_value_add", {lhs, rhs}, HIRType::makeAny());
            } else if (useFloat) {
                result = builder_.createAddF64(lhs, rhs);
            } else {
                result = builder_.createAddI64(lhs, rhs);
            }
        } else if (op == "-=") {
            if (useBigInt) {
                result = builder_.createCall("ts_bigint_sub", {lhs, rhs}, HIRType::makeObject());
            } else if (eitherAny) {
                result = builder_.createCall("ts_value_sub", {lhs, rhs}, HIRType::makeAny());
            } else if (useFloat) {
                result = builder_.createSubF64(lhs, rhs);
            } else {
                result = builder_.createSubI64(lhs, rhs);
            }
        } else if (op == "*=") {
            if (useBigInt) {
                result = builder_.createCall("ts_bigint_mul", {lhs, rhs}, HIRType::makeObject());
            } else if (eitherAny) {
                result = builder_.createCall("ts_value_mul", {lhs, rhs}, HIRType::makeAny());
            } else if (useFloat) {
                result = builder_.createMulF64(lhs, rhs);
            } else {
                result = builder_.createMulI64(lhs, rhs);
            }
        } else if (op == "/=") {
            if (useBigInt) {
                result = builder_.createCall("ts_bigint_div", {lhs, rhs}, HIRType::makeObject());
            } else if (eitherAny) {
                result = builder_.createCall("ts_value_div", {lhs, rhs}, HIRType::makeAny());
            } else if (useFloat) {
                result = builder_.createDivF64(lhs, rhs);
            } else {
                result = builder_.createDivI64(lhs, rhs);
            }
        } else if (op == "%=") {
            if (useBigInt) {
                result = builder_.createCall("ts_bigint_mod", {lhs, rhs}, HIRType::makeObject());
            } else if (eitherAny) {
                result = builder_.createCall("ts_value_mod", {lhs, rhs}, HIRType::makeAny());
            } else if (useFloat) {
                result = builder_.createModF64(lhs, rhs);
            } else {
                result = builder_.createModI64(lhs, rhs);
            }
        } else if (op == "&=") {
            result = builder_.createAndI64(lhs, rhs);
        } else if (op == "|=") {
            result = builder_.createOrI64(lhs, rhs);
        } else if (op == "^=") {
            result = builder_.createXorI64(lhs, rhs);
        } else if (op == "<<=") {
            result = builder_.createShlI64(lhs, rhs);
        } else if (op == ">>=") {
            result = builder_.createShrI64(lhs, rhs);
        } else if (op == ">>>=") {
            result = builder_.createUShrI64(lhs, rhs);
        }

        // Now store the result back to the LHS
        // Handle identifier LHS
        auto* ident = dynamic_cast<ast::Identifier*>(node->left.get());
        if (ident) {
            // For module-scoped variables from inner functions, use __modvar_ globals
            if (currentFunction_ && isModuleGlobalVar(ident->name)) {
                size_t scopeIdx = 0;
                if (isCapturedVariable(ident->name, &scopeIdx)) {
                    builder_.createStoreGlobal(modVarName(ident->name), result);
                    lastValue_ = result;
                    return;
                }
            }

            // Check if this is a captured variable from an outer function
            {
                size_t scopeIndex = 0;
                if (currentFunction_ && isCapturedVariable(ident->name, &scopeIndex)) {
                    auto* capInfo = lookupVariableInfo(ident->name);
                    auto type = capInfo && capInfo->elemType ? capInfo->elemType : result->type;
                    registerCapture(ident->name, type, scopeIndex);
                    currentFunction_->hasClosure = true;
                    builder_.createStoreCapture(ident->name, result);
                    lastValue_ = result;
                    return;
                }
            }

            auto* info = lookupVariableInfo(ident->name);
            if (info && info->isAlloca) {
                builder_.createStore(result, info->value, info->elemType);
                // If this variable is captured by a nested closure, also update the cell
                if (info->isCapturedByNested && info->closurePtr && info->captureIndex >= 0) {
                    auto closureVal = builder_.createLoad(HIRType::makeAny(), info->closurePtr);
                    builder_.createStoreCaptureFromClosure(closureVal, info->captureIndex, result);
                }
            } else if (info) {
                // Direct value - promote to alloca for mutability
                auto allocaPtr = builder_.createAlloca(result->type, ident->name);
                builder_.createStore(result, allocaPtr, result->type);
                info->value = allocaPtr;
                info->elemType = result->type;
                info->isAlloca = true;
            }

            // If this variable is a module-scoped global, also update __modvar_ global
            if (isModuleGlobalVar(ident->name)) {
                builder_.createStoreGlobal(modVarName(ident->name), result);
            }

            lastValue_ = result;
            return;
        }

        // Handle property access LHS (e.g., obj.prop += val)
        auto* propAccess = dynamic_cast<ast::PropertyAccessExpression*>(node->left.get());
        if (propAccess) {
            auto obj = lowerExpression(propAccess->expression.get());
            auto propName = builder_.createConstString(propAccess->name);
            std::vector<std::shared_ptr<HIRValue>> args = {obj, propName, boxValueIfNeeded(result)};
            builder_.createCall("ts_object_set_property", args, HIRType::makeVoid());
            lastValue_ = result;
            return;
        }

        // Handle element access LHS (e.g., arr[i] += val)
        auto* elemAccess = dynamic_cast<ast::ElementAccessExpression*>(node->left.get());
        if (elemAccess) {
            auto arr = lowerExpression(elemAccess->expression.get());
            auto idx = lowerExpression(elemAccess->argumentExpression.get());
            std::vector<std::shared_ptr<HIRValue>> args = {arr, idx, boxValueIfNeeded(result)};
            builder_.createCall("ts_array_set", args, HIRType::makeVoid());
            lastValue_ = result;
            return;
        }

        // Fallback - just return the computed value
        lastValue_ = result;
    } else {
        // Unknown operator - return lhs
        lastValue_ = lhs;
    }
}

void ASTToHIR::visitConditionalExpression(ast::ConditionalExpression* node) {
    setSourceLine(node);
    auto cond = lowerExpression(node->condition.get());

    // Use branch-based evaluation for correct short-circuit semantics.
    // JavaScript's ternary operator must NOT eagerly evaluate both branches
    // because they may have side effects (function calls, property access, etc.)
    int blockId = blockCounter_++;
    auto* trueBB = builder_.createBlock("cond_true_" + std::to_string(blockId));
    auto* falseBB = builder_.createBlock("cond_false_" + std::to_string(blockId));
    auto* endBB = builder_.createBlock("cond_end_" + std::to_string(blockId));

    builder_.createCondBranch(cond, trueBB, falseBB);

    builder_.setInsertPoint(trueBB);
    currentBlock_ = trueBB;  // Keep ASTToHIR's currentBlock_ in sync
    auto trueVal = lowerExpression(node->whenTrue.get());
    auto boxedTrue = boxValueIfNeeded(trueVal);
    auto* trueEndBB = builder_.getInsertBlock(); // may differ after calls
    builder_.createBranch(endBB);

    builder_.setInsertPoint(falseBB);
    currentBlock_ = falseBB;  // Keep ASTToHIR's currentBlock_ in sync
    auto falseVal = lowerExpression(node->whenFalse.get());
    auto boxedFalse = boxValueIfNeeded(falseVal);
    auto* falseEndBB = builder_.getInsertBlock();
    builder_.createBranch(endBB);

    builder_.setInsertPoint(endBB);
    currentBlock_ = endBB;  // Keep ASTToHIR's currentBlock_ in sync
    lastValue_ = builder_.createPhi(HIRType::makeAny(),
        {{boxedTrue, trueEndBB}, {boxedFalse, falseEndBB}});
}

void ASTToHIR::visitAssignmentExpression(ast::AssignmentExpression* node) {
    setSourceLine(node);
    auto rhs = lowerExpression(node->right.get());

    // Handle simple identifier assignment
    auto* ident = dynamic_cast<ast::Identifier*>(node->left.get());
    if (ident) {
        // For module-scoped variables accessed from inner functions, use __modvar_ globals
        // instead of closure cells. Closure cells are per-closure snapshots, but module
        // variables must be shared across all functions in the module.
        if (currentFunction_ && isModuleGlobalVar(ident->name)) {
            size_t scopeIndex = 0;
            if (isCapturedVariable(ident->name, &scopeIndex)) {
                builder_.createStoreGlobal(modVarName(ident->name), rhs);
                lastValue_ = rhs;
                return;
            }
        }

        // Check if this is a captured variable from an outer function
        size_t scopeIndex = 0;
        if (currentFunction_ && isCapturedVariable(ident->name, &scopeIndex)) {
            // Store to captured variable
            auto* info = lookupVariableInfo(ident->name);
            auto type = info && info->elemType ? info->elemType : rhs->type;
            registerCapture(ident->name, type, scopeIndex);
            currentFunction_->hasClosure = true;
            builder_.createStoreCapture(ident->name, rhs);
            lastValue_ = rhs;
            return;
        }

        // Look up variable info to see if it's an alloca
        auto* info = lookupVariableInfo(ident->name);
        if (info && info->isAlloca) {
            // Emit store to the alloca, with type info for coercion
            builder_.createStore(rhs, info->value, info->elemType);
            // If this variable is captured by a nested closure, also update the cell
            // so the closure sees the new value (e.g., interval = setInterval(...))
            if (info->isCapturedByNested && info->closurePtr && info->captureIndex >= 0) {
                auto closureVal = builder_.createLoad(HIRType::makeAny(), info->closurePtr);
                builder_.createStoreCaptureFromClosure(closureVal, info->captureIndex, rhs);
            }
        } else if (info) {
            // Direct value - promote to alloca for mutability
            // Create new alloca and store
            auto allocaPtr = builder_.createAlloca(rhs->type, ident->name);
            builder_.createStore(rhs, allocaPtr, rhs->type);
            // Update variable info to be alloca-based
            info->value = allocaPtr;
            info->elemType = rhs->type;
            info->isAlloca = true;
        } else {
            // New variable - should not happen in assignment, but handle gracefully
            defineVariable(ident->name, rhs);
        }

        // If this variable is a module-scoped global, also update the __modvar_ global
        // so other functions (arrow functions, function expressions) from the same module
        // can read the updated value via LoadGlobalTyped.
        if (isModuleGlobalVar(ident->name)) {
            builder_.createStoreGlobal(modVarName(ident->name), rhs);
        }

        lastValue_ = rhs;
        return;
    }

    // Handle property access assignment
    auto* propAccess = dynamic_cast<ast::PropertyAccessExpression*>(node->left.get());
    if (propAccess) {
        // Check for static property assignment: ClassName.propertyName = value
        auto* classNameIdent = dynamic_cast<ast::Identifier*>(propAccess->expression.get());
        if (classNameIdent) {
            for (auto& cls : module_->classes) {
                if (cls->name == classNameIdent->name) {
                    // Check if this is a static property
                    std::string globalName = cls->name + "_static_" + propAccess->name;
                    auto it = staticPropertyGlobals_.find(globalName);
                    if (it != staticPropertyGlobals_.end()) {
                        // Store to the static property global
                        auto globalPtr = it->second.first;
                        auto propType = it->second.second;
                        builder_.createStore(rhs, globalPtr, propType);
                        lastValue_ = rhs;
                        return;
                    }
                    break;
                }
            }
        }

        auto obj = lowerExpression(propAccess->expression.get());

        // Check for setter: look up the class type and see if it has __setter_<propName>
        HIRClass* targetClass = nullptr;

        // Check if expression has an inferred class type
        if (propAccess->expression && propAccess->expression->inferredType) {
            auto exprType = propAccess->expression->inferredType;
            if (exprType->kind == ts::TypeKind::Class) {
                auto classType = std::dynamic_pointer_cast<ts::ClassType>(exprType);
                if (classType) {
                    for (auto& cls : module_->classes) {
                        if (cls->name == classType->name) {
                            targetClass = cls.get();
                            break;
                        }
                    }
                }
            }
        }

        // If accessing 'this', use currentClass_
        if (!targetClass) {
            auto* thisIdent = dynamic_cast<ast::Identifier*>(propAccess->expression.get());
            if (thisIdent && thisIdent->name == "this" && currentClass_) {
                targetClass = currentClass_;
            }
        }

        // Check if the target class has a setter for this property
        if (targetClass) {
            std::string setterKey = "__setter_" + propAccess->name;
            auto setterIt = targetClass->methods.find(setterKey);
            if (setterIt != targetClass->methods.end()) {
                // Found a setter - call it instead of direct property assignment
                HIRFunction* setterFunc = setterIt->second;
                builder_.createCall(setterFunc->name, {obj, rhs}, HIRType::makeVoid());
                lastValue_ = rhs;
                return;
            }
        }

        builder_.createSetPropStatic(obj, propAccess->name, rhs);
        lastValue_ = rhs;
        return;
    }

    // Handle element access assignment
    auto* elemAccess = dynamic_cast<ast::ElementAccessExpression*>(node->left.get());
    if (elemAccess) {
        auto obj = lowerExpression(elemAccess->expression.get());
        auto idx = lowerExpression(elemAccess->argumentExpression.get());
        builder_.createSetElem(obj, idx, rhs);
        lastValue_ = rhs;
        return;
    }

    lastValue_ = rhs;
}

void ASTToHIR::visitCallExpression(ast::CallExpression* node) {
    setSourceLine(node);
    if (!node) return;
    if (!node->callee) return;
    std::vector<std::shared_ptr<HIRValue>> args;
    for (auto& arg : node->arguments) {
        args.push_back(lowerExpression(arg.get()));
    }

    // Handle super() call - calls parent class constructor
    auto* superExpr = dynamic_cast<ast::SuperExpression*>(node->callee.get());
    if (superExpr && currentClass_ && currentClass_->baseClass) {
        if (currentClass_->baseClass->constructor) {
            // Base class has explicit constructor - call it with [this, ...args]
            std::vector<std::shared_ptr<HIRValue>> ctorArgs;
            auto thisVal = lookupVariable("this");
            if (thisVal) {
                ctorArgs.push_back(thisVal);
            } else {
                ctorArgs.push_back(builder_.createConstNull());
            }
            for (auto& arg : args) {
                ctorArgs.push_back(arg);
            }
            builder_.createCall(currentClass_->baseClass->constructor->name, ctorArgs, HIRType::makeVoid());
        }
        // If base class has no explicit constructor (e.g., abstract class),
        // super() is a no-op - just continue with the derived class constructor
        lastValue_ = builder_.createConstUndefined();
        return;
    }

    // Handle method call
    auto* propAccess = dynamic_cast<ast::PropertyAccessExpression*>(node->callee.get());
    if (propAccess) {
        // Case 0: Namespace method call - import * as ns from './mod'; ns.func()
        // Check specializations (always complete) to determine if this is a user-defined
        // function. module_->functions may not have the function yet if the specialization
        // hasn't been processed, but specializations_ is set at the start.
        // Extension modules (timers/promises, fs, etc.) don't have specializations,
        // so they fall through to the normal dispatch path.
        if (propAccess->expression->inferredType &&
            propAccess->expression->inferredType->kind == ts::TypeKind::Namespace) {
            std::string funcName = propAccess->name;

            // Compute mangled name based on argument types
            std::vector<std::shared_ptr<ts::Type>> argTypes;
            for (auto& arg : node->arguments) {
                argTypes.push_back(arg->inferredType ? arg->inferredType
                                   : std::make_shared<ts::Type>(ts::TypeKind::Any));
            }
            // Get module path from namespace type for cross-module disambiguation
            std::string nsModulePath;
            if (auto nsType = std::dynamic_pointer_cast<ts::NamespaceType>(propAccess->expression->inferredType)) {
                if (nsType->module) {
                    nsModulePath = nsType->module->path;
                }
            }
            std::string mangledName = Monomorphizer::generateMangledName(
                funcName, argTypes, node->resolvedTypeArguments, nsModulePath);

            // Check specializations to determine if this is a user-defined function
            bool foundSpec = false;
            std::string callName = mangledName;
            std::shared_ptr<ts::Type> specReturnType;

            if (specializations_) {
                // Try mangled name first
                for (const auto& spec : *specializations_) {
                    if (spec.specializedName == mangledName) {
                        foundSpec = true;
                        specReturnType = spec.returnType;
                        break;
                    }
                }
                // Try original name as fallback, but skip class methods.
                // Class methods (spec.classType != null) have originalName matching
                // their method name (e.g., "inc" for SemVer.inc), which can collide
                // with standalone module functions of the same name.
                if (!foundSpec) {
                    for (const auto& spec : *specializations_) {
                        if (spec.originalName == funcName && spec.specializedName != funcName
                            && !spec.classType) {
                            foundSpec = true;
                            callName = spec.specializedName;
                            specReturnType = spec.returnType;
                            break;
                        }
                    }
                }
            }

            if (foundSpec) {
                // Look up HIR function for parameter info (may not be available yet
                // if this function's specialization hasn't been processed)
                HIRFunction* targetFunc = nullptr;
                for (auto& f : module_->functions) {
                    if (f->name == callName) {
                        targetFunc = f.get();
                        break;
                    }
                }

                // Pad args with undefined for missing params
                if (targetFunc && args.size() < targetFunc->params.size()) {
                    for (size_t i = args.size(); i < targetFunc->params.size(); ++i) {
                        args.push_back(builder_.createConstUndefined());
                    }
                }

                // Box arguments when target parameter is Any type
                if (targetFunc) {
                    for (size_t i = 0; i < args.size() && i < targetFunc->params.size(); ++i) {
                        const auto& [paramName, paramType] = targetFunc->params[i];
                        if (paramType && paramType->kind == HIRTypeKind::Any) {
                            args[i] = boxValueIfNeeded(args[i]);
                        }
                    }
                }

                // Determine return type from HIR function or specialization
                std::shared_ptr<HIRType> returnType;
                if (targetFunc && targetFunc->returnType) {
                    returnType = targetFunc->returnType;
                } else if (specReturnType) {
                    returnType = convertType(specReturnType);
                } else {
                    returnType = HIRType::makeAny();
                }

                lastValue_ = builder_.createCall(callName, args, returnType);
                return;
            }
            // If not found in specializations, check if this is a CJS namespace import.
            // CJS namespace imports (e.g., import * as ns from './cjs-module') store the
            // module.exports object in moduleGlobalVars_. We must use explicit
            // GetPropDynamic + CallIndirect instead of createCallMethod, because
            // createCallMethod's built-in method matching (e.g., "add" -> Set.add) can
            // incorrectly intercept common method names.
            auto* nsIdent = dynamic_cast<ast::Identifier*>(propAccess->expression.get());
            if (nsIdent && isModuleGlobalVar(nsIdent->name)) {
                auto obj = lowerExpression(propAccess->expression.get());
                auto func = builder_.createGetPropStatic(obj, funcName, HIRType::makeAny());
                // Box all arguments for dynamic call
                std::vector<std::shared_ptr<HIRValue>> boxedArgs;
                for (auto& arg : args) {
                    boxedArgs.push_back(boxValueIfNeeded(arg));
                }
                lastValue_ = builder_.createCallIndirect(func, boxedArgs, HIRType::makeAny());
                return;
            }
            // Otherwise fall through to normal dispatch
        }

        // Check if we can use a direct call for method invocation

        // Case 1: Method call on 'this' - we know the class statically
        auto* thisIdent = dynamic_cast<ast::Identifier*>(propAccess->expression.get());
        if (thisIdent && thisIdent->name == "this" && currentClass_) {
            // Look up the method in the current class
            auto it = currentClass_->methods.find(propAccess->name);
            if (it != currentClass_->methods.end()) {
                HIRFunction* method = it->second;
                fprintf(stderr, "  Case1: this.%s -> method=%p name=%s\n",
                    propAccess->name.c_str(), (void*)method,
                    method ? method->name.c_str() : "null");
                fflush(stderr);
                if (!method) {
                    // Placeholder method - construct name
                    std::string methodFuncName = currentClass_->name + "_" + propAccess->name;
                    auto obj = lowerExpression(propAccess->expression.get());
                    std::vector<std::shared_ptr<HIRValue>> methodArgs;
                    methodArgs.push_back(obj);
                    for (auto& arg : args) methodArgs.push_back(arg);
                    lastValue_ = builder_.createCall(methodFuncName, methodArgs, HIRType::makeAny());
                    return;
                }
                // Build args: [this, ...args]
                std::vector<std::shared_ptr<HIRValue>> methodArgs;
                auto thisVal = lookupVariable("this");
                if (thisVal) {
                    methodArgs.push_back(thisVal);
                } else {
                    methodArgs.push_back(builder_.createConstNull());
                }
                for (auto& arg : args) {
                    methodArgs.push_back(arg);
                }
                // Direct call to the method function
                // Generator methods return Generator, not the method's declared return type
                auto resultType = method->returnType;
                if (method->isGenerator) {
                    resultType = HIRType::makeClass("Generator", 0);
                }
                lastValue_ = builder_.createCall(method->name, methodArgs, resultType);
                return;
            } else {
                // Method not found in current class. Check if it's:
                // 1. An abstract method → dynamic dispatch via vtable
                // 2. Inherited from a user-defined base class → direct call
                // 3. Inherited from a runtime/extension base class → dynamic dispatch

                // Check abstract methods first
                if (currentClass_->abstractMethods.count(propAccess->name)) {
                    auto obj = lookupVariable("this");
                    if (!obj) obj = builder_.createConstNull();
                    lastValue_ = builder_.createCallMethod(obj, propAccess->name, args, HIRType::makeAny());
                    return;
                }

                // Walk base class chain for user-defined inherited methods
                HIRClass* searchClass = currentClass_->baseClass;
                while (searchClass) {
                    auto baseIt = searchClass->methods.find(propAccess->name);
                    if (baseIt != searchClass->methods.end() && baseIt->second) {
                        // Found in user-defined base class - direct call
                        HIRFunction* method = baseIt->second;
                        std::vector<std::shared_ptr<HIRValue>> methodArgs;
                        auto thisVal = lookupVariable("this");
                        methodArgs.push_back(thisVal ? thisVal : builder_.createConstNull());
                        for (auto& arg : args) methodArgs.push_back(arg);
                        auto resultType = method->returnType;
                        if (method->isGenerator) {
                            resultType = HIRType::makeClass("Generator", 0);
                        }
                        lastValue_ = builder_.createCall(method->name, methodArgs, resultType);
                        return;
                    }
                    searchClass = searchClass->baseClass;
                }

                // Not found in any user class - use dynamic dispatch.
                // This handles methods inherited from runtime/extension base
                // classes (e.g., Counter extends EventEmitter → this.emit()).
                auto obj = lookupVariable("this");
                if (!obj) obj = builder_.createConstNull();
                lastValue_ = builder_.createCallMethod(obj, propAccess->name, args, HIRType::makeAny());
                return;
            }
        }

        // Case 2: Check if object has a known class type from inference
        std::string className;
        if (propAccess->expression->inferredType) {
            auto& type = propAccess->expression->inferredType;
            if (type->kind == ts::TypeKind::Class) {
                auto classType = std::dynamic_pointer_cast<ts::ClassType>(type);
                if (classType) {
                    className = classType->name;
                }
            }
        }
        // Also check: if the expression is a NewExpression for a known class,
        // use direct VTable dispatch. This handles patterns like:
        //   new SemVer(a).compare(new SemVer(b))
        // where the type analyzer hasn't set inferredType on the NewExpression.
        if (className.empty()) {
            auto* newExpr = dynamic_cast<ast::NewExpression*>(propAccess->expression.get());
            if (newExpr) {
                auto* newIdent = dynamic_cast<ast::Identifier*>(newExpr->expression.get());
                if (newIdent) {
                    for (auto& cls : module_->classes) {
                        if (cls->name == newIdent->name) {
                            className = newIdent->name;
                            break;
                        }
                    }
                }
            }
        }

        if (!className.empty()) {
            // Look up the class and search up the inheritance chain
            bool foundInUserClass = false;
            for (auto& cls : module_->classes) {
                if (cls->name == className) {
                    // Search in this class and all base classes
                    HIRClass* searchClass = cls.get();
                    while (searchClass) {
                        auto it = searchClass->methods.find(propAccess->name);
                        if (it != searchClass->methods.end()) {
                            HIRFunction* method = it->second;
                            fprintf(stderr, "  Case2: %s.%s -> method=%p\n",
                                className.c_str(), propAccess->name.c_str(), (void*)method);
                            fflush(stderr);
                            // Determine function name and return type.
                            // method may be nullptr (pre-registered placeholder from spec pre-pass)
                            std::string methodFuncName;
                            auto resultType = HIRType::makeAny();
                            if (method) {
                                methodFuncName = method->name;
                                resultType = method->returnType;
                                if (method->isGenerator) {
                                    resultType = HIRType::makeClass("Generator", 0);
                                }
                            } else {
                                // Placeholder - construct name from convention
                                methodFuncName = searchClass->name + "_" + propAccess->name;
                            }
                            // Build args: [obj, ...args]
                            auto obj = lowerExpression(propAccess->expression.get());
                            std::vector<std::shared_ptr<HIRValue>> methodArgs;
                            methodArgs.push_back(obj);
                            for (auto& arg : args) {
                                methodArgs.push_back(arg);
                            }
                            lastValue_ = builder_.createCall(methodFuncName, methodArgs, resultType);
                            return;
                        }
                        // Move to base class
                        searchClass = searchClass->baseClass;
                    }
                    foundInUserClass = true; // Class exists but method not found
                    break;
                }
            }

            // Case 2b: Extension class instance method call.
            // Only for types with kind == "class" (have real standalone C functions).
            // Types with kind == "interface" (Stats, Dirent) use closure-based dispatch.
            // Skip if className comes from a user-imported module (moduleGlobalVars_).
            // This prevents user-defined classes (e.g., eventemitter3's EventEmitter)
            // from being dispatched to the runtime's built-in extension methods.
            if (!foundInUserClass && !isModuleGlobalVar(className)) {
                auto& extReg = ext::ExtensionRegistry::instance();

                // Check for static methods FIRST when expression is a bare identifier
                // matching a type/global name (e.g., Response.json() vs resp.json()).
                // This prevents static methods from being shadowed by instance methods.
                auto* bareIdent = dynamic_cast<ast::Identifier*>(propAccess->expression.get());
                if (bareIdent && extReg.isClassKind(bareIdent->name)) {
                    const ext::MethodDefinition* extStaticMethod = extReg.findStaticMethod(bareIdent->name, propAccess->name);
                    if (extStaticMethod && extStaticMethod->lowering) {
                        std::string funcName = extStaticMethod->hirName.value_or(extStaticMethod->call);
                        auto resultType = extTypeRefToHIR(extStaticMethod->returns);
                        lastValue_ = builder_.createCall(funcName, args, resultType);
                        return;
                    }
                }

                if (extReg.isClassKind(className)) {
                    const ext::MethodDefinition* extMethod = extReg.findMethod(className, propAccess->name);
                    if (extMethod && extMethod->lowering) {
                        std::string funcName = extMethod->hirName.value_or(extMethod->call);
                        auto obj = lowerExpression(propAccess->expression.get());
                        std::vector<std::shared_ptr<HIRValue>> methodArgs;
                        methodArgs.push_back(obj);
                        for (auto& arg : args) {
                            methodArgs.push_back(arg);
                        }
                        // Map ext.json return type to HIR type for proper downstream handling
                        auto resultType = extTypeRefToHIR(extMethod->returns);
                        lastValue_ = builder_.createCall(funcName, methodArgs, resultType);
                        return;
                    }
                }

                // Case 2c: Built-in WeakRef/FinalizationRegistry instance methods
                if (className == "WeakRef" && propAccess->name == "deref") {
                    auto obj = lowerExpression(propAccess->expression.get());
                    lastValue_ = builder_.createCall("ts_weakref_deref", {obj}, HIRType::makeAny());
                    return;
                }
                if (className == "FinalizationRegistry") {
                    if (propAccess->name == "register") {
                        auto obj = lowerExpression(propAccess->expression.get());
                        std::vector<std::shared_ptr<HIRValue>> methodArgs;
                        methodArgs.push_back(obj);
                        for (auto& arg : args) {
                            methodArgs.push_back(arg);
                        }
                        // Pad to 4 args (registry, target, heldValue, unregisterToken)
                        while (methodArgs.size() < 4) {
                            methodArgs.push_back(builder_.createConstUndefined());
                        }
                        builder_.createCall("ts_finalization_registry_register", methodArgs, HIRType::makeVoid());
                        lastValue_ = builder_.createConstUndefined();
                        return;
                    }
                    if (propAccess->name == "unregister") {
                        auto obj = lowerExpression(propAccess->expression.get());
                        std::vector<std::shared_ptr<HIRValue>> methodArgs;
                        methodArgs.push_back(obj);
                        for (auto& arg : args) {
                            methodArgs.push_back(arg);
                        }
                        lastValue_ = builder_.createCall("ts_finalization_registry_unregister", methodArgs, HIRType::makeBool());
                        return;
                    }
                }
            }
        }

        // Case 3: Static method call - ClassName.methodName(...)
        auto* classNameIdent = dynamic_cast<ast::Identifier*>(propAccess->expression.get());
        if (classNameIdent) {
            // Check if this is a class name
            for (auto& cls : module_->classes) {
                if (cls->name == classNameIdent->name) {
                    // Check for static method
                    auto it = cls->staticMethods.find(propAccess->name);
                    if (it != cls->staticMethods.end()) {
                        HIRFunction* method = it->second;
                        // Static methods don't need 'this' parameter
                        lastValue_ = builder_.createCall(method->name, args, method->returnType);
                        return;
                    }
                    // Fallback: For imported classes, staticMethods may not be populated
                    // because the class body is compiled later (via module init specialization).
                    // Emit a forward-reference call using the conventional name.
                    {
                        std::string staticFuncName = cls->name + "_static_" + propAccess->name;
                        lastValue_ = builder_.createCall(staticFuncName, args, HIRType::makeAny());
                        return;
                    }
                    break;
                }
            }

            // Case 3b: Extension static method call - Buffer.from(...), Buffer.alloc(...), etc.
            // Check ExtensionRegistry for static methods on extension-defined class types.
            // Only match methods that have a lowering spec (actual runtime function).
            {
                auto& extReg = ext::ExtensionRegistry::instance();
                const ext::MethodDefinition* extStaticMethod = extReg.findStaticMethod(classNameIdent->name, propAccess->name);
                if (extStaticMethod && extStaticMethod->lowering) {
                    std::string funcName = extStaticMethod->hirName.value_or(extStaticMethod->call);
                    // Map ext.json return type to HIR type for proper downstream handling
                    auto resultType = extTypeRefToHIR(extStaticMethod->returns);
                    lastValue_ = builder_.createCall(funcName, args, resultType);
                    return;
                }
            }

            // Case 4: Node.js builtin module method call - path.basename(...), fs.readFileSync(...), etc.
            // Check against ExtensionRegistry instead of hardcoded list
            auto& registry = ext::ExtensionRegistry::instance();
            if (registry.isRegisteredModule(classNameIdent->name) || registry.isRegisteredObject(classNameIdent->name)) {
                const ext::MethodDefinition* methodDef = registry.findObjectMethod(classNameIdent->name, propAccess->name);

                // If the method is NOT found in the ext.json AND the identifier is a local
                // variable with a known non-module type (string, number, etc.), skip Case 4.
                // This prevents local variables that shadow module names
                // (e.g. `const path = url.fileURLToPath(...)`) from being treated as module calls.
                bool isLocalVarShadow = false;
                if (!methodDef) {
                    // Method not found on the module/object. Could be a local variable
                    // shadowing a module name (e.g., `var events = []` vs `events` module).
                    // Don't generate a bogus ts_{module}_{method} symbol - fall through
                    // to generic method handlers (push, join, etc.) instead.
                    isLocalVarShadow = true;
                }

                if (!isLocalVarShadow) {
                // Use the HIR name (matching LoweringRegistry derivation) so the registered lowering spec is found
                std::string runtimeFunc;
                if (methodDef && methodDef->hirName) {
                    runtimeFunc = *methodDef->hirName;
                } else {
                    runtimeFunc = "ts_" + classNameIdent->name + "_" + propAccess->name;
                }
                // Use ext.json return type if available, otherwise default to any
                auto resultType = methodDef ? extTypeRefToHIR(methodDef->returns) : HIRType::makeAny();

                if (methodDef) {
                    // Find if there's a rest parameter and at what position
                    size_t restParamIndex = SIZE_MAX;
                    for (size_t i = 0; i < methodDef->params.size(); ++i) {
                        if (methodDef->params[i].rest) {
                            restParamIndex = i;
                            break;
                        }
                    }

                    // Skip array packing for ALL console functions - they have special
                    // handling in HIRToLLVM (TypeDispatch for log/error/warn/info/debug,
                    // direct single-arg calls for group/time/count/etc.)
                    bool isConsoleFunctionWithSpecialHandling =
                        classNameIdent->name == "console";

                    if (restParamIndex != SIZE_MAX && args.size() >= restParamIndex &&
                        !isConsoleFunctionWithSpecialHandling) {
                        // Pack all arguments from restParamIndex onwards into an array
                        std::vector<std::shared_ptr<HIRValue>> packedArgs;

                        // Copy non-rest arguments
                        for (size_t i = 0; i < restParamIndex; ++i) {
                            packedArgs.push_back(args[i]);
                        }

                        // Create array for rest arguments
                        auto zero = builder_.createConstInt(0);
                        auto restArray = builder_.createCall("ts_array_create", {}, HIRType::makeArray(HIRType::makeAny(), false));

                        // Push rest arguments into the array (boxed)
                        for (size_t i = restParamIndex; i < args.size(); ++i) {
                            auto boxedArg = boxValueIfNeeded(args[i]);
                            builder_.createCall("ts_array_push", {restArray, boxedArg}, HIRType::makeInt64());
                        }

                        packedArgs.push_back(restArray);
                        lastValue_ = builder_.createCall(runtimeFunc, packedArgs, resultType);
                        return;
                    }
                }

                // No rest parameter or not enough args - emit direct call
                lastValue_ = builder_.createCall(runtimeFunc, args, resultType);
                return;
                } // end if (!isLocalVarShadow)
            }
        }

        // Case 4b: Nested object method call - path.posix.join(...), path.win32.basename(...), etc.
        // Pattern: <module>.<nested>.<method>(...)
        {
            auto* innerPropAccess = dynamic_cast<ast::PropertyAccessExpression*>(propAccess->expression.get());
            if (innerPropAccess) {
                auto* moduleIdent = dynamic_cast<ast::Identifier*>(innerPropAccess->expression.get());
                if (moduleIdent) {
                    auto& registry = ext::ExtensionRegistry::instance();
                    const ext::MethodDefinition* methodDef = registry.findNestedObjectMethod(
                        moduleIdent->name, innerPropAccess->name, propAccess->name);
                    if (methodDef && methodDef->lowering) {
                        std::string runtimeFunc;
                        if (methodDef->hirName) {
                            runtimeFunc = *methodDef->hirName;
                        } else {
                            runtimeFunc = "ts_" + moduleIdent->name + "_" + innerPropAccess->name + "_" + propAccess->name;
                        }
                        auto resultType = extTypeRefToHIR(methodDef->returns);

                        // Handle rest parameters (same logic as Case 4)
                        size_t restParamIndex = SIZE_MAX;
                        for (size_t i = 0; i < methodDef->params.size(); ++i) {
                            if (methodDef->params[i].rest) {
                                restParamIndex = i;
                                break;
                            }
                        }
                        if (restParamIndex != SIZE_MAX && args.size() >= restParamIndex) {
                            std::vector<std::shared_ptr<HIRValue>> packedArgs;
                            for (size_t i = 0; i < restParamIndex; ++i) {
                                packedArgs.push_back(args[i]);
                            }
                            auto restArray = builder_.createCall("ts_array_create", {}, HIRType::makeArray(HIRType::makeAny(), false));
                            for (size_t i = restParamIndex; i < args.size(); ++i) {
                                auto boxedArg = boxValueIfNeeded(args[i]);
                                builder_.createCall("ts_array_push", {restArray, boxedArg}, HIRType::makeInt64());
                            }
                            packedArgs.push_back(restArray);

                            // Inject platform constant (e.g., 1=win32, 2=posix) for _ex functions
                            if (methodDef->platformArg) {
                                packedArgs.push_back(builder_.createConstInt(*methodDef->platformArg));
                            }

                            lastValue_ = builder_.createCall(runtimeFunc, packedArgs, resultType);
                            return;
                        }

                        // Non-rest args: inject platformArg if present
                        if (methodDef->platformArg) {
                            auto argsWithPlatform = args;
                            argsWithPlatform.push_back(builder_.createConstInt(*methodDef->platformArg));
                            lastValue_ = builder_.createCall(runtimeFunc, argsWithPlatform, resultType);
                            return;
                        }

                        lastValue_ = builder_.createCall(runtimeFunc, args, resultType);
                        return;
                    }
                }
            }
        }

        // Handle Function.prototype.call(thisArg, ...args)
        // Use ts_call_with_this_N to properly save/restore the caller's this context.
        // Previously used ts_set_call_this + ts_call_N which permanently clobbered this.
        if (propAccess->name == "call" && !args.empty()) {
            auto func = lowerExpression(propAccess->expression.get());
            auto boxedFunc = boxValueIfNeeded(func);
            auto thisArg = args[0];
            auto boxedThis = boxValueIfNeeded(thisArg);
            std::vector<std::shared_ptr<HIRValue>> callArgs = {boxedFunc, boxedThis};
            for (size_t i = 1; i < args.size(); i++) {
                callArgs.push_back(boxValueIfNeeded(args[i]));
            }
            size_t numArgs = args.size() - 1; // minus thisArg
            std::string callFn = "ts_call_with_this_" + std::to_string(numArgs);
            lastValue_ = builder_.createCall(callFn, callArgs, HIRType::makeAny());
            return;
        }

        // Fallback: Dynamic method call
        auto obj = lowerExpression(propAccess->expression.get());
        lastValue_ = builder_.createCallMethod(obj, propAccess->name, args, HIRType::makeAny());
        return;
    }

    // Handle direct function call
    auto* ident = dynamic_cast<ast::Identifier*>(node->callee.get());
    if (ident) {
        // First check if this is a captured variable from an outer function
        size_t scopeIndex = 0;
        if (currentFunction_ && isCapturedVariable(ident->name, &scopeIndex)) {
            // For module-level variables that have __modvar_ globals, prefer
            // the global over closure cells. Closure cells may be null due to
            // capture ordering (e.g., fmtLong captures plural, but plural's
            // closure isn't created yet when fmtLong's closure is created).
            if (isModuleGlobalVar(ident->name)) {
                std::string globalName = modVarName(ident->name);
                auto funcPtr = builder_.createLoadGlobalTyped(globalName, HIRType::makeAny());
                lastValue_ = builder_.createCallIndirect(funcPtr, args, HIRType::makeAny());
                return;
            }
            // Look up the variable info to get its type
            auto* info = lookupVariableInfo(ident->name);
            if (info) {
                auto type = info->elemType ? info->elemType : (info->value ? info->value->type : HIRType::makeAny());
                // Register this capture for the current function
                registerCapture(ident->name, type, scopeIndex);
                // Mark the function as having closures
                currentFunction_->hasClosure = true;
                // Use LoadCapture for captured variables
                auto funcPtr = builder_.createLoadCapture(ident->name, type);
                // Get return type from function type if available
                std::shared_ptr<HIRType> resultType = HIRType::makeAny();
                if (type && type->kind == HIRTypeKind::Function && type->returnType) {
                    resultType = type->returnType;
                }
                lastValue_ = builder_.createCallIndirect(funcPtr, args, resultType);
                return;
            }
        }

        // Check if this is a local variable (might be a closure)
        auto* info = lookupVariableInfo(ident->name);
        if (info) {
            // It's a local variable - load the function pointer and call indirectly
            std::shared_ptr<HIRValue> funcPtr;
            std::shared_ptr<HIRType> funcType;
            if (info->isAlloca && info->elemType) {
                funcPtr = builder_.createLoad(info->elemType, info->value);
                funcType = info->elemType;
            } else {
                funcPtr = info->value;
                funcType = info->value->type;
            }
            // Get return type from function type if available
            std::shared_ptr<HIRType> resultType = HIRType::makeAny();
            if (funcType && funcType->kind == HIRTypeKind::Function && funcType->returnType) {
                resultType = funcType->returnType;
            }
            lastValue_ = builder_.createCallIndirect(funcPtr, args, resultType);
            return;
        }
        // Check if this is a CJS module binding (stored in __modvar_ global).
        // CJS named imports that are function expressions (not FunctionDeclarations)
        // are stored in moduleGlobalVars_ and must be called indirectly.
        if (isModuleGlobalVar(ident->name)) {
            std::string globalName = modVarName(ident->name);
            auto funcPtr = builder_.createLoadGlobalTyped(globalName, HIRType::makeAny());
            lastValue_ = builder_.createCallIndirect(funcPtr, args, HIRType::makeAny());
            return;
        }
        // Handle builtin globals that are called as functions
        if (ident->name == "Symbol") {
            // Symbol(description?) creates a unique symbol
            std::shared_ptr<HIRValue> desc;
            if (!args.empty()) {
                desc = args[0];
            } else {
                desc = builder_.createConstNull();
            }
            lastValue_ = builder_.createCall("ts_symbol_create", {desc}, HIRType::makeSymbol());
            return;
        }

        if (ident->name == "BigInt") {
            // BigInt(value) converts value to BigInt
            if (!args.empty()) {
                lastValue_ = builder_.createCall("ts_bigint_from_value", {args[0]}, HIRType::makeBigInt());
            } else {
                lastValue_ = builder_.createConstNull();
            }
            return;
        }

        if (ident->name == "Boolean") {
            // Boolean(value) converts to boolean using JavaScript truthiness
            if (!args.empty()) {
                // ts_value_to_bool expects a boxed TsValue*, so we need to box the argument
                auto arg = args[0];
                std::shared_ptr<HIRValue> boxed;
                if (arg->type) {
                    switch (arg->type->kind) {
                        case HIRTypeKind::Int64:
                            boxed = builder_.createBoxInt(arg);
                            break;
                        case HIRTypeKind::Float64:
                            boxed = builder_.createBoxFloat(arg);
                            break;
                        case HIRTypeKind::Bool:
                            boxed = builder_.createBoxBool(arg);
                            break;
                        case HIRTypeKind::String:
                            boxed = builder_.createBoxString(arg);
                            break;
                        case HIRTypeKind::Any:
                            // Already boxed
                            boxed = arg;
                            break;
                        default:
                            // For objects, arrays, etc. - box as object
                            boxed = builder_.createBoxObject(arg);
                            break;
                    }
                } else {
                    // Unknown type, assume it needs boxing as object
                    boxed = builder_.createBoxObject(arg);
                }
                lastValue_ = builder_.createCall("ts_value_to_bool", {boxed}, HIRType::makeBool());
            } else {
                lastValue_ = builder_.createConstBool(false);
            }
            return;
        }

        if (ident->name == "gc") {
            // gc() forces garbage collection
            lastValue_ = builder_.createCall("ts_gc_collect", {}, HIRType::makeVoid());
            return;
        }

        if (ident->name == "Number") {
            // Number(value) converts to number
            if (!args.empty()) {
                lastValue_ = builder_.createCall("ts_to_number", {args[0]}, HIRType::makeFloat64());
            } else {
                lastValue_ = builder_.createConstFloat(0.0);
            }
            return;
        }

        if (ident->name == "String") {
            // String(value) converts to string
            if (!args.empty()) {
                lastValue_ = builder_.createCall("ts_to_string", {args[0]}, HIRType::makeString());
            } else {
                lastValue_ = builder_.createCall("ts_string_create", {builder_.createConstNull()}, HIRType::makeString());
            }
            return;
        }

        if (ident->name == "Array") {
            // Array() → empty array; Array(n) → sized array; Array(a,b,c) → [a,b,c]
            if (args.empty()) {
                lastValue_ = builder_.createCall("ts_array_create", {}, HIRType::makeArray(HIRType::makeAny(), false));
            } else if (args.size() == 1) {
                lastValue_ = builder_.createCall("ts_array_constructor", {args[0]}, HIRType::makeArray(HIRType::makeAny(), false));
            } else {
                // Array(a, b, c) → create + push each element
                auto arr = builder_.createCall("ts_array_create", {}, HIRType::makeArray(HIRType::makeAny(), false));
                for (auto& arg : args) {
                    builder_.createCall("ts_array_push_any", {arr, arg}, HIRType::makeVoid());
                }
                lastValue_ = arr;
            }
            return;
        }

        if (ident->name == "Object") {
            // Object() and Object(value) - create or return object
            if (args.empty()) {
                lastValue_ = builder_.createCall("ts_object_create_empty", {}, HIRType::makeAny());
            } else {
                lastValue_ = builder_.createCall("ts_object_constructor", {args[0]}, HIRType::makeAny());
            }
            return;
        }

        if (ident->name == "Function") {
            // Function('return this')() - eval-like pattern used by lodash _root.js
            // Return a closure that returns globalThis
            lastValue_ = builder_.createCall("ts_function_constructor_stub", {}, HIRType::makeAny());
            return;
        }

        // Error constructors called as functions (without new) - same as new Error()
        if (ident->name == "Error" || ident->name == "TypeError" || ident->name == "RangeError" ||
            ident->name == "ReferenceError" || ident->name == "SyntaxError" || ident->name == "URIError" ||
            ident->name == "EvalError") {
            std::shared_ptr<HIRValue> message;
            if (!args.empty()) {
                message = args[0];
            } else {
                message = builder_.createConstString("");
            }
            lastValue_ = builder_.createCall("ts_error_create", {message}, HIRType::makeAny());
            return;
        }

        // Global URI encoding/decoding functions
        if (ident->name == "encodeURIComponent") {
            if (!args.empty()) {
                lastValue_ = builder_.createCall("ts_encode_uri_component", {args[0]}, HIRType::makeString());
            } else {
                lastValue_ = builder_.createConstString("undefined");
            }
            return;
        }
        if (ident->name == "decodeURIComponent") {
            if (!args.empty()) {
                lastValue_ = builder_.createCall("ts_decode_uri_component", {args[0]}, HIRType::makeString());
            } else {
                lastValue_ = builder_.createConstString("undefined");
            }
            return;
        }
        if (ident->name == "encodeURI") {
            if (!args.empty()) {
                lastValue_ = builder_.createCall("ts_encode_uri", {args[0]}, HIRType::makeString());
            } else {
                lastValue_ = builder_.createConstString("undefined");
            }
            return;
        }
        if (ident->name == "decodeURI") {
            if (!args.empty()) {
                lastValue_ = builder_.createCall("ts_decode_uri", {args[0]}, HIRType::makeString());
            } else {
                lastValue_ = builder_.createConstString("undefined");
            }
            return;
        }
        if (ident->name == "escape") {
            if (!args.empty()) {
                lastValue_ = builder_.createCall("ts_escape", {args[0]}, HIRType::makeString());
            } else {
                lastValue_ = builder_.createConstString("undefined");
            }
            return;
        }
        if (ident->name == "unescape") {
            if (!args.empty()) {
                lastValue_ = builder_.createCall("ts_unescape", {args[0]}, HIRType::makeString());
            } else {
                lastValue_ = builder_.createConstString("undefined");
            }
            return;
        }

        // Not a local variable - direct function call
        // First check specializations for rest parameters - this info is available
        // even before the HIR functions are created
        HIRFunction* targetFunc = nullptr;
        std::string callName;
        bool hasRestParam = false;
        size_t restParamIndex = 0;
        std::shared_ptr<HIRType> restElemType = HIRType::makeAny();

        // Track if we found default parameters and should use the specialization's name
        bool hasDefaultParams = false;
        size_t requiredParamCount = 0;
        size_t totalParamCount = 0;
        ast::FunctionDeclaration* foundFuncNode = nullptr;

        // Look up specialization by original function name to check for rest params and default params
        if (specializations_) {
            for (const auto& spec : *specializations_) {
                if (spec.originalName == ident->name) {
                    // Found a specialization for this function
                    if (auto* funcNode = dynamic_cast<ast::FunctionDeclaration*>(spec.node)) {
                        foundFuncNode = funcNode;
                        totalParamCount = funcNode->parameters.size();

                        // Check if function has rest parameter or default parameters
                        for (size_t i = 0; i < funcNode->parameters.size(); ++i) {
                            if (funcNode->parameters[i]->isRest) {
                                hasRestParam = true;
                                restParamIndex = i;
                                // Get the element type from the parameter type annotation
                                // e.g., "...numbers: number[]" -> element type is number (Float64)
                                std::string paramType = funcNode->parameters[i]->type;
                                if (!paramType.empty()) {
                                    // Extract element type from array type (e.g., "number[]" -> "number")
                                    if (paramType.size() > 2 && paramType.substr(paramType.size() - 2) == "[]") {
                                        std::string elemTypeStr = paramType.substr(0, paramType.size() - 2);
                                        restElemType = convertTypeFromString(elemTypeStr);
                                    }
                                }
                                // Use this specialization's name - it already has the correct mangling
                                callName = spec.specializedName;
                                break;
                            }
                            // Check for default parameter
                            if (funcNode->parameters[i]->initializer) {
                                hasDefaultParams = true;
                            } else {
                                // Count required params (params before any with defaults)
                                if (!hasDefaultParams) {
                                    requiredParamCount = i + 1;
                                }
                            }
                        }

                        // If function has default params, always use the specialization name
                        // because params with defaults are now Any type
                        if (!hasRestParam && hasDefaultParams) {
                            callName = spec.specializedName;
                            // Look up the HIR function to get param types for boxing
                            for (auto& f : module_->functions) {
                                if (f->name == spec.specializedName) {
                                    targetFunc = f.get();
                                    break;
                                }
                            }
                            // Pad args with undefined for missing default params
                            if (args.size() < totalParamCount) {
                                for (size_t i = args.size(); i < totalParamCount; ++i) {
                                    args.push_back(builder_.createConstUndefined());
                                }
                            }
                        }
                    }
                    if (hasRestParam || (hasDefaultParams && !callName.empty())) break;
                }
            }
        }

        // If we didn't find a rest-parameter function or function with default params,
        // compute the mangled name based on argument types
        if (!hasRestParam && callName.empty()) {
            std::vector<std::shared_ptr<ts::Type>> argTypes;
            for (auto& arg : node->arguments) {
                argTypes.push_back(arg->inferredType ? arg->inferredType : std::make_shared<ts::Type>(ts::TypeKind::Any));
            }
            std::string mangledName = Monomorphizer::generateMangledName(ident->name, argTypes, node->resolvedTypeArguments, currentModulePath_);
            callName = mangledName;

            // Look up the function - try mangled name first, then original name
            for (auto& f : module_->functions) {
                if (f->name == mangledName) {
                    targetFunc = f.get();
                    break;
                }
            }
            // If not found with mangled name, try original name (for runtime functions etc.)
            if (!targetFunc) {
                for (auto& f : module_->functions) {
                    if (f->name == ident->name) {
                        targetFunc = f.get();
                        callName = ident->name;  // Use original name
                        break;
                    }
                }
            }
        }
        // If still not found, determine if this is a runtime function or user function
        if (!targetFunc) {
            // Check if this is a named import from an extension module
            // e.g., import { join } from 'path'; join('a', 'b')
            auto extIt = extensionImports_.find(ident->name);
            if (extIt != extensionImports_.end()) {
                const auto& [moduleName, exportedName] = extIt->second;
                auto& extReg2 = ext::ExtensionRegistry::instance();
                const ext::MethodDefinition* methodDef = extReg2.findObjectMethod(moduleName, exportedName);

                std::string runtimeFunc;
                if (methodDef && methodDef->hirName) {
                    runtimeFunc = *methodDef->hirName;
                } else {
                    runtimeFunc = "ts_" + moduleName + "_" + exportedName;
                }
                auto resultType = methodDef ? extTypeRefToHIR(methodDef->returns) : HIRType::makeAny();

                // Handle rest parameters (same logic as Case 4)
                if (methodDef) {
                    size_t restParamIndex = SIZE_MAX;
                    for (size_t i = 0; i < methodDef->params.size(); ++i) {
                        if (methodDef->params[i].rest) {
                            restParamIndex = i;
                            break;
                        }
                    }

                    if (restParamIndex != SIZE_MAX && args.size() >= restParamIndex) {
                        std::vector<std::shared_ptr<HIRValue>> packedArgs;
                        for (size_t i = 0; i < restParamIndex; ++i) {
                            packedArgs.push_back(args[i]);
                        }
                        auto restArray = builder_.createCall("ts_array_create", {}, HIRType::makeArray(HIRType::makeAny(), false));
                        for (size_t i = restParamIndex; i < args.size(); ++i) {
                            auto boxedArg = boxValueIfNeeded(args[i]);
                            builder_.createCall("ts_array_push", {restArray, boxedArg}, HIRType::makeInt64());
                        }
                        packedArgs.push_back(restArray);
                        lastValue_ = builder_.createCall(runtimeFunc, packedArgs, resultType);
                        return;
                    }
                }

                lastValue_ = builder_.createCall(runtimeFunc, args, resultType);
                return;
            }
            // Check ExtensionRegistry: if this is a registered module/object being called
            // directly (e.g., assert(true)), use its "default" method
            auto& extReg = ext::ExtensionRegistry::instance();
            if (extReg.isRegisteredModule(ident->name) || extReg.isRegisteredObject(ident->name)) {
                const ext::MethodDefinition* defaultMethod = extReg.findObjectMethod(ident->name, "default");
                if (defaultMethod) {
                    callName = defaultMethod->hirName.value_or(defaultMethod->call);
                } else {
                    callName = ident->name;  // Keep original name for registered modules
                }
            }
            // Runtime functions start with "ts_" - use original name
            // User functions should use the mangled name
            else if (ident->name.substr(0, 3) == "ts_" ||
                ident->name == "console" ||
                ident->name == "Math" ||
                ident->name == "JSON" ||
                ident->name == "parseInt" ||
                ident->name == "parseFloat" ||
                ident->name == "isNaN" ||
                ident->name == "isFinite" ||
                ident->name == "fetch" ||
                ident->name == "require") {
                callName = ident->name;  // Keep original name for runtime functions
            }
            // Otherwise keep the mangled name (already set above)
        }

        // Handle rest parameters: package excess arguments into an array
        // We use the hasRestParam flag computed from specializations_ lookup above
        if (hasRestParam) {
            std::vector<std::shared_ptr<HIRValue>> newArgs;

            // Add arguments before the rest parameter
            for (size_t i = 0; i < restParamIndex && i < args.size(); ++i) {
                newArgs.push_back(args[i]);
            }

            // Pad with undefined for missing non-rest arguments
            while (newArgs.size() < restParamIndex) {
                newArgs.push_back(builder_.createConstUndefined());
            }

            // Create the rest array
            size_t restArgsCount = (args.size() > restParamIndex) ? args.size() - restParamIndex : 0;
            auto lenVal = builder_.createConstInt(static_cast<int64_t>(restArgsCount));
            auto restArray = builder_.createNewArrayBoxed(lenVal, restElemType);

            // Add elements to the rest array
            for (size_t i = restParamIndex; i < args.size(); ++i) {
                auto idxVal = builder_.createConstInt(static_cast<int64_t>(i - restParamIndex));
                builder_.createSetElem(restArray, idxVal, args[i]);
            }

            newArgs.push_back(restArray);
            args = std::move(newArgs);
        } else if (targetFunc) {
            // If we found the function and have fewer args than params, pad with undefined
            if (args.size() < targetFunc->params.size()) {
                for (size_t i = args.size(); i < targetFunc->params.size(); ++i) {
                    args.push_back(builder_.createConstUndefined());
                }
            }
        }

        // Box arguments when target parameter is Any type but argument has concrete type
        if (targetFunc) {
            for (size_t i = 0; i < args.size() && i < targetFunc->params.size(); ++i) {
                const auto& [paramName, paramType] = targetFunc->params[i];
                if (paramType && paramType->kind == HIRTypeKind::Any) {
                    // Parameter is Any, need to box the argument if it has a concrete type
                    args[i] = boxValueIfNeeded(args[i]);
                }
            }
        }

        // For require() calls, inject the referrer path as the second argument
        // so the runtime can resolve relative paths correctly.
        // ts_require(TsValue* specifier, const char* referrerPath)
        if (callName == "require") {
            std::string referrerPath = node->sourceFile;
            if (referrerPath.empty()) {
                referrerPath = mainSourceFile_;
            }
            auto referrerVal = builder_.createConstCString(referrerPath);
            args.push_back(referrerVal);
        }

        // Determine return type from target function if available
        auto returnType = (targetFunc && targetFunc->returnType) ? targetFunc->returnType : HIRType::makeAny();
        lastValue_ = builder_.createCall(callName, args, returnType);
        return;
    }

    // Generic case - callee is an expression (IIFE, function expression, etc.)
    // Lower the callee expression to get the function/closure pointer
    auto calleeVal = lowerExpression(node->callee.get());

    // Determine return type from the callee's function type if available
    std::shared_ptr<HIRType> resultType = HIRType::makeAny();
    if (calleeVal && calleeVal->type && calleeVal->type->kind == HIRTypeKind::Function && calleeVal->type->returnType) {
        resultType = calleeVal->type->returnType;
    }

    // Call the function indirectly
    lastValue_ = builder_.createCallIndirect(calleeVal, args, resultType);
}

void ASTToHIR::visitNewExpression(ast::NewExpression* node) {
    setSourceLine(node);
    // Get constructor/class name
    auto* ident = dynamic_cast<ast::Identifier*>(node->expression.get());
    std::string className = "Object";
    if (ident) {
        // First check if this is a variable pointing to a class expression
        auto it = variableToClassName_.find(ident->name);
        if (it != variableToClassName_.end()) {
            className = it->second;  // Use the actual generated class name
        } else {
            className = ident->name;
        }
    } else if (auto* propAccess = dynamic_cast<ast::PropertyAccessExpression*>(node->expression.get())) {
        // Handle new ns.ClassName() where ns is a namespace import
        if (propAccess->expression->inferredType &&
            propAccess->expression->inferredType->kind == ts::TypeKind::Namespace) {
            className = propAccess->name;
        }
    }

    // Handle built-in Array class
    if (className == "Array") {
        // new Array() or new Array(length) or new Array(elem1, elem2, ...)
        // Try to infer element type from type parameter
        std::shared_ptr<HIRType> elemType = HIRType::makeAny();
        if (node->inferredType && node->inferredType->kind == ts::TypeKind::Array) {
            auto arrayType = std::static_pointer_cast<ts::ArrayType>(node->inferredType);
            if (arrayType->elementType) {
                elemType = convertType(arrayType->elementType);
            }
        }

        if (node->arguments.empty()) {
            // new Array() - create empty array
            auto zero = builder_.createConstInt(0);
            lastValue_ = builder_.createNewArrayBoxed(zero, elemType);
        } else if (node->arguments.size() == 1) {
            // Check if single argument is a number (length) or element
            auto& arg = node->arguments[0];
            bool isNumericArg = arg->inferredType &&
                (arg->inferredType->kind == ts::TypeKind::Double ||
                 arg->inferredType->kind == ts::TypeKind::Int);
            if (isNumericArg) {
                // new Array(length) - create array with capacity
                auto lenVal = lowerExpression(arg.get());
                lastValue_ = builder_.createNewArrayBoxed(lenVal, elemType);
            } else {
                // new Array(elem) - create array with single element
                auto zero = builder_.createConstInt(0);
                auto arr = builder_.createNewArrayBoxed(zero, elemType);
                auto elemVal = lowerExpression(arg.get());
                builder_.createCall("ts_array_push", {arr, elemVal}, HIRType::makeInt64());
                lastValue_ = arr;
            }
        } else {
            // new Array(elem1, elem2, ...) - create array with elements
            auto zero = builder_.createConstInt(0);
            auto arr = builder_.createNewArrayBoxed(zero, elemType);
            for (auto& arg : node->arguments) {
                auto elemVal = lowerExpression(arg.get());
                builder_.createCall("ts_array_push", {arr, elemVal}, HIRType::makeInt64());
            }
            lastValue_ = arr;
        }
        return;
    }

    // Handle TypedArray constructors
    if (className == "Uint8Array" || className == "Int8Array" ||
        className == "Uint8ClampedArray" || className == "Int16Array" ||
        className == "Uint16Array" || className == "Int32Array" ||
        className == "Uint32Array" || className == "Float32Array" ||
        className == "Float64Array" || className == "BigInt64Array" ||
        className == "BigUint64Array") {
        std::shared_ptr<HIRValue> lenVal;
        if (!node->arguments.empty()) {
            lenVal = lowerExpression(node->arguments[0].get());
            // Ensure length is i64 (may be f64 from numeric literal)
            if (lenVal && lenVal->type && lenVal->type->kind == HIRTypeKind::Float64) {
                lenVal = builder_.createCastF64ToI64(lenVal);
            }
        } else {
            lenVal = builder_.createConstInt(0);
        }
        auto arrType = HIRType::makeArray(HIRType::makeInt64(), true); // typed array
        // Route to appropriate runtime create function
        if (className == "Uint8Array") {
            lastValue_ = builder_.createCall("ts_typed_array_create_u8", {lenVal}, arrType);
        } else if (className == "Uint32Array") {
            lastValue_ = builder_.createCall("ts_typed_array_create_u32", {lenVal}, arrType);
        } else if (className == "Float64Array") {
            lastValue_ = builder_.createCall("ts_typed_array_create_f64", {lenVal}, arrType);
        } else if (className == "Uint8ClampedArray") {
            lastValue_ = builder_.createCall("ts_typed_array_create_u8c", {lenVal}, arrType);
        } else if (className == "Int8Array") {
            lastValue_ = builder_.createCall("ts_typed_array_create_i8", {lenVal}, arrType);
        } else if (className == "Int16Array") {
            lastValue_ = builder_.createCall("ts_typed_array_create_i16", {lenVal}, arrType);
        } else if (className == "Uint16Array") {
            lastValue_ = builder_.createCall("ts_typed_array_create_u16", {lenVal}, arrType);
        } else if (className == "Int32Array") {
            lastValue_ = builder_.createCall("ts_typed_array_create_i32", {lenVal}, arrType);
        } else if (className == "Float32Array") {
            lastValue_ = builder_.createCall("ts_typed_array_create_f32", {lenVal}, arrType);
        } else if (className == "BigInt64Array") {
            lastValue_ = builder_.createCall("ts_typed_array_create_i64", {lenVal}, arrType);
        } else if (className == "BigUint64Array") {
            lastValue_ = builder_.createCall("ts_typed_array_create_u64", {lenVal}, arrType);
        }
        return;
    }

    // Handle built-in Map class
    if (className == "Map") {
        lastValue_ = builder_.createCall("ts_map_create_explicit", {}, HIRType::makeMap());
        return;
    }

    // Handle built-in Set class
    if (className == "Set") {
        lastValue_ = builder_.createCall("ts_set_create", {}, HIRType::makeSet());
        return;
    }

    // Handle built-in WeakMap class (implemented as Map wrapper)
    if (className == "WeakMap") {
        lastValue_ = builder_.createCall("ts_map_create", {}, HIRType::makeMap());
        return;
    }

    // Handle built-in WeakSet class (implemented as Set wrapper with distinct magic)
    if (className == "WeakSet") {
        lastValue_ = builder_.createCall("ts_weakset_create", {}, HIRType::makeSet());
        return;
    }

    // Handle built-in WeakRef class
    if (className == "WeakRef") {
        if (!node->arguments.empty()) {
            auto target = lowerExpression(node->arguments[0].get());
            lastValue_ = builder_.createCall("ts_weakref_create", {target}, HIRType::makeClass("WeakRef", 0));
        } else {
            lastValue_ = builder_.createCall("ts_weakref_create",
                {builder_.createConstNull()}, HIRType::makeClass("WeakRef", 0));
        }
        return;
    }

    // Handle built-in FinalizationRegistry class
    if (className == "FinalizationRegistry") {
        if (!node->arguments.empty()) {
            auto callback = lowerExpression(node->arguments[0].get());
            lastValue_ = builder_.createCall("ts_finalization_registry_create", {callback},
                HIRType::makeClass("FinalizationRegistry", 0));
        } else {
            lastValue_ = builder_.createCall("ts_finalization_registry_create",
                {builder_.createConstNull()}, HIRType::makeClass("FinalizationRegistry", 0));
        }
        return;
    }

    // Handle built-in RegExp class
    if (className == "RegExp") {
        std::shared_ptr<HIRValue> patternArg;
        std::shared_ptr<HIRValue> flagsArg;
        if (!node->arguments.empty()) {
            patternArg = lowerExpression(node->arguments[0].get());
        } else {
            patternArg = builder_.createConstString("");
        }
        if (node->arguments.size() >= 2) {
            flagsArg = lowerExpression(node->arguments[1].get());
        } else {
            flagsArg = builder_.createConstNull();
        }
        lastValue_ = builder_.createCall("ts_regexp_create", {patternArg, flagsArg}, HIRType::makeObject());
        return;
    }

    // Handle built-in Date class
    if (className == "Date") {
        if (node->arguments.empty()) {
            // new Date() - current time
            lastValue_ = builder_.createCall("ts_date_create", {}, HIRType::makeClass("Date", 0));
        } else if (node->arguments.size() == 1) {
            auto arg = lowerExpression(node->arguments[0].get());
            auto& argNode = node->arguments[0];
            bool isNumericArg = false;
            if (argNode->inferredType &&
                (argNode->inferredType->kind == ts::TypeKind::Double ||
                 argNode->inferredType->kind == ts::TypeKind::Int)) {
                isNumericArg = true;
            } else if (dynamic_cast<ast::NumericLiteral*>(argNode.get())) {
                isNumericArg = true;
            }
            if (isNumericArg) {
                // new Date(milliseconds)
                lastValue_ = builder_.createCall("ts_date_create_ms", {arg}, HIRType::makeClass("Date", 0));
            } else {
                // new Date(dateString)
                lastValue_ = builder_.createCall("ts_date_create_str", {arg}, HIRType::makeClass("Date", 0));
            }
        } else {
            // new Date() with multiple args - just use current time for now
            lastValue_ = builder_.createCall("ts_date_create", {}, HIRType::makeClass("Date", 0));
        }
        return;
    }

    // Handle built-in Error classes
    if (className == "Error" || className == "TypeError" || className == "RangeError" ||
        className == "ReferenceError" || className == "SyntaxError" || className == "URIError" ||
        className == "EvalError" || className == "AggregateError") {
        // new Error(message) or new Error(message, { cause: ... })
        std::shared_ptr<HIRValue> message;
        if (!node->arguments.empty()) {
            message = lowerExpression(node->arguments[0].get());
        } else {
            // Create empty string
            message = builder_.createConstString("");
        }

        // Call ts_error_create (returns already-boxed TsValue*)
        if (node->arguments.size() >= 2) {
            // ES2022: Error with options { cause: ... }
            auto options = lowerExpression(node->arguments[1].get());
            lastValue_ = builder_.createCall("ts_error_create_with_options", {message, options}, HIRType::makeAny());
        } else {
            lastValue_ = builder_.createCall("ts_error_create", {message}, HIRType::makeAny());
        }
        return;
    }

    // TextEncoder() - no arguments
    if (className == "TextEncoder") {
        lastValue_ = builder_.createCall("ts_text_encoder_create", {}, HIRType::makeObject());
        return;
    }

    // TextDecoder(label?, options?)
    if (className == "TextDecoder") {
        std::vector<std::shared_ptr<HIRValue>> decoderArgs;
        if (!node->arguments.empty()) {
            decoderArgs.push_back(lowerExpression(node->arguments[0].get()));
        } else {
            decoderArgs.push_back(builder_.createConstNull());
        }
        // fatal and ignoreBOM default to false
        auto falseVal = builder_.createConstBool(false);
        decoderArgs.push_back(falseVal);
        decoderArgs.push_back(falseVal);
        if (node->arguments.size() >= 2) {
            // TODO: extract fatal and ignoreBOM from options object
        }
        lastValue_ = builder_.createCall("ts_text_decoder_create", decoderArgs, HIRType::makeObject());
        return;
    }

    // Lower constructor arguments
    std::vector<std::shared_ptr<HIRValue>> args;
    for (auto& arg : node->arguments) {
        args.push_back(lowerExpression(arg.get()));
    }

    // Look up the class - prefer one with constructor set (handles duplicate
    // HIRClass from spec pre-pass vs visitClassDeclaration)
    HIRClass* hirClass = nullptr;
    for (auto& cls : module_->classes) {
        if (cls->name == className) {
            hirClass = cls.get();
            if (hirClass->constructor) break;  // Found one with constructor
        }
    }
    {
        int count = 0;
        for (auto& cls : module_->classes) {
            if (cls->name == className) {
                SPDLOG_WARN("visitNewExpression: class[{}]={} ctor={} methods={} shape={}",
                    count++, className,
                    cls->constructor ? cls->constructor->name : "null",
                    cls->methods.size(), cls->shape ? "yes" : "no");
            }
        }
    }

    // Check if this is an extension type with a constructor (e.g., URL, URLSearchParams)
    if (!hirClass) {
        auto& extReg = ext::ExtensionRegistry::instance();
        const ext::TypeDefinition* extType = extReg.findType(className);
        if (extType && extType->constructor && !extType->constructor->call.empty()) {
            // Extension type with a constructor - call the factory function directly
            std::string hirName = extType->constructor->hirName
                ? *extType->constructor->hirName
                : extType->constructor->call;

            // The constructor is a static factory function that returns the object
            lastValue_ = builder_.createCall(hirName, args, HIRType::makePtr());
            return;
        }
    }

    // Create new object with the correct type
    std::shared_ptr<HIRValue> newObj;
    if (hirClass && hirClass->shape && hirClass->shape->id != UINT32_MAX) {
        // Use flat object layout for class instances with registered shapes
        newObj = builder_.createNewObjectDynamic(hirClass->shape.get());
        // Set type to Class so codegen can find the vtable
        if (newObj && newObj->type) {
            newObj->type = HIRType::makeClass(className, hirClass->shape->id);
        }
    } else if (hirClass && hirClass->shape) {
        // Class with shape but no properties (no flat object)
        newObj = builder_.createNewObject(hirClass->shape.get());
    } else if (!hirClass && ident) {
        // Unknown class - treat as a constructor function call
        // (e.g., function EventEmitter() {...} from an imported JS module)
        // Use lowerExpression on the identifier to get the function value
        auto constructorVal = lowerExpression(node->expression.get());
        if (constructorVal) {
            // Use ts_new_from_constructor to properly set up prototype chain
            // and call the constructor function with this = new object
            std::string funcName;
            std::vector<std::shared_ptr<HIRValue>> callArgs;
            callArgs.push_back(constructorVal);

            size_t numArgs = args.size();
            if (numArgs <= 8) {
                funcName = "ts_new_from_constructor_" + std::to_string(numArgs);
                for (size_t i = 0; i < numArgs; i++) {
                    callArgs.push_back(args[i]);
                }
            } else {
                // Cap at 8 args, drop extras
                SPDLOG_WARN("Constructor call with {} args, capping at 8", numArgs);
                funcName = "ts_new_from_constructor_8";
                for (size_t i = 0; i < 8; i++) {
                    callArgs.push_back(args[i]);
                }
            }

            lastValue_ = builder_.createCall(funcName, callArgs, HIRType::makeAny());
            return;
        }
        // Expression couldn't be lowered, fall back to plain dynamic object
        newObj = builder_.createNewObjectDynamic();
    } else {
        // Fallback to dynamic object (for built-in or unknown classes)
        newObj = builder_.createNewObjectDynamic();
    }

    // Propagate escape analysis from AST
    if (!node->escapes) {
        builder_.markLastNonEscaping();
    }

    if (hirClass && hirClass->constructor) {
        // Build constructor call args: [this, ...args]
        std::vector<std::shared_ptr<HIRValue>> ctorArgs;
        ctorArgs.push_back(newObj);  // 'this' is the new object
        for (auto& arg : args) {
            ctorArgs.push_back(arg);
        }

        // Call the constructor
        builder_.createCall(hirClass->constructor->name, ctorArgs, HIRType::makeVoid());
    } else if (hirClass && !hirClass->constructor && specializations_) {
        // The HIRClass was created (e.g., by pre-pass for imported classes) but the
        // constructor function hasn't been generated yet. Look through specializations
        // to find the constructor and emit the call by name.
        std::string ctorName;
        for (const auto& spec : *specializations_) {
            if (spec.originalName == "constructor" && spec.classType) {
                auto ct = std::dynamic_pointer_cast<ts::ClassType>(spec.classType);
                if (ct && ct->name == className) {
                    ctorName = spec.specializedName;
                    SPDLOG_WARN("visitNewExpression: found ctor spec '{}' for class '{}'",
                        ctorName, className);
                    break;
                }
            }
        }
        if (ctorName.empty()) {
            SPDLOG_WARN("visitNewExpression: NO ctor spec found for '{}' in {} specializations",
                className, specializations_->size());
            for (const auto& spec : *specializations_) {
                if (spec.classType) {
                    auto ct = std::dynamic_pointer_cast<ts::ClassType>(spec.classType);
                    if (ct && ct->name == className) {
                        SPDLOG_WARN("  spec: original='{}' specialized='{}' class='{}'",
                            spec.originalName, spec.specializedName, ct->name);
                    }
                }
            }
        }
        if (!ctorName.empty()) {
            std::vector<std::shared_ptr<HIRValue>> ctorArgs;
            ctorArgs.push_back(newObj);  // 'this' is the new object
            for (auto& arg : args) {
                ctorArgs.push_back(arg);
            }
            // Check if this is a JS slow-path constructor (from .js file).
            // Only JS constructors can return objects per [[Construct]] semantics.
            // Typed TS constructors always return void.
            bool isJsConstructor = false;
            if (node->expression) {
                // Check if the class declaration comes from a .js file
                for (const auto& spec : *specializations_) {
                    if (spec.specializedName == ctorName && spec.node) {
                        auto sf = spec.node->sourceFile;
                        if (sf.size() >= 3 && sf.substr(sf.size() - 3) == ".js") {
                            isJsConstructor = true;
                        }
                        break;
                    }
                }
            }

            if (isJsConstructor) {
                // Call with ptr return type — per JS spec, if a constructor
                // returns an object, 'new' uses that object instead of 'this'.
                auto ctorResult = builder_.createCall(ctorName, ctorArgs, HIRType::makeAny());
                if (ctorResult) {
                    auto isUndef = builder_.createCall("ts_value_is_undefined",
                        {ctorResult}, HIRType::makeBool());
                    int blockId = blockCounter_++;
                    auto* useCtor = builder_.createBlock("new_ctor_ret_" + std::to_string(blockId));
                    auto* useThis = builder_.createBlock("new_use_this_" + std::to_string(blockId));
                    auto* mergeNew = builder_.createBlock("new_merge_" + std::to_string(blockId));
                    builder_.createCondBranch(isUndef, useThis, useCtor);

                    builder_.setInsertPoint(useCtor);
                    currentBlock_ = useCtor;
                    builder_.createBranch(mergeNew);

                    builder_.setInsertPoint(useThis);
                    currentBlock_ = useThis;
                    builder_.createBranch(mergeNew);

                    builder_.setInsertPoint(mergeNew);
                    currentBlock_ = mergeNew;
                    newObj = builder_.createPhi(HIRType::makeAny(),
                        {{ctorResult, useCtor}, {newObj, useThis}});
                }
            } else {
                // Typed TS constructor — always void, always use 'this'
                builder_.createCall(ctorName, ctorArgs, HIRType::makeVoid());
            }
        }
    }

    // The result is the new object (or the constructor's return value)
    lastValue_ = newObj;
}

void ASTToHIR::visitParenthesizedExpression(ast::ParenthesizedExpression* node) {
    setSourceLine(node);
    if (node->expression) {
        lastValue_ = lowerExpression(node->expression.get());
    }
}

void ASTToHIR::visitArrayLiteralExpression(ast::ArrayLiteralExpression* node) {
    setSourceLine(node);
    // Try to infer element type from the array's inferred type
    std::shared_ptr<HIRType> elemType = HIRType::makeAny();
    if (node->inferredType && node->inferredType->kind == ts::TypeKind::Array) {
        auto arrayType = std::static_pointer_cast<ts::ArrayType>(node->inferredType);
        if (arrayType->elementType) {
            elemType = convertType(arrayType->elementType);
        }
    }

    // Check if we have any spread elements - if so, we need dynamic approach
    bool hasSpread = false;
    for (auto& elem : node->elements) {
        if (dynamic_cast<ast::SpreadElement*>(elem.get())) {
            hasSpread = true;
            break;
        }
    }

    if (hasSpread) {
        // With spread elements, use ts_array_create and dynamic push/concat
        auto arr = builder_.createCall("ts_array_create", {}, HIRType::makeArray(elemType, false));

        for (auto& elem : node->elements) {
            if (auto* spread = dynamic_cast<ast::SpreadElement*>(elem.get())) {
                // Spread element: concatenate the spread array
                // ts_array_concat returns a NEW array, so we must capture it
                auto spreadArr = lowerExpression(spread->expression.get());
                arr = builder_.createCall("ts_array_concat", {arr, spreadArr}, HIRType::makeArray(elemType, false));
            } else {
                // Regular element: push it
                auto elemVal = lowerExpression(elem.get());
                builder_.createCall("ts_array_push", {arr, elemVal}, HIRType::makeInt64());
            }
        }

        lastValue_ = arr;
    } else {
        // No spread elements - use efficient pre-allocated array
        auto lenVal = builder_.createConstInt(static_cast<int64_t>(node->elements.size()));
        auto arr = builder_.createNewArrayBoxed(lenVal, elemType);

        int64_t idx = 0;
        for (auto& elem : node->elements) {
            auto elemVal = lowerExpression(elem.get());
            auto idxVal = builder_.createConstInt(idx++);
            builder_.createSetElem(arr, idxVal, elemVal);
        }

        lastValue_ = arr;
    }
}

void ASTToHIR::visitElementAccessExpression(ast::ElementAccessExpression* node) {
    setSourceLine(node);
    // Check for enum reverse mapping: EnumName[numericValue]
    auto* classNameIdent = dynamic_cast<ast::Identifier*>(node->expression.get());
    if (classNameIdent) {
        auto enumReverseIt = enumReverseMap_.find(classNameIdent->name);
        if (enumReverseIt != enumReverseMap_.end()) {
            // This is an enum reverse mapping access
            if (auto* numLit = dynamic_cast<ast::NumericLiteral*>(node->argumentExpression.get())) {
                // Constant index - look up at compile time
                int64_t idx = static_cast<int64_t>(numLit->value);
                auto memberIt = enumReverseIt->second.find(idx);
                if (memberIt != enumReverseIt->second.end()) {
                    lastValue_ = builder_.createConstString(memberIt->second);
                    return;
                }
            }
            // Dynamic index - need runtime lookup (TODO: generate runtime object for dynamic access)
            // For now, fall through to dynamic access
        }
    }

    auto obj = lowerExpression(node->expression.get());

    // Handle optional chaining: obj?.[idx]
    if (node->isOptional) {
        // Check if obj is nullish
        auto isNullish = builder_.createCall("ts_value_is_nullish", {obj}, HIRType::makeBool());

        // Create undefined value before branching (so it's in the current block)
        auto undef = builder_.createConstUndefined();

        // Create blocks for conditional access (with unique names)
        int blockId = blockCounter_++;
        auto* accessBlock = builder_.createBlock("opt_access_" + std::to_string(blockId));
        auto* mergeBlock = builder_.createBlock("opt_merge_" + std::to_string(blockId));

        // Branch based on nullish check
        auto* currentBlock = builder_.getInsertBlock();
        builder_.createCondBranch(isNullish, mergeBlock, accessBlock);

        // Access block: perform the element access
        builder_.setInsertPoint(accessBlock);
        auto idx = lowerExpression(node->argumentExpression.get());
        auto accessResult = builder_.createGetElem(obj, idx);
        auto* finalAccessBlock = builder_.getInsertBlock();
        builder_.createBranch(mergeBlock);

        // Merge block: phi node to select result
        builder_.setInsertPoint(mergeBlock);
        std::vector<std::pair<std::shared_ptr<HIRValue>, HIRBlock*>> phiIncoming;
        phiIncoming.push_back(std::make_pair(undef, currentBlock));
        phiIncoming.push_back(std::make_pair(accessResult, finalAccessBlock));
        lastValue_ = builder_.createPhi(HIRType::makeAny(), phiIncoming);
        return;
    }

    auto idx = lowerExpression(node->argumentExpression.get());
    lastValue_ = builder_.createGetElem(obj, idx);
}

void ASTToHIR::visitPropertyAccessExpression(ast::PropertyAccessExpression* node) {
    setSourceLine(node);
    // Check for static property access: ClassName.propertyName
    auto* classNameIdent = dynamic_cast<ast::Identifier*>(node->expression.get());
    if (classNameIdent) {
        // Check for enum member access: EnumName.MemberName
        auto enumIt = enumValues_.find(classNameIdent->name);
        if (enumIt != enumValues_.end()) {
            auto memberIt = enumIt->second.find(node->name);
            if (memberIt != enumIt->second.end()) {
                const EnumValue& ev = memberIt->second;
                if (ev.isString) {
                    lastValue_ = builder_.createConstString(ev.strValue);
                } else {
                    // Use float64 for consistency with JS number semantics
                    lastValue_ = builder_.createConstFloat(static_cast<double>(ev.numValue));
                }
                return;
            }
        }

        for (auto& cls : module_->classes) {
            if (cls->name == classNameIdent->name) {
                // Check if this is a static property
                std::string globalName = cls->name + "_static_" + node->name;
                auto it = staticPropertyGlobals_.find(globalName);
                if (it != staticPropertyGlobals_.end()) {
                    // Load from the static property global
                    auto globalPtr = it->second.first;
                    auto propType = it->second.second;
                    lastValue_ = builder_.createLoad(propType, globalPtr);
                    return;
                }
                break;
            }
        }

        // Check for namespace property access: ns.prop where ns is a namespace import
        // Only intercept for user-defined modules; extension modules fall through
        // to normal dispatch via lowerExpression + extension registry.
        if (classNameIdent->inferredType &&
            classNameIdent->inferredType->kind == ts::TypeKind::Namespace) {

            // Check specializations first (always complete, not affected by processing order)
            if (specializations_) {
                for (const auto& spec : *specializations_) {
                    if (spec.originalName == node->name || spec.specializedName == node->name) {
                        auto funcType = HIRType::makeFunction();
                        lastValue_ = builder_.createLoadFunction(spec.specializedName, funcType);
                        return;
                    }
                }
            }

            // Check already-processed HIR functions
            for (const auto& func : module_->functions) {
                if (func->name == node->name) {
                    auto funcType = HIRType::makeFunction();
                    funcType->returnType = func->returnType;
                    for (const auto& param : func->params) {
                        funcType->paramTypes.push_back(param.second);
                    }
                    lastValue_ = builder_.createLoadFunction(node->name, funcType);
                    return;
                }
            }

            // Check for module-level globals (exported variables)
            std::string globalName = modVarName(node->name);
            auto globalVar = lookupVariable(globalName);
            if (globalVar) {
                lastValue_ = globalVar;
                return;
            }

            // Check for enum member access through namespace
            for (const auto& enumPair : enumValues_) {
                auto memberIt = enumPair.second.find(node->name);
                if (memberIt != enumPair.second.end()) {
                    const EnumValue& ev = memberIt->second;
                    if (ev.isString) {
                        lastValue_ = builder_.createConstString(ev.strValue);
                    } else {
                        lastValue_ = builder_.createConstFloat(static_cast<double>(ev.numValue));
                    }
                    return;
                }
            }

            // If nothing found, fall through to normal dispatch
            // (extension modules are handled via lowerExpression + extension registry)
        }
    }

    auto obj = lowerExpression(node->expression.get());

    // Determine the property type - check if this is 'this' access in a class context
    std::shared_ptr<HIRType> propType = HIRType::makeAny();

    // Special handling for built-in type properties
    if (node->expression && node->expression->inferredType) {
        auto exprType = node->expression->inferredType;

        // Array.length returns a number - call ts_array_length directly
        if (exprType->kind == ts::TypeKind::Array && node->name == "length") {
            lastValue_ = builder_.createCall("ts_array_length", {obj}, HIRType::makeInt64());
            return;
        }
        // String.length returns a number - call ts_string_length directly
        else if (exprType->kind == ts::TypeKind::String && node->name == "length") {
            lastValue_ = builder_.createCall("ts_string_length", {obj}, HIRType::makeInt64());
            return;
        }
    }

    if (currentClass_) {
        // Check if the expression is 'this'
        auto* thisIdent = dynamic_cast<ast::Identifier*>(node->expression.get());
        if (thisIdent && thisIdent->name == "this" && currentClass_->shape) {
            // Look up the property type from the class shape
            auto typeIt = currentClass_->shape->propertyTypes.find(node->name);
            if (typeIt != currentClass_->shape->propertyTypes.end()) {
                propType = typeIt->second;
            }
        }
    }

    // Check for getter: look up the class type and see if it has __getter_<propName>
    HIRClass* targetClass = nullptr;

    // First, check if expression has an inferred class type
    if (node->expression && node->expression->inferredType) {
        auto exprType = node->expression->inferredType;
        if (exprType->kind == ts::TypeKind::Class) {
            auto classType = std::dynamic_pointer_cast<ts::ClassType>(exprType);
            if (classType) {
                // Find the HIRClass by name
                for (auto& cls : module_->classes) {
                    if (cls->name == classType->name) {
                        targetClass = cls.get();
                        break;
                    }
                }
            }
        }
    }

    // If accessing 'this', use currentClass_
    if (!targetClass) {
        auto* thisIdent = dynamic_cast<ast::Identifier*>(node->expression.get());
        if (thisIdent && thisIdent->name == "this" && currentClass_) {
            targetClass = currentClass_;
        }
    }

    // Check if the target class has a getter for this property
    if (targetClass) {
        std::string getterKey = "__getter_" + node->name;
        auto getterIt = targetClass->methods.find(getterKey);
        if (getterIt != targetClass->methods.end()) {
            // Found a getter - call it instead of direct property access
            HIRFunction* getterFunc = getterIt->second;
            auto returnType = getterFunc->returnType ? getterFunc->returnType : HIRType::makeAny();
            lastValue_ = builder_.createCall(getterFunc->name, {obj}, returnType);
            return;
        }
    }

    // Check ExtensionRegistry for property getters on extension-defined classes
    // (e.g., http2Session.destroyed, http2Stream.pending, buf.length)
    // Only match properties that have both a getter AND a lowering spec (actual runtime function).
    if (!targetClass && node->expression && node->expression->inferredType) {
        auto exprType = node->expression->inferredType;
        if (exprType->kind == ts::TypeKind::Class) {
            auto classType = std::dynamic_pointer_cast<ts::ClassType>(exprType);
            if (classType) {
                auto& extReg = ext::ExtensionRegistry::instance();
                const ext::PropertyDefinition* propDef = extReg.findProperty(classType->name, node->name);
                if (propDef && propDef->getter && propDef->lowering) {
                    // Property has a getter function with lowering spec - emit a call to it
                    std::string getterFunc = *propDef->getter;
                    auto retType = extTypeRefToHIR(propDef->type);
                    lastValue_ = builder_.createCall(getterFunc, {obj}, retType);
                    return;
                }
            }
        }
    }

    // Check ExtensionRegistry for property getters on module-level objects
    // (e.g., http.STATUS_CODES, http.METHODS)
    if (node->expression) {
        auto* ident = dynamic_cast<ast::Identifier*>(node->expression.get());
        if (ident) {
            auto& extReg = ext::ExtensionRegistry::instance();
            const ext::PropertyDefinition* propDef = extReg.findObjectProperty(ident->name, node->name);
            if (propDef && propDef->getter && propDef->lowering) {
                std::string getterFunc = *propDef->getter;
                // Map the lowering return type to the correct HIR type
                std::shared_ptr<HIRType> retType;
                switch (propDef->lowering->returns) {
                    case ext::LoweringType::I32:
                    case ext::LoweringType::I1:
                        retType = HIRType::makeBool();
                        break;
                    case ext::LoweringType::I64:
                        retType = HIRType::makeInt64();
                        break;
                    case ext::LoweringType::F64:
                        retType = HIRType::makeFloat64();
                        break;
                    case ext::LoweringType::Void:
                        retType = HIRType::makeVoid();
                        break;
                    default:
                        retType = HIRType::makeAny();
                        break;
                }
                lastValue_ = builder_.createCall(getterFunc, {}, retType);
                return;
            }
        }

        // Check for nested object property getters (e.g., path.posix.sep, path.win32.delimiter)
        auto* propAccess = dynamic_cast<ast::PropertyAccessExpression*>(node->expression.get());
        if (propAccess) {
            auto* parentIdent = dynamic_cast<ast::Identifier*>(propAccess->expression.get());
            if (parentIdent) {
                auto& extReg = ext::ExtensionRegistry::instance();
                const ext::PropertyDefinition* propDef = extReg.findNestedObjectProperty(
                    parentIdent->name, propAccess->name, node->name);
                if (propDef && propDef->getter && propDef->lowering) {
                    std::string getterFunc = *propDef->getter;
                    std::shared_ptr<HIRType> retType;
                    switch (propDef->lowering->returns) {
                        case ext::LoweringType::I32:
                        case ext::LoweringType::I1:
                            retType = HIRType::makeBool();
                            break;
                        case ext::LoweringType::I64:
                            retType = HIRType::makeInt64();
                            break;
                        case ext::LoweringType::F64:
                            retType = HIRType::makeFloat64();
                            break;
                        case ext::LoweringType::Void:
                            retType = HIRType::makeVoid();
                            break;
                        default:
                            retType = HIRType::makeAny();
                            break;
                    }
                    lastValue_ = builder_.createCall(getterFunc, {}, retType);
                    return;
                }
            }
        }
    }

    // Handle optional chaining: obj?.prop
    if (node->isOptional) {
        // Check if obj is nullish
        auto isNullish = builder_.createCall("ts_value_is_nullish", {obj}, HIRType::makeBool());

        // Create undefined value before branching (so it's in the current block)
        auto undef = builder_.createConstUndefined();

        // Create blocks for conditional access (with unique names)
        int blockId = blockCounter_++;
        auto* accessBlock = builder_.createBlock("opt_access_" + std::to_string(blockId));
        auto* mergeBlock = builder_.createBlock("opt_merge_" + std::to_string(blockId));

        // Branch based on nullish check
        auto* currentBlock = builder_.getInsertBlock();
        builder_.createCondBranch(isNullish, mergeBlock, accessBlock);

        // Access block: perform the property access
        builder_.setInsertPoint(accessBlock);
        auto accessResult = builder_.createGetPropStatic(obj, node->name, propType);
        auto* finalAccessBlock = builder_.getInsertBlock();
        builder_.createBranch(mergeBlock);

        // Merge block: phi node to select result
        builder_.setInsertPoint(mergeBlock);
        std::vector<std::pair<std::shared_ptr<HIRValue>, HIRBlock*>> phiIncoming;
        phiIncoming.push_back(std::make_pair(undef, currentBlock));
        phiIncoming.push_back(std::make_pair(accessResult, finalAccessBlock));
        lastValue_ = builder_.createPhi(HIRType::makeAny(), phiIncoming);
        return;
    }

    lastValue_ = builder_.createGetPropStatic(obj, node->name, propType);
}

void ASTToHIR::visitObjectLiteralExpression(ast::ObjectLiteralExpression* node) {
    setSourceLine(node);
    // Pre-scan: check if ALL properties are static string names (eligible for flat object)
    HIRShape* flatShape = nullptr;
    bool allStatic = true;
    std::vector<std::string> propNames;

    for (auto& prop : node->properties) {
        if (dynamic_cast<ast::SpreadElement*>(prop.get())) {
            allStatic = false;
            break;
        }
        if (dynamic_cast<ast::MethodDefinition*>(prop.get())) {
            allStatic = false;
            break;
        }
        if (auto* pa = dynamic_cast<ast::PropertyAssignment*>(prop.get())) {
            if (pa->name.empty()) {
                allStatic = false;
                break;
            }
            propNames.push_back(pa->name);
        } else if (auto* spa = dynamic_cast<ast::ShorthandPropertyAssignment*>(prop.get())) {
            if (spa->name.empty()) {
                allStatic = false;
                break;
            }
            propNames.push_back(spa->name);
        } else {
            allStatic = false;
            break;
        }
    }

    if (allStatic && !propNames.empty()) {
        auto shape = std::make_shared<HIRShape>();
        shape->id = nextShapeId_++;
        for (uint32_t i = 0; i < (uint32_t)propNames.size(); i++) {
            shape->propertyOffsets[propNames[i]] = i;
        }
        shape->size = 16 + (uint32_t)propNames.size() * 8 + 8;
        flatShape = shape.get();
        module_->shapes.push_back(shape);
    }

    auto obj = builder_.createNewObjectDynamic(flatShape);

    for (auto& prop : node->properties) {
        // Handle spread element: {...other}
        if (auto* spread = dynamic_cast<ast::SpreadElement*>(prop.get())) {
            auto spreadObj = lowerExpression(spread->expression.get());
            // Use ts_object_assign to copy properties from spreadObj to obj
            builder_.createCall("ts_object_assign", {obj, spreadObj}, HIRType::makeAny());
            continue;
        }

        // Handle MethodDefinition (including getters/setters) specially
        if (auto* method = dynamic_cast<ast::MethodDefinition*>(prop.get())) {
            // Create a function for the method
            auto funcValue = lowerMethodDefinitionToFunction(method);

            // Determine the property key
            std::string keyName;
            if (auto* id = dynamic_cast<ast::Identifier*>(method->nameNode.get())) {
                if (method->isGetter) {
                    keyName = "__getter_" + id->name;
                } else if (method->isSetter) {
                    keyName = "__setter_" + id->name;
                } else {
                    keyName = id->name;
                }
            } else if (!method->name.empty()) {
                if (method->isGetter) {
                    keyName = "__getter_" + method->name;
                } else if (method->isSetter) {
                    keyName = "__setter_" + method->name;
                } else {
                    keyName = method->name;
                }
            }

            if (!keyName.empty() && funcValue) {
                builder_.createSetPropStatic(obj, keyName, funcValue);
            }
        } else {
            // Save the object before visiting property (which may overwrite lastValue_)
            lastValue_ = obj;
            prop->accept(this);
        }
    }

    // Ensure lastValue_ is the object after all properties are set
    lastValue_ = obj;
}

void ASTToHIR::visitPropertyAssignment(ast::PropertyAssignment* node) {
    setSourceLine(node);
    // Save the object before lowerExpression overwrites lastValue_
    auto obj = lastValue_;

    auto val = lowerExpression(node->initializer.get());

    // PropertyAssignment has name (string) directly
    std::string propName = node->name;

    if (!propName.empty() && obj) {
        builder_.createSetPropStatic(obj, propName, val);
    }

    // Restore lastValue_ to the object for any subsequent properties
    lastValue_ = obj;
}

void ASTToHIR::visitShorthandPropertyAssignment(ast::ShorthandPropertyAssignment* node) {
    setSourceLine(node);
    // Save the object before any potential modification to lastValue_
    auto obj = lastValue_;

    // Check if this is a captured variable from an outer function first
    // (same logic as visitIdentifier - lookupVariable alone doesn't detect captures)
    std::shared_ptr<HIRValue> val;
    size_t scopeIndex = 0;
    if (currentFunction_ && isCapturedVariable(node->name, &scopeIndex)) {
        auto* info = lookupVariableInfo(node->name);
        if (info) {
            auto type = info->elemType ? info->elemType : (info->value ? info->value->type : HIRType::makeAny());
            registerCapture(node->name, type, scopeIndex);
            currentFunction_->hasClosure = true;
            val = builder_.createLoadCapture(node->name, type);
        }
    }
    // Also check module globals (same as visitIdentifier)
    if (!val && currentFunction_ && isModuleGlobalVar(node->name)) {
        size_t si = 0;
        if (isCapturedVariable(node->name, &si)) {
            std::string globalName = modVarName(node->name);
            auto type = module_->globals.count(globalName) ? module_->globals[globalName] : HIRType::makeAny();
            val = builder_.createLoadGlobalTyped(globalName, type);
        }
    }
    if (!val)
        val = lookupVariable(node->name);
    if (!val) {
        // Variable not found - check if it's a function name in the module
        for (const auto& func : module_->functions) {
            if (func->name == node->name) {
                // Found a function with this name - load it as a function value
                auto funcType = HIRType::makeFunction();
                funcType->returnType = func->returnType;
                for (const auto& param : func->params) {
                    funcType->paramTypes.push_back(param.second);
                }
                val = builder_.createLoadFunction(node->name, funcType);
                break;
            }
        }

        // Also check specializations - functions might be pending compilation
        if (!val && specializations_) {
            for (const auto& spec : *specializations_) {
                if (spec.originalName == node->name || spec.specializedName == node->name) {
                    // Found a function declaration - use LoadFunction
                    auto funcType = HIRType::makeFunction();
                    if (auto* funcNode = dynamic_cast<ast::FunctionDeclaration*>(spec.node)) {
                        if (!funcNode->returnType.empty()) {
                            funcType->returnType = convertTypeFromString(funcNode->returnType);
                        }
                        for (const auto& param : funcNode->parameters) {
                            auto paramType = param->type.empty()
                                ? HIRType::makeAny()
                                : convertTypeFromString(param->type);
                            funcType->paramTypes.push_back(paramType);
                        }
                    }
                    val = builder_.createLoadFunction(spec.specializedName, funcType);
                    break;
                }
            }
        }

        // If still not found, create undefined
        if (!val) {
            val = createValue(HIRType::makeAny());
            builder_.createConstUndefined(val);
        }
    }

    if (obj) {
        builder_.createSetPropStatic(obj, node->name, val);
    }

    // Restore lastValue_ to the object for any subsequent properties
    lastValue_ = obj;
}

void ASTToHIR::visitComputedPropertyName(ast::ComputedPropertyName* node) {
    setSourceLine(node);
    if (node->expression) {
        lowerExpression(node->expression.get());
    }
}

void ASTToHIR::visitMethodDefinition(ast::MethodDefinition* node) {
    setSourceLine(node);
    // Methods are handled during class lowering
}

void ASTToHIR::visitStaticBlock(ast::StaticBlock* node) {
    setSourceLine(node);
    for (auto& stmt : node->body) {
        lowerStatement(stmt.get());
    }
}

void ASTToHIR::visitIdentifier(ast::Identifier* node) {
    setSourceLine(node);
    if (node->name == "Object" || node->name == "String") {
        SPDLOG_DEBUG("[IDENT-TOP] name={} func={}", node->name, currentFunction_ ? currentFunction_->name : "null");
    }
    // Handle 'this' keyword specially
    if (node->name == "this") {
        // Check if 'this' is a captured variable from an outer function
        // (e.g., arrow functions in class methods capturing lexical this)
        size_t scopeIndex = 0;
        if (currentFunction_ && isCapturedVariable("this", &scopeIndex)) {
            auto* info = lookupVariableInfo("this");
            if (info) {
                auto type = info->elemType ? info->elemType : (info->value ? info->value->type : HIRType::makeAny());
                registerCapture("this", type, scopeIndex);
                currentFunction_->hasClosure = true;
                lastValue_ = builder_.createLoadCapture("this", type);
                return;
            }
        }
        // Not captured - look up 'this' in the variable scope
        lastValue_ = lookupVariable("this");
        if (lastValue_) {
            return;
        }
        // If not found in scope, check the dynamic this context
        // (set by Function.prototype.call/apply)
        lastValue_ = builder_.createCall("ts_get_call_this", {}, HIRType::makeAny());
        return;
    }

    // JavaScript built-in globals must be resolved BEFORE moduleGlobalVars_ check.
    // In untyped JS modules, identifiers like String, Object, Array may appear in
    // moduleGlobalVars_ (from Analyzer function usage tracking) but should resolve
    // to runtime globals, not null module-scoped variables.
    {
        static const std::set<std::string> jsBuiltinGlobals = {
            "Math", "JSON", "Object", "Array", "String", "Number",
            "Boolean", "Date", "RegExp", "Promise", "Error", "Buffer",
            "process", "global", "globalThis", "Symbol", "Map", "Set",
            "WeakMap", "WeakSet", "Proxy", "Reflect",
            "EvalError", "RangeError", "ReferenceError", "SyntaxError",
            "TypeError", "URIError", "Function", "console",
            "parseInt", "parseFloat", "isNaN", "isFinite",
            "encodeURIComponent", "decodeURIComponent", "encodeURI", "decodeURI",
            "setInterval", "clearInterval", "setTimeout", "clearTimeout",
            "setImmediate", "clearImmediate", "queueMicrotask",
        };
        if (jsBuiltinGlobals.count(node->name)) {
            SPDLOG_DEBUG("[IDENT] builtin global: {} in func={}", node->name, currentFunction_ ? currentFunction_->name : "null");
            lastValue_ = builder_.createLoadGlobal(node->name);
            return;
        }
    }

    // For module-scoped variables, use __modvar_ globals when accessed from inner
    // functions or from the defining function when an inner function also uses it.
    // This ensures the module init function sees updates from closures that modify
    // the variable (e.g., let R = 0; const f = () => { R++; }; f(); exports.R = R).
    if (currentFunction_ && isModuleGlobalVar(node->name)) {
        // Check for a local variable in the CURRENT function's scope first —
        // local declarations (var/let/const or parameters) inside nested
        // functions shadow module globals. We must only check scopes belonging
        // to the current function (not outer functions) because outer function
        // locals aren't accessible via LLVM alloca from a different function.
        {
            bool foundLocal = false;
            for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
                auto found = it->variables.find(node->name);
                if (found != it->variables.end() && it->owningFunction == currentFunction_) {
                    // Found in current function's scope — use the local
                    auto* info = &found->second;
                    if (info->isAlloca && info->elemType) {
                        lastValue_ = builder_.createLoad(info->elemType, info->value);
                    } else {
                        lastValue_ = info->value;
                    }
                    foundLocal = true;
                    break;
                }
                // Stop at function boundaries — don't look into outer functions
                if (it->isFunctionBoundary && it->owningFunction != currentFunction_) {
                    break;
                }
            }
            if (foundLocal) return;
        }

        size_t scopeIndex = 0;
        if (isCapturedVariable(node->name, &scopeIndex)) {
            // Check: is the variable defined in a non-module-init function?
            // If so, it's a function parameter/local captured by a closure —
            // use LoadCapture, not LoadGlobal. Module globals from __module_init_
            // should use LoadGlobal; function parameters should use captures.
            auto* info = lookupVariableInfo(node->name);
            bool isModuleInitVar = true;
            if (info) {
                // Find the owning function of the variable
                for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
                    if (it->variables.count(node->name)) {
                        if (it->owningFunction &&
                            it->owningFunction->name.find("__module_init_") != 0 &&
                            it->owningFunction->name != "user_main" &&
                            it->owningFunction->name != "__synthetic_user_main") {
                            isModuleInitVar = false;
                        }
                        break;
                    }
                }
            }

            if (isModuleInitVar) {
                // Module-level variable — use __modvar_ global
                moduleGlobalsUsedByInnerByModule_[node->name].insert(currentModulePath_);
                std::string globalName = modVarName(node->name);
                auto type = module_->globals.count(globalName) ? module_->globals[globalName] : HIRType::makeAny();
                lastValue_ = builder_.createLoadGlobalTyped(globalName, type);
                return;
            }
            // Not a module-init var — fall through to the capture path below.
            // Do NOT check moduleGlobalsUsedByInner_ here because that set is
            // global across all modules. A same-named variable in a different
            // module (e.g., `var path = require('path')`) would incorrectly
            // redirect this function-local `path` to LoadGlobal.
        } else {
            // Not a captured variable but name matches a module global —
            // check if the defining function uses it via module global
            if (isModuleGlobalUsedByInner(node->name)) {
                std::string globalName = modVarName(node->name);
                auto type = module_->globals.count(globalName) ? module_->globals[globalName] : HIRType::makeAny();
                lastValue_ = builder_.createLoadGlobalTyped(globalName, type);
                return;
            }
        }
    }

    // Check if this is a captured variable from an outer function
    size_t scopeIndex = 0;
    if (currentFunction_ && isCapturedVariable(node->name, &scopeIndex)) {
        // Look up the variable info to get its type
        auto* info = lookupVariableInfo(node->name);
        if (info) {
            auto type = info->elemType ? info->elemType : (info->value ? info->value->type : HIRType::makeAny());
            // Register this capture for the current function
            registerCapture(node->name, type, scopeIndex);
            // Mark the function as having closures
            currentFunction_->hasClosure = true;
            // Use LoadCapture for captured variables
            lastValue_ = builder_.createLoadCapture(node->name, type);
            return;
        }
    }

    // Check for local/parameter variables
    lastValue_ = lookupVariable(node->name);
    if (lastValue_) {
        return;
    }

    // Handle namespace identifiers standalone - these are compile-time constructs
    // with no runtime representation (used only as prefixes for ns.member access).
    // Skip if the name is a registered extension module (path, fs, etc.) - those
    // are handled by the extension registry below via createLoadGlobal.
    // Also skip if this is a CJS module namespace import (stored in moduleGlobalVars_).
    if (node->inferredType && node->inferredType->kind == ts::TypeKind::Namespace) {
        auto& extReg = ext::ExtensionRegistry::instance();
        if (!extReg.isRegisteredGlobalOrModule(node->name) && !isModuleGlobalVar(node->name)) {
            lastValue_ = builder_.createConstUndefined();
            return;
        }
    }

    // Check for JavaScript built-in objects EARLY — before namespace/extension checks.
    // In untyped JS modules, built-ins like String, Object, Array may have
    // incorrect inferred types (Namespace, Any, etc.) that cause them to be
    // resolved as undefined instead of via LoadGlobal.
    {
        static const std::set<std::string> builtinObjects = {
            "Math", "JSON", "Object", "Array", "String", "Number",
            "Boolean", "Date", "RegExp", "Promise", "Error", "Buffer",
            "process", "global", "globalThis", "Symbol", "Map", "Set",
            "WeakMap", "WeakSet", "Proxy", "Reflect",
            "EvalError", "RangeError", "ReferenceError", "SyntaxError",
            "TypeError", "URIError", "Function",
        };
        if (builtinObjects.count(node->name)) {
            lastValue_ = builder_.createLoadGlobal(node->name);
            return;
        }
    }

    // Handle special constants first (these are always hardcoded)
    if (node->name == "undefined") {
        lastValue_ = builder_.createConstUndefined();
        return;
    }
    if (node->name == "NaN") {
        lastValue_ = builder_.createConstFloat(std::nan(""));
        return;
    }
    if (node->name == "Infinity") {
        lastValue_ = builder_.createConstFloat(std::numeric_limits<double>::infinity());
        return;
    }

    // Check ExtensionRegistry for registered objects/modules/globals
    // These include: console, Math, JSON, Object, Array, String, Number, Boolean,
    // Date, RegExp, Promise, Error, Buffer, process, global, globalThis,
    // and Node.js modules like path, fs, os, url, util, crypto, http, https, net, etc.
    auto& registry = ext::ExtensionRegistry::instance();
    if (registry.isRegisteredGlobalOrModule(node->name)) {
        // Emit LoadGlobal for global objects
        lastValue_ = builder_.createLoadGlobal(node->name);
        return;
    }

    // Check for known built-in functions used as values (not in call position)
    // These need native function wrappers so they can be passed as callbacks
    static const std::set<std::string> builtinFunctions = {
        "encodeURIComponent", "decodeURIComponent", "encodeURI", "decodeURI",
        "parseInt", "parseFloat"
    };
    if (builtinFunctions.count(node->name)) {
        auto nameVal = builder_.createConstString(node->name);
        lastValue_ = builder_.createCall("ts_get_builtin_function", {nameVal}, HIRType::makeAny());
        return;
    }

    // Fallback: Check for known JavaScript built-in objects not yet in extension files
    // This maintains backwards compatibility while migrating to registry-based lookups
    static const std::set<std::string> builtinObjects = {
        "Math", "JSON", "Object", "Array", "String", "Number",
        "Boolean", "Date", "RegExp", "Promise", "Error", "Buffer",
        "process", "global", "globalThis"
    };
    if (builtinObjects.count(node->name)) {
        lastValue_ = builder_.createLoadGlobal(node->name);
        return;
    }

    // Check if this is a module-scoped variable from an imported module
    // This must be checked BEFORE module_->functions because imported JS modules
    // compile their functions into module_->functions, but we need to use the
    // imported closure (which has prototype/properties set up) not a fresh one.
    if (isModuleGlobalVar(node->name)) {
        std::string globalName = modVarName(node->name);
        auto type = module_->globals.count(globalName) ? module_->globals[globalName] : HIRType::makeAny();
        lastValue_ = builder_.createLoadGlobalTyped(globalName, type);
        return;
    }

    // Check if this is a function name in the module
    // Functions are declared at module level and can be referenced as values
    for (const auto& func : module_->functions) {
        if (func->name == node->name) {
            // Found a function with this name - load it as a function value
            auto funcType = HIRType::makeFunction();
            funcType->returnType = func->returnType;
            for (const auto& param : func->params) {
                funcType->paramTypes.push_back(param.second);
            }
            lastValue_ = builder_.createLoadFunction(node->name, funcType);
            return;
        }
    }

    // Also check specializations - functions might be pending compilation
    if (specializations_) {
        for (const auto& spec : *specializations_) {
            if (spec.originalName == node->name || spec.specializedName == node->name) {
                // Found a function declaration - use LoadFunction
                auto funcType = HIRType::makeFunction();
                if (auto* funcNode = dynamic_cast<ast::FunctionDeclaration*>(spec.node)) {
                    if (!funcNode->returnType.empty()) {
                        funcType->returnType = convertTypeFromString(funcNode->returnType);
                    }
                    for (const auto& param : funcNode->parameters) {
                        auto paramType = param->type.empty()
                            ? HIRType::makeAny()
                            : convertTypeFromString(param->type);
                        funcType->paramTypes.push_back(paramType);
                    }
                }
                lastValue_ = builder_.createLoadFunction(spec.specializedName, funcType);
                return;
            }
        }
    }

    // Unknown variable - create undefined
    lastValue_ = createValue(HIRType::makeAny());
    builder_.createConstUndefined(lastValue_);
}

void ASTToHIR::visitSuperExpression(ast::SuperExpression* node) {
    setSourceLine(node);
    // TODO: Proper super handling
    lastValue_ = createValue(HIRType::makeObject());
    builder_.createConstNull(lastValue_);
}

void ASTToHIR::visitStringLiteral(ast::StringLiteral* node) {
    setSourceLine(node);
    lastValue_ = builder_.createConstString(node->value);
}

void ASTToHIR::visitRegularExpressionLiteral(ast::RegularExpressionLiteral* node) {
    setSourceLine(node);
    // Create a RegExp object from the literal text (e.g., "/pattern/flags")
    // The runtime function ts_regexp_from_literal parses the literal and creates the RegExp
    auto literalStr = builder_.createConstString(node->text);
    lastValue_ = builder_.createCall("ts_regexp_from_literal", {literalStr}, HIRType::makeObject());
}

void ASTToHIR::visitNumericLiteral(ast::NumericLiteral* node) {
    setSourceLine(node);
    // In TypeScript/JavaScript, all numbers are IEEE 754 double-precision floats
    lastValue_ = builder_.createConstFloat(node->value);
}

void ASTToHIR::visitBigIntLiteral(ast::BigIntLiteral* node) {
    setSourceLine(node);
    // Create a BigInt from the literal string value (e.g., "123n" -> "123")
    // The value in the AST includes the 'n' suffix, so we need to strip it
    std::string valueStr = node->value;
    if (!valueStr.empty() && valueStr.back() == 'n') {
        valueStr.pop_back();
    }

    // Create the string constant for the BigInt value
    auto strVal = builder_.createConstString(valueStr);

    // Call ts_bigint_create_str with base 10
    auto radix = builder_.createConstInt(10);
    lastValue_ = builder_.createCall("ts_bigint_create_str", {strVal, radix}, HIRType::makeObject());
}

void ASTToHIR::visitBooleanLiteral(ast::BooleanLiteral* node) {
    setSourceLine(node);
    lastValue_ = builder_.createConstBool(node->value);
}

void ASTToHIR::visitNullLiteral(ast::NullLiteral* node) {
    setSourceLine(node);
    lastValue_ = builder_.createConstNull();
}

void ASTToHIR::visitUndefinedLiteral(ast::UndefinedLiteral* node) {
    setSourceLine(node);
    lastValue_ = builder_.createConstUndefined();
}

void ASTToHIR::visitAwaitExpression(ast::AwaitExpression* node) {
    setSourceLine(node);
    if (node->expression) {
        // Lower the promise expression
        auto promise = lowerExpression(node->expression.get());
        if (!promise) {
            // The inner expression returned void (e.g., calling a function typed as () => void).
            // In JavaScript, all function calls return a value at runtime. When the function
            // is actually a promisified wrapper, it returns a Promise even though the original
            // type says void. Retroactively patch the last Call/CallIndirect instruction to
            // produce an Any-typed result so the await can use it.
            auto* block = builder_.getInsertBlock();
            if (block && !block->instructions.empty()) {
                auto& lastInst = block->instructions.back();
                if ((lastInst->opcode == HIROpcode::Call || lastInst->opcode == HIROpcode::CallIndirect ||
                     lastInst->opcode == HIROpcode::CallMethod) && !lastInst->result) {
                    auto result = builder_.createValue(HIRType::makeAny());
                    lastInst->result = result;
                    promise = result;
                }
            }
            if (!promise) {
                promise = builder_.createConstUndefined();
            }
        }
        // Create await instruction to wait for promise resolution
        lastValue_ = builder_.createAwait(promise);
    } else {
        // await with no expression returns undefined
        lastValue_ = builder_.createConstUndefined();
    }
}

void ASTToHIR::visitYieldExpression(ast::YieldExpression* node) {
    setSourceLine(node);
    // Yield: yield value or yield* iterable
    // yield returns the value passed to next() when generator is resumed
    // yield* delegates to another generator/iterable

    if (node->isAsterisk) {
        // yield* iterable - delegate to another generator
        if (node->expression) {
            auto iterable = lowerExpression(node->expression.get());
            lastValue_ = builder_.createYieldStar(iterable);
        } else {
            // yield* with no expression - undefined behavior, yield undefined
            auto undef = builder_.createConstUndefined();
            lastValue_ = builder_.createYieldStar(undef);
        }
    } else {
        // Regular yield
        if (node->expression) {
            auto value = lowerExpression(node->expression.get());
            lastValue_ = builder_.createYield(value);
        } else {
            // yield with no expression yields undefined
            auto undef = builder_.createConstUndefined();
            lastValue_ = builder_.createYield(undef);
        }
    }
}

void ASTToHIR::visitDynamicImport(ast::DynamicImport* node) {
    setSourceLine(node);
    // TODO: Dynamic import support
    lastValue_ = createValue(HIRType::makeAny());
    builder_.createConstUndefined(lastValue_);
}

void ASTToHIR::visitArrowFunction(ast::ArrowFunction* node) {
    setSourceLine(node);
    // Generate unique function name for the arrow function
    std::string funcName = "__arrow_fn_" + std::to_string(arrowFuncCounter_++);

    // Create HIR function
    auto func = std::make_unique<HIRFunction>(funcName);
    func->isAsync = node->isAsync;
    func->isGenerator = false;  // Arrow functions can't be generators
    func->sourceLine = node->line;
    func->sourceFile = node->sourceFile;

    // Set display name from assignment context (e.g., const myArrow = () => ...)
    if (!pendingClosureDisplayName_.empty()) {
        func->displayName = pendingClosureDisplayName_;
    }

    // Get function type info from inferred type if available
    std::shared_ptr<ts::FunctionType> tsFuncType = nullptr;
    if (node->inferredType && node->inferredType->kind == ts::TypeKind::Function) {
        tsFuncType = std::static_pointer_cast<ts::FunctionType>(node->inferredType);
    }

    // Add hidden __closure__ parameter as first parameter (for call_indirect compatibility)
    // call_indirect always passes the closure as the first argument
    func->params.push_back({"__closure__", HIRType::makePtr()});

    // Collect destructured parameter patterns for later extraction
    struct ArrowDestructuredParam {
        size_t paramIndex;
        ast::ObjectBindingPattern* objPattern = nullptr;
        ast::ArrayBindingPattern* arrPattern = nullptr;
    };
    std::vector<ArrowDestructuredParam> arrowDestructuredParams;

    // Handle parameters - use inferred types from function type if available
    for (size_t i = 0; i < node->parameters.size(); ++i) {
        auto& param = node->parameters[i];
        std::shared_ptr<HIRType> paramType;

        // First try explicit type annotation
        if (!param->type.empty()) {
            paramType = convertTypeFromString(param->type);
        }
        // Then try inferred type from function signature
        else if (tsFuncType && i < tsFuncType->paramTypes.size() && tsFuncType->paramTypes[i]) {
            paramType = convertType(tsFuncType->paramTypes[i]);
        }
        // Finally fall back to Any
        else {
            paramType = HIRType::makeAny();
        }

        // If parameter has a default value, force Any type to receive undefined
        if (param->initializer) {
            paramType = HIRType::makeAny();
        }

        std::string paramName;
        if (auto* ident = dynamic_cast<ast::Identifier*>(param->name.get())) {
            paramName = ident->name;
        } else if (auto* objPat = dynamic_cast<ast::ObjectBindingPattern*>(param->name.get())) {
            paramName = "param" + std::to_string(func->params.size());
            paramType = HIRType::makeAny();
            arrowDestructuredParams.push_back({func->params.size(), objPat, nullptr});
        } else if (auto* arrPat = dynamic_cast<ast::ArrayBindingPattern*>(param->name.get())) {
            paramName = "param" + std::to_string(func->params.size());
            paramType = HIRType::makeAny();
            arrowDestructuredParams.push_back({func->params.size(), nullptr, arrPat});
        } else {
            paramName = "param" + std::to_string(func->params.size());
        }
        func->params.push_back({paramName, paramType});
    }

    // Note: Arrow functions don't have their own 'arguments' object
    // (they inherit from enclosing scope), so no hidden __argN__ params needed.

    // Determine return type from inferred type or default to Any
    std::shared_ptr<HIRType> returnType = HIRType::makeAny();
    if (tsFuncType && tsFuncType->returnType) {
        returnType = convertType(tsFuncType->returnType);
    }
    func->returnType = returnType;

    // Save current context
    HIRFunction* savedFunc = currentFunction_;
    HIRBlock* savedBlock = currentBlock_;
    auto savedCaptures = pendingCaptures_;  // Save outer function's pending captures
    auto savedInnerFuncClosures = std::move(innerFuncClosures_);
    innerFuncClosures_.clear();
    // Save loop/switch/label stacks - nested functions must not see parent's break/continue targets
    auto savedLoopStack = loopStack_;
    auto savedSwitchStack = switchStack_;
    auto savedLabeledLoops = labeledLoops_;
    loopStack_ = {};
    switchStack_ = {};
    labeledLoops_ = {};

    currentFunction_ = func.get();
    clearPendingCaptures();  // Start fresh for this function

    // Create entry block
    auto entryBlock = func->createBlock("entry");
    builder_.setInsertPoint(entryBlock);
    currentBlock_ = entryBlock;

    // Enter function scope (marks function boundary for capture detection)
    pushFunctionScope(func.get());

    // Update function's value counter to start after parameters BEFORE the loop
    // This ensures values created during parameter processing (allocas, etc.)
    // don't conflict with parameter IDs 0, 1, 2, ...
    func->nextValueId = static_cast<uint32_t>(func->params.size());

    // Register parameters in the scope (with default value handling)
    for (size_t i = 0; i < func->params.size(); ++i) {
        const auto& [paramName, paramType] = func->params[i];
        auto paramValue = std::make_shared<HIRValue>(static_cast<uint32_t>(i), paramType, paramName);

        // Check if this parameter has a default value (skip __closure__ at index 0)
        size_t astParamIdx = (i >= 1) ? (i - 1) : SIZE_MAX;
        ast::Parameter* astParam = (astParamIdx < node->parameters.size())
            ? node->parameters[astParamIdx].get() : nullptr;
        if (astParam && astParam->initializer) {
            // Parameter has a default value - need to check if undefined and use default
            auto allocaVal = builder_.createAlloca(paramType);

            auto isUndefined = builder_.createCall("ts_value_is_undefined",
                {paramValue}, HIRType::makeBool());

            auto defaultBB = func->createBlock("default_param");
            auto usedBB = func->createBlock("use_param");
            auto mergeBB = func->createBlock("param_merge");

            builder_.createCondBranch(isUndefined, defaultBB, usedBB);

            // Default block - evaluate default expression and store
            builder_.setInsertPoint(defaultBB);
            currentBlock_ = defaultBB;
            auto* initExpr = dynamic_cast<ast::Expression*>(astParam->initializer.get());
            auto defaultVal = initExpr ? lowerExpression(initExpr) : builder_.createConstUndefined();
            if (paramType->kind == HIRTypeKind::Any) {
                defaultVal = forceBoxValue(defaultVal);
            }
            builder_.createStore(defaultVal, allocaVal);
            builder_.createBranch(mergeBB);

            // Use param block - store the passed parameter value
            builder_.setInsertPoint(usedBB);
            currentBlock_ = usedBB;
            builder_.createStore(paramValue, allocaVal);
            builder_.createBranch(mergeBB);

            // Merge block - continue execution
            builder_.setInsertPoint(mergeBB);
            currentBlock_ = mergeBB;

            defineVariableAlloca(paramName, allocaVal, paramType);
        } else {
            // No default value - store into an alloca so reassignment works
            auto allocaVal = builder_.createAlloca(paramType);
            builder_.createStore(paramValue, allocaVal);
            defineVariableAlloca(paramName, allocaVal, paramType);
        }
    }

    // Emit destructuring extraction for parameters with binding patterns
    for (auto& dp : arrowDestructuredParams) {
        auto paramValue = std::make_shared<HIRValue>(
            static_cast<uint32_t>(dp.paramIndex),
            HIRType::makeAny(),
            func->params[dp.paramIndex].first);
        if (dp.objPattern) {
            lowerObjectBindingPattern(dp.objPattern, paramValue);
        } else if (dp.arrPattern) {
            lowerArrayBindingPattern(dp.arrPattern, paramValue);
        }
    }

    // Lower function body
    // The body can be either a BlockStatement or an Expression (implicit return)
    if (auto* blockStmt = dynamic_cast<ast::BlockStatement*>(node->body.get())) {
        // JavaScript function hoisting: pre-declare nested function names as variables
        // This allows functions to be called before they appear in source order.
        for (auto& stmt : blockStmt->statements) {
            if (auto* nestedFunc = dynamic_cast<ast::FunctionDeclaration*>(stmt.get())) {
                auto nestedFuncType = std::make_shared<HIRType>(HIRTypeKind::Function);
                for (auto& param : nestedFunc->parameters) {
                    std::shared_ptr<HIRType> paramType = HIRType::makeAny();
                    if (!param->type.empty()) {
                        paramType = convertTypeFromString(param->type);
                    }
                    nestedFuncType->paramTypes.push_back(paramType);
                }
                nestedFuncType->returnType = nestedFunc->returnType.empty()
                    ? HIRType::makeAny()
                    : convertTypeFromString(nestedFunc->returnType);
                auto allocaVal = builder_.createAlloca(nestedFuncType, nestedFunc->name);
                builder_.createStore(builder_.createConstNull(), allocaVal);
                defineVariableAlloca(nestedFunc->name, allocaVal, nestedFuncType);
            }
        }

        // FIRST PASS: Process FunctionDeclarations to create closures (hoisting)
        for (auto& stmt : blockStmt->statements) {
            if (dynamic_cast<ast::FunctionDeclaration*>(stmt.get())) {
                lowerStatement(stmt.get());
            }
        }
        emitMutualRecursionFixup();

        // SECOND PASS: Process non-FunctionDeclaration statements in order
        for (auto& stmt : blockStmt->statements) {
            if (!dynamic_cast<ast::FunctionDeclaration*>(stmt.get())) {
                lowerStatement(stmt.get());
                if (builder_.isBlockTerminated()) {
                    break;
                }
            }
        }
    } else if (auto* exprBody = dynamic_cast<ast::Expression*>(node->body.get())) {
        // Expression body - implicit return
        auto retVal = lowerExpression(exprBody);
        // If return type is void, don't return the value (just execute the expression for side effects)
        if (returnType->kind != HIRTypeKind::Void) {
            builder_.createReturn(retVal);
        }
    }

    // Add implicit return void if no terminator
    if (!hasTerminator()) {
        builder_.createReturnVoid();
    }

    // Copy pending captures to the function's captures list
    for (const auto& cap : pendingCaptures_) {
        func->captures.push_back({cap.name, cap.type});
    }
    bool hasClosure = !pendingCaptures_.empty();

    // Save the captures list for later use (after we restore context)
    std::vector<std::pair<std::string, std::shared_ptr<HIRType>>> innerCaptures = func->captures;

    popScope();

    // Restore saved context
    currentFunction_ = savedFunc;
    currentBlock_ = savedBlock;
    pendingCaptures_ = savedCaptures;  // Restore outer function's pending captures
    innerFuncClosures_ = std::move(savedInnerFuncClosures);
    loopStack_ = savedLoopStack;
    switchStack_ = savedSwitchStack;
    labeledLoops_ = savedLabeledLoops;
    if (savedBlock) {
        builder_.setInsertPoint(savedBlock);
    }

    // Get the function pointer before adding to module
    HIRFunction* funcPtr = func.get();

    // Add function to module
    module_->functions.push_back(std::move(func));

    // Build function type for the closure (used for type inference at call sites)
    auto closureFuncType = std::make_shared<HIRType>(HIRTypeKind::Function);
    for (const auto& [paramName, paramType] : funcPtr->params) {
        closureFuncType->paramTypes.push_back(paramType);
    }
    closureFuncType->returnType = funcPtr->returnType;

    // Return either a closure or plain function pointer
    if (hasClosure && savedFunc) {
        // Create a closure with captured values
        std::vector<std::shared_ptr<HIRValue>> captureValues;
        for (const auto& cap : innerCaptures) {
            const std::string& capName = cap.first;
            const auto& capType = cap.second;

            // Check if this variable requires capture propagation (i.e., it's from
            // an outer function's scope, not the current function's scope)
            size_t scopeIndex = 0;
            bool needsCapturePropagation = isCapturedVariable(capName, &scopeIndex);

            if (needsCapturePropagation) {
                // Variable is in an outer function's scope - we need to propagate
                // the capture through the current function.
                // Register this capture for the current function too
                registerCapture(capName, capType, scopeIndex);
                currentFunction_->hasClosure = true;
                // Also add to the function's captures list directly since it was
                // already finalized before we detected the propagation need
                bool alreadyInCaptures = false;
                for (const auto& existingCap : currentFunction_->captures) {
                    if (existingCap.first == capName) {
                        alreadyInCaptures = true;
                        break;
                    }
                }
                if (!alreadyInCaptures) {
                    currentFunction_->captures.push_back({capName, capType});
                }
                // Use LoadCapture to get the value
                auto val = builder_.createLoadCapture(capName, capType);
                captureValues.push_back(val);
            } else {
                // Variable is directly accessible in the current function's scope
                auto* info = lookupVariableInfo(capName);
                if (info) {
                    std::shared_ptr<HIRValue> val;
                    if (info->isAlloca && info->elemType) {
                        val = builder_.createLoad(info->elemType, info->value);
                    } else {
                        val = info->value;
                    }
                    captureValues.push_back(val);
                } else {
                    // Variable not found - shouldn't happen, but emit a placeholder
                    captureValues.push_back(builder_.createConstNull());
                }
            }
        }
        lastValue_ = builder_.createMakeClosure(funcName, captureValues, closureFuncType);

        // Mark each captured variable in the outer scope as "captured by nested"
        // so subsequent reads/writes in the outer function also use the cell
        int captureIdx = 0;
        for (const auto& cap : innerCaptures) {
            const std::string& capName = cap.first;
            size_t scopeIndex = 0;
            if (!isCapturedVariable(capName, &scopeIndex)) {
                // Variable is in this function's scope, mark it as captured
                auto* info = lookupVariableInfo(capName);
                if (info && !info->isCapturedByNested) {
                    info->isCapturedByNested = true;
                    // Store closure pointer in an alloca to ensure SSA dominance
                    // (closure may be created in try block but accessed from catch block)
                    auto closureAlloca = builder_.createAlloca(HIRType::makeAny(), capName + "$closure");
                    builder_.createStore(lastValue_, closureAlloca);
                    info->closurePtr = closureAlloca;
                    info->captureIndex = captureIdx;
                }
            }
            captureIdx++;
        }
    } else {
        // No captures, but still wrap in a closure for consistency with call_indirect
        // which always expects a TsClosure* (not a raw function pointer)
        std::vector<std::shared_ptr<HIRValue>> emptyCaptureValues;
        lastValue_ = builder_.createMakeClosure(funcName, emptyCaptureValues, closureFuncType);
    }
}

void ASTToHIR::visitFunctionExpression(ast::FunctionExpression* node) {
    setSourceLine(node);
    SPDLOG_DEBUG("[FE] ENTER: name={} scopes={} currentFunc={} bodySize={}",
        node->name.empty() ? "(anon)" : node->name,
        scopes_.size(),
        currentFunction_ ? currentFunction_->name : "null",
        node->body.size());
    // Generate function name: use the node's name if available, otherwise generate one
    std::string funcName;
    if (!node->name.empty()) {
        // Named function expression - use the name but make it unique
        funcName = "__fn_expr_" + node->name + "_" + std::to_string(funcExprCounter_++);
    } else {
        // Anonymous function expression
        funcName = "__fn_expr_" + std::to_string(funcExprCounter_++);
    }

    // Create HIR function
    auto func = std::make_unique<HIRFunction>(funcName);
    func->isAsync = node->isAsync;
    func->isGenerator = node->isGenerator;
    func->sourceLine = node->line;
    func->sourceFile = node->sourceFile;

    // Set display name: prefer node name, then assignment context
    if (!node->name.empty()) {
        func->displayName = node->name;
    } else if (!pendingClosureDisplayName_.empty()) {
        func->displayName = pendingClosureDisplayName_;
    }

    // Add hidden __closure__ parameter as first parameter (for call_indirect compatibility)
    // call_indirect always passes the closure as the first argument
    func->params.push_back({"__closure__", HIRType::makePtr()});

    // Handle parameters
    for (auto& param : node->parameters) {
        auto paramType = param->type.empty()
            ? HIRType::makeAny()
            : convertTypeFromString(param->type);

        // If parameter has a default value, force Any type to receive undefined
        if (param->initializer) {
            paramType = HIRType::makeAny();
        }

        std::string paramName;
        if (auto* ident = dynamic_cast<ast::Identifier*>(param->name.get())) {
            paramName = ident->name;
        } else {
            paramName = "param" + std::to_string(func->params.size());
        }
        func->params.push_back({paramName, paramType});
    }

    // If the function body uses 'arguments', add hidden __argN__ params
    // so the padded calling convention args can be captured.
    {
        bool bodyUsesArguments = false;
        for (auto& stmt : node->body) {
            if (containsArgumentsIdentifier(stmt.get())) {
                bodyUsesArguments = true;
                break;
            }
        }
        if (bodyUsesArguments) {
            while (func->params.size() < 5) {
                std::string argName = "__arg" + std::to_string(func->params.size() - 1) + "__";
                func->params.push_back({argName, HIRType::makeAny()});
            }
        }
    }

    // Determine return type from explicit return type or inferred type
    std::shared_ptr<HIRType> returnType = HIRType::makeAny();
    if (!node->returnType.empty()) {
        returnType = convertTypeFromString(node->returnType);
    } else if (node->inferredType && node->inferredType->kind == ts::TypeKind::Function) {
        auto funcType = std::static_pointer_cast<ts::FunctionType>(node->inferredType);
        if (funcType->returnType) {
            returnType = convertType(funcType->returnType);
        }
    }
    func->returnType = returnType;

    // Save current context
    HIRFunction* savedFunc = currentFunction_;
    HIRBlock* savedBlock = currentBlock_;
    auto savedCaptures = pendingCaptures_;  // Save outer function's pending captures
    auto savedInnerFuncClosures = std::move(innerFuncClosures_);
    innerFuncClosures_.clear();
    // Save loop/switch/label stacks - nested functions must not see parent's break/continue targets
    auto savedLoopStack = loopStack_;
    auto savedSwitchStack = switchStack_;
    auto savedLabeledLoops = labeledLoops_;
    loopStack_ = {};
    switchStack_ = {};
    labeledLoops_ = {};

    currentFunction_ = func.get();
    clearPendingCaptures();  // Start fresh for this function

    // Create entry block
    auto entryBlock = func->createBlock("entry");
    builder_.setInsertPoint(entryBlock);
    currentBlock_ = entryBlock;

    // Enter function scope (marks function boundary for capture detection)
    pushFunctionScope(func.get());

    // Update function's value counter to start after parameters BEFORE the loop
    func->nextValueId = static_cast<uint32_t>(func->params.size());

    // Register parameters in the scope (with default value handling)
    for (size_t i = 0; i < func->params.size(); ++i) {
        const auto& [paramName, paramType] = func->params[i];
        auto paramValue = std::make_shared<HIRValue>(static_cast<uint32_t>(i), paramType, paramName);

        // Check if this parameter has a default value (skip __closure__ at index 0)
        size_t astParamIdx = (i >= 1) ? (i - 1) : SIZE_MAX;
        ast::Parameter* astParam = (astParamIdx < node->parameters.size())
            ? node->parameters[astParamIdx].get() : nullptr;
        if (astParam && astParam->initializer) {
            // Parameter has a default value - need to check if undefined and use default
            auto allocaVal = builder_.createAlloca(paramType);

            auto isUndefined = builder_.createCall("ts_value_is_undefined",
                {paramValue}, HIRType::makeBool());

            auto defaultBB = func->createBlock("default_param");
            auto usedBB = func->createBlock("use_param");
            auto mergeBB = func->createBlock("param_merge");

            builder_.createCondBranch(isUndefined, defaultBB, usedBB);

            // Default block - evaluate default expression and store
            builder_.setInsertPoint(defaultBB);
            currentBlock_ = defaultBB;
            auto* initExpr = dynamic_cast<ast::Expression*>(astParam->initializer.get());
            auto defaultVal = initExpr ? lowerExpression(initExpr) : builder_.createConstUndefined();
            if (paramType->kind == HIRTypeKind::Any) {
                defaultVal = forceBoxValue(defaultVal);
            }
            builder_.createStore(defaultVal, allocaVal);
            builder_.createBranch(mergeBB);

            // Use param block - store the passed parameter value
            builder_.setInsertPoint(usedBB);
            currentBlock_ = usedBB;
            builder_.createStore(paramValue, allocaVal);
            builder_.createBranch(mergeBB);

            // Merge block - continue execution
            builder_.setInsertPoint(mergeBB);
            currentBlock_ = mergeBB;

            defineVariableAlloca(paramName, allocaVal, paramType);
        } else {
            // No default value - store into an alloca so reassignment works
            auto allocaVal = builder_.createAlloca(paramType);
            builder_.createStore(paramValue, allocaVal);
            defineVariableAlloca(paramName, allocaVal, paramType);
        }
    }

    // If the function is named, make it available in its own scope (for recursion)
    // Alias the name to the __closure__ parameter (index 0) which represents
    // the function/closure pointer itself - ts_call_N handles dispatch correctly
    if (!node->name.empty()) {
        auto* closureInfo = lookupVariableInfo("__closure__");
        if (closureInfo && closureInfo->isAlloca) {
            defineVariableAlloca(node->name, closureInfo->value, closureInfo->elemType ? closureInfo->elemType : HIRType::makePtr());
        } else {
            // Fallback: reference parameter 0 directly
            auto closureParam = std::make_shared<HIRValue>(0, HIRType::makePtr(), node->name);
            defineVariable(node->name, closureParam);
        }
    }

    // Create 'arguments' array if the function body references 'arguments'.
    // Must be done at function entry before body lowering.
    {
        bool usesArguments = false;
        for (auto& stmt : node->body) {
            if (containsArgumentsIdentifier(stmt.get())) {
                usesArguments = true;
                break;
            }
        }
        if (usesArguments) {
            std::vector<std::shared_ptr<HIRValue>> callArgs;

            size_t userIdx = 0;
            for (size_t i = 0; i < func->params.size() && userIdx < 10; ++i) {
                if (func->params[i].first == "__closure__") continue;
                auto paramVal = lookupVariable(func->params[i].first);
                if (!paramVal) {
                    paramVal = builder_.createConstUndefined();
                }
                callArgs.push_back(paramVal);
                userIdx++;
            }
            while (userIdx < 10) {
                callArgs.push_back(builder_.createConstUndefined());
                userIdx++;
            }

            auto argsArray = builder_.createCall("ts_create_arguments_from_params",
                callArgs, HIRType::makeAny());

            auto allocaVal = builder_.createAlloca(HIRType::makeAny(), "arguments");
            builder_.createStore(argsArray, allocaVal, HIRType::makeAny());
            defineVariableAlloca("arguments", allocaVal, HIRType::makeAny());
        }
    }

    // JavaScript var hoisting for function expressions with nested func decls.
    // Pre-declare var names so the first pass (function declarations) can find
    // variable info to mark as isCapturedByNested for shared cell access.
    // Safe for function expressions since all vars are Any/ptr type.
    {
        bool hasNestedFuncDecls = false;
        for (auto& stmt : node->body) {
            if (dynamic_cast<ast::FunctionDeclaration*>(stmt.get())) {
                hasNestedFuncDecls = true;
                break;
            }
        }
        if (!hasNestedFuncDecls) goto skip_var_hoisting;
    }
    for (auto& stmt : node->body) {
        ast::VariableDeclaration* varDecl = nullptr;
        if (stmt->getKind() == "VariableDeclaration") {
            varDecl = dynamic_cast<ast::VariableDeclaration*>(stmt.get());
        } else if (auto* block = dynamic_cast<ast::BlockStatement*>(stmt.get())) {
            // Multi-variable declarations get wrapped in BlockStatement
            for (auto& inner : block->statements) {
                if (auto* vd = dynamic_cast<ast::VariableDeclaration*>(inner.get())) {
                    if (auto* ident = dynamic_cast<ast::Identifier*>(vd->name.get())) {
                        if (!lookupVariableInfoInCurrentFunction(ident->name)) {
                            auto allocaVal = builder_.createAlloca(HIRType::makeAny(), ident->name);
                            builder_.createStore(builder_.createConstUndefined(), allocaVal);
                            defineVariableAlloca(ident->name, allocaVal, HIRType::makeAny());
                        }
                    }
                }
            }
        }
        if (varDecl) {
            if (auto* ident = dynamic_cast<ast::Identifier*>(varDecl->name.get())) {
                if (!lookupVariableInfoInCurrentFunction(ident->name)) {
                    auto allocaVal = builder_.createAlloca(HIRType::makeAny(), ident->name);
                    builder_.createStore(builder_.createConstUndefined(), allocaVal);
                    defineVariableAlloca(ident->name, allocaVal, HIRType::makeAny());
                }
            }
        }
    }

    skip_var_hoisting:

    // JavaScript function hoisting: pre-declare nested function names as variables.
    // This allows functions to be called before they appear in source order.
    for (auto& stmt : node->body) {
        if (auto* funcDecl = dynamic_cast<ast::FunctionDeclaration*>(stmt.get())) {
            auto funcType = std::make_shared<HIRType>(HIRTypeKind::Function);
            for (auto& param : funcDecl->parameters) {
                std::shared_ptr<HIRType> paramType = HIRType::makeAny();
                if (!param->type.empty()) {
                    paramType = convertTypeFromString(param->type);
                }
                funcType->paramTypes.push_back(paramType);
            }
            funcType->returnType = funcDecl->returnType.empty()
                ? HIRType::makeAny()
                : convertTypeFromString(funcDecl->returnType);

            // Use existing alloca from var hoisting if available, else create new
            auto* existing = lookupVariableInfoInCurrentFunction(funcDecl->name);
            if (existing && existing->isAlloca) {
                // Update type to function type
                existing->elemType = funcType;
            } else {
                auto allocaVal = builder_.createAlloca(funcType, funcDecl->name);
                builder_.createStore(builder_.createConstNull(), allocaVal);
                defineVariableAlloca(funcDecl->name, allocaVal, funcType);
            }
        }
    }

    // Lower function body in two passes for proper JavaScript function hoisting:
    // FIRST PASS: Process FunctionDeclarations to create closures
    for (auto& stmt : node->body) {
        if (dynamic_cast<ast::FunctionDeclaration*>(stmt.get())) {
            lowerStatement(stmt.get());
        }
    }
    emitMutualRecursionFixup();

    // SECOND PASS: Process non-FunctionDeclaration statements in order
    for (auto& stmt : node->body) {
        if (!dynamic_cast<ast::FunctionDeclaration*>(stmt.get())) {
            lowerStatement(stmt.get());
            if (builder_.isBlockTerminated()) {
                break;
            }
        }
    }

    // Add implicit return undefined if no terminator
    if (!hasTerminator()) {
        builder_.createReturnVoid();
    }

    // Copy pending captures to the function's captures list
    for (const auto& cap : pendingCaptures_) {
        func->captures.push_back({cap.name, cap.type});
    }
    bool hasClosure = !pendingCaptures_.empty();

    // Save the captures list for later use (after we restore context)
    std::vector<std::pair<std::string, std::shared_ptr<HIRType>>> innerCaptures = func->captures;

    popScope();

    // Restore saved context
    currentFunction_ = savedFunc;
    currentBlock_ = savedBlock;
    pendingCaptures_ = savedCaptures;  // Restore outer function's pending captures
    innerFuncClosures_ = std::move(savedInnerFuncClosures);
    loopStack_ = savedLoopStack;
    switchStack_ = savedSwitchStack;
    labeledLoops_ = savedLabeledLoops;
    if (savedBlock) {
        builder_.setInsertPoint(savedBlock);
    }

    // Get the function pointer before adding to module
    HIRFunction* funcPtr = func.get();

    // Add function to module
    module_->functions.push_back(std::move(func));

    // Build function type for the closure (used for type inference at call sites)
    auto closureFuncType = std::make_shared<HIRType>(HIRTypeKind::Function);
    for (const auto& [paramName, paramType] : funcPtr->params) {
        closureFuncType->paramTypes.push_back(paramType);
    }
    closureFuncType->returnType = funcPtr->returnType;

    // Return either a closure or plain function pointer
    if (hasClosure && savedFunc) {
        // Create a closure with captured values
        std::vector<std::shared_ptr<HIRValue>> captureValues;
        for (const auto& cap : innerCaptures) {
            const std::string& capName = cap.first;
            const auto& capType = cap.second;

            // Check if this variable requires capture propagation (i.e., it's from
            // an outer function's scope, not the current function's scope)
            size_t scopeIndex = 0;
            bool needsCapturePropagation = isCapturedVariable(capName, &scopeIndex);

            if (needsCapturePropagation) {
                // Variable is in an outer function's scope - we need to propagate
                // the capture through the current function.
                // Register this capture for the current function too
                registerCapture(capName, capType, scopeIndex);
                currentFunction_->hasClosure = true;
                // Also add to the function's captures list directly since it was
                // already finalized before we detected the propagation need
                bool alreadyInCaptures = false;
                for (const auto& existingCap : currentFunction_->captures) {
                    if (existingCap.first == capName) {
                        alreadyInCaptures = true;
                        break;
                    }
                }
                if (!alreadyInCaptures) {
                    currentFunction_->captures.push_back({capName, capType});
                }
                // Use LoadCapture to get the value
                auto val = builder_.createLoadCapture(capName, capType);
                captureValues.push_back(val);
            } else {
                // Variable is directly accessible in the current function's scope
                auto* info = lookupVariableInfo(capName);
                if (info) {
                    std::shared_ptr<HIRValue> val;
                    if (info->isAlloca && info->elemType) {
                        val = builder_.createLoad(info->elemType, info->value);
                    } else {
                        val = info->value;
                    }
                    captureValues.push_back(val);
                } else {
                    // Variable not found - shouldn't happen, but emit a placeholder
                    captureValues.push_back(builder_.createConstNull());
                }
            }
        }
        lastValue_ = builder_.createMakeClosure(funcName, captureValues, closureFuncType);

        // Mark each captured variable in the outer scope as "captured by nested"
        // so subsequent reads/writes in the outer function also use the cell
        int captureIdx = 0;
        for (const auto& cap : innerCaptures) {
            const std::string& capName = cap.first;
            size_t scopeIndex = 0;
            if (!isCapturedVariable(capName, &scopeIndex)) {
                // Variable is in this function's scope, mark it as captured
                auto* info = lookupVariableInfo(capName);
                if (info && !info->isCapturedByNested) {
                    info->isCapturedByNested = true;
                    // Store closure pointer in an alloca to ensure SSA dominance
                    auto closureAlloca = builder_.createAlloca(HIRType::makeAny(), capName + "$closure");
                    builder_.createStore(lastValue_, closureAlloca);
                    info->closurePtr = closureAlloca;
                    info->captureIndex = captureIdx;
                }
            }
            captureIdx++;
        }
    } else {
        // No captures, but still wrap in a closure for consistency with call_indirect
        // which always expects a TsClosure* (not a raw function pointer)
        std::vector<std::shared_ptr<HIRValue>> emptyCaptureValues;
        lastValue_ = builder_.createMakeClosure(funcName, emptyCaptureValues, closureFuncType);
    }
}

std::shared_ptr<HIRValue> ASTToHIR::lowerMethodDefinitionToFunction(ast::MethodDefinition* node) {
    // Generate function name based on method name and type
    std::string prefix = node->isGetter ? "__getter_" : (node->isSetter ? "__setter_" : "__method_");
    std::string methodName = node->name;
    if (methodName.empty() && node->nameNode) {
        if (auto* id = dynamic_cast<ast::Identifier*>(node->nameNode.get())) {
            methodName = id->name;
        }
    }
    std::string funcName = prefix + methodName + "_" + std::to_string(methodCounter_++);

    // Create HIR function
    auto func = std::make_unique<HIRFunction>(funcName);
    func->isAsync = node->isAsync;
    func->isGenerator = node->isGenerator;
    func->sourceLine = node->line;
    func->sourceFile = node->sourceFile;

    // Add implicit 'this' parameter for methods
    func->params.push_back({"this", HIRType::makeAny()});

    // Handle explicit parameters
    // For getters/setters, force params to be Any (TsValue*) since they will be called
    // through the runtime's dynamic dispatch which passes TsValue* arguments
    bool forceAnyParams = node->isGetter || node->isSetter;
    for (auto& param : node->parameters) {
        auto paramType = (forceAnyParams || param->type.empty())
            ? HIRType::makeAny()
            : convertTypeFromString(param->type);

        std::string paramName;
        if (auto* ident = dynamic_cast<ast::Identifier*>(param->name.get())) {
            paramName = ident->name;
        } else {
            paramName = "param" + std::to_string(func->params.size());
        }
        func->params.push_back({paramName, paramType});
    }

    // Determine return type - always Any for method definitions since they are called
    // through dynamic dispatch (ts_call_N) which expects ptr (NaN-boxed TsValue*) returns
    func->returnType = HIRType::makeAny();

    // Save current context
    HIRFunction* savedFunc = currentFunction_;
    HIRBlock* savedBlock = currentBlock_;
    auto savedCaptures = pendingCaptures_;
    // Save loop/switch/label stacks - nested functions must not see parent's break/continue targets
    auto savedLoopStack = loopStack_;
    auto savedSwitchStack = switchStack_;
    auto savedLabeledLoops = labeledLoops_;
    loopStack_ = {};
    switchStack_ = {};
    labeledLoops_ = {};

    currentFunction_ = func.get();
    clearPendingCaptures();

    // Create entry block
    auto entryBlock = func->createBlock("entry");
    builder_.setInsertPoint(entryBlock);
    currentBlock_ = entryBlock;

    // Enter function scope
    pushFunctionScope(func.get());

    // Register parameters in the scope (including 'this')
    for (size_t i = 0; i < func->params.size(); ++i) {
        const auto& [paramName, paramType] = func->params[i];
        auto paramValue = std::make_shared<HIRValue>(static_cast<uint32_t>(i), paramType, paramName);
        defineVariable(paramName, paramValue);
    }
    func->nextValueId = static_cast<uint32_t>(func->params.size());

    // Lower function body
    for (auto& stmt : node->body) {
        lowerStatement(stmt.get());
    }

    // Add implicit return undefined if no terminator
    if (!hasTerminator()) {
        builder_.createReturnVoid();
    }

    // Copy pending captures to the function's captures list
    for (const auto& cap : pendingCaptures_) {
        func->captures.push_back({cap.name, cap.type});
    }
    bool hasClosure = !pendingCaptures_.empty();

    // Save the captures list for later use
    std::vector<std::pair<std::string, std::shared_ptr<HIRType>>> innerCaptures = func->captures;

    popScope();

    // Restore saved context
    currentFunction_ = savedFunc;
    currentBlock_ = savedBlock;
    pendingCaptures_ = savedCaptures;
    loopStack_ = savedLoopStack;
    switchStack_ = savedSwitchStack;
    labeledLoops_ = savedLabeledLoops;
    if (savedBlock) {
        builder_.setInsertPoint(savedBlock);
    }

    // Get the function pointer before adding to module
    HIRFunction* funcPtr = func.get();

    // Add function to module
    module_->functions.push_back(std::move(func));

    // Build function type for the closure
    auto closureFuncType = std::make_shared<HIRType>(HIRTypeKind::Function);
    for (const auto& [paramName, paramType] : funcPtr->params) {
        closureFuncType->paramTypes.push_back(paramType);
    }
    closureFuncType->returnType = funcPtr->returnType;

    // Return either a closure or plain function pointer
    if (hasClosure && savedFunc) {
        // Create a closure with captured values
        std::vector<std::shared_ptr<HIRValue>> captureValues;
        for (const auto& cap : innerCaptures) {
            const std::string& capName = cap.first;
            const auto& capType = cap.second;

            size_t scopeIndex = 0;
            bool needsCapturePropagation = isCapturedVariable(capName, &scopeIndex);

            if (needsCapturePropagation) {
                registerCapture(capName, capType, scopeIndex);
                currentFunction_->hasClosure = true;
                bool alreadyInCaptures = false;
                for (const auto& existingCap : currentFunction_->captures) {
                    if (existingCap.first == capName) {
                        alreadyInCaptures = true;
                        break;
                    }
                }
                if (!alreadyInCaptures) {
                    currentFunction_->captures.push_back({capName, capType});
                }
                auto val = builder_.createLoadCapture(capName, capType);
                captureValues.push_back(val);
            } else {
                auto* info = lookupVariableInfo(capName);
                if (info) {
                    std::shared_ptr<HIRValue> val;
                    if (info->isAlloca && info->elemType) {
                        val = builder_.createLoad(info->elemType, info->value);
                    } else {
                        val = info->value;
                    }
                    captureValues.push_back(val);
                } else {
                    captureValues.push_back(builder_.createConstNull());
                }
            }
        }
        return builder_.createMakeClosure(funcName, captureValues, closureFuncType);
    } else {
        // Pass the function type so SetPropStatic knows to box it as a function
        return builder_.createLoadFunction(funcName, closureFuncType);
    }
}

void ASTToHIR::visitTemplateExpression(ast::TemplateExpression* node) {
    setSourceLine(node);
    // Start with the head string
    auto currentStr = builder_.createConstString(node->head);

    for (auto& span : node->spans) {
        // Lower the embedded expression
        auto exprValue = lowerExpression(span.expression.get());

        // Convert to string based on type
        std::shared_ptr<HIRValue> strValue;
        auto exprType = span.expression->inferredType;

        if (exprType && exprType->kind == TypeKind::Int) {
            // Integer to string conversion
            strValue = builder_.createCall("ts_string_from_int", {exprValue}, HIRType::makeString());
        } else if (exprType && exprType->kind == TypeKind::Double) {
            // Double to string conversion
            strValue = builder_.createCall("ts_string_from_double", {exprValue}, HIRType::makeString());
        } else if (exprType && exprType->kind == TypeKind::Boolean) {
            // Boolean to string conversion
            strValue = builder_.createCall("ts_string_from_bool", {exprValue}, HIRType::makeString());
        } else if (exprType && exprType->kind == TypeKind::String) {
            // Already a string, use directly
            strValue = exprValue;
        } else {
            // For any/boxed types, use runtime coercion to string
            strValue = builder_.createCall("ts_string_from_value", {exprValue}, HIRType::makeString());
        }

        // Concatenate expression result
        currentStr = builder_.createStringConcat(currentStr, strValue);

        // Concatenate the literal part after the expression
        auto litValue = builder_.createConstString(span.literal);
        currentStr = builder_.createStringConcat(currentStr, litValue);
    }

    lastValue_ = currentStr;
}

void ASTToHIR::visitTaggedTemplateExpression(ast::TaggedTemplateExpression* node) {
    setSourceLine(node);
    // Tagged template: tag`str${expr}str...`
    // Calls: tag(stringsArray, ...expressions)
    // stringsArray is an array of the literal parts with a 'raw' property

    if (!node->tag || !node->templateExpr) {
        lastValue_ = builder_.createConstUndefined();
        return;
    }

    // Lower the tag function
    auto tagFn = lowerExpression(node->tag.get());

    // Get template parts - templateExpr could be TemplateExpression or NoSubstitutionTemplateLiteral
    std::vector<std::string> stringParts;
    std::vector<std::shared_ptr<HIRValue>> expressions;

    auto* templateExpr = dynamic_cast<ast::TemplateExpression*>(node->templateExpr.get());
    if (templateExpr) {
        // Template with substitutions
        stringParts.push_back(templateExpr->head);

        for (const auto& span : templateExpr->spans) {
            if (span.expression) {
                expressions.push_back(lowerExpression(span.expression.get()));
            }
            stringParts.push_back(span.literal);
        }
    } else {
        // NoSubstitutionTemplateLiteral - just a single string
        auto* strLit = dynamic_cast<ast::StringLiteral*>(node->templateExpr.get());
        if (strLit) {
            stringParts.push_back(strLit->value);
        }
    }

    // Create the strings array with the proper elements
    auto arrayLen = builder_.createConstInt(static_cast<int64_t>(stringParts.size()));
    auto stringsArray = builder_.createNewArrayBoxed(arrayLen, HIRType::makeString());
    for (size_t i = 0; i < stringParts.size(); ++i) {
        auto idx = builder_.createConstInt(static_cast<int64_t>(i));
        auto strVal = builder_.createConstString(stringParts[i]);
        builder_.createSetElem(stringsArray, idx, strVal);
    }

    // Add 'raw' property to the strings array (same values for now)
    // TODO: Handle raw string escapes properly (e.g., `\n` vs actual newline)
    auto rawArray = builder_.createNewArrayBoxed(arrayLen, HIRType::makeString());
    for (size_t i = 0; i < stringParts.size(); ++i) {
        auto idx = builder_.createConstInt(static_cast<int64_t>(i));
        auto strVal = builder_.createConstString(stringParts[i]);
        builder_.createSetElem(rawArray, idx, strVal);
    }
    builder_.createSetPropStatic(stringsArray, "raw", rawArray);

    // Build argument list: [stringsArray, ...expressions]
    std::vector<std::shared_ptr<HIRValue>> args;
    args.push_back(stringsArray);
    for (const auto& expr : expressions) {
        args.push_back(expr);
    }

    // Call the tag function with indirect call (since tag could be any callable)
    lastValue_ = builder_.createCallIndirect(tagFn, args, HIRType::makeAny());
}

void ASTToHIR::visitAsExpression(ast::AsExpression* node) {
    setSourceLine(node);
    // Type assertion - just lower the expression
    if (node->expression) {
        lastValue_ = lowerExpression(node->expression.get());
    }
}

void ASTToHIR::visitNonNullExpression(ast::NonNullExpression* node) {
    setSourceLine(node);
    // Non-null assertion - just lower the expression
    if (node->expression) {
        lastValue_ = lowerExpression(node->expression.get());
    }
}

void ASTToHIR::visitPrefixUnaryExpression(ast::PrefixUnaryExpression* node) {
    setSourceLine(node);
    auto operand = lowerExpression(node->operand.get());

    const std::string& op = node->op;
    if (op == "-") {
        // Determine if operand is floating point
        bool isFloat = false;
        if (operand && operand->type && operand->type->kind == HIRTypeKind::Float64) {
            isFloat = true;
        } else if (node->operand->inferredType && node->operand->inferredType->kind == ts::TypeKind::Double) {
            isFloat = true;
        }
        lastValue_ = isFloat ? builder_.createNegF64(operand) : builder_.createNegI64(operand);
    } else if (op == "!") {
        lastValue_ = builder_.createLogicalNot(operand);
    } else if (op == "~") {
        lastValue_ = builder_.createNotI64(operand);
    } else if (op == "+") {
        // Unary plus: no-op for numeric types, ToNumber for others (Any/String)
        bool isNumeric = false;
        if (operand && operand->type) {
            auto k = operand->type->kind;
            isNumeric = (k == HIRTypeKind::Int64 || k == HIRTypeKind::Float64 ||
                         k == HIRTypeKind::Bool);
        }
        if (isNumeric) {
            lastValue_ = operand;
        } else {
            // Double negation forces ToNumber conversion (NegF64 handles ptr→double)
            lastValue_ = builder_.createNegF64(builder_.createNegF64(operand));
        }
    } else if (op == "typeof") {
        lastValue_ = builder_.createTypeOf(operand);
    } else if (op == "++" || op == "--") {
        // Determine if operand is floating point
        bool isFloat = false;
        if (operand && operand->type && operand->type->kind == HIRTypeKind::Float64) {
            isFloat = true;
        } else if (node->operand->inferredType && node->operand->inferredType->kind == ts::TypeKind::Double) {
            isFloat = true;
        }
        // Check if operand is Any type (NaN-boxed) - need runtime dispatch
        bool isAny = false;
        if (!isFloat && operand && operand->type && operand->type->kind == HIRTypeKind::Any) {
            isAny = true;
        }

        std::shared_ptr<HIRValue> result;
        if (isAny) {
            // For NaN-boxed values, use ts_value_inc/dec which coerce to number
            // (unlike ts_value_add which does string concatenation for strings)
            result = (op == "++") ? builder_.createCall("ts_value_inc", {operand}, HIRType::makeAny())
                                  : builder_.createCall("ts_value_dec", {operand}, HIRType::makeAny());
        } else if (isFloat) {
            auto one = builder_.createConstFloat(1.0);
            result = (op == "++") ? builder_.createAddF64(operand, one)
                                  : builder_.createSubF64(operand, one);
        } else {
            auto one = builder_.createConstInt(1);
            result = (op == "++") ? builder_.createAddI64(operand, one)
                                  : builder_.createSubI64(operand, one);
        }

        // Update variable if operand is an identifier
        auto* ident = dynamic_cast<ast::Identifier*>(node->operand.get());
        if (ident) {
            // For module-scoped variables from inner functions, use __modvar_ globals
            bool handledAsModGlobal = false;
            if (currentFunction_ && isModuleGlobalVar(ident->name)) {
                size_t scopeIdx = 0;
                if (isCapturedVariable(ident->name, &scopeIdx)) {
                    builder_.createStoreGlobal(modVarName(ident->name), result);
                    handledAsModGlobal = true;
                }
            }
            if (!handledAsModGlobal) {
                // Check if this is a captured variable from an outer function
                size_t scopeIndex = 0;
                if (currentFunction_ && isCapturedVariable(ident->name, &scopeIndex)) {
                    // Store to captured variable
                    auto* info = lookupVariableInfo(ident->name);
                    auto type = info && info->elemType ? info->elemType : result->type;
                    registerCapture(ident->name, type, scopeIndex);
                    currentFunction_->hasClosure = true;
                    builder_.createStoreCapture(ident->name, result);
                } else {
                    auto* info = lookupVariableInfo(ident->name);
                    if (info && info->isAlloca) {
                        builder_.createStore(result, info->value, info->elemType);
                        // If captured by nested closure, also update the cell
                        if (info->isCapturedByNested && info->closurePtr && info->captureIndex >= 0) {
                            auto closureVal = builder_.createLoad(HIRType::makeAny(), info->closurePtr);
                            builder_.createStoreCaptureFromClosure(closureVal, info->captureIndex, result);
                        }
                        // If used by inner function AND module global, also update __modvar_
                        if (isModuleGlobalUsedByInner(ident->name)) {
                            builder_.createStoreGlobal(modVarName(ident->name), result);
                        }
                    } else {
                        defineVariable(ident->name, result);
                    }
                }
            }
        }
        // Handle property access (e.g., this.#count++ or obj.field++)
        auto* prop = dynamic_cast<ast::PropertyAccessExpression*>(node->operand.get());
        if (prop) {
            auto obj = lowerExpression(prop->expression.get());
            std::string propName = prop->name;
            builder_.createSetPropStatic(obj, propName, result);
        }
        lastValue_ = result;  // Prefix returns new value
    } else {
        lastValue_ = operand;
    }
}

void ASTToHIR::visitDeleteExpression(ast::DeleteExpression* node) {
    setSourceLine(node);
    // Handle delete obj.prop or delete obj["prop"]
    if (auto* propAccess = dynamic_cast<ast::PropertyAccessExpression*>(node->expression.get())) {
        // delete obj.prop
        auto obj = lowerExpression(propAccess->expression.get());
        auto key = builder_.createConstString(propAccess->name);
        lastValue_ = builder_.createCall("ts_object_delete_property", {obj, key}, HIRType::makeBool());
        return;
    }

    if (auto* elemAccess = dynamic_cast<ast::ElementAccessExpression*>(node->expression.get())) {
        // delete obj["prop"] or delete obj[key]
        auto obj = lowerExpression(elemAccess->expression.get());
        auto key = lowerExpression(elemAccess->argumentExpression.get());
        lastValue_ = builder_.createCall("ts_object_delete_property", {obj, key}, HIRType::makeBool());
        return;
    }

    // For other cases (like delete x), just return true
    // JavaScript spec says delete on non-references returns true
    lastValue_ = builder_.createConstBool(true);
}

void ASTToHIR::visitPostfixUnaryExpression(ast::PostfixUnaryExpression* node) {
    setSourceLine(node);
    auto operand = lowerExpression(node->operand.get());
    auto oldValue = operand;

    const std::string& op = node->op;
    if (op == "++" || op == "--") {
        // Determine if operand is floating point
        bool isFloat = false;
        if (operand && operand->type && operand->type->kind == HIRTypeKind::Float64) {
            isFloat = true;
        } else if (node->operand->inferredType && node->operand->inferredType->kind == ts::TypeKind::Double) {
            isFloat = true;
        }
        // Check if operand is Any type (NaN-boxed) - need runtime dispatch
        bool isAny = false;
        if (!isFloat && operand && operand->type && operand->type->kind == HIRTypeKind::Any) {
            isAny = true;
        }

        std::shared_ptr<HIRValue> result;
        if (isAny) {
            // For NaN-boxed values, use ts_value_inc/dec which coerce to number
            // (unlike ts_value_add which does string concatenation for strings)
            result = (op == "++") ? builder_.createCall("ts_value_inc", {operand}, HIRType::makeAny())
                                  : builder_.createCall("ts_value_dec", {operand}, HIRType::makeAny());
        } else if (isFloat) {
            auto one = builder_.createConstFloat(1.0);
            result = (op == "++") ? builder_.createAddF64(operand, one)
                                  : builder_.createSubF64(operand, one);
        } else {
            auto one = builder_.createConstInt(1);
            result = (op == "++") ? builder_.createAddI64(operand, one)
                                  : builder_.createSubI64(operand, one);
        }

        // Update variable if operand is an identifier
        auto* ident = dynamic_cast<ast::Identifier*>(node->operand.get());
        if (ident) {
            // For module-scoped variables from inner functions, use __modvar_ globals
            bool handledAsModGlobal = false;
            if (currentFunction_ && isModuleGlobalVar(ident->name)) {
                size_t scopeIdx = 0;
                if (isCapturedVariable(ident->name, &scopeIdx)) {
                    builder_.createStoreGlobal(modVarName(ident->name), result);
                    handledAsModGlobal = true;
                }
            }
            if (!handledAsModGlobal) {
                // Check if this is a captured variable from an outer function
                size_t scopeIndex = 0;
                if (currentFunction_ && isCapturedVariable(ident->name, &scopeIndex)) {
                    // Store to captured variable
                    auto* info = lookupVariableInfo(ident->name);
                    auto type = info && info->elemType ? info->elemType : result->type;
                    registerCapture(ident->name, type, scopeIndex);
                    currentFunction_->hasClosure = true;
                    builder_.createStoreCapture(ident->name, result);
                } else {
                    auto* info = lookupVariableInfo(ident->name);
                    if (info && info->isAlloca) {
                        builder_.createStore(result, info->value, info->elemType);
                        // If captured by nested closure, also update the cell
                        if (info->isCapturedByNested && info->closurePtr && info->captureIndex >= 0) {
                            auto closureVal = builder_.createLoad(HIRType::makeAny(), info->closurePtr);
                            builder_.createStoreCaptureFromClosure(closureVal, info->captureIndex, result);
                        }
                        // If used by inner function AND module global, also update __modvar_
                        if (isModuleGlobalUsedByInner(ident->name)) {
                            builder_.createStoreGlobal(modVarName(ident->name), result);
                        }
                    } else {
                        defineVariable(ident->name, result);
                    }
                }
            }
        }
        // Handle property access (e.g., this.#count++ or obj.field++)
        auto* prop = dynamic_cast<ast::PropertyAccessExpression*>(node->operand.get());
        if (prop) {
            auto obj = lowerExpression(prop->expression.get());
            std::string propName = prop->name;
            builder_.createSetPropStatic(obj, propName, result);
        }
        // Postfix returns old value
        lastValue_ = oldValue;
    } else {
        lastValue_ = operand;
    }
}

void ASTToHIR::visitClassDeclaration(ast::ClassDeclaration* node) {
    setSourceLine(node);
    SPDLOG_WARN("visitClassDeclaration: name={} currentFunc={}",
        node->name, currentFunction_ ? currentFunction_->name : "null");

    // Create HIR class
    auto* hirClass = builder_.createClass(node->name);
    if (!hirClass) return;

    // Track the current class for 'this' handling
    HIRClass* savedClass = currentClass_;
    currentClass_ = hirClass;

    // Handle inheritance - look up base class
    if (!node->baseClass.empty()) {
        for (auto& cls : module_->classes) {
            if (cls->name == node->baseClass) {
                hirClass->baseClass = cls.get();
                break;
            }
        }
    }

    // Create class shape (layout of instance properties)
    auto shape = std::make_shared<HIRShape>();
    shape->className = node->name;

    // First pass: collect properties for the shape
    uint32_t propertyOffset = 0;

    // If we have a base class, copy its properties first
    if (hirClass->baseClass && hirClass->baseClass->shape) {
        auto baseShape = hirClass->baseClass->shape;
        shape->parent = baseShape.get();
        // Copy base class properties
        for (const auto& [name, offset] : baseShape->propertyOffsets) {
            shape->propertyOffsets[name] = offset;
        }
        for (const auto& [name, type] : baseShape->propertyTypes) {
            shape->propertyTypes[name] = type;
        }
        propertyOffset = baseShape->size;  // Start our properties after base class properties
    }

    // Add this class's own properties
    for (auto& memberPtr : node->members) {
        if (auto* propDef = dynamic_cast<ast::PropertyDefinition*>(memberPtr.get())) {
            if (!propDef->isStatic) {
                auto propType = propDef->type.empty()
                    ? HIRType::makeAny()
                    : convertTypeFromString(propDef->type);
                shape->propertyOffsets[propDef->name] = propertyOffset;
                shape->propertyTypes[propDef->name] = propType;
                propertyOffset++;
            }
        }
    }

    // Scan constructor body for this.x = expr assignments
    for (auto& memberPtr : node->members) {
        if (auto* method = dynamic_cast<ast::MethodDefinition*>(memberPtr.get())) {
            if (method->name == "constructor" && method->hasBody) {
                scanConstructorBodyForProperties(method->body, shape, propertyOffset);
                break;
            }
        }
    }

    shape->size = propertyOffset;
    hirClass->shape = shape;

    // Register class shape for flat object codegen if it has properties or instance methods.
    // Classes with methods but no PropertyDefinition fields (e.g., JS classes where properties
    // are assigned in the constructor body) still need flat objects for vtable method dispatch.
    {
        bool hasInstanceMethods = false;
        for (auto& memberPtr : node->members) {
            if (auto* md = dynamic_cast<ast::MethodDefinition*>(memberPtr.get())) {
                if (md->name != "constructor" && !md->isStatic && !md->isAbstract && md->hasBody) {
                    hasInstanceMethods = true;
                    break;
                }
            }
        }
        if (!shape->propertyOffsets.empty() || hasInstanceMethods) {
            shape->id = nextShapeId_++;
            module_->shapes.push_back(shape);
        }
    }

    // Static property pass: create globals for static properties
    for (auto& memberPtr : node->members) {
        if (auto* propDef = dynamic_cast<ast::PropertyDefinition*>(memberPtr.get())) {
            if (propDef->isStatic) {
                auto propType = propDef->type.empty()
                    ? HIRType::makeAny()
                    : convertTypeFromString(propDef->type);
                std::string globalName = node->name + "_static_" + propDef->name;

                // Create global variable for the static property
                auto globalPtr = builder_.createGlobal(globalName, propType);
                staticPropertyGlobals_[globalName] = {globalPtr, propType};

                // Defer initialization to user_main
                if (propDef->initializer) {
                    deferredStaticInits_.push_back({globalPtr, propType, propDef->initializer.get()});
                }
            }
        }
        // Collect static blocks for deferred execution
        if (auto* staticBlock = dynamic_cast<ast::StaticBlock*>(memberPtr.get())) {
            deferredStaticBlocks_.push_back(staticBlock);
        }
    }

    // Inherit abstract methods from base class
    if (hirClass->baseClass) {
        hirClass->abstractMethods = hirClass->baseClass->abstractMethods;
    }

    // Track abstract methods declared in this class and pre-register concrete methods.
    // Pre-registration ensures that when lowering method bodies, calls to other methods
    // in the same class (defined later) can be found in the methods map.
    for (auto& memberPtr : node->members) {
        if (auto* methodDef = dynamic_cast<ast::MethodDefinition*>(memberPtr.get())) {
            if (methodDef->isAbstract) {
                hirClass->abstractMethods.insert(methodDef->name);
            } else if (methodDef->hasBody && methodDef->name != "constructor") {
                // Concrete method overrides abstract - remove from set
                hirClass->abstractMethods.erase(methodDef->name);
                // Pre-register with nullptr so forward references resolve
                std::string methodKey = methodDef->name;
                if (methodDef->isGetter) methodKey = "__getter_" + methodDef->name;
                else if (methodDef->isSetter) methodKey = "__setter_" + methodDef->name;
                if (!methodDef->isStatic && hirClass->methods.find(methodKey) == hirClass->methods.end()) {
                    hirClass->methods[methodKey] = nullptr;
                }
            }
        }
    }

    // Second pass: create methods
    for (auto& memberPtr : node->members) {
        if (auto* methodDef = dynamic_cast<ast::MethodDefinition*>(memberPtr.get())) {
            // Skip abstract methods - they have no body
            if (methodDef->isAbstract || !methodDef->hasBody) {
                continue;
            }

            // Generate a unique function name for the method
            std::string methodFuncName;
            std::string methodKey = methodDef->name;  // Key used for registration in class
            if (methodDef->name == "constructor") {
                methodFuncName = node->name + "_constructor";
            } else if (methodDef->isGetter) {
                // Getter: ClassName___getter_propName
                methodFuncName = node->name + "___getter_" + methodDef->name;
                methodKey = "__getter_" + methodDef->name;
            } else if (methodDef->isSetter) {
                // Setter: ClassName___setter_propName
                methodFuncName = node->name + "___setter_" + methodDef->name;
                methodKey = "__setter_" + methodDef->name;
            } else if (methodDef->isStatic) {
                methodFuncName = node->name + "_static_" + methodDef->name;
            } else {
                methodFuncName = node->name + "_" + methodDef->name;
            }

            // Create HIR function for this method
            auto func = std::make_unique<HIRFunction>(methodFuncName);
            func->isAsync = methodDef->isAsync;
            func->isGenerator = methodDef->isGenerator;
            func->sourceLine = methodDef->line;
            func->sourceFile = methodDef->sourceFile;

            // For instance methods (and constructor), 'this' is the first parameter
            if (!methodDef->isStatic) {
                func->params.push_back({"this", HIRType::makeObject()});
            }

            // Add explicit parameters
            for (auto& param : methodDef->parameters) {
                auto paramType = param->type.empty()
                    ? HIRType::makeAny()
                    : convertTypeFromString(param->type);

                std::string paramName;
                if (auto* ident = dynamic_cast<ast::Identifier*>(param->name.get())) {
                    paramName = ident->name;
                } else {
                    paramName = "param" + std::to_string(func->params.size());
                }
                func->params.push_back({paramName, paramType});
            }

            // Set return type
            // Setters always return void, regardless of explicit type annotation
            if (methodDef->isSetter) {
                func->returnType = HIRType::makeVoid();
            } else {
                func->returnType = methodDef->returnType.empty()
                    ? HIRType::makeAny()
                    : convertTypeFromString(methodDef->returnType);
            }

            // Save current function and create entry block
            HIRFunction* savedFunc = currentFunction_;
            currentFunction_ = func.get();

            // Create entry block
            auto entryBlock = func->createBlock("entry");
            builder_.setInsertPoint(entryBlock);
            currentBlock_ = entryBlock;

            // Enter function scope
            pushFunctionScope(func.get());

            // Register parameters in scope
            for (size_t i = 0; i < func->params.size(); ++i) {
                const auto& [paramName, paramType] = func->params[i];
                auto paramValue = std::make_shared<HIRValue>(static_cast<uint32_t>(i), paramType, paramName);
                defineVariable(paramName, paramValue);
            }
            func->nextValueId = static_cast<uint32_t>(func->params.size());

            // For constructors, initialize instance property default values before user code
            if (methodDef->name == "constructor") {
                // Get 'this' pointer (first parameter)
                auto thisValue = lookupVariable("this");
                if (thisValue) {
                    // Iterate over all property definitions and emit initializers
                    for (auto& member : node->members) {
                        if (auto* propDef = dynamic_cast<ast::PropertyDefinition*>(member.get())) {
                            if (!propDef->isStatic && propDef->initializer) {
                                // Lower the initializer expression
                                auto initVal = lowerExpression(propDef->initializer.get());

                                // Store the initialized value to the property
                                builder_.createSetPropStatic(thisValue, propDef->name, initVal);
                            }
                        }
                    }
                }
            }

            // Lower method body
            for (auto& stmt : methodDef->body) {
                lowerStatement(stmt.get());
            }

            // Add implicit return if no terminator
            if (!hasTerminator()) {
                builder_.createReturnVoid();
            }

            popScope();

            // Restore saved function
            currentFunction_ = savedFunc;
            if (savedFunc) {
                auto* savedBlock = savedFunc->getEntryBlock();
                builder_.setInsertPoint(savedBlock);
                currentBlock_ = savedBlock;
            }

            // Register method in the class
            HIRFunction* funcPtr = func.get();
            if (methodDef->name == "constructor") {
                hirClass->constructor = funcPtr;
            } else if (methodDef->isStatic) {
                hirClass->staticMethods[methodDef->name] = funcPtr;
            } else {
                // Use methodKey for registration (includes __getter_/__setter_ prefix for accessors)
                hirClass->methods[methodKey] = funcPtr;
                // Add to vtable for virtual dispatch
                hirClass->vtable.push_back({methodKey, funcPtr});
            }

            // Add function to module
            module_->functions.push_back(std::move(func));
        }
    }

    // If no explicit constructor was defined, but we have property initializers,
    // generate a default constructor to initialize them
    if (!hirClass->constructor) {
        // Check if there are any property initializers
        bool hasPropertyInitializers = false;
        for (auto& memberPtr : node->members) {
            if (auto* propDef = dynamic_cast<ast::PropertyDefinition*>(memberPtr.get())) {
                if (!propDef->isStatic && propDef->initializer) {
                    hasPropertyInitializers = true;
                    break;
                }
            }
        }

        // Also need constructor if we have a base class (to call super())
        bool needsDefaultConstructor = hasPropertyInitializers || hirClass->baseClass;

        if (needsDefaultConstructor) {
            std::string ctorName = node->name + "_constructor";
            auto defaultCtor = std::make_unique<HIRFunction>(ctorName);

            // 'this' is the first parameter
            defaultCtor->params.push_back({"this", HIRType::makeObject()});
            defaultCtor->nextValueId = 1;

            // Create entry block
            HIRBlock* ctorBlock = defaultCtor->createBlock("entry");
            HIRFunction* savedFunc = currentFunction_;
            currentFunction_ = defaultCtor.get();
            builder_.setInsertPoint(ctorBlock);
            currentBlock_ = ctorBlock;
            pushScope();

            // Define 'this' in scope
            auto thisValue = std::make_shared<HIRValue>(0, HIRType::makeObject(), "this");
            defineVariable("this", thisValue);

            // Call super() if we have a base class
            if (hirClass->baseClass && hirClass->baseClass->constructor) {
                std::vector<std::shared_ptr<HIRValue>> superArgs;
                superArgs.push_back(thisValue);
                builder_.createCall(hirClass->baseClass->constructor->name, superArgs, HIRType::makeVoid());
            }

            // Initialize property defaults
            for (auto& memberPtr : node->members) {
                if (auto* propDef = dynamic_cast<ast::PropertyDefinition*>(memberPtr.get())) {
                    if (!propDef->isStatic && propDef->initializer) {
                        auto initVal = lowerExpression(propDef->initializer.get());
                        builder_.createSetPropStatic(thisValue, propDef->name, initVal);
                    }
                }
            }

            // Return void
            builder_.createReturnVoid();

            popScope();
            currentFunction_ = savedFunc;
            if (savedFunc) {
                auto* savedBlock = savedFunc->getEntryBlock();
                builder_.setInsertPoint(savedBlock);
                currentBlock_ = savedBlock;
            }

            // Register the default constructor
            hirClass->constructor = defaultCtor.get();
            module_->functions.push_back(std::move(defaultCtor));
        }
    }

    // Generate decorator static init function if class has any decorators
    // (class decorators, method decorators, property decorators, or parameter decorators)
    generateClassDecoratorStaticInit(node->name, node->decorators, node->members);

    // Restore class context
    currentClass_ = savedClass;
}

void ASTToHIR::visitClassExpression(ast::ClassExpression* node) {
    setSourceLine(node);
    // Generate a unique class name for anonymous class expressions
    // Use the same naming convention as the analyzer (__anon_class_X)
    std::string className = node->name.empty()
        ? "__anon_class_" + std::to_string(classExprCounter_++)
        : node->name;

    // Create HIR class
    auto* hirClass = builder_.createClass(className);
    if (!hirClass) {
        lastValue_ = builder_.createConstNull();
        return;
    }

    // Track the current class for 'this' handling
    HIRClass* savedClass = currentClass_;
    currentClass_ = hirClass;

    // Handle inheritance - look up base class
    if (!node->baseClass.empty()) {
        for (auto& cls : module_->classes) {
            if (cls->name == node->baseClass) {
                hirClass->baseClass = cls.get();
                break;
            }
        }
    }

    // Create class shape (layout of instance properties)
    auto shape = std::make_shared<HIRShape>();
    shape->className = className;

    // First pass: collect properties for the shape
    uint32_t propertyOffset = 0;

    // If we have a base class, copy its properties first
    if (hirClass->baseClass && hirClass->baseClass->shape) {
        auto baseShape = hirClass->baseClass->shape;
        shape->parent = baseShape.get();
        // Copy base class properties
        for (const auto& [name, offset] : baseShape->propertyOffsets) {
            shape->propertyOffsets[name] = offset;
        }
        for (const auto& [name, type] : baseShape->propertyTypes) {
            shape->propertyTypes[name] = type;
        }
        propertyOffset = baseShape->size;  // Start our properties after base class properties
    }

    // Add this class's own properties
    for (auto& memberPtr : node->members) {
        if (auto* propDef = dynamic_cast<ast::PropertyDefinition*>(memberPtr.get())) {
            if (!propDef->isStatic) {
                auto propType = propDef->type.empty()
                    ? HIRType::makeAny()
                    : convertTypeFromString(propDef->type);
                shape->propertyOffsets[propDef->name] = propertyOffset;
                shape->propertyTypes[propDef->name] = propType;
                propertyOffset++;
            }
        }
    }

    // Scan constructor body for this.x = expr assignments
    for (auto& memberPtr : node->members) {
        if (auto* method = dynamic_cast<ast::MethodDefinition*>(memberPtr.get())) {
            if (method->name == "constructor" && method->hasBody) {
                scanConstructorBodyForProperties(method->body, shape, propertyOffset);
                break;
            }
        }
    }

    shape->size = propertyOffset;
    hirClass->shape = shape;

    // Register class shape for flat object codegen if it has properties or instance methods
    {
        bool hasInstanceMethods = false;
        for (auto& memberPtr : node->members) {
            if (auto* md = dynamic_cast<ast::MethodDefinition*>(memberPtr.get())) {
                if (md->name != "constructor" && !md->isStatic && !md->isAbstract && md->hasBody) {
                    hasInstanceMethods = true;
                    break;
                }
            }
        }
        if (!shape->propertyOffsets.empty() || hasInstanceMethods) {
            shape->id = nextShapeId_++;
            module_->shapes.push_back(shape);
        }
    }

    // Static property pass: create globals for static properties
    for (auto& memberPtr : node->members) {
        if (auto* propDef = dynamic_cast<ast::PropertyDefinition*>(memberPtr.get())) {
            if (propDef->isStatic) {
                auto propType = propDef->type.empty()
                    ? HIRType::makeAny()
                    : convertTypeFromString(propDef->type);
                std::string globalName = className + "_static_" + propDef->name;

                // Create global variable for the static property
                auto globalPtr = builder_.createGlobal(globalName, propType);
                staticPropertyGlobals_[globalName] = {globalPtr, propType};

                // Defer initialization to user_main
                if (propDef->initializer) {
                    deferredStaticInits_.push_back({globalPtr, propType, propDef->initializer.get()});
                }
            }
        }
        // Collect static blocks for deferred execution
        if (auto* staticBlock = dynamic_cast<ast::StaticBlock*>(memberPtr.get())) {
            deferredStaticBlocks_.push_back(staticBlock);
        }
    }

    // Save the current insert point before processing methods
    // (so we can restore it after and not pollute method bodies with later instructions)
    HIRBlock* savedBlockBeforeMethods = currentBlock_;
    HIRFunction* savedFuncBeforeMethods = currentFunction_;

    // Second pass: create methods
    for (auto& memberPtr : node->members) {
        if (auto* methodDef = dynamic_cast<ast::MethodDefinition*>(memberPtr.get())) {
            // Skip abstract methods - they have no body
            if (methodDef->isAbstract || !methodDef->hasBody) {
                continue;
            }

            // Generate a unique function name for the method
            std::string methodFuncName;
            std::string methodKey = methodDef->name;  // Key used for registration in class
            if (methodDef->name == "constructor") {
                methodFuncName = className + "_constructor";
            } else if (methodDef->isGetter) {
                // Getter: ClassName___getter_propName
                methodFuncName = className + "___getter_" + methodDef->name;
                methodKey = "__getter_" + methodDef->name;
            } else if (methodDef->isSetter) {
                // Setter: ClassName___setter_propName
                methodFuncName = className + "___setter_" + methodDef->name;
                methodKey = "__setter_" + methodDef->name;
            } else if (methodDef->isStatic) {
                methodFuncName = className + "_static_" + methodDef->name;
            } else {
                methodFuncName = className + "_" + methodDef->name;
            }

            // Create HIR function for this method
            auto func = std::make_unique<HIRFunction>(methodFuncName);
            func->isAsync = methodDef->isAsync;
            func->isGenerator = methodDef->isGenerator;
            func->sourceLine = methodDef->line;
            func->sourceFile = methodDef->sourceFile;

            // For instance methods (and constructor), 'this' is the first parameter
            if (!methodDef->isStatic) {
                func->params.push_back({"this", HIRType::makeObject()});
            }

            // Add explicit parameters
            for (auto& param : methodDef->parameters) {
                auto paramType = param->type.empty()
                    ? HIRType::makeAny()
                    : convertTypeFromString(param->type);

                std::string paramName;
                if (auto* ident = dynamic_cast<ast::Identifier*>(param->name.get())) {
                    paramName = ident->name;
                } else {
                    paramName = "param" + std::to_string(func->params.size());
                }
                func->params.push_back({paramName, paramType});
            }

            // Set return type
            // Setters always return void, regardless of explicit type annotation
            if (methodDef->isSetter) {
                func->returnType = HIRType::makeVoid();
            } else {
                func->returnType = methodDef->returnType.empty()
                    ? HIRType::makeAny()
                    : convertTypeFromString(methodDef->returnType);
            }

            // Save current function and create entry block
            HIRFunction* savedFunc = currentFunction_;
            currentFunction_ = func.get();

            // Create entry block
            auto entryBlock = func->createBlock("entry");
            builder_.setInsertPoint(entryBlock);
            currentBlock_ = entryBlock;

            // Enter function scope
            pushFunctionScope(func.get());

            // Register parameters in scope
            for (size_t i = 0; i < func->params.size(); ++i) {
                const auto& [paramName, paramType] = func->params[i];
                auto paramValue = std::make_shared<HIRValue>(static_cast<uint32_t>(i), paramType, paramName);
                defineVariable(paramName, paramValue);
            }
            func->nextValueId = static_cast<uint32_t>(func->params.size());

            // For constructors, initialize instance property default values before user code
            if (methodDef->name == "constructor") {
                // Get 'this' pointer (first parameter)
                auto thisValue = lookupVariable("this");
                if (thisValue) {
                    // Iterate over all property definitions and emit initializers
                    for (auto& member : node->members) {
                        if (auto* propDef = dynamic_cast<ast::PropertyDefinition*>(member.get())) {
                            if (!propDef->isStatic && propDef->initializer) {
                                // Lower the initializer expression
                                auto initVal = lowerExpression(propDef->initializer.get());

                                // Store the initialized value to the property
                                builder_.createSetPropStatic(thisValue, propDef->name, initVal);
                            }
                        }
                    }
                }
            }

            // Lower method body
            for (auto& stmt : methodDef->body) {
                lowerStatement(stmt.get());
            }

            // Add implicit return if no terminator
            if (!hasTerminator()) {
                builder_.createReturnVoid();
            }

            popScope();

            // Restore saved function
            currentFunction_ = savedFunc;
            if (savedFunc) {
                auto* savedBlock = savedFunc->getEntryBlock();
                builder_.setInsertPoint(savedBlock);
                currentBlock_ = savedBlock;
            }

            // Register method in the class
            HIRFunction* funcPtr = func.get();
            if (methodDef->name == "constructor") {
                hirClass->constructor = funcPtr;
            } else if (methodDef->isStatic) {
                hirClass->staticMethods[methodDef->name] = funcPtr;
            } else {
                // Use methodKey for registration (includes __getter_/__setter_ prefix for accessors)
                hirClass->methods[methodKey] = funcPtr;
                // Add to vtable for virtual dispatch
                hirClass->vtable.push_back({methodKey, funcPtr});
            }

            // Add function to module
            module_->functions.push_back(std::move(func));
        }
    }

    // If no explicit constructor was defined, but we have property initializers,
    // generate a default constructor to initialize them
    if (!hirClass->constructor) {
        // Check if there are any property initializers
        bool hasPropertyInitializers = false;
        for (auto& memberPtr : node->members) {
            if (auto* propDef = dynamic_cast<ast::PropertyDefinition*>(memberPtr.get())) {
                if (!propDef->isStatic && propDef->initializer) {
                    hasPropertyInitializers = true;
                    break;
                }
            }
        }

        // Also need constructor if we have a base class (to call super())
        bool needsDefaultConstructor = hasPropertyInitializers || hirClass->baseClass;

        if (needsDefaultConstructor) {
            std::string ctorName = className + "_constructor";
            auto defaultCtor = std::make_unique<HIRFunction>(ctorName);

            // 'this' is the first parameter
            defaultCtor->params.push_back({"this", HIRType::makeObject()});
            defaultCtor->nextValueId = 1;

            // Create entry block
            HIRBlock* ctorBlock = defaultCtor->createBlock("entry");
            HIRFunction* savedFunc = currentFunction_;
            currentFunction_ = defaultCtor.get();
            builder_.setInsertPoint(ctorBlock);
            currentBlock_ = ctorBlock;
            pushScope();

            // Define 'this' in scope
            auto thisValue = std::make_shared<HIRValue>(0, HIRType::makeObject(), "this");
            defineVariable("this", thisValue);

            // Call super() if we have a base class
            if (hirClass->baseClass && hirClass->baseClass->constructor) {
                std::vector<std::shared_ptr<HIRValue>> superArgs;
                superArgs.push_back(thisValue);
                builder_.createCall(hirClass->baseClass->constructor->name, superArgs, HIRType::makeVoid());
            }

            // Initialize property defaults
            for (auto& memberPtr : node->members) {
                if (auto* propDef = dynamic_cast<ast::PropertyDefinition*>(memberPtr.get())) {
                    if (!propDef->isStatic && propDef->initializer) {
                        auto initVal = lowerExpression(propDef->initializer.get());
                        builder_.createSetPropStatic(thisValue, propDef->name, initVal);
                    }
                }
            }

            // Return void
            builder_.createReturnVoid();

            popScope();
            currentFunction_ = savedFunc;
            if (savedFunc) {
                auto* savedBlock = savedFunc->getEntryBlock();
                builder_.setInsertPoint(savedBlock);
                currentBlock_ = savedBlock;
            }

            // Register the default constructor
            hirClass->constructor = defaultCtor.get();
            module_->functions.push_back(std::move(defaultCtor));
        }
    }

    // Restore the insert point to what it was before processing methods
    currentFunction_ = savedFuncBeforeMethods;
    currentBlock_ = savedBlockBeforeMethods;
    if (savedBlockBeforeMethods) {
        builder_.setInsertPoint(savedBlockBeforeMethods);
    }

    // Restore class context
    currentClass_ = savedClass;

    // Store the generated class name for variable tracking (used by visitVariableDeclaration)
    lastGeneratedClassName_ = className;

    // The result of a class expression is a reference to the class constructor
    // We use LoadFunction to get the constructor pointer
    if (hirClass->constructor) {
        lastValue_ = builder_.createLoadFunction(hirClass->constructor->name);
    } else {
        // If no explicit constructor, load the implicit default constructor
        // For now, just return a pointer to the class (the runtime will handle allocation)
        lastValue_ = builder_.createLoadFunction(className + "_constructor");
    }

    // Set up prototype object with instance methods for dynamic dispatch.
    // This is critical for untyped JS classes (e.g. npm modules) where method
    // calls go through ts_object_get_property -> prototype chain lookup.
    if (!hirClass->methods.empty()) {
        auto ctorVal = lastValue_;

        // Create prototype TsMap
        auto proto = builder_.createCall("ts_object_create_empty", {}, HIRType::makeAny());

        // Populate prototype with instance methods
        for (auto& [methodKey, methodFunc] : hirClass->methods) {
            if (!methodFunc) continue;  // skip abstract methods

            // Load the method as a closure (LoadFunction wraps in TsClosure)
            auto methodClosure = builder_.createLoadFunction(methodFunc->name);

            // Store on prototype: proto.methodName = closure
            // For getters/setters, methodKey already has __getter_/__setter_ prefix
            builder_.createSetPropStatic(proto, methodKey, methodClosure);
        }

        // Set constructor.prototype = proto
        builder_.createSetPropStatic(ctorVal, "prototype", proto);
    }
}

void ASTToHIR::visitInterfaceDeclaration(ast::InterfaceDeclaration* node) {
    setSourceLine(node);
    // Interfaces are type-only, nothing to generate
}

void ASTToHIR::visitObjectBindingPattern(ast::ObjectBindingPattern* node) {
    setSourceLine(node);
    // Handled during variable declaration
}

void ASTToHIR::visitArrayBindingPattern(ast::ArrayBindingPattern* node) {
    setSourceLine(node);
    // Handled during variable declaration
}

void ASTToHIR::visitBindingElement(ast::BindingElement* node) {
    setSourceLine(node);
    // Handled during variable declaration
}

void ASTToHIR::visitSpreadElement(ast::SpreadElement* node) {
    setSourceLine(node);
    if (node->expression) {
        lastValue_ = lowerExpression(node->expression.get());
    }
}

void ASTToHIR::visitOmittedExpression(ast::OmittedExpression* node) {
    setSourceLine(node);
    lastValue_ = builder_.createConstUndefined();
}

void ASTToHIR::visitTypeAliasDeclaration(ast::TypeAliasDeclaration* node) {
    setSourceLine(node);
    // Type aliases are type-only, nothing to generate
}

// Compile-time constant expression evaluator for enum member initializers.
// Returns {true, value} on success, {false, 0} if expression cannot be evaluated.
std::pair<bool, int64_t> ASTToHIR::constEvalEnumExpr(
    ast::Node* expr, const std::map<std::string, EnumValue>& members,
    const std::string& enumName) {

    if (auto* numLit = dynamic_cast<ast::NumericLiteral*>(expr)) {
        return {true, static_cast<int64_t>(numLit->value)};
    }

    if (auto* binExpr = dynamic_cast<ast::BinaryExpression*>(expr)) {
        auto [lok, lval] = constEvalEnumExpr(binExpr->left.get(), members, enumName);
        auto [rok, rval] = constEvalEnumExpr(binExpr->right.get(), members, enumName);
        if (!lok || !rok) return {false, 0};

        if (binExpr->op == "+") return {true, lval + rval};
        if (binExpr->op == "-") return {true, lval - rval};
        if (binExpr->op == "*") return {true, lval * rval};
        if (binExpr->op == "/" && rval != 0) return {true, lval / rval};
        if (binExpr->op == "%") return {true, lval % rval};
        if (binExpr->op == "<<") return {true, lval << rval};
        if (binExpr->op == ">>") return {true, lval >> rval};
        if (binExpr->op == "|") return {true, lval | rval};
        if (binExpr->op == "&") return {true, lval & rval};
        if (binExpr->op == "^") return {true, lval ^ rval};
        return {false, 0};
    }

    // Identifier referencing another enum member
    if (auto* ident = dynamic_cast<ast::Identifier*>(expr)) {
        auto it = members.find(ident->name);
        if (it != members.end() && !it->second.isString) {
            return {true, it->second.numValue};
        }
        return {false, 0};
    }

    // PropertyAccess: "hello".length or EnumName.Member
    if (auto* propAccess = dynamic_cast<ast::PropertyAccessExpression*>(expr)) {
        if (propAccess->name == "length") {
            if (auto* strLit = dynamic_cast<ast::StringLiteral*>(propAccess->expression.get())) {
                return {true, static_cast<int64_t>(strLit->value.size())};
            }
        }
        // EnumName.Member reference
        if (auto* ident = dynamic_cast<ast::Identifier*>(propAccess->expression.get())) {
            if (ident->name == enumName) {
                auto it = members.find(propAccess->name);
                if (it != members.end() && !it->second.isString) {
                    return {true, it->second.numValue};
                }
            }
        }
        return {false, 0};
    }

    // Math.floor/ceil/round/trunc/abs(expr)
    if (auto* callExpr = dynamic_cast<ast::CallExpression*>(expr)) {
        auto* prop = dynamic_cast<ast::PropertyAccessExpression*>(callExpr->callee.get());
        if (prop) {
            auto* obj = dynamic_cast<ast::Identifier*>(prop->expression.get());
            if (obj && obj->name == "Math" && callExpr->arguments.size() == 1) {
                auto [ok, val] = constEvalEnumExpr(callExpr->arguments[0].get(), members, enumName);
                if (!ok) return {false, 0};
                double dval = static_cast<double>(val);
                // Also handle float literal arguments directly
                if (auto* flit = dynamic_cast<ast::NumericLiteral*>(callExpr->arguments[0].get())) {
                    dval = flit->value;
                }
                if (prop->name == "floor") return {true, static_cast<int64_t>(std::floor(dval))};
                if (prop->name == "ceil") return {true, static_cast<int64_t>(std::ceil(dval))};
                if (prop->name == "round") return {true, static_cast<int64_t>(std::round(dval))};
                if (prop->name == "trunc") return {true, static_cast<int64_t>(std::trunc(dval))};
                if (prop->name == "abs") return {true, static_cast<int64_t>(std::abs(dval))};
            }
        }
        return {false, 0};
    }

    // Unary prefix: -expr, ~expr
    if (auto* prefix = dynamic_cast<ast::PrefixUnaryExpression*>(expr)) {
        auto [ok, val] = constEvalEnumExpr(prefix->operand.get(), members, enumName);
        if (!ok) return {false, 0};
        if (prefix->op == "-") return {true, -val};
        if (prefix->op == "~") return {true, ~val};
        return {false, 0};
    }

    // Parenthesized expression
    if (auto* paren = dynamic_cast<ast::ParenthesizedExpression*>(expr)) {
        return constEvalEnumExpr(paren->expression.get(), members, enumName);
    }

    return {false, 0};
}

void ASTToHIR::visitEnumDeclaration(ast::EnumDeclaration* node) {
    setSourceLine(node);
    // Process enum members and store values
    std::map<std::string, EnumValue> members;
    std::map<int64_t, std::string> reverseMap;
    int64_t autoValue = 0;

    for (auto& member : node->members) {
        EnumValue ev;

        if (member.initializer) {
            // Has an explicit initializer
            if (auto* numLit = dynamic_cast<ast::NumericLiteral*>(member.initializer.get())) {
                ev.isString = false;
                ev.numValue = static_cast<int64_t>(numLit->value);
                autoValue = ev.numValue + 1;
                reverseMap[ev.numValue] = member.name;
            } else if (auto* strLit = dynamic_cast<ast::StringLiteral*>(member.initializer.get())) {
                ev.isString = true;
                ev.strValue = strLit->value;
            } else {
                // Try const-eval for computed initializers
                auto [ok, val] = constEvalEnumExpr(member.initializer.get(), members, node->name);
                if (ok) {
                    ev.isString = false;
                    ev.numValue = val;
                    autoValue = val + 1;
                    reverseMap[ev.numValue] = member.name;
                } else {
                    // Fallback to auto-increment
                    ev.isString = false;
                    ev.numValue = autoValue++;
                    reverseMap[ev.numValue] = member.name;
                }
            }
        } else {
            // Auto-increment numeric value
            ev.isString = false;
            ev.numValue = autoValue++;
            reverseMap[ev.numValue] = member.name;
        }

        members[member.name] = ev;
    }

    enumValues_[node->name] = std::move(members);
    if (!reverseMap.empty()) {
        enumReverseMap_[node->name] = std::move(reverseMap);
    }
}

//==============================================================================
// JSX Lowering
//==============================================================================

// Helper to lower JSX attributes into a props object
std::shared_ptr<HIRValue> ASTToHIR::lowerJsxAttributes(const std::vector<ast::NodePtr>& attributes) {
    // Create a new object for props
    auto propsObj = builder_.createNewObjectDynamic();

    for (const auto& attr : attributes) {
        if (auto* jsxAttr = dynamic_cast<ast::JsxAttribute*>(attr.get())) {
            // Regular attribute: <div name={value} /> or <div name="string" />
            auto propName = builder_.createConstString(jsxAttr->name);
            std::shared_ptr<HIRValue> propValue;

            if (jsxAttr->initializer) {
                // Attribute has a value
                propValue = lowerExpression(jsxAttr->initializer.get());
            } else {
                // Boolean attribute: <div disabled /> means disabled={true}
                propValue = builder_.createConstBool(true);
            }

            builder_.createSetPropDynamic(propsObj, propName, propValue);
        } else if (auto* spreadAttr = dynamic_cast<ast::JsxSpreadAttribute*>(attr.get())) {
            // Spread attribute: <div {...props} />
            // For now, we'll just skip spread attributes (would need Object.assign)
            // A more complete implementation would merge the spread object into props
            if (spreadAttr->expression) {
                // TODO: Implement spread merging with Object.assign
                // For now, just evaluate the expression for side effects
                lowerExpression(spreadAttr->expression.get());
            }
        }
    }

    return propsObj;
}

// Helper to lower JSX children into an array
std::shared_ptr<HIRValue> ASTToHIR::lowerJsxChildren(const std::vector<ast::ExprPtr>& children) {
    // Create a new array for children
    auto childArray = builder_.createNewArrayBoxed(builder_.createConstInt(static_cast<int64_t>(children.size())));

    int64_t index = 0;
    for (const auto& child : children) {
        auto childValue = lowerExpression(child.get());
        auto indexVal = builder_.createConstInt(index++);
        builder_.createSetElem(childArray, indexVal, childValue);
    }

    return childArray;
}

void ASTToHIR::visitJsxElement(ast::JsxElement* node) {
    setSourceLine(node);
    // Lower JSX element: <tagName attributes>children</tagName>
    // Creates an object { type: tagName, props: {...}, children: [...] }

    // Create tag name string
    auto tagName = builder_.createConstString(node->tagName);

    // Lower attributes to props object
    auto props = lowerJsxAttributes(node->attributes);

    // Lower children to array
    auto children = lowerJsxChildren(node->children);

    // Call ts_jsx_create_element(tagName, props, children)
    lastValue_ = builder_.createCall("ts_jsx_create_element", {tagName, props, children}, HIRType::makeObject());
}

void ASTToHIR::visitJsxSelfClosingElement(ast::JsxSelfClosingElement* node) {
    setSourceLine(node);
    // Lower self-closing JSX element: <tagName attributes />
    // Same as JsxElement but with empty children

    // Create tag name string
    auto tagName = builder_.createConstString(node->tagName);

    // Lower attributes to props object
    auto props = lowerJsxAttributes(node->attributes);

    // Create empty children array
    auto children = builder_.createNewArrayBoxed(builder_.createConstInt(0));

    // Call ts_jsx_create_element(tagName, props, children)
    lastValue_ = builder_.createCall("ts_jsx_create_element", {tagName, props, children}, HIRType::makeObject());
}

void ASTToHIR::visitJsxFragment(ast::JsxFragment* node) {
    setSourceLine(node);
    // Lower JSX fragment: <>children</>
    // Fragments have null tagName and no props

    // Null tagName for fragments
    auto tagName = builder_.createConstNull();

    // Empty props object for fragments
    auto props = builder_.createNewObjectDynamic();

    // Lower children to array
    auto children = lowerJsxChildren(node->children);

    // Call ts_jsx_create_element(null, {}, children)
    lastValue_ = builder_.createCall("ts_jsx_create_element", {tagName, props, children}, HIRType::makeObject());
}

void ASTToHIR::visitJsxExpression(ast::JsxExpression* node) {
    setSourceLine(node);
    if (node->expression) {
        lastValue_ = lowerExpression(node->expression.get());
    }
}

void ASTToHIR::visitJsxText(ast::JsxText* node) {
    setSourceLine(node);
    lastValue_ = builder_.createConstString(node->text);
}

} // namespace ts::hir
