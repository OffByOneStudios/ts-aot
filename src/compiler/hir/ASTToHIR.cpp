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

// ECMA-262 §14.3.2: collect all `var` declarations and `function`
// declarations reachable from `node`, without crossing a nested
// FunctionDeclaration / FunctionExpression / ArrowFunction boundary.
// Used by visitFunctionDeclaration / spec lowering to pre-declare every
// hoisted name on entry so assignments inside conditional branches bind
// to the same function-scope slot.
static void collectHoistedVarNames(ast::Node* node, std::vector<std::string>& out) {
    if (!node) return;
    // Stop at nested function bodies — they have their own VariableEnvironment.
    if (dynamic_cast<ast::FunctionExpression*>(node)) return;
    if (dynamic_cast<ast::FunctionDeclaration*>(node)) {
        // Hoist the function name itself if it's a declaration.
        auto* fd = static_cast<ast::FunctionDeclaration*>(node);
        if (!fd->name.empty()) out.push_back(fd->name);
        return;
    }
    if (dynamic_cast<ast::ArrowFunction*>(node)) return;
    if (auto* vd = dynamic_cast<ast::VariableDeclaration*>(node)) {
        if (vd->varKind == ast::VarKind::Var) {
            if (auto* ident = dynamic_cast<ast::Identifier*>(vd->name.get())) {
                out.push_back(ident->name);
            }
            // (binding patterns left to existing per-statement lowering)
        }
        if (vd->initializer) collectHoistedVarNames(vd->initializer.get(), out);
        return;
    }
    if (auto* block = dynamic_cast<ast::BlockStatement*>(node)) {
        for (auto& s : block->statements) collectHoistedVarNames(s.get(), out);
        return;
    }
    if (auto* expr = dynamic_cast<ast::ExpressionStatement*>(node)) {
        collectHoistedVarNames(expr->expression.get(), out);
        return;
    }
    if (auto* ret = dynamic_cast<ast::ReturnStatement*>(node)) {
        collectHoistedVarNames(ret->expression.get(), out);
        return;
    }
    if (auto* ifStmt = dynamic_cast<ast::IfStatement*>(node)) {
        collectHoistedVarNames(ifStmt->thenStatement.get(), out);
        collectHoistedVarNames(ifStmt->elseStatement.get(), out);
        return;
    }
    if (auto* whileStmt = dynamic_cast<ast::WhileStatement*>(node)) {
        collectHoistedVarNames(whileStmt->body.get(), out);
        return;
    }
    if (auto* forStmt = dynamic_cast<ast::ForStatement*>(node)) {
        collectHoistedVarNames(forStmt->initializer.get(), out);
        collectHoistedVarNames(forStmt->body.get(), out);
        return;
    }
    if (auto* forOf = dynamic_cast<ast::ForOfStatement*>(node)) {
        collectHoistedVarNames(forOf->initializer.get(), out);
        collectHoistedVarNames(forOf->body.get(), out);
        return;
    }
    if (auto* forIn = dynamic_cast<ast::ForInStatement*>(node)) {
        collectHoistedVarNames(forIn->initializer.get(), out);
        collectHoistedVarNames(forIn->body.get(), out);
        return;
    }
    if (auto* sw = dynamic_cast<ast::SwitchStatement*>(node)) {
        for (auto& cl : sw->clauses) {
            if (auto* cc = dynamic_cast<ast::CaseClause*>(cl.get())) {
                for (auto& s : cc->statements) collectHoistedVarNames(s.get(), out);
            }
            if (auto* dc = dynamic_cast<ast::DefaultClause*>(cl.get())) {
                for (auto& s : dc->statements) collectHoistedVarNames(s.get(), out);
            }
        }
        return;
    }
    if (auto* tryStmt = dynamic_cast<ast::TryStatement*>(node)) {
        for (auto& s : tryStmt->tryBlock) collectHoistedVarNames(s.get(), out);
        if (tryStmt->catchClause) {
            for (auto& s : tryStmt->catchClause->block) collectHoistedVarNames(s.get(), out);
        }
        for (auto& s : tryStmt->finallyBlock) collectHoistedVarNames(s.get(), out);
        return;
    }
    if (auto* labeled = dynamic_cast<ast::LabeledStatement*>(node)) {
        collectHoistedVarNames(labeled->statement.get(), out);
        return;
    }
    // Expressions that may contain statements — none in JS, but recurse into
    // nested expressions that have child statements anyway. Most expression
    // forms can't introduce hoisted vars at this level.
}

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
            // Skip compiler-synthesized `__module_init_*`, `__synthetic_*`, and
            // `exports` (handled separately). The CJS-style globals `__filename`
            // and `__dirname` (injected by the Monomorphizer for user modules)
            // ARE registered as module globals so inner functions can read them
            // via @__modvar_X. Without this exemption, user_main reading
            // `__filename` would see undefined.
            if (name == "exports") return;
            if (name.find("__") == 0 && name != "__filename" && name != "__dirname") return;
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
                // Skip compiler-synthesized names but allow the CJS-style globals
                // __filename and __dirname (injected by the Monomorphizer).
                if (ident->name == "exports") return;
                if (ident->name.find("__") == 0 &&
                    ident->name != "__filename" && ident->name != "__dirname") return;
                moduleVarDecls_[ident->name] = varDecl;
                registerModuleGlobalName(ident->name, globalType);
            } else {
                // Destructuring pattern: const { a, b } = ... or const [a, b] = ...
                // For destructured require(), each extracted variable is Any type
                registerBindingNames(varDecl->name.get(), HIRType::makeAny());
            }
        };

        // Recursively walk into block-like statements so `var` declarations
        // inside any nested block hoist to module-global scope per ECMA-262.
        // Stops at function/method boundaries (those are their own scopes).
        std::function<void(ast::Statement*)> walkForVars;
        walkForVars = [&](ast::Statement* s) {
            if (!s) return;
            if (auto* varDecl = dynamic_cast<ast::VariableDeclaration*>(s)) {
                // Only `var` (and legacy `function` declarations) hoist;
                // `let`/`const` are block-scoped per ECMA-262.
                if (varDecl->varKind == ast::VarKind::Var) {
                    registerModuleVar(varDecl);
                }
                return;
            }
            if (auto* block = dynamic_cast<ast::BlockStatement*>(s)) {
                for (auto& inner : block->statements) {
                    walkForVars(dynamic_cast<ast::Statement*>(inner.get()));
                }
                return;
            }
            if (auto* ifSt = dynamic_cast<ast::IfStatement*>(s)) {
                walkForVars(dynamic_cast<ast::Statement*>(ifSt->thenStatement.get()));
                walkForVars(dynamic_cast<ast::Statement*>(ifSt->elseStatement.get()));
                return;
            }
            if (auto* w = dynamic_cast<ast::WhileStatement*>(s)) {
                walkForVars(dynamic_cast<ast::Statement*>(w->body.get()));
                return;
            }
            if (auto* f = dynamic_cast<ast::ForStatement*>(s)) {
                walkForVars(dynamic_cast<ast::Statement*>(f->initializer.get()));
                walkForVars(dynamic_cast<ast::Statement*>(f->body.get()));
                return;
            }
            if (auto* fi = dynamic_cast<ast::ForInStatement*>(s)) {
                walkForVars(dynamic_cast<ast::Statement*>(fi->initializer.get()));
                walkForVars(dynamic_cast<ast::Statement*>(fi->body.get()));
                return;
            }
            if (auto* fo = dynamic_cast<ast::ForOfStatement*>(s)) {
                walkForVars(dynamic_cast<ast::Statement*>(fo->initializer.get()));
                walkForVars(dynamic_cast<ast::Statement*>(fo->body.get()));
                return;
            }
            if (auto* lab = dynamic_cast<ast::LabeledStatement*>(s)) {
                walkForVars(dynamic_cast<ast::Statement*>(lab->statement.get()));
                return;
            }
            if (auto* sw = dynamic_cast<ast::SwitchStatement*>(s)) {
                for (auto& clause : sw->clauses) {
                    if (auto* cc = dynamic_cast<ast::CaseClause*>(clause.get())) {
                        for (auto& cs : cc->statements) walkForVars(dynamic_cast<ast::Statement*>(cs.get()));
                    } else if (auto* dc = dynamic_cast<ast::DefaultClause*>(clause.get())) {
                        for (auto& cs : dc->statements) walkForVars(dynamic_cast<ast::Statement*>(cs.get()));
                    }
                }
                return;
            }
            if (auto* tr = dynamic_cast<ast::TryStatement*>(s)) {
                for (auto& tb : tr->tryBlock) walkForVars(dynamic_cast<ast::Statement*>(tb.get()));
                if (tr->catchClause) {
                    for (auto& cb : tr->catchClause->block) walkForVars(dynamic_cast<ast::Statement*>(cb.get()));
                }
                for (auto& fb : tr->finallyBlock) walkForVars(dynamic_cast<ast::Statement*>(fb.get()));
                return;
            }
            // Don't recurse into FunctionDeclaration, FunctionExpression,
            // ArrowFunction, ClassDeclaration — those are own-scope boundaries.
        };

        for (auto& stmt : funcNode->body) {
            if (auto* funcDecl = dynamic_cast<ast::FunctionDeclaration*>(stmt.get())) {
                // Function declarations must also be registered as module globals.
                // Without this, functions captured by closures in the same module
                // use closure cells, which fail when the captured function is declared
                // after the capturing function (capture gets null due to source ordering).
                if (!funcDecl->name.empty() && funcDecl->name.find("__") != 0) {
                    moduleGlobalVarsByModule_[funcDecl->name].insert(currentModulePath_);
                    module_->globals[modVarName(funcDecl->name)] = HIRType::makeAny();
                }
            } else if (auto* varDecl = dynamic_cast<ast::VariableDeclaration*>(stmt.get())) {
                // Top-level let/const are module-scoped (their "enclosing
                // block" IS the module). They need to be __modvar_ globals so
                // inner functions reading them across the module_init →
                // user_main → callback path see the same storage. Without
                // this, top-level `const stats = {...}` would be scalar-
                // replaced inside __module_init and inner reads from
                // user_main get NANBOX_UNDEFINED. (walkForVars below
                // intentionally still skips let/const at nested-block depth
                // since those are block-scoped per ECMA-262.)
                registerModuleVar(varDecl);
            } else {
                walkForVars(dynamic_cast<ast::Statement*>(stmt.get()));
            }
        }
    }

    // Phase 9c-i: pre-register top-level class expressions assigned to
    // variables (`const X = class { ... }`). Without this, function bodies
    // visited in the second pass below see no class registered for X — they
    // fall through visitNewExpression's "Unknown class" branch.
    //
    // Calling visitClassExpression here registers the HIRClass, shape,
    // constructor, and methods. The trailer (loadFunction + prototype setup)
    // is skipped because currentFunction_ is null at this point; the second
    // invocation from visitVariableDeclaration during normal lowering hits
    // the astClassExprToHIRClass_ cache and emits the trailer in the correct
    // function context.
    //
    // Critically, this pre-pass MUST run AFTER moduleGlobalVarsByModule_ is
    // populated (the var-scan loop above): visitClassExpression eagerly
    // emits method bodies, and writes inside those bodies need to see
    // `isModuleGlobalVar(name)` as true so they take the StoreGlobal path
    // instead of falling through to a method-local alloca that's invisible
    // to module_init's reads. Class-expression methods have no spec-loop
    // entry (Monomorphizer doesn't specialize anonymous class methods), so
    // this is the ONLY emission of those bodies.
    for (auto& stmt : program->body) {
        auto* varDecl = dynamic_cast<ast::VariableDeclaration*>(stmt.get());
        if (!varDecl || !varDecl->initializer) continue;
        auto* classExpr = dynamic_cast<ast::ClassExpression*>(varDecl->initializer.get());
        if (!classExpr) continue;
        visitClassExpression(classExpr);
        if (auto* ident = dynamic_cast<ast::Identifier*>(varDecl->name.get())) {
            variableToClassName_[ident->name] = lastGeneratedClassName_;
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
        // ECMA-262: only the INSTANCE constructor counts; a `static constructor()`
        // is a static method that happens to be named "constructor" and has
        // unrelated semantics. Without the !isStatic filter, scanning a
        // static-constructor body crashes downstream when its `this` (the
        // class itself) is treated as an instance.
        for (auto& memberPtr : classDecl->members) {
            if (auto* method = dynamic_cast<ast::MethodDefinition*>(memberPtr.get())) {
                if (method->name == "constructor" && method->hasBody && !method->isStatic) {
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

        // Also check if there's an explicit instance constructor in the class.
        // Static methods named "constructor" don't count — they're orthogonal.
        bool hasExplicitConstructor = false;
        for (auto& memberPtr2 : classDecl->members) {
            if (auto* method = dynamic_cast<ast::MethodDefinition*>(memberPtr2.get())) {
                if (method->name == "constructor" && method->hasBody && !method->isStatic) {
                    hasExplicitConstructor = true;
                    break;
                }
            }
        }

        if (hasPropertyInitializers && !hasExplicitConstructor) {
            std::string ctorName = className + "_constructor";
            auto defaultCtor = std::make_unique<HIRFunction>(ctorName);
            {
                // .name of the (default) constructor = the class name, or the
                // inferred binding name for an anonymous class expression.
                std::string cn = classDecl->name.empty() ? pendingClosureDisplayName_ : classDecl->name;
                if (!cn.empty()) defaultCtor->displayName = cn;
            }
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

            // Initialize property defaults from AST. Every declared
            // instance field is installed even without initializer
            // (value defaults to undefined) per ECMA-262 15.7.
            for (auto& memberPtr2 : classDecl->members) {
                if (auto* propDef = dynamic_cast<ast::PropertyDefinition*>(memberPtr2.get())) {
                    if (!propDef->isStatic) {
                        std::shared_ptr<HIRValue> initVal;
                        if (propDef->initializer) {
                            initVal = lowerExpression(propDef->initializer.get());
                        } else {
                            initVal = builder_.createConstUndefined();
                        }
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

    // Pre-scan: walk all non-module-init spec bodies for identifier
    // references to module-global vars, populating
    // moduleGlobalsUsedByInnerByModule_ BEFORE module_init lowering.
    // Without this, module_init lowers reads like `console.log(callCount)`
    // via the stale local-alloca fast path because the marker is only set
    // when the inner function (e.g. a class method that mutates callCount)
    // is later lowered. Result: writes from inner functions land in
    // __modvar_callCount but module_init's read pulls the stale local copy.
    {
        std::function<void(ast::Node*, const std::string&)> scanIds;
        scanIds = [&](ast::Node* node, const std::string& modPath) {
            if (!node) return;
            if (auto* id = dynamic_cast<ast::Identifier*>(node)) {
                auto it = moduleGlobalVarsByModule_.find(id->name);
                if (it != moduleGlobalVarsByModule_.end() &&
                    it->second.count(modPath)) {
                    moduleGlobalsUsedByInnerByModule_[id->name].insert(modPath);
                }
                return;
            }
            // Don't descend into nested function/method nodes — they're
            // separate specs scanned independently.
            if (dynamic_cast<ast::FunctionDeclaration*>(node)) return;
            if (dynamic_cast<ast::FunctionExpression*>(node)) return;
            if (dynamic_cast<ast::ArrowFunction*>(node)) return;
            if (auto* block = dynamic_cast<ast::BlockStatement*>(node)) {
                for (auto& s : block->statements) scanIds(s.get(), modPath);
                return;
            }
            if (auto* expr = dynamic_cast<ast::ExpressionStatement*>(node)) { scanIds(expr->expression.get(), modPath); return; }
            if (auto* ret = dynamic_cast<ast::ReturnStatement*>(node)) { scanIds(ret->expression.get(), modPath); return; }
            if (auto* ifSt = dynamic_cast<ast::IfStatement*>(node)) {
                scanIds(ifSt->condition.get(), modPath);
                scanIds(ifSt->thenStatement.get(), modPath);
                scanIds(ifSt->elseStatement.get(), modPath);
                return;
            }
            if (auto* w = dynamic_cast<ast::WhileStatement*>(node)) {
                scanIds(w->condition.get(), modPath);
                scanIds(w->body.get(), modPath);
                return;
            }
            if (auto* f = dynamic_cast<ast::ForStatement*>(node)) {
                scanIds(f->initializer.get(), modPath);
                scanIds(f->condition.get(), modPath);
                scanIds(f->incrementor.get(), modPath);
                scanIds(f->body.get(), modPath);
                return;
            }
            if (auto* fo = dynamic_cast<ast::ForOfStatement*>(node)) {
                scanIds(fo->expression.get(), modPath);
                scanIds(fo->body.get(), modPath);
                return;
            }
            if (auto* fi = dynamic_cast<ast::ForInStatement*>(node)) {
                scanIds(fi->expression.get(), modPath);
                scanIds(fi->body.get(), modPath);
                return;
            }
            if (auto* sw = dynamic_cast<ast::SwitchStatement*>(node)) {
                scanIds(sw->expression.get(), modPath);
                for (auto& cl : sw->clauses) {
                    if (auto* cc = dynamic_cast<ast::CaseClause*>(cl.get())) {
                        scanIds(cc->expression.get(), modPath);
                        for (auto& s : cc->statements) scanIds(s.get(), modPath);
                    }
                    if (auto* dc = dynamic_cast<ast::DefaultClause*>(cl.get())) {
                        for (auto& s : dc->statements) scanIds(s.get(), modPath);
                    }
                }
                return;
            }
            if (auto* tryStmt = dynamic_cast<ast::TryStatement*>(node)) {
                for (auto& s : tryStmt->tryBlock) scanIds(s.get(), modPath);
                if (tryStmt->catchClause) {
                    for (auto& s : tryStmt->catchClause->block) scanIds(s.get(), modPath);
                }
                for (auto& s : tryStmt->finallyBlock) scanIds(s.get(), modPath);
                return;
            }
            if (auto* th = dynamic_cast<ast::ThrowStatement*>(node)) { scanIds(th->expression.get(), modPath); return; }
            if (auto* vd = dynamic_cast<ast::VariableDeclaration*>(node)) {
                scanIds(vd->initializer.get(), modPath);
                return;
            }
            if (auto* lab = dynamic_cast<ast::LabeledStatement*>(node)) { scanIds(lab->statement.get(), modPath); return; }
            if (auto* call = dynamic_cast<ast::CallExpression*>(node)) {
                scanIds(call->callee.get(), modPath);
                for (auto& a : call->arguments) scanIds(a.get(), modPath);
                return;
            }
            if (auto* ne = dynamic_cast<ast::NewExpression*>(node)) {
                scanIds(ne->expression.get(), modPath);
                for (auto& a : ne->arguments) scanIds(a.get(), modPath);
                return;
            }
            if (auto* bin = dynamic_cast<ast::BinaryExpression*>(node)) {
                scanIds(bin->left.get(), modPath);
                scanIds(bin->right.get(), modPath);
                return;
            }
            if (auto* as = dynamic_cast<ast::AssignmentExpression*>(node)) {
                scanIds(as->left.get(), modPath);
                scanIds(as->right.get(), modPath);
                return;
            }
            if (auto* c = dynamic_cast<ast::ConditionalExpression*>(node)) {
                scanIds(c->condition.get(), modPath);
                scanIds(c->whenTrue.get(), modPath);
                scanIds(c->whenFalse.get(), modPath);
                return;
            }
            if (auto* p = dynamic_cast<ast::PrefixUnaryExpression*>(node)) { scanIds(p->operand.get(), modPath); return; }
            if (auto* p2 = dynamic_cast<ast::PostfixUnaryExpression*>(node)) { scanIds(p2->operand.get(), modPath); return; }
            if (auto* pa = dynamic_cast<ast::PropertyAccessExpression*>(node)) { scanIds(pa->expression.get(), modPath); return; }
            if (auto* ea = dynamic_cast<ast::ElementAccessExpression*>(node)) {
                scanIds(ea->expression.get(), modPath);
                scanIds(ea->argumentExpression.get(), modPath);
                return;
            }
            if (auto* arr = dynamic_cast<ast::ArrayLiteralExpression*>(node)) {
                for (auto& e : arr->elements) scanIds(e.get(), modPath);
                return;
            }
            if (auto* obj = dynamic_cast<ast::ObjectLiteralExpression*>(node)) {
                for (auto& pr : obj->properties) {
                    if (auto* pa2 = dynamic_cast<ast::PropertyAssignment*>(pr.get())) {
                        scanIds(pa2->initializer.get(), modPath);
                    }
                }
                return;
            }
            if (auto* tmpl = dynamic_cast<ast::TemplateExpression*>(node)) {
                for (auto& span : tmpl->spans) scanIds(span.expression.get(), modPath);
                return;
            }
            if (auto* paren = dynamic_cast<ast::ParenthesizedExpression*>(node)) { scanIds(paren->expression.get(), modPath); return; }
            if (auto* sp = dynamic_cast<ast::SpreadElement*>(node)) { scanIds(sp->expression.get(), modPath); return; }
            if (auto* del = dynamic_cast<ast::DeleteExpression*>(node)) { scanIds(del->expression.get(), modPath); return; }
            if (auto* aw = dynamic_cast<ast::AwaitExpression*>(node)) { scanIds(aw->expression.get(), modPath); return; }
            if (auto* y = dynamic_cast<ast::YieldExpression*>(node)) { scanIds(y->expression.get(), modPath); return; }
            if (auto* asx = dynamic_cast<ast::AsExpression*>(node)) { scanIds(asx->expression.get(), modPath); return; }
            if (auto* nn = dynamic_cast<ast::NonNullExpression*>(node)) { scanIds(nn->expression.get(), modPath); return; }
        };
        for (const auto& spec : specializations) {
            if (spec.originalName.find("__module_init_") == 0) continue;
            if (auto* funcNode = dynamic_cast<ast::FunctionDeclaration*>(spec.node)) {
                for (auto& s : funcNode->body) scanIds(s.get(), spec.modulePath);
            } else if (auto* methodNode = dynamic_cast<ast::MethodDefinition*>(spec.node)) {
                for (auto& s : methodNode->body) scanIds(s.get(), spec.modulePath);
            }
        }

        // Class-expression methods aren't in `specializations` (the
        // Monomorphizer doesn't synthesize specs for anonymous class
        // members). Walk the AST directly so `var X = 0; var C = class
        // { m(){ X = X + 1; } }; new C().m(); console.log(X)` registers
        // the marker in time for module_init's `console.log(X)` read to
        // take the LoadGlobal path. Without this, the read pulls a stale
        // local copy and assertions like
        // `assert.sameValue(callCount, 1, 'method invoked exactly once')`
        // see 0 instead of the incremented value.
        for (auto& stmt : program->body) {
            auto* varDecl = dynamic_cast<ast::VariableDeclaration*>(stmt.get());
            if (!varDecl || !varDecl->initializer) continue;
            auto* classExpr = dynamic_cast<ast::ClassExpression*>(varDecl->initializer.get());
            if (!classExpr) continue;
            for (auto& memberPtr : classExpr->members) {
                auto* md = dynamic_cast<ast::MethodDefinition*>(memberPtr.get());
                if (!md || !md->hasBody) continue;
                for (auto& s : md->body) scanIds(s.get(), currentModulePath_);
            }
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
                ast::Node* defaultInitializer = nullptr;
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
                    specDestructuredParams.push_back({func->params.size(), objPat, nullptr,
                        param->initializer.get()});
                } else if (auto* arrPat = dynamic_cast<ast::ArrayBindingPattern*>(param->name.get())) {
                    paramName = "param" + std::to_string(func->params.size());
                    paramType = HIRType::makeAny();
                    specDestructuredParams.push_back({func->params.size(), nullptr, arrPat,
                        param->initializer.get()});
                } else {
                    paramName = "param" + std::to_string(func->params.size());
                }

                if (param->isRest) {
                    func->hasRestParam = true;
                }

                // ECMA-262 §10.2.5: function .length is the count of leading
                // simple parameters (no default, no rest, no destructuring
                // pattern). Record the first non-simple index so emit-time
                // arity uses the correct value. SIZE_MAX (initial value)
                // means all params are simple.
                if (func->firstNonSimpleParamIndex == SIZE_MAX) {
                    bool isDestructured =
                        dynamic_cast<ast::ObjectBindingPattern*>(param->name.get()) ||
                        dynamic_cast<ast::ArrayBindingPattern*>(param->name.get());
                    if (param->initializer || param->isRest || isDestructured) {
                        func->firstNonSimpleParamIndex = paramIdx;
                    }
                }

                func->params.push_back({paramName, paramType});
            }

            // If the function body uses 'arguments', add hidden __argN__ params
            // so extra call arguments beyond declared params can be captured.
            {
                bool bodyUsesArguments = false;
                for (auto& stmt : funcNode->body) {
                    if (containsArgumentsIdentifier(stmt.get())) {
                        bodyUsesArguments = true;
                        break;
                    }
                }
                if (bodyUsesArguments) {
                    while (func->params.size() < 10) {
                        std::string argName = "__arg" + std::to_string(func->params.size()) + "__";
                        func->params.push_back({argName, HIRType::makeAny()});
                    }
                }
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
                // Apply parameter default value if the AST recorded one for this
                // pattern parameter (e.g. `function f([a] = [1])`). Without this
                // step, the destructure source would be the raw `undefined` arg
                // and downstream RequireObjectCoercible / GetIterator would
                // throw incorrectly.
                if (dp.defaultInitializer) {
                    auto isUndef = builder_.createIsUndefined(paramValue);
                    auto* defaultExpr = dynamic_cast<ast::Expression*>(dp.defaultInitializer);
                    if (defaultExpr) {
                        auto defaultVal = lowerExpression(defaultExpr);
                        defaultVal = boxValueIfNeeded(defaultVal);
                        paramValue = builder_.createSelect(isUndef, defaultVal, paramValue);
                    }
                }
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

            // ECMA-262 §14.3.2: hoist every `var` declaration in the
            // function body — including those nested inside if/else,
            // loops, try, switch — to the FunctionEnvironment. Without
            // this, an assignment in branch B to a `var` declared in
            // branch A binds to a fresh slot per branch (or the global
            // object) and the surrounding function sees `undefined`.
            // Also catches nested FunctionDeclarations so closures see
            // them in source-order-independent ways.
            {
                std::vector<std::string> hoistedVars;
                for (auto& stmt : funcNode->body) {
                    collectHoistedVarNames(stmt.get(), hoistedVars);
                }
                for (auto& name : hoistedVars) {
                    if (lookupVariableInfoInCurrentFunction(name)) continue;
                    auto allocaVal = builder_.createAlloca(HIRType::makeAny(), name);
                    builder_.createStore(builder_.createConstUndefined(), allocaVal, HIRType::makeAny());
                    defineVariableAlloca(name, allocaVal, HIRType::makeAny());
                }
            }

            // Pre-declare top-level let/const so nested FunctionDeclaration
            // bodies (lowered in pass 1 below) can resolve outer-scope
            // captures. Mirrors the same block in visitFunctionDeclaration.
            for (auto& stmt : funcNode->body) {
                if (auto* vd = dynamic_cast<ast::VariableDeclaration*>(stmt.get())) {
                    if (vd->varKind != ast::VarKind::Let && vd->varKind != ast::VarKind::Const) continue;
                    auto* ident = dynamic_cast<ast::Identifier*>(vd->name.get());
                    if (!ident) continue;
                    if (lookupVariableInfoInCurrentFunction(ident->name)) continue;
                    auto allocaVal = builder_.createAlloca(HIRType::makeAny(), ident->name);
                    builder_.createStore(builder_.createConstUndefined(), allocaVal, HIRType::makeAny());
                    defineVariableAlloca(ident->name, allocaVal, HIRType::makeAny());
                }
            }

            // Create 'arguments' array-like object if the function body references 'arguments'.
            // Must be done at function entry (before any other code) because inner calls
            // will overwrite ts_last_call_argc, making lazy creation incorrect.
            {
                bool usesArguments = false;
                for (auto& stmt : funcNode->body) {
                    if (containsArgumentsIdentifier(stmt.get())) {
                        usesArguments = true;
                        break;
                    }
                }
                if (usesArguments) {
                    std::vector<std::shared_ptr<HIRValue>> callArgs;
                    size_t userIdx = 0;
                    for (size_t i = 0; i < funcPtr->params.size() && userIdx < 10; ++i) {
                        if (funcPtr->params[i].first == "__closure__") continue;
                        auto paramVal = lookupVariable(funcPtr->params[i].first);
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

            // If this is the entry-point function (user_main or its
            // synthetic equivalent for top-level scripts), flush deferred
            // class-prototype installs and static-property initializers
            // at the very start of the body so that subsequent statements
            // see `E.prototype.<key>` populated. This is the path most
            // top-level code goes through (the visitor-based
            // visitFunctionDeclaration is not used for spec functions).
            if (funcNode->name == "user_main" || funcNode->name == "__synthetic_user_main") {
                emitDeferredStaticInits();
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

            // Collect destructured parameter patterns so we can emit the
            // extraction (get_elem / get_prop) code at method entry — same
            // shape as the FunctionDeclaration path above. Without this,
            // `class C { method([x, y, z]) {} }` produces a HIR function
            // with a single `param0` and no destructuring, leaving x/y/z
            // unbound and crashing on use.
            struct MethDestructuredParam {
                size_t paramIndex;
                ast::ObjectBindingPattern* objPattern = nullptr;
                ast::ArrayBindingPattern* arrPattern = nullptr;
                ast::Node* defaultInitializer = nullptr;
            };
            std::vector<MethDestructuredParam> methDestructuredParams;

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
                } else if (auto* objPat = dynamic_cast<ast::ObjectBindingPattern*>(param->name.get())) {
                    paramName = "param" + std::to_string(func->params.size());
                    paramType = HIRType::makeAny();
                    methDestructuredParams.push_back({func->params.size(), objPat, nullptr,
                        param->initializer.get()});
                } else if (auto* arrPat = dynamic_cast<ast::ArrayBindingPattern*>(param->name.get())) {
                    paramName = "param" + std::to_string(func->params.size());
                    paramType = HIRType::makeAny();
                    methDestructuredParams.push_back({func->params.size(), nullptr, arrPat,
                        param->initializer.get()});
                } else {
                    paramName = "param" + std::to_string(func->params.size());
                }

                // Track first non-simple param for ECMA-262 §10.2.5 fn.length.
                if (func->firstNonSimpleParamIndex == SIZE_MAX) {
                    bool isDestructured =
                        dynamic_cast<ast::ObjectBindingPattern*>(param->name.get()) ||
                        dynamic_cast<ast::ArrayBindingPattern*>(param->name.get());
                    if (param->initializer || param->isRest || isDestructured) {
                        func->firstNonSimpleParamIndex = paramIdx;
                    }
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

            // Push method to module BEFORE lowering body (same reason as functions above).
            // visitClassDeclaration emits a same-named placeholder during the
            // early pre-pass — but at pre-pass time the module-scope vars
            // haven't been registered, so identifier writes there miss the
            // StoreGlobal path and the body becomes a no-op. Replace any
            // existing entry so HIRToLLVM lowers ONE HIRFunction (and
            // InliningPass picks the spec-loop body, which IS scope-correct).
            // Without this, both HIRFunctions push and HIRToLLVM merges them
            // into one llvm::Function with two entry blocks; the spec-loop's
            // body becomes "entry1: No predecessors!" — unreachable.
            auto* methPtr = func.get();
            bool replacedExisting = false;
            for (auto& existing : module_->functions) {
                if (existing && existing->name == spec.specializedName) {
                    // HIRClass fields (vtable, methods, staticMethods, constructor)
                    // hold raw HIRFunction* into module_->functions. Replacing the
                    // unique_ptr here destroys the old HIRFunction; any HIRClass
                    // pointer to it becomes dangling and HIRToLLVM later reads
                    // freed memory via methodFunc->mangledName. Retarget them
                    // to the new HIRFunction before the move.
                    HIRFunction* oldPtr = existing.get();
                    for (auto& cls : module_->classes) {
                        if (!cls) continue;
                        for (auto& entry : cls->vtable) {
                            if (entry.second == oldPtr) entry.second = methPtr;
                        }
                        for (auto& entry : cls->methods) {
                            if (entry.second == oldPtr) entry.second = methPtr;
                        }
                        for (auto& entry : cls->staticMethods) {
                            if (entry.second == oldPtr) entry.second = methPtr;
                        }
                        if (cls->constructor == oldPtr) cls->constructor = methPtr;
                    }
                    existing = std::move(func);
                    replacedExisting = true;
                    break;
                }
            }
            if (!replacedExisting) {
                module_->functions.push_back(std::move(func));
            }

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
            // argTypeOffset is 1 for instance methods (slot 0 = synthetic 'this',
            // user params start at HIR index 1) and 0 for static methods. Map
            // the HIR param index back to the AST parameter so we can honor
            // default-value initializers (e.g. `method(a = 99) {}`). Without
            // this mapping the spec path silently drops scalar defaults — the
            // earlier destructured-default fix (loop below) only handled
            // `method([a] = [1])` patterns.
            for (size_t i = 0; i < methPtr->params.size(); ++i) {
                const auto& [paramName, paramType] = methPtr->params[i];
                auto paramValue = std::make_shared<HIRValue>(static_cast<uint32_t>(i), paramType, paramName);

                // Map HIR param index → AST parameter index. Skip the
                // synthetic 'this' (argTypeOffset==1, i==0) which has no AST
                // counterpart. Skip destructured params — they are handled by
                // the methDestructuredParams loop below which applies defaults
                // before pattern extraction.
                size_t astParamIdx = (i >= argTypeOffset) ? (i - argTypeOffset) : SIZE_MAX;
                ast::Parameter* astParam = (astParamIdx < methodNode->parameters.size())
                    ? methodNode->parameters[astParamIdx].get() : nullptr;
                bool isDestructured = astParam && (
                    dynamic_cast<ast::ObjectBindingPattern*>(astParam->name.get()) ||
                    dynamic_cast<ast::ArrayBindingPattern*>(astParam->name.get()));

                if (astParam && astParam->initializer && !isDestructured) {
                    // Scalar default (e.g. `method(a = 99) {}`). Mirror the
                    // FunctionDeclaration spec path: branch on undefined, use
                    // the default expression, otherwise use the passed value.
                    auto allocaVal = builder_.createAlloca(paramType);
                    auto isUndefined = builder_.createCall("ts_value_is_undefined",
                        {paramValue}, HIRType::makeBool());

                    auto defaultBB = methPtr->createBlock("default_param");
                    auto usedBB = methPtr->createBlock("use_param");
                    auto mergeBB = methPtr->createBlock("param_merge");

                    builder_.createCondBranch(isUndefined, defaultBB, usedBB);

                    builder_.setInsertPoint(defaultBB);
                    currentBlock_ = defaultBB;
                    auto* initExpr = dynamic_cast<ast::Expression*>(astParam->initializer.get());
                    auto defaultVal = initExpr ? lowerExpression(initExpr)
                                               : builder_.createConstUndefined();
                    if (paramType->kind == HIRTypeKind::Any) {
                        defaultVal = forceBoxValue(defaultVal);
                    }
                    builder_.createStore(defaultVal, allocaVal);
                    builder_.createBranch(mergeBB);

                    builder_.setInsertPoint(usedBB);
                    currentBlock_ = usedBB;
                    builder_.createStore(paramValue, allocaVal);
                    builder_.createBranch(mergeBB);

                    builder_.setInsertPoint(mergeBB);
                    currentBlock_ = mergeBB;

                    defineVariableAlloca(paramName, allocaVal, paramType);
                } else {
                    auto allocaVal = builder_.createAlloca(paramType);
                    builder_.createStore(paramValue, allocaVal);
                    defineVariableAlloca(paramName, allocaVal, paramType);
                }
            }

            // Emit destructuring extraction for parameters with binding
            // patterns. Mirrors the FunctionDeclaration path above; without
            // this, class `method([x, y, z]) {}` would receive `param0` but
            // never bind x/y/z, crashing on use.
            for (auto& dp : methDestructuredParams) {
                auto paramValue = std::make_shared<HIRValue>(
                    static_cast<uint32_t>(dp.paramIndex),
                    HIRType::makeAny(),
                    methPtr->params[dp.paramIndex].first);
                // Apply parameter default value if recorded for this pattern
                // parameter (e.g. `method([a] = [1])`).
                if (dp.defaultInitializer) {
                    auto isUndef = builder_.createIsUndefined(paramValue);
                    auto* defaultExpr = dynamic_cast<ast::Expression*>(dp.defaultInitializer);
                    if (defaultExpr) {
                        auto defaultVal = lowerExpression(defaultExpr);
                        defaultVal = boxValueIfNeeded(defaultVal);
                        paramValue = builder_.createSelect(isUndef, defaultVal, paramValue);
                    }
                }
                if (dp.objPattern) {
                    lowerObjectBindingPattern(dp.objPattern, paramValue);
                } else if (dp.arrPattern) {
                    lowerArrayBindingPattern(dp.arrPattern, paramValue);
                }
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
                                if (!propDef->isStatic) {
                                    // ECMA-262 15.7: install every
                                    // instance field; default to
                                    // undefined when no initializer.
                                    std::shared_ptr<HIRValue> initVal;
                                    if (propDef->initializer) {
                                        initVal = lowerExpression(propDef->initializer.get());
                                    } else {
                                        initVal = builder_.createConstUndefined();
                                    }
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
                            // visitClassDeclaration may have already pushed a
                            // vtable entry for this method (with a stale func
                            // pointer if we replaced the HIRFunction in
                            // module_->functions). Replace the existing entry
                            // by methodKey rather than appending — otherwise
                            // VTable codegen sees two entries and the first
                            // points to a freed unique_ptr.
                            bool updatedVtable = false;
                            for (auto& vt : hirClass->vtable) {
                                if (vt.first == methodKey) {
                                    vt.second = methPtr;
                                    updatedVtable = true;
                                    break;
                                }
                            }
                            if (!updatedVtable) {
                                hirClass->vtable.push_back({methodKey, methPtr});
                            }
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

// Broadcast a write to every closure cell that captures this variable. The
// primary cell (info.closurePtr / info.captureIndex) is updated first, then
// each entry in info.additionalCaptures. Without this, when multiple nested
// closures capture the same variable (lodash captures `upperFirst` from
// many helpers), an assignment to the var would only update the first
// closure's cell, leaving subsequent ones holding stale values.
void ASTToHIR::broadcastCaptureWrite(VariableInfo* info,
                                     std::shared_ptr<HIRValue> newValue) {
    if (!info || !info->isCapturedByNested) return;
    if (info->closurePtr && info->captureIndex >= 0) {
        auto closureVal = builder_.createLoad(HIRType::makeAny(), info->closurePtr);
        builder_.createStoreCaptureFromClosure(closureVal, info->captureIndex, newValue);
    }
    for (const auto& cap : info->additionalCaptures) {
        if (cap.first && cap.second >= 0) {
            auto closureVal = builder_.createLoad(HIRType::makeAny(), cap.first);
            builder_.createStoreCaptureFromClosure(closureVal, cap.second, newValue);
        }
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
// Parameter Binder Helpers (Strategy B Phase 6)
//==============================================================================

void ASTToHIR::bindOneParameter(HIRFunction* func,
                                size_t hirParamIndex,
                                ast::Parameter* astParam,
                                bool useAlloca) {
    const auto& [paramName, paramType] = func->params[hirParamIndex];
    auto paramValue = std::make_shared<HIRValue>(
        static_cast<uint32_t>(hirParamIndex), paramType, paramName);

    if (astParam && astParam->initializer) {
        // Parameter has a default value - check if undefined and use default.
        // We can't use pointer comparison because ts_value_make_undefined()
        // creates a new TsValue* each time, so pointers won't match. Instead
        // use ts_value_is_undefined() which checks the type field.
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
        // Force box the default value if parameter type is Any. We use
        // forceBoxValue because the expression might be a function call that
        // gets inlined later, changing its type from Any to a concrete type.
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
        return;
    }

    if (useAlloca) {
        // No default value - store into an alloca so reassignment works
        auto allocaVal = builder_.createAlloca(paramType);
        builder_.createStore(paramValue, allocaVal);
        defineVariableAlloca(paramName, allocaVal, paramType);
    } else {
        // Direct value registration (used by methods — params are not reassigned)
        defineVariable(paramName, paramValue);
    }
}

void ASTToHIR::extractDestructuringForParam(HIRFunction* func,
                                            size_t hirParamIndex,
                                            ast::ObjectBindingPattern* objPattern,
                                            ast::ArrayBindingPattern* arrPattern,
                                            ast::Node* defaultInitializer) {
    auto paramValue = std::make_shared<HIRValue>(
        static_cast<uint32_t>(hirParamIndex),
        HIRType::makeAny(),
        func->params[hirParamIndex].first);
    // Apply parameter default value before destructuring per ECMA-262
    // FunctionDeclarationInstantiation step on FormalParameters with
    // Initializer: if the actual argument is undefined, use the default.
    if (auto* defaultExpr = dynamic_cast<ast::Expression*>(defaultInitializer)) {
        auto isUndef = builder_.createIsUndefined(paramValue);
        auto defaultVal = lowerExpression(defaultExpr);
        defaultVal = boxValueIfNeeded(defaultVal);
        paramValue = builder_.createSelect(isUndef, defaultVal, paramValue);
    }
    if (objPattern) {
        lowerObjectBindingPattern(objPattern, paramValue);
    } else if (arrPattern) {
        lowerArrayBindingPattern(arrPattern, paramValue);
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

    // Install class prototypes. For each class with instance methods or
    // accessors, build a real prototype object holding `__getter_<key>`,
    // `__setter_<key>`, and method names, then assign it to the
    // constructor's `prototype` property. This makes
    // `E.prototype['<key>']` find the installed function and supports
    // test262 accessor-name probes that read directly from the
    // prototype.
    // Helper to install a class method/accessor with the spec method
    // descriptor: { writable: true, enumerable: false, configurable: true }.
    // verifyProperty(C.prototype, "m", { enumerable: false, ... }) and
    // Object.keys(C) tests require non-enumerable. Routes through the
    // ts_object_set_method runtime which uses TsMap::SetWithAttrs.
    auto installMethod = [&](std::shared_ptr<HIRValue> recv,
                             const std::string& key,
                             std::shared_ptr<HIRValue> closure) {
        auto keyStr = builder_.createConstString(key);
        std::vector<std::shared_ptr<HIRValue>> args = {recv, keyStr, closure};
        builder_.createCall("ts_object_set_method", args, HIRType::makeVoid());
    };
    for (auto* hirClass : deferredClassPrototypes_) {
        if (!hirClass) continue;
        // Every class needs a real prototype object (not just classes with
        // user-defined methods) so that `c.constructor === C` and
        // `Object.getPrototypeOf(c) === C.prototype` hold per ECMA-262
        // §15.7. Previously classes with only staticMethods (or no
        // methods at all) skipped the prototype install entirely, leaving
        // a default Function.prototype object with no constructor backref.
        std::string ctorName = hirClass->constructor
            ? hirClass->constructor->name
            : hirClass->name + "_constructor";
        auto ctorVal = builder_.createLoadFunction(ctorName);

        // Build the prototype map. Always create one — even for classes
        // without methods — so the constructor backref and parent-prototype
        // chain can be installed.
        auto proto = builder_.createCall("ts_object_create_empty", {}, HIRType::makeAny());
        for (auto& [methodKey, methodFunc] : hirClass->methods) {
            if (!methodFunc) continue;
            auto methodClosure = builder_.createLoadFunction(methodFunc->name);
            installMethod(proto, methodKey, methodClosure);
        }

        // ECMA-262 §15.7: `Class.prototype.constructor` is the class
        // itself, with {writable:true, enumerable:false, configurable:true}.
        // installMethod uses ts_object_set_method which writes with those
        // exact descriptor flags.
        installMethod(proto, "constructor", ctorVal);

        // ECMA-262 §15.7.14 (ClassDefinitionEvaluation): if the class
        // extends Base, set Object.getPrototypeOf(C.prototype) =
        // Base.prototype. Without this, `(new Derived()) instanceof Base`
        // is false and `Derived.prototype.method` doesn't fall through to
        // Base.prototype.method via prototype-chain walks.
        if (hirClass->baseClass) {
            std::string baseCtorName = hirClass->baseClass->constructor
                ? hirClass->baseClass->constructor->name
                : hirClass->baseClass->name + "_constructor";
            auto baseCtorVal = builder_.createLoadFunction(baseCtorName);
            auto basePropName = builder_.createConstString("prototype");
            auto baseProtoVal = builder_.createCall("ts_object_get_dynamic",
                {baseCtorVal, basePropName}, HIRType::makeAny());
            builder_.createCall("ts_object_setPrototypeOf",
                {proto, baseProtoVal}, HIRType::makeVoid());
        }

        // `prototype` itself stays writable+configurable but intentionally
        // enumerable per spec — keep createSetPropStatic.
        builder_.createSetPropStatic(ctorVal, "prototype", proto);

        // Install static methods on the constructor itself so dynamic
        // access like `F.method()` (where `F` is a class-expression-bound
        // variable) resolves to the function. Class declarations have a
        // direct Case 3 dispatch in visitCallExpression that bypasses
        // this, but class expressions and indirect access need it.
        for (auto& [methodName, methodFunc] : hirClass->staticMethods) {
            if (!methodFunc) continue;
            auto methodClosure = builder_.createLoadFunction(methodFunc->name);
            installMethod(ctorVal, methodName, methodClosure);
        }
    }
    deferredClassPrototypes_.clear();

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
    // Note: empty-body functions (e.g., `function F() {}`) are valid JS functions
    // that return undefined. We still create a closure for them so typeof/instanceof
    // work correctly. The body loop below will simply not emit any statements.

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
        ast::Node* defaultInitializer = nullptr;
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
            destructuredParams.push_back({func->params.size(), objPat, nullptr,
                param->initializer.get()});
        } else if (auto* arrPat = dynamic_cast<ast::ArrayBindingPattern*>(param->name.get())) {
            paramName = "param" + std::to_string(func->params.size());
            paramType = HIRType::makeAny();
            destructuredParams.push_back({func->params.size(), nullptr, arrPat,
                param->initializer.get()});
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

        // Track first non-simple param for ECMA-262 §10.2.5 fn.length.
        if (func->firstNonSimpleParamIndex == SIZE_MAX) {
            bool isDestructured =
                dynamic_cast<ast::ObjectBindingPattern*>(param->name.get()) ||
                dynamic_cast<ast::ArrayBindingPattern*>(param->name.get());
            if (param->initializer || param->isRest || isDestructured) {
                func->firstNonSimpleParamIndex = paramIdx;
            }
        }

        func->params.push_back({paramName, paramType});
    }

    // If the function body uses 'arguments', add hidden __argN__ params
    // so call args beyond the declared param count can flow through the
    // trampoline into ts_create_arguments_from_params. Without this,
    // top-level functions called with extra args see arguments[N]
    // resolve to undefined because direct_0's LLVM signature has no
    // slot to receive them.
    {
        bool bodyUsesArgs = false;
        for (auto& stmt : node->body) {
            if (containsArgumentsIdentifier(stmt.get())) {
                bodyUsesArgs = true;
                break;
            }
        }
        if (bodyUsesArgs) {
            while (func->params.size() < 10) {
                std::string argName = "__arg" + std::to_string(func->params.size()) + "__";
                func->params.push_back({argName, HIRType::makeAny()});
            }
        }
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

    // Register parameters in the scope so they can be looked up.
    // Parameter values have IDs 0, 1, 2, ... matching their index in HIRToLLVM.
    // Strategy B Phase 6a: per-parameter logic factored into bindOneParameter.
    for (size_t i = 0; i < func->params.size(); ++i) {
        ast::Parameter* astParam = (i < node->parameters.size()) ? node->parameters[i].get() : nullptr;
        bindOneParameter(func.get(), i, astParam, /*useAlloca=*/true);
    }

    // Emit destructuring extraction for parameters with binding patterns.
    // Strategy B Phase 6a: per-parameter logic factored into extractDestructuringForParam.
    for (auto& dp : destructuredParams) {
        extractDestructuringForParam(func.get(), dp.paramIndex,
            dp.objPattern, dp.arrPattern, dp.defaultInitializer);
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

    // If this is user_main (or the synthetic equivalent for top-level
    // scripts), emit deferred static property initializations and class
    // prototype installs at the very start of the function body.
    if (node->name == "user_main" || node->name == "__synthetic_user_main") {
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

    // ECMA-262 §14.3.2: hoist every `var` declaration in the function
    // body — including those buried inside if/else, loops, try, switch
    // — to the enclosing FunctionEnvironment. Without this, assignments
    // like `} else if (...) { result = x; }` after a `var result =`
    // in a different branch bind to a fresh slot per branch (or the
    // global object) and the surrounding function sees `undefined`.
    {
        std::vector<std::string> hoistedVars;
        for (auto& stmt : node->body) {
            collectHoistedVarNames(stmt.get(), hoistedVars);
        }
        for (auto& name : hoistedVars) {
            if (lookupVariableInfoInCurrentFunction(name)) continue;
            auto allocaVal = builder_.createAlloca(HIRType::makeAny(), name);
            builder_.createStore(builder_.createConstUndefined(), allocaVal, HIRType::makeAny());
            defineVariableAlloca(name, allocaVal, HIRType::makeAny());
        }
    }

    // Pre-declare top-level `let` / `const` so nested FunctionDeclaration
    // bodies (lowered in pass 1 below, BEFORE the let initializers run
    // in pass 2) can resolve outer-scope captures. Without this,
    // `function outer() { let count = 0; function inner() { count++; } }`
    // lowers inner's body when `count` isn't yet in scope, so inner
    // resolves `count` to const-undefined and the closure has no
    // captures — outer's count stays 0 forever. Block-scoped lets
    // nested inside if / loops are intentionally NOT pre-declared
    // (they have their own block scope and would shadow the wrong
    // way at the function level).
    for (auto& stmt : node->body) {
        if (auto* vd = dynamic_cast<ast::VariableDeclaration*>(stmt.get())) {
            if (vd->varKind != ast::VarKind::Let && vd->varKind != ast::VarKind::Const) continue;
            auto* ident = dynamic_cast<ast::Identifier*>(vd->name.get());
            if (!ident) continue;
            if (lookupVariableInfoInCurrentFunction(ident->name)) continue;
            auto allocaVal = builder_.createAlloca(HIRType::makeAny(), ident->name);
            builder_.createStore(builder_.createConstUndefined(), allocaVal, HIRType::makeAny());
            defineVariableAlloca(ident->name, allocaVal, HIRType::makeAny());
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

        // Mark captured variables as "captured by nested" and register the
        // closure cell so later writes can propagate. When a variable is
        // captured by MULTIPLE nested closures (e.g., lodash's `upperFirst`
        // referenced from many helper closures), record each one in
        // additionalCaptures — write sites iterate all of them so every
        // closure sees the assignment.
        int captureIdx = 0;
        for (const auto& cap : innerCaptures) {
            const std::string& capName = cap.first;
            size_t scopeIndex = 0;
            if (!isCapturedVariable(capName, &scopeIndex)) {
                auto* info = lookupVariableInfo(capName);
                if (info) {
                    auto closureAlloca = builder_.createAlloca(HIRType::makeAny(), capName + "$closure");
                    builder_.createStore(closureVal, closureAlloca);
                    if (!info->isCapturedByNested) {
                        info->isCapturedByNested = true;
                        info->closurePtr = closureAlloca;
                        info->captureIndex = captureIdx;
                    } else {
                        info->additionalCaptures.emplace_back(closureAlloca, captureIdx);
                    }
                }
            }
            captureIdx++;
        }

        // Store the closure into the pre-created alloca (if it exists)
        // This enables function hoisting - the alloca was created before processing statements
        auto* existingInfo = lookupVariableInfo(node->name);
        if (existingInfo && existingInfo->isAlloca) {
            builder_.createStore(closureVal, existingInfo->value);
            broadcastCaptureWrite(existingInfo, closureVal);
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
            broadcastCaptureWrite(existingInfo, closureVal);
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
                dynamic_cast<ast::FunctionExpression*>(node->initializer.get()) ||
                dynamic_cast<ast::ClassExpression*>(node->initializer.get())) {
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
            // If this variable is captured by nested closures, propagate the
            // assignment to every capture cell (lodash declares many helpers
            // that all capture the same forward-referenced var).
            broadcastCaptureWrite(existingInfo, initValue);
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
    // ECMA-262 8.5.2 BindingInitialization for ObjectBindingPattern:
    //   1. Perform ? RequireObjectCoercible(value).
    // Skip the check for empty `{}` patterns — spec for `ObjectBindingPattern : {}`
    // returns NormalCompletion(empty) without coercing.
    if (!pattern->elements.empty()) {
        builder_.createCall("ts_destructure_require_object", {sourceValue},
                            HIRType::makeVoid());
    }
    for (auto& elem : pattern->elements) {
        if (auto* binding = dynamic_cast<ast::BindingElement*>(elem.get())) {
            lowerBindingElement(binding, sourceValue, true /* isObjectPattern */);
        }
    }
}

void ASTToHIR::lowerArrayBindingPattern(ast::ArrayBindingPattern* pattern,
                                         std::shared_ptr<HIRValue> sourceValue) {
    // ECMA-262 8.5.2 BindingInitialization for ArrayBindingPattern uses
    // GetIterator, which throws TypeError when the source is null or
    // undefined (no @@iterator on those). Even for `[] = null` an empty
    // pattern still constructs an iterator, so the check applies
    // unconditionally.
    builder_.createCall("ts_destructure_require_object", {sourceValue},
                        HIRType::makeVoid());
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

    // ECMA-262 14.7.4.4 CreatePerIterationEnvironment: each iteration of
    // `for (let i ...)` should create a fresh binding so closures created in
    // the body snapshot THIS iter's value. Until we have full by-reference
    // cells, simulate the per-iter semantics for vars declared in the for's
    // init scope: clear closurePtr before the update step. This makes the
    // update read/write the alloca directly (via the existing null-closure
    // fallback) and leaves the captured closure's cell holding its body-time
    // snapshot. Next iter's cond also reads via the cleared alloca, then the
    // body re-creates a closure (new cell) for the new iter.
    if (!scopes_.empty()) {
        for (auto& kv : scopes_.back().variables) {
            auto& info = kv.second;
            if (info.isCapturedByNested && info.closurePtr) {
                // Raw C++ nullptr — not NaN-boxed null (0x02). The
                // LoadCaptureFromClosure runtime check tests for raw 0x0 to
                // trigger the alloca fallback path.
                auto nullVal = builder_.createConstRawNullPtr();
                builder_.createStore(nullVal, info.closurePtr);
            }
        }
    }

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

    // Any-typed iterables: route through the iterator protocol.
    bool isAnyIterable = !isGenerator && !isIteratorObject && iterable->type &&
        iterable->type->kind == HIRTypeKind::Any;
    if (isAnyIterable) {
        isIteratorObject = true;
    }
    // String-typed iterables: also route through the iterator protocol so
    // ts_iterator_get can return a proper code-point iterator. Without this,
    // we fell into the array fast path (createArrayLength + getElem) which
    // reads garbage from TsString and infinite-loops.
    bool isStringIterable = !isGenerator && !isIteratorObject && iterable->type &&
        iterable->type->kind == HIRTypeKind::String;
    if (isStringIterable) {
        isIteratorObject = true;
    }

    // For every iterator-protocol path, coerce to the actual iterator via
    // ts_iterator_get. Handles three cases:
    //   1. Value is already an iterator (has .next) → return as-is.
    //   2. Value is iterable (has [Symbol.iterator]) → call it, return result.
    //   3. Neither → return iterable unchanged; subsequent .next() returns
    //      undefined, .done is truthy, loop exits. Safe for non-iterables.
    // Generators/Map.keys()/custom iterables all go through this path uniformly.
    if (isGenerator || isIteratorObject) {
        iterable = builder_.createCall("ts_iterator_get", {iterable}, HIRType::makeAny());
    }

    isGenerator = isGenerator || isIteratorObject;

    if (isGenerator) {
        // Generator iteration: call .next() in a loop, check .done, get .value
        // Store result in an alloca so we can access it in both cond and body blocks
        auto resultAlloca = builder_.createAlloca(HIRType::makeObject(), "forof.result");

        // Store the iterator object in an alloca so it survives across yield
        // resume points. In a generator/async function the cond block can be
        // re-entered from a yield_resume path that doesn't dominate the SSA
        // value produced by ts_iterator_get above. Allocas are hoisted to the
        // function entry, so the stored value is visible from every block.
        auto iterAlloca = builder_.createAlloca(HIRType::makeObject(), "forof.iter");
        builder_.createStore(iterable, iterAlloca);

        builder_.createBranch(condBlock);

        // Condition: call gen.next(), check if result.done is true
        builder_.setInsertPoint(condBlock);
        currentBlock_ = condBlock;
        auto iterReload = builder_.createLoad(HIRType::makeObject(), iterAlloca);
        auto nextResult = builder_.createCallMethod(iterReload, "next", {}, HIRType::makeObject());
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
            } else {
                // Assignment-target form: `for (lhs of iter)` where lhs is
                // an existing variable, member access, element access, or
                // an array/object destructuring pattern. The parser hands
                // these to us as an ExpressionStatement (initializer is
                // StmtPtr, not ExprPtr), so unwrap one layer if needed.
                ast::Node* lhsNode = node->initializer.get();
                if (auto* es = dynamic_cast<ast::ExpressionStatement*>(lhsNode)) {
                    lhsNode = es->expression.get();
                }
                if (auto* lhsExpr = dynamic_cast<ast::Expression*>(lhsNode)) {
                    auto savedLast = lastValue_;
                    // Push elemVal as the synthetic RHS by stashing it as
                    // lastValue_ during a dispatch on a fake AssignmentExpression.
                    // We can't construct an AST AssignmentExpression here, so
                    // inline the destructure dispatch instead.
                    if (auto* arrLit = dynamic_cast<ast::ArrayLiteralExpression*>(lhsExpr)) {
                        // ECMA-262 13.15.5.1 early errors: rest element must
                        // be last and cannot have a default initializer.
                        for (size_t ri = 0; ri < arrLit->elements.size(); ++ri) {
                            auto* slot = arrLit->elements[ri].get();
                            if (auto* sp = dynamic_cast<ast::SpreadElement*>(slot)) {
                                if (ri + 1 != arrLit->elements.size()) {
                                    throw std::runtime_error("SyntaxError: Rest element must be last element in destructuring pattern");
                                }
                                if (dynamic_cast<ast::AssignmentExpression*>(sp->expression.get())) {
                                    throw std::runtime_error("SyntaxError: Rest element cannot have a default initializer");
                                }
                            }
                        }
                        // Inline minimal version of the dstr-assign code:
                        // for each element, extract source[i] (with default
                        // handling) and assign to the target.
                        builder_.createCall("ts_destructure_require_object",
                                            {elemVal}, HIRType::makeVoid());
                        int64_t index = 0;
                        for (auto& slotPtr : arrLit->elements) {
                            ast::Expression* slot = slotPtr.get();
                            if (!slot || dynamic_cast<ast::OmittedExpression*>(slot)) {
                                ++index;
                                continue;
                            }
                            ast::Expression* tgt = slot;
                            std::shared_ptr<HIRValue> value;
                            if (auto* sp = dynamic_cast<ast::SpreadElement*>(slot)) {
                                auto idxConst = builder_.createConstInt(index);
                                value = builder_.createCallMethod(elemVal, "slice",
                                    {idxConst}, HIRType::makeAny());
                                tgt = dynamic_cast<ast::Expression*>(sp->expression.get());
                            } else {
                                auto idxConst = builder_.createConstInt(index);
                                value = builder_.createGetElem(elemVal, idxConst, HIRType::makeAny());
                                if (auto* assn = dynamic_cast<ast::AssignmentExpression*>(slot)) {
                                    if (auto* defExpr = dynamic_cast<ast::Expression*>(assn->right.get())) {
                                        auto isUndef = builder_.createIsUndefined(value);
                                        auto defVal = lowerExpression(defExpr);
                                        defVal = boxValueIfNeeded(defVal);
                                        value = boxValueIfNeeded(value);
                                        value = builder_.createSelect(isUndef, defVal, value);
                                    }
                                    tgt = dynamic_cast<ast::Expression*>(assn->left.get());
                                }
                            }
                            // Simple identifier assignment (most common
                            // case in for-of dstr tests). Member/element
                            // forms are rare but supported via a fallback
                            // through the assign path.
                            if (auto* id = dynamic_cast<ast::Identifier*>(tgt)) {
                                auto* info = lookupVariableInfo(id->name);
                                if (info && info->isAlloca) {
                                    builder_.createStore(value, info->value, info->elemType);
                                } else if (info) {
                                    auto allocaPtr = builder_.createAlloca(value->type, id->name);
                                    builder_.createStore(value, allocaPtr, value->type);
                                    info->value = allocaPtr;
                                    info->elemType = value->type;
                                    info->isAlloca = true;
                                } else {
                                    defineVariable(id->name, value);
                                }
                                if (currentFunction_ && isModuleGlobalVar(id->name)) {
                                    builder_.createStoreGlobal(modVarName(id->name), value);
                                }
                            } else if (auto* pa = dynamic_cast<ast::PropertyAccessExpression*>(tgt)) {
                                auto obj = lowerExpression(pa->expression.get());
                                builder_.createSetPropStatic(obj, pa->name, value);
                            } else if (auto* ea = dynamic_cast<ast::ElementAccessExpression*>(tgt)) {
                                auto obj = lowerExpression(ea->expression.get());
                                auto idx = lowerExpression(ea->argumentExpression.get());
                                builder_.createSetElem(obj, idx, value);
                            }
                            ++index;
                        }
                    } else if (auto* id = dynamic_cast<ast::Identifier*>(lhsExpr)) {
                        // Plain `for (x of arr)` without let/const — assign
                        // to existing variable each iteration.
                        auto* info = lookupVariableInfo(id->name);
                        if (info && info->isAlloca) {
                            builder_.createStore(elemVal, info->value, info->elemType);
                        } else if (info) {
                            auto allocaPtr = builder_.createAlloca(elemVal->type, id->name);
                            builder_.createStore(elemVal, allocaPtr, elemVal->type);
                            info->value = allocaPtr;
                            info->elemType = elemVal->type;
                            info->isAlloca = true;
                        } else {
                            defineVariable(id->name, elemVal);
                        }
                        if (currentFunction_ && isModuleGlobalVar(id->name)) {
                            builder_.createStoreGlobal(modVarName(id->name), elemVal);
                        }
                    }
                    lastValue_ = savedLast;
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
            } else {
                // Assignment-target form: `for (lhs of iter)` where lhs is
                // an existing variable, member access, element access, or
                // an array/object destructuring pattern. The parser hands
                // these to us as an ExpressionStatement (initializer is
                // StmtPtr, not ExprPtr), so unwrap one layer if needed.
                ast::Node* lhsNode = node->initializer.get();
                if (auto* es = dynamic_cast<ast::ExpressionStatement*>(lhsNode)) {
                    lhsNode = es->expression.get();
                }
                if (auto* lhsExpr = dynamic_cast<ast::Expression*>(lhsNode)) {
                    auto savedLast = lastValue_;
                    // Push elemVal as the synthetic RHS by stashing it as
                    // lastValue_ during a dispatch on a fake AssignmentExpression.
                    // We can't construct an AST AssignmentExpression here, so
                    // inline the destructure dispatch instead.
                    if (auto* arrLit = dynamic_cast<ast::ArrayLiteralExpression*>(lhsExpr)) {
                        // ECMA-262 13.15.5.1 early errors: rest element must
                        // be last and cannot have a default initializer.
                        for (size_t ri = 0; ri < arrLit->elements.size(); ++ri) {
                            auto* slot = arrLit->elements[ri].get();
                            if (auto* sp = dynamic_cast<ast::SpreadElement*>(slot)) {
                                if (ri + 1 != arrLit->elements.size()) {
                                    throw std::runtime_error("SyntaxError: Rest element must be last element in destructuring pattern");
                                }
                                if (dynamic_cast<ast::AssignmentExpression*>(sp->expression.get())) {
                                    throw std::runtime_error("SyntaxError: Rest element cannot have a default initializer");
                                }
                            }
                        }
                        // Inline minimal version of the dstr-assign code:
                        // for each element, extract source[i] (with default
                        // handling) and assign to the target.
                        builder_.createCall("ts_destructure_require_object",
                                            {elemVal}, HIRType::makeVoid());
                        int64_t index = 0;
                        for (auto& slotPtr : arrLit->elements) {
                            ast::Expression* slot = slotPtr.get();
                            if (!slot || dynamic_cast<ast::OmittedExpression*>(slot)) {
                                ++index;
                                continue;
                            }
                            ast::Expression* tgt = slot;
                            std::shared_ptr<HIRValue> value;
                            if (auto* sp = dynamic_cast<ast::SpreadElement*>(slot)) {
                                auto idxConst = builder_.createConstInt(index);
                                value = builder_.createCallMethod(elemVal, "slice",
                                    {idxConst}, HIRType::makeAny());
                                tgt = dynamic_cast<ast::Expression*>(sp->expression.get());
                            } else {
                                auto idxConst = builder_.createConstInt(index);
                                value = builder_.createGetElem(elemVal, idxConst, HIRType::makeAny());
                                if (auto* assn = dynamic_cast<ast::AssignmentExpression*>(slot)) {
                                    if (auto* defExpr = dynamic_cast<ast::Expression*>(assn->right.get())) {
                                        auto isUndef = builder_.createIsUndefined(value);
                                        auto defVal = lowerExpression(defExpr);
                                        defVal = boxValueIfNeeded(defVal);
                                        value = boxValueIfNeeded(value);
                                        value = builder_.createSelect(isUndef, defVal, value);
                                    }
                                    tgt = dynamic_cast<ast::Expression*>(assn->left.get());
                                }
                            }
                            // Simple identifier assignment (most common
                            // case in for-of dstr tests). Member/element
                            // forms are rare but supported via a fallback
                            // through the assign path.
                            if (auto* id = dynamic_cast<ast::Identifier*>(tgt)) {
                                auto* info = lookupVariableInfo(id->name);
                                if (info && info->isAlloca) {
                                    builder_.createStore(value, info->value, info->elemType);
                                } else if (info) {
                                    auto allocaPtr = builder_.createAlloca(value->type, id->name);
                                    builder_.createStore(value, allocaPtr, value->type);
                                    info->value = allocaPtr;
                                    info->elemType = value->type;
                                    info->isAlloca = true;
                                } else {
                                    defineVariable(id->name, value);
                                }
                                if (currentFunction_ && isModuleGlobalVar(id->name)) {
                                    builder_.createStoreGlobal(modVarName(id->name), value);
                                }
                            } else if (auto* pa = dynamic_cast<ast::PropertyAccessExpression*>(tgt)) {
                                auto obj = lowerExpression(pa->expression.get());
                                builder_.createSetPropStatic(obj, pa->name, value);
                            } else if (auto* ea = dynamic_cast<ast::ElementAccessExpression*>(tgt)) {
                                auto obj = lowerExpression(ea->expression.get());
                                auto idx = lowerExpression(ea->argumentExpression.get());
                                builder_.createSetElem(obj, idx, value);
                            }
                            ++index;
                        }
                    } else if (auto* id = dynamic_cast<ast::Identifier*>(lhsExpr)) {
                        // Plain `for (x of arr)` without let/const — assign
                        // to existing variable each iteration.
                        auto* info = lookupVariableInfo(id->name);
                        if (info && info->isAlloca) {
                            builder_.createStore(elemVal, info->value, info->elemType);
                        } else if (info) {
                            auto allocaPtr = builder_.createAlloca(elemVal->type, id->name);
                            builder_.createStore(elemVal, allocaPtr, elemVal->type);
                            info->value = allocaPtr;
                            info->elemType = elemVal->type;
                            info->isAlloca = true;
                        } else {
                            defineVariable(id->name, elemVal);
                        }
                        if (currentFunction_ && isModuleGlobalVar(id->name)) {
                            builder_.createStoreGlobal(modVarName(id->name), elemVal);
                        }
                    }
                    lastValue_ = savedLast;
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

    // Get keys array: own + inherited enumerable string keys (for-in walks the
    // prototype chain, unlike Object.keys which is own-only).
    auto keys = builder_.createCall("ts_object_for_in_keys", {obj}, HIRType::makeArray(HIRType::makeString()));

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

    // Classify the case expressions to pick a lowering strategy:
    //   - all numeric literals -> dense integer switch (fast path)
    //   - all string literals  -> ts_string_eq if-else chain (cheap value eq)
    //   - anything else (identifiers, member access, mixed types) -> general
    //     strict-equality (===) if-else chain. JS evaluates case expressions
    //     top-to-bottom and stops at the first strict-equal match; the chain
    //     models exactly that. The previous code only emitted comparisons for
    //     StringLiteral/NumericLiteral case labels and SILENTLY DROPPED any
    //     non-literal case (e.g. `case dateTag:` where dateTag is a variable),
    //     so such cases never matched and fell through -- breaking lodash's
    //     equalByTag (tag constants) and any switch over computed values.
    bool anyCaseExpr = false, allNumeric = true, allString = true;
    for (auto& clause : node->clauses) {
        auto* caseClause = dynamic_cast<ast::CaseClause*>(clause.get());
        if (caseClause && caseClause->expression) {
            anyCaseExpr = true;
            if (!dynamic_cast<ast::NumericLiteral*>(caseClause->expression.get())) allNumeric = false;
            if (!dynamic_cast<ast::StringLiteral*>(caseClause->expression.get())) allString = false;
        }
    }

    if (anyCaseExpr && allNumeric) {
        // Dense integer switch.
        std::vector<std::pair<int64_t, HIRBlock*>> cases;
        size_t blockIdx = 0;
        for (auto& clause : node->clauses) {
            auto* caseClause = dynamic_cast<ast::CaseClause*>(clause.get());
            if (caseClause && caseClause->expression) {
                auto* numLit = dynamic_cast<ast::NumericLiteral*>(caseClause->expression.get());
                if (numLit && blockIdx < caseBlocks.size()) {
                    cases.push_back({static_cast<int64_t>(numLit->value), caseBlocks[blockIdx]});
                }
            }
            blockIdx++;
        }
        builder_.createSwitch(switchVal, defaultBlock, cases);
    } else {
        // If-else comparison chain. switchVal may be boxed (any/TsValue*).
        // For all-string-literal switches keep ts_string_eq (HIRToLLVM's
        // handler unboxes via ts_value_get_string). Otherwise evaluate each
        // case expression at runtime and compare with full === semantics.
        bool useStringEq = anyCaseExpr && allString;
        std::shared_ptr<HIRValue> boxedSwitch;
        if (!useStringEq) boxedSwitch = boxValueIfNeeded(switchVal);

        size_t blockIdx = 0;
        for (auto& clause : node->clauses) {
            auto* caseClause = dynamic_cast<ast::CaseClause*>(clause.get());

            if (caseClause && caseClause->expression && blockIdx < caseBlocks.size()) {
                std::shared_ptr<HIRValue> cmpResult;
                if (useStringEq) {
                    auto* strLit = dynamic_cast<ast::StringLiteral*>(caseClause->expression.get());
                    auto caseStr = builder_.createConstString(strLit->value);
                    cmpResult = builder_.createCall("ts_string_eq",
                        {switchVal, caseStr}, HIRType::makeBool());
                } else {
                    // Evaluate the case expression in the current check block
                    // so side effects occur in order and only until a match.
                    auto caseVal = lowerExpression(caseClause->expression.get());
                    auto boxedCase = boxValueIfNeeded(caseVal);
                    cmpResult = builder_.createCall("ts_value_strict_eq",
                        {boxedSwitch, boxedCase}, HIRType::makeAny());
                }

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
            blockIdx++;
        }

        // If we're in a check block (not the default block itself) without a
        // terminator, branch to default.
        if (!hasTerminator() && currentBlock_ != defaultBlock) {
            builder_.createBranch(defaultBlock);
        }
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

    // Logical-assignment operators with short-circuit semantics
    // (ECMA-262 §13.15.2): a ??= b, a ||= b, a &&= b.
    // RHS must NOT evaluate when the assignment is skipped — and the
    // assignment itself must be skipped (not just no-op'd) so that
    // non-configurable properties aren't redefined etc.
    //
    //   a ??= b   →   if (a is nullish) a = b; return a
    //   a ||= b   →   if (a is falsy)   a = b; return a
    //   a &&= b   →   if (a is truthy)  a = b; return a
    if (op == "??=" || op == "||=" || op == "&&=") {
        // 1. Load LHS.
        auto lhs = lowerExpression(node->left.get());
        auto boxedLhs = boxValueIfNeeded(lhs);

        // 2. Compute condition (true = perform assignment).
        std::shared_ptr<HIRValue> shouldAssign;
        if (op == "??=") {
            shouldAssign = builder_.createCall(
                "ts_value_is_nullish", {boxedLhs}, HIRType::makeBool());
        } else {
            // ||= or &&= : need truthiness conversion.
            std::shared_ptr<HIRValue> isTruthy;
            if (lhs->type && lhs->type->kind == HIRTypeKind::Bool) {
                isTruthy = lhs;
            } else {
                isTruthy = builder_.createCall(
                    "ts_value_to_bool", {boxedLhs}, HIRType::makeBool());
            }
            if (op == "&&=") {
                shouldAssign = isTruthy;
            } else {  // ||=
                // shouldAssign = !isTruthy. ts_value_to_bool returns i1;
                // emit XOR with constant true to get NOT.
                auto trueConst = builder_.createConstBool(true);
                shouldAssign = builder_.createXorI64(isTruthy, trueConst);
            }
        }

        // 3. Branch structure.
        int blockId = blockCounter_++;
        auto* assignBlock = builder_.createBlock("lassign_rhs_" + std::to_string(blockId));
        auto* mergeBlock = builder_.createBlock("lassign_merge_" + std::to_string(blockId));
        auto* lhsBlock = builder_.getInsertBlock();

        builder_.createCondBranch(shouldAssign, assignBlock, mergeBlock);

        // 4. Assign block — lower RHS, store back, fall through to merge.
        builder_.setInsertPoint(assignBlock);
        currentBlock_ = assignBlock;
        auto rhs = lowerExpression(node->right.get());
        auto boxedRhs = boxValueIfNeeded(rhs);

        // Inline store-back. Mirrors the LValue handling in the existing
        // compound-assignment branch below. The three supported LHS forms
        // cover the spec's AssignmentTarget enumeration: identifier,
        // property access, element access. We box the rhs when storing
        // into an Any-typed slot — the LHS variable's existing type is
        // usually Any (since logical-assignment LHS is by definition a
        // value that could be nullish/falsy/etc.), so writing a primitive
        // Int64 directly into a ptr slot produces garbage on readback.
        if (auto* ident = dynamic_cast<ast::Identifier*>(node->left.get())) {
            // boxedRhs is used when the slot is Any/ptr-typed.
            auto storeIntoSlot = [&](std::shared_ptr<HIRValue> slotPtr,
                                     std::shared_ptr<HIRType> slotType) {
                std::shared_ptr<HIRValue> toStore = rhs;
                if (slotType && slotType->kind == HIRTypeKind::Any) {
                    toStore = boxedRhs;
                }
                builder_.createStore(toStore, slotPtr, slotType);
            };
            if (currentFunction_ && isModuleGlobalVar(ident->name)) {
                size_t scopeIdx = 0;
                if (isCapturedVariable(ident->name, &scopeIdx)) {
                    moduleGlobalsUsedByInnerByModule_[ident->name].insert(currentModulePath_);
                    builder_.createStoreGlobal(modVarName(ident->name), boxedRhs);
                } else if (auto* info = lookupVariableInfo(ident->name)) {
                    if (info->isAlloca) {
                        storeIntoSlot(info->value, info->elemType);
                    }
                    builder_.createStoreGlobal(modVarName(ident->name), boxedRhs);
                }
            } else {
                size_t scopeIndex = 0;
                if (currentFunction_ && isCapturedVariable(ident->name, &scopeIndex)) {
                    auto* capInfo = lookupVariableInfo(ident->name);
                    auto type = capInfo && capInfo->elemType ? capInfo->elemType : rhs->type;
                    registerCapture(ident->name, type, scopeIndex);
                    currentFunction_->hasClosure = true;
                    builder_.createStoreCapture(ident->name, boxedRhs);
                } else if (auto* info = lookupVariableInfo(ident->name)) {
                    if (info->isAlloca) {
                        storeIntoSlot(info->value, info->elemType);
                        broadcastCaptureWrite(info, boxedRhs);
                    } else {
                        auto allocaPtr = builder_.createAlloca(rhs->type, ident->name);
                        builder_.createStore(rhs, allocaPtr, rhs->type);
                        info->value = allocaPtr;
                        info->elemType = rhs->type;
                        info->isAlloca = true;
                    }
                }
            }
        } else if (auto* propAccess = dynamic_cast<ast::PropertyAccessExpression*>(node->left.get())) {
            auto obj = lowerExpression(propAccess->expression.get());
            auto propName = builder_.createConstString(propAccess->name);
            builder_.createCall("ts_object_set_property",
                {obj, propName, boxedRhs}, HIRType::makeVoid());
        } else if (auto* elemAccess = dynamic_cast<ast::ElementAccessExpression*>(node->left.get())) {
            auto arr = lowerExpression(elemAccess->expression.get());
            auto idx = lowerExpression(elemAccess->argumentExpression.get());
            builder_.createCall("ts_array_set",
                {arr, idx, boxedRhs}, HIRType::makeVoid());
        }

        auto* finalAssignBlock = builder_.getInsertBlock();
        builder_.createBranch(mergeBlock);

        // 5. Merge — phi between original LHS (skipped path) and RHS (took path).
        builder_.setInsertPoint(mergeBlock);
        currentBlock_ = mergeBlock;
        std::vector<std::pair<std::shared_ptr<HIRValue>, HIRBlock*>> phiIncoming;
        phiIncoming.push_back(std::make_pair(boxedLhs, lhsBlock));
        phiIncoming.push_back(std::make_pair(boxedRhs, finalAssignBlock));
        lastValue_ = builder_.createPhi(HIRType::makeAny(), phiIncoming);
        return;
    }

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

    // Strategy B Phase 4c: AST fallback removed.
    //
    // Until Phase 4a, these helpers had to fall back to `astNode->inferredType`
    // because GetPropStatic for typed property access emitted with type=Any,
    // losing the analyzer's type info on the HIR side. Phase 4a fixed that:
    // ASTToHIR now passes the class-shape-derived type to createGetPropStatic,
    // and the LLVM value at the SSA name is the actual typed thing.
    //
    // The AST fallback is now redundant and removed. The Phase 0b probe
    // (commit caa81b8) regressed `array_churn` and `linked_list` by 36-46%
    // because of the missing type info; that regression should NOT recur
    // after 4a + 4b.
    //
    // BigInt is the only type still keyed off `astNode->inferredType` because
    // HIRTypeKind::BigInt isn't yet propagated through HIRValue::type.
    auto isString = [](const std::shared_ptr<HIRValue>& val, ast::Expression*) {
        return val && val->type && val->type->kind == HIRTypeKind::String;
    };

    auto isFloat64 = [](const std::shared_ptr<HIRValue>& val, ast::Expression*) {
        return val && val->type && val->type->kind == HIRTypeKind::Float64;
    };

    auto isBigInt = [](ast::Expression* astNode) {
        if (!astNode) return false;
        // BigInt literal (e.g. `1n`) — trust the syntactic tag even if the
        // analyzer didn't run (untyped JS relaxed mode).
        if (dynamic_cast<ast::BigIntLiteral*>(astNode)) return true;
        // Otherwise rely on inferredType from the analyzer.
        return astNode->inferredType &&
               astNode->inferredType->kind == ts::TypeKind::BigInt;
    };

    auto isNumber = [&isFloat64](const std::shared_ptr<HIRValue>& val, ast::Expression*) {
        if (val && val->type) {
            if (val->type->kind == HIRTypeKind::Int64 ||
                val->type->kind == HIRTypeKind::Float64) return true;
        }
        return false;
    };

    auto isBoolean = [](const std::shared_ptr<HIRValue>& val, ast::Expression*) {
        return val && val->type && val->type->kind == HIRTypeKind::Bool;
    };

    auto isAnyOrNullish = [](const std::shared_ptr<HIRValue>& val, ast::Expression*) {
        return val && val->type && val->type->kind == HIRTypeKind::Any;
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
    // BigInt: check HIRValue type (set by visitBigIntLiteral and propagated
    // through variable references) first, then fall back to the ast-level tag.
    // Require BOTH operands to be known BigInt — mixed BigInt/Number is a
    // TypeError per spec and must not hit the bigint-only fast path
    // (which would type-mismatch ts_bigint_add/gt against a raw double).
    auto hirIsBigInt = [](const std::shared_ptr<HIRValue>& v) {
        return v && v->type && v->type->kind == HIRTypeKind::BigInt;
    };
    bool lhsIsBigInt = hirIsBigInt(lhs) || isBigInt(node->left.get());
    bool rhsIsBigInt = hirIsBigInt(rhs) || isBigInt(node->right.get());
    bool useBigInt = lhsIsBigInt && rhsIsBigInt;

    if (op == "+") {
        // Strategy B Phase 3: emit generic Add. SpecializationPass (which
        // runs after TypePropagationPass) will rewrite this into the
        // appropriate type-specific instruction (StringConcat, AddF64,
        // AddI64, ts_bigint_add, ts_value_add) based on operand types.
        //
        // We still use the AST-fallback helpers here to compute a precise
        // best-guess result type for the generic instruction. This is the
        // load-bearing AST fallback identified in the Phase 0b probe — it
        // can be removed once Phase 4 fixes GetPropStatic precision and
        // SpecializationPass has access to all the same type info.
        std::shared_ptr<HIRType> resultType;
        if (isString(lhs, node->left.get()) || isString(rhs, node->right.get())) {
            resultType = HIRType::makeString();
        } else if (useBigInt) {
            resultType = HIRType::makeBigInt();
        } else if (isAnyOrNullish(lhs, node->left.get()) && isAnyOrNullish(rhs, node->right.get())) {
            resultType = HIRType::makeAny();
        } else if (useFloat) {
            resultType = HIRType::makeFloat64();
        } else {
            resultType = HIRType::makeInt64();
        }
        lastValue_ = builder_.createAdd(lhs, rhs, resultType);
    } else if (op == "-" || op == "*" || op == "/" || op == "%") {
        // Strategy B Phase 3: emit generic Sub/Mul/Div/Mod. SpecializationPass
        // will rewrite to the type-specific opcode based on operand types.
        // Note these operators (unlike +) use OR for the Any-fallback check —
        // if either operand is Any, dispatch dynamically.
        std::shared_ptr<HIRType> resultType;
        if (useBigInt) {
            resultType = HIRType::makeBigInt();
        } else if (isAnyOrNullish(lhs, node->left.get()) || isAnyOrNullish(rhs, node->right.get())) {
            resultType = HIRType::makeAny();
        } else if (useFloat) {
            resultType = HIRType::makeFloat64();
        } else {
            resultType = HIRType::makeInt64();
        }
        if (op == "-")      lastValue_ = builder_.createSub(lhs, rhs, resultType);
        else if (op == "*") lastValue_ = builder_.createMul(lhs, rhs, resultType);
        else if (op == "/") lastValue_ = builder_.createDiv(lhs, rhs, resultType);
        else                lastValue_ = builder_.createMod(lhs, rhs, resultType);
    } else if (op == "**") {
        // Exponentiation. No specialized HIR opcode; dispatch directly:
        // - BigInt ** BigInt → ts_bigint_pow (arbitrary-precision integer).
        // - Numeric           → ts_math_pow (double, matches Math.pow).
        // Mixed BigInt/Number is a TypeError per spec; we route through
        // ts_math_pow which will coerce the BigInt to NaN (approximate).
        if (useBigInt) {
            lastValue_ = builder_.createCall("ts_bigint_pow", {lhs, rhs}, HIRType::makeBigInt());
        } else {
            // Ensure both operands are Float64 for ts_math_pow.
            auto castToF64 = [this](std::shared_ptr<HIRValue> v) {
                if (v && v->type) {
                    if (v->type->kind == HIRTypeKind::Int64) return builder_.createCastI64ToF64(v);
                    if (v->type->kind == HIRTypeKind::Float64) return v;
                }
                // Any / object: let the runtime coerce via ts_value_get_double on the call site.
                return v;
            };
            auto lhsF = castToF64(lhs);
            auto rhsF = castToF64(rhs);
            lastValue_ = builder_.createCall("ts_math_pow", {lhsF, rhsF}, HIRType::makeFloat64());
        }
    } else if (op == "<" || op == "<=" || op == ">" || op == ">=") {
        // Strategy B Phase 4d: emit generic ordering comparison.
        // After Phase 4a+4c, operand types are reliable in HIR, so
        // SpecializationPass can pick the right typed form from
        // operand val->type alone (no AST fallback needed).
        //
        // Equality forms (==, !=, ===, !==) are NOT migrated here —
        // they have many special cases (typesIncompatibleForStrictEqual,
        // isString-pair check, isUndefinedLiteral/isNullLiteral) that are
        // language-feature handling, not pure type-driven specialization.
        if (useBigInt) {
            const char* fn = (op == "<")  ? "ts_bigint_lt"
                           : (op == "<=") ? "ts_bigint_le"
                           : (op == ">")  ? "ts_bigint_gt"
                           :                "ts_bigint_ge";
            lastValue_ = builder_.createCall(fn, {lhs, rhs}, HIRType::makeBool());
        } else {
            std::shared_ptr<HIRValue> v;
            if      (op == "<")  v = builder_.createCmpLt(lhs, rhs);
            else if (op == "<=") v = builder_.createCmpLe(lhs, rhs);
            else if (op == ">")  v = builder_.createCmpGt(lhs, rhs);
            else                 v = builder_.createCmpGe(lhs, rhs);
            lastValue_ = v;
        }
    } else if (op == "==") {
        // Loose equality - use coercing comparison
        if (useBigInt) {
            lastValue_ = builder_.createCall("ts_bigint_eq", {lhs, rhs}, HIRType::makeBool());
        } else if (lhsIsBigInt || rhsIsBigInt) {
            // Mixed BigInt/Number/String: route through ts_value_eq which
            // handles BigInt↔Number/String value comparison per spec.
            // Emitting CmpEqF64 here would unbox the BigInt ptr as double → garbage.
            lastValue_ = builder_.createCall("ts_value_eq", {lhs, rhs}, HIRType::makeAny());
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
        } else if (lhsIsBigInt || rhsIsBigInt) {
            // Mixed: route through ts_value_eq + negate (same reasoning as ==).
            auto eq = builder_.createCall("ts_value_eq", {lhs, rhs}, HIRType::makeAny());
            lastValue_ = builder_.createLogicalNot(eq);
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
            // Mirror the binary `+` path exactly so that `x += y` lowers
            // identically to `x = x + y`. Emitting a low-level StringConcat
            // directly here is wrong when an operand is Any/boxed (e.g.
            // `arr[i] += ''` where arr[i] is a dynamic value): StringConcat
            // assumes string operands and reads a boxed number as a string
            // pointer → garbage. The generic Add goes through
            // SpecializationPass, which routes Any+String through the runtime
            // coercing add (ts_value_add) just like the binary `+` operator.
            std::shared_ptr<HIRType> resultType;
            if (isString(lhs, node->left.get()) || isString(rhs, node->right.get())) {
                resultType = HIRType::makeString();
            } else if (useBigInt) {
                resultType = HIRType::makeBigInt();
            } else if (isAnyOrNullish(lhs, node->left.get()) && isAnyOrNullish(rhs, node->right.get())) {
                resultType = HIRType::makeAny();
            } else if (useFloat) {
                resultType = HIRType::makeFloat64();
            } else {
                resultType = HIRType::makeInt64();
            }
            result = builder_.createAdd(lhs, rhs, resultType);
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
                    // Mark as used-by-inner so reads in __module_init take the
                    // global path instead of the stale local fast-path alloca.
                    moduleGlobalsUsedByInnerByModule_[ident->name].insert(currentModulePath_);
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
                broadcastCaptureWrite(info, result);
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
            // Use createSetElem (same as the simple-assignment path in
            // visitAssignmentExpression) so the value is boxed correctly by
            // type. The previous manual `ts_array_set` + boxValueIfNeeded(result)
            // wrapped a String result via ts_value_make_object → a TsString
            // stored as a generic object, so readback/typeof was wrong
            // (e.g. `arr[i] += ''` produced undefined instead of the string).
            builder_.createSetElem(arr, idx, result);
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
                // Mark as used-by-inner so reads in __module_init take the
                // global path instead of the stale local fast-path alloca.
                moduleGlobalsUsedByInnerByModule_[ident->name].insert(currentModulePath_);
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
            broadcastCaptureWrite(info, rhs);
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
            // Skip nullptr placeholders (see getter path comment) — same UAF
            // family for private-setter-before-super class-body lowering.
            if (setterIt != targetClass->methods.end() && setterIt->second) {
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

    // Handle destructuring assignment: `[a, b = 1, ...rest] = arr` (LHS is
    // an ArrayLiteralExpression because the parser cannot distinguish the
    // assignment-target from the value form until it sees the `=`). Each
    // element is one of:
    //   - Identifier: simple assignment of source[i]
    //   - AssignmentExpression (target = default): use default when
    //     source[i] is undefined
    //   - SpreadElement (...rest): assign source.slice(i) to rest
    //   - OmittedExpression: skip the slot
    // Without this branch, `[x, y] = [1, 2]` falls through and stores
    // nothing — variables remain undefined.
    auto* arrLit = dynamic_cast<ast::ArrayLiteralExpression*>(node->left.get());
    if (arrLit) {
        // ECMA-262 13.15.5.1 (AssignmentPattern early errors):
        //   - AssignmentRestElement must be the LAST element of the
        //     pattern. `[...x,]` and `[...x, y]` are SyntaxErrors.
        //   - AssignmentRestElement is `... DestructuringAssignmentTarget`
        //     and CANNOT have an Initializer. `[...x = 1]` is a SyntaxError.
        // We catch these here as compile-time errors. The parser doesn't
        // validate because it can't distinguish AssignmentExpression LHS
        // from a value array until it sees the `=`.
        for (size_t i = 0; i < arrLit->elements.size(); ++i) {
            auto* slot = arrLit->elements[i].get();
            if (auto* sp = dynamic_cast<ast::SpreadElement*>(slot)) {
                if (i + 1 != arrLit->elements.size()) {
                    throw std::runtime_error("SyntaxError: Rest element must be last element in destructuring pattern");
                }
                if (auto* assn = dynamic_cast<ast::AssignmentExpression*>(sp->expression.get())) {
                    (void)assn;
                    throw std::runtime_error("SyntaxError: Rest element cannot have a default initializer");
                }
            }
        }
        // Per ECMA-262 RequireObjectCoercible — null/undefined source throws.
        builder_.createCall("ts_destructure_require_object", {rhs},
                            HIRType::makeVoid());
        // Lower each LHS slot. We re-enter visitAssignmentExpression for
        // each per-slot assignment so all the existing target shapes
        // (identifier, member access, element access, nested pattern) are
        // handled uniformly.
        auto assignToTarget = [&](ast::Expression* target,
                                  std::shared_ptr<HIRValue> value) {
            // Build a synthetic AssignmentExpression with `target = <value>`.
            // The RHS expression is dummy because we set lastValue_
            // directly via a captured-binding shim; instead, just inline
            // the dispatch.
            // Simple identifier: use the same logic as the start of this
            // function (module-global handling, captured, alloca, etc).
            if (auto* tgt = dynamic_cast<ast::Identifier*>(target)) {
                // Inline a simplified version of the identifier-LHS path.
                if (currentFunction_ && isModuleGlobalVar(tgt->name)) {
                    size_t scopeIndex = 0;
                    if (isCapturedVariable(tgt->name, &scopeIndex)) {
                        moduleGlobalsUsedByInnerByModule_[tgt->name].insert(currentModulePath_);
                        builder_.createStoreGlobal(modVarName(tgt->name), value);
                        return;
                    }
                }
                size_t scopeIndex = 0;
                if (currentFunction_ && isCapturedVariable(tgt->name, &scopeIndex)) {
                    auto* info = lookupVariableInfo(tgt->name);
                    auto type = info && info->elemType ? info->elemType : value->type;
                    registerCapture(tgt->name, type, scopeIndex);
                    currentFunction_->hasClosure = true;
                    builder_.createStoreCapture(tgt->name, value);
                    return;
                }
                auto* info = lookupVariableInfo(tgt->name);
                if (info && info->isAlloca) {
                    builder_.createStore(value, info->value, info->elemType);
                    broadcastCaptureWrite(info, value);
                } else if (info) {
                    auto allocaPtr = builder_.createAlloca(value->type, tgt->name);
                    builder_.createStore(value, allocaPtr, value->type);
                    info->value = allocaPtr;
                    info->elemType = value->type;
                    info->isAlloca = true;
                } else {
                    defineVariable(tgt->name, value);
                }
                if (isModuleGlobalVar(tgt->name)) {
                    builder_.createStoreGlobal(modVarName(tgt->name), value);
                }
                return;
            }
            if (auto* tgt = dynamic_cast<ast::PropertyAccessExpression*>(target)) {
                auto obj = lowerExpression(tgt->expression.get());
                builder_.createSetPropStatic(obj, tgt->name, value);
                return;
            }
            if (auto* tgt = dynamic_cast<ast::ElementAccessExpression*>(target)) {
                auto obj = lowerExpression(tgt->expression.get());
                auto idx = lowerExpression(tgt->argumentExpression.get());
                builder_.createSetElem(obj, idx, value);
                return;
            }
            // Nested array pattern is rare here because nested patterns
            // come through the binding form; if it shows up we extend
            // this helper later.
        };

        int64_t index = 0;
        for (auto& elemPtr : arrLit->elements) {
            ast::Expression* elem = elemPtr.get();
            if (!elem || dynamic_cast<ast::OmittedExpression*>(elem)) {
                ++index;
                continue;
            }
            if (auto* spread = dynamic_cast<ast::SpreadElement*>(elem)) {
                // ...rest = source.slice(index) — dispatch via prototype so
                // typed arrays / array-likes use their own slice method.
                auto idxConst = builder_.createConstInt(index);
                auto restVal = builder_.createCallMethod(rhs, "slice",
                    {idxConst}, HIRType::makeAny());
                if (auto* tgtExpr = dynamic_cast<ast::Expression*>(spread->expression.get())) {
                    assignToTarget(tgtExpr, restVal);
                }
                ++index;
                continue;
            }
            // Extract source[index] (may be undefined for missing slots).
            auto idxConst = builder_.createConstInt(index);
            auto extracted = builder_.createGetElem(rhs, idxConst, HIRType::makeAny());

            ast::Expression* target = elem;
            // Default-initializer form: `target = defaultValue` — the
            // parser represents this as an AssignmentExpression in the
            // array-literal element position (covers both compound and
            // simple `=`; per ECMA-262 only `=` is valid here, but we
            // accept any AssignmentExpression and treat it as `=` since
            // the AST doesn't carry an operator).
            if (auto* assignDefault = dynamic_cast<ast::AssignmentExpression*>(elem)) {
                auto* defaultExpr = dynamic_cast<ast::Expression*>(assignDefault->right.get());
                if (defaultExpr) {
                    auto isUndef = builder_.createIsUndefined(extracted);
                    auto defaultVal = lowerExpression(defaultExpr);
                    defaultVal = boxValueIfNeeded(defaultVal);
                    extracted = boxValueIfNeeded(extracted);
                    extracted = builder_.createSelect(isUndef, defaultVal, extracted);
                }
                target = dynamic_cast<ast::Expression*>(assignDefault->left.get());
            }
            if (target) assignToTarget(target, extracted);
            ++index;
        }
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

    // Spread arguments at the call site (`f(...a, b, ...c)`). Without
    // expansion, ASTToHIR would pass each spread array as a single arg
    // and the callee would see e.g. `[args[0]=arrayA, args[1]=arrayB]`
    // when it expected `[args[0]=1, args[1]=2, ...]`. visitSpreadElement
    // returns just the underlying expression's HIRValue (the array), so
    // we detect spreads here and lower to a runtime apply: build a
    // TsArray containing every argument expanded, then call
    // ts_function_apply(callee, undefined, expandedArgs).
    //
    // Excludes super() (handled below; takes its own ctor path) and
    // any case where there's no spread (preserve the fast direct-call
    // paths below).
    bool hasSpread = false;
    for (auto& arg : node->arguments) {
        if (dynamic_cast<ast::SpreadElement*>(arg.get())) { hasSpread = true; break; }
    }
    if (hasSpread && !dynamic_cast<ast::SuperExpression*>(node->callee.get())) {
        auto anyArr = HIRType::makeArray(HIRType::makeAny(), false);
        auto packed = builder_.createCall("ts_array_create", {}, anyArr);
        for (size_t i = 0; i < node->arguments.size(); ++i) {
            if (dynamic_cast<ast::SpreadElement*>(node->arguments[i].get())) {
                // ts_array_concat returns a NEW array, capture it.
                packed = builder_.createCall("ts_array_concat",
                    {packed, boxValueIfNeeded(args[i])}, anyArr);
            } else {
                builder_.createCall("ts_array_push",
                    {packed, boxValueIfNeeded(args[i])}, HIRType::makeInt64());
            }
        }
        auto calleeVal = lowerExpression(node->callee.get());
        auto undef = builder_.createConstUndefined();
        lastValue_ = builder_.createCall(
            "ts_function_apply",
            {boxValueIfNeeded(calleeVal), undef, packed},
            HIRType::makeAny());
        return;
    }

    // Handle super() call - calls parent class constructor
    auto* superExpr = dynamic_cast<ast::SuperExpression*>(node->callee.get());
    if (superExpr && currentClass_ && currentClass_->baseClass) {
        if (currentClass_->baseClass->constructor) {
            // Base class has explicit constructor - call it with [this, ...args]
            // Truncate or pad args to match the base constructor's arity:
            // verifier rejects extra args (super(1,2) on a zero-arg base),
            // and missing args are undefined.
            HIRFunction* baseCtor = currentClass_->baseClass->constructor;
            // Param 0 of a constructor is `this` (implicit), so user-visible
            // arity is params.size() - 1.
            size_t expectedUserArgs = baseCtor->params.empty() ? 0 : baseCtor->params.size() - 1;
            std::vector<std::shared_ptr<HIRValue>> ctorArgs;
            auto thisVal = lookupVariable("this");
            if (thisVal) {
                ctorArgs.push_back(thisVal);
            } else {
                ctorArgs.push_back(builder_.createConstNull());
            }
            if (baseCtor->hasRestParam) {
                for (auto& arg : args) ctorArgs.push_back(arg);
            } else {
                for (size_t i = 0; i < expectedUserArgs; ++i) {
                    if (i < args.size()) ctorArgs.push_back(args[i]);
                    else ctorArgs.push_back(builder_.createConstUndefined());
                }
            }
            builder_.createCall(baseCtor->name, ctorArgs, HIRType::makeVoid());
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

        // Case super: `super.method(...)` inside a class method. Walk the
        // base-class chain (skipping currentClass_ itself, so an override
        // doesn't shadow the parent's implementation) and emit a direct
        // call to the resolved method with `this` from the current scope.
        // ECMA-262 §13.3.7 GetSuperBase: super resolves to the home
        // object's [[Prototype]], which for class methods is the parent
        // prototype.
        auto* superRecv = dynamic_cast<ast::SuperExpression*>(propAccess->expression.get());
        if (superRecv && currentClass_ && currentClass_->baseClass) {
            HIRClass* searchClass = currentClass_->baseClass;
            while (searchClass) {
                auto it = searchClass->methods.find(propAccess->name);
                if (it != searchClass->methods.end() && it->second) {
                    HIRFunction* method = it->second;
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
            // Method not found in any user-defined base class. Fall back
            // to dynamic dispatch on `this` — this handles methods
            // inherited from a runtime/extension base (e.g. EventEmitter).
            auto obj = lookupVariable("this");
            if (!obj) obj = builder_.createConstNull();
            lastValue_ = builder_.createCallMethod(obj, propAccess->name, args, HIRType::makeAny());
            return;
        }

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
                    // Class-expression binding: `var C = class { ... }`
                    // stores `__anon_class_N` under variableToClassName_["C"].
                    // The direct-name search above finds nothing (class is
                    // anonymous); consult the map. Only used as a fallback
                    // so real class declarations take the natural-name path.
                    // visitNewExpression already does this lookup at the
                    // construct site; Case 2 method-dispatch must do it too
                    // or we fall through to dynamic prototype lookup which
                    // can't find vtable methods on the FLAT instance.
                    if (className.empty()) {
                        auto vIt = variableToClassName_.find(newIdent->name);
                        if (vIt != variableToClassName_.end()) {
                            for (auto& cls : module_->classes) {
                                if (cls->name == vIt->second) {
                                    className = vIt->second;
                                    break;
                                }
                            }
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
                    // Check for static method (raw name first, then
                    // __getter_<name> for static accessors which are now
                    // routed via methodKey for runtime dispatch).
                    auto it = cls->staticMethods.find(propAccess->name);
                    if (it == cls->staticMethods.end()) {
                        it = cls->staticMethods.find("__getter_" + propAccess->name);
                    }
                    if (it != cls->staticMethods.end()) {
                        HIRFunction* method = it->second;
                        // Static getter: invoke the getter with the class as `this`,
                        // then call the returned value with the user's args. Direct-
                        // calling the getter with `args` produces an arity mismatch
                        // and an LLVM verifier failure. Save/restore call-this to
                        // avoid leaking the receiver into subsequent calls.
                        if (method && method->name.find("___getter_") != std::string::npos) {
                            auto classObj = lowerExpression(propAccess->expression.get());
                            auto boxedClass = boxValueIfNeeded(classObj);
                            auto savedThis = builder_.createCall("ts_get_call_this", {}, HIRType::makeAny());
                            builder_.createCall("ts_set_call_this", {boxedClass}, HIRType::makeVoid());
                            auto returnedFn = builder_.createCall(method->name, {},
                                method->returnType ? method->returnType : HIRType::makeAny());
                            builder_.createCall("ts_set_call_this", {savedThis}, HIRType::makeVoid());
                            std::vector<std::shared_ptr<HIRValue>> callArgs;
                            callArgs.push_back(boxValueIfNeeded(returnedFn));
                            for (auto& a : args) callArgs.push_back(boxValueIfNeeded(a));
                            std::string callFn = "ts_call_" + std::to_string(args.size());
                            lastValue_ = builder_.createCall(callFn, callArgs, HIRType::makeAny());
                            return;
                        }
                        // Static methods don't need 'this' parameter.
                        // Truncate or pad args to match the callee's arity:
                        // verifier rejects extra args, and missing args
                        // need explicit `undefined` so the receiver always
                        // sees the same shape.
                        std::vector<std::shared_ptr<HIRValue>> calleeArgs;
                        size_t expected = method->params.size();
                        if (method->hasRestParam && expected > 0) {
                            // Keep all user args; the rest-param lowering
                            // collects the trailing values into an array.
                            calleeArgs = args;
                        } else {
                            for (size_t i = 0; i < expected; ++i) {
                                if (i < args.size()) calleeArgs.push_back(args[i]);
                                else calleeArgs.push_back(builder_.createConstUndefined());
                            }
                        }
                        lastValue_ = builder_.createCall(method->name, calleeArgs, method->returnType);
                        return;
                    }
                    // Object.prototype methods are inherited by every class
                    // constructor via Function.prototype → Object.prototype.
                    // Calls like `C.hasOwnProperty(...)` should go through
                    // dynamic dispatch on the class object so the prototype
                    // chain resolves them at runtime — emitting the user-
                    // class-static convention (`C_static_hasOwnProperty`)
                    // would yield an undefined-symbol linker error.
                    static const std::set<std::string> objectProtoMethods = {
                        // Object.prototype methods inherited via Function.prototype
                        "hasOwnProperty", "isPrototypeOf", "propertyIsEnumerable",
                        "toString", "toLocaleString", "valueOf",
                        // Function.prototype methods on the class constructor
                        "bind", "call", "apply",
                    };
                    if (objectProtoMethods.count(propAccess->name)) {
                        auto obj = lowerExpression(propAccess->expression.get());
                        lastValue_ = builder_.createCallMethod(obj, propAccess->name, args, HIRType::makeAny());
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
                // Also check: if the identifier is a locally-declared function (not
                // imported via require), it shadows the extension module. Node.js modules
                // like `assert` are NOT globals — they must be imported via require().
                // A local `function assert(){}` should NOT be treated as the Node assert
                // module. Only check for function-typed locals to avoid shadowing
                // `var path = require('path')` which IS the module.
                // A function PARAMETER named like a Node module shadows the
                // builtin: it holds a user value, not the module. This is the
                // common QUnit/test pattern `function(assert){ assert.deepEqual
                // (...) }` where `assert` is the harness's own object — routing it
                // to the node assert builtin (which exit(1)s on failure) is wrong.
                // A real `var/const path = require('path')` alias is NEVER a
                // parameter, so this does not disturb module aliases. Walk every
                // enclosing function on the scope stack so a CAPTURED parameter
                // (e.g. `assert` used inside a nested `forEach` callback) is also
                // caught, not just a direct parameter of the current function.
                if (!isLocalVarShadow) {
                    for (auto& sc : scopes_) {
                        if (!sc.owningFunction) continue;
                        for (auto& p : sc.owningFunction->params) {
                            if (p.first == classNameIdent->name) { isLocalVarShadow = true; break; }
                        }
                        if (isLocalVarShadow) break;
                    }
                }
                if (!isLocalVarShadow) {
                    auto* varInfo = lookupVariableInfo(classNameIdent->name);
                    // Keep the existing function-typed-local shadow.
                    if (varInfo && varInfo->elemType &&
                        varInfo->elemType->kind == HIRTypeKind::Function) {
                        isLocalVarShadow = true;
                    }
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
        // GC verification-harness builtins (GC-001). Handled FIRST so the
        // analyzer's FunctionType registration can't divert them to a weak
        // undefined-returning stub. Drive/inspect the collector from compiled
        // TS so a single allocation + forced GC reproduces moving-GC corruption.
        if (ident->name == "__ts_gc_minor") {
            lastValue_ = builder_.createCall("ts_gc_minor_collect", {}, HIRType::makeVoid());
            return;
        }
        if (ident->name == "__ts_gc_major") {
            lastValue_ = builder_.createCall("ts_gc_force_collect", {}, HIRType::makeVoid());
            return;
        }
        if (ident->name == "__ts_gc_collection_count") {
            lastValue_ = builder_.createCall("ts_gc_dbg_collection_count", {}, HIRType::makeFloat64());
            return;
        }
        if (ident->name == "__ts_gc_live_size") {
            lastValue_ = builder_.createCall("ts_gc_dbg_live_size", {}, HIRType::makeFloat64());
            return;
        }
        if (ident->name == "__ts_gc_verify") {
            // Runs a verified minor GC; returns the number of invariant violations.
            lastValue_ = builder_.createCall("ts_gc_verify_now", {}, HIRType::makeFloat64());
            return;
        }
        if (ident->name == "__ts_gc_is_nursery") {
            if (args.empty()) { lastValue_ = builder_.createConstBool(false); return; }
            // Box the argument to a TsValue* so the runtime can unbox uniformly.
            auto arg = args[0];
            std::shared_ptr<HIRValue> boxed;
            if (arg->type) {
                switch (arg->type->kind) {
                    case HIRTypeKind::Int64:  boxed = builder_.createBoxInt(arg); break;
                    case HIRTypeKind::Float64: boxed = builder_.createBoxFloat(arg); break;
                    case HIRTypeKind::Bool:   boxed = builder_.createBoxBool(arg); break;
                    case HIRTypeKind::String: boxed = builder_.createBoxString(arg); break;
                    case HIRTypeKind::Any:    boxed = arg; break;
                    default:                  boxed = builder_.createBoxObject(arg); break;
                }
            } else {
                boxed = builder_.createBoxObject(arg);
            }
            lastValue_ = builder_.createCall("ts_gc_dbg_is_nursery", {boxed}, HIRType::makeBool());
            return;
        }
        if (ident->name == "__ts_gc_watch") {
            if (args.empty()) { lastValue_ = builder_.createConstBool(false); return; }
            auto arg = args[0];
            std::shared_ptr<HIRValue> boxed;
            if (arg->type) {
                switch (arg->type->kind) {
                    case HIRTypeKind::Int64:  boxed = builder_.createBoxInt(arg); break;
                    case HIRTypeKind::Float64: boxed = builder_.createBoxFloat(arg); break;
                    case HIRTypeKind::Bool:   boxed = builder_.createBoxBool(arg); break;
                    case HIRTypeKind::String: boxed = builder_.createBoxString(arg); break;
                    case HIRTypeKind::Any:    boxed = arg; break;
                    default:                  boxed = builder_.createBoxObject(arg); break;
                }
            } else {
                boxed = builder_.createBoxObject(arg);
            }
            lastValue_ = builder_.createCall("ts_gc_dbg_watch", {boxed}, HIRType::makeVoid());
            return;
        }
        if (ident->name == "__ts_gc_watch_alive") {
            lastValue_ = builder_.createCall("ts_gc_dbg_watch_alive", {}, HIRType::makeBool());
            return;
        }
        if (ident->name == "__ts_dbg_bits") {
            if (args.empty()) { lastValue_ = builder_.createConstBool(false); return; }
            auto arg = args[0];
            std::shared_ptr<HIRValue> boxed;
            if (arg->type) {
                switch (arg->type->kind) {
                    case HIRTypeKind::Int64:  boxed = builder_.createBoxInt(arg); break;
                    case HIRTypeKind::Float64: boxed = builder_.createBoxFloat(arg); break;
                    case HIRTypeKind::Bool:   boxed = builder_.createBoxBool(arg); break;
                    case HIRTypeKind::String: boxed = builder_.createBoxString(arg); break;
                    case HIRTypeKind::Any:    boxed = arg; break;
                    default:                  boxed = builder_.createBoxObject(arg); break;
                }
            } else {
                boxed = builder_.createBoxObject(arg);
            }
            lastValue_ = builder_.createCall("ts_dbg_bits", {boxed}, HIRType::makeVoid());
            return;
        }

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

        // Date(...) without `new`: per ECMA-262 21.4.2.1, returns the
        // current time as a string regardless of args. The args are
        // evaluated for side effects but discarded.
        if (ident->name == "Date") {
            // Evaluate args for side-effect, then call ts_date_now_string().
            // (createCall with the evaluated arg values is unnecessary —
            // they were already evaluated when args was built.)
            lastValue_ = builder_.createCall("ts_date_now_string", {}, HIRType::makeString());
            return;
        }

        // RegExp(pattern[, flags]) — same semantics as `new RegExp(...)` per
        // ECMA-262 §22.2.4.1 RegExp Constructor. Without this case, the call
        // fell through to user-function resolution and the Monomorphizer
        // generated an empty-body stub (RegExp_m<hash>_any → undefined),
        // which silently broke libraries like lodash that do
        // `var re = RegExp('...')` at module init.
        if (ident->name == "RegExp") {
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
            lastValue_ = builder_.createCall("ts_regexp_create",
                {patternArg, flagsArg}, HIRType::makeObject());
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
            if (ident->name != "Error") {
                auto nameStr = builder_.createConstString(ident->name);
                lastValue_ = builder_.createCall("ts_error_create_typed_js", {nameStr, message}, HIRType::makeAny());
            } else {
                lastValue_ = builder_.createCall("ts_error_create", {message}, HIRType::makeAny());
            }
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
            // Counter-form fallback: visitFunctionDeclaration (line ~2044)
            // appends `_<counter>` to nested function declarations, producing
            // names like `verifyProperty_3`. The call site computed the
            // type-mangled name (`verifyProperty_any_any_any`) which doesn't
            // match. If neither lookup found a target, scan for an exact
            // base-name match against names of the form `<name>_<digits>$`
            // and use that.
            //
            // EXCLUSION: skip this fallback when ident->name is a known
            // global builtin (isFinite, parseInt, etc.). Bundled JS modules
            // like lodash declare inner functions with these same names
            // inside their IIFE wrappers (`function isFinite(value)` inside
            // `runInContext`). The greedy scan would grab the inner function
            // by base-name + `_<digits>` pattern and shadow the global.
            // The known-globals branch below (lines ~7029-7062) handles the
            // correct lookup against the runtime registration.
            auto isBareGlobalIdent = [](const std::string& n) {
                return n == "isFinite" || n == "isNaN" ||
                       n == "parseInt" || n == "parseFloat" ||
                       n == "encodeURI" || n == "encodeURIComponent" ||
                       n == "decodeURI" || n == "decodeURIComponent" ||
                       n == "eval";
            };
            if (!targetFunc && !isBareGlobalIdent(ident->name)) {
                for (auto& f : module_->functions) {
                    const std::string& fn = f->name;
                    if (fn.size() <= ident->name.size() + 1) continue;
                    if (fn.compare(0, ident->name.size(), ident->name) != 0) continue;
                    if (fn[ident->name.size()] != '_') continue;
                    bool allDigits = true;
                    for (size_t i = ident->name.size() + 1; i < fn.size(); ++i) {
                        if (fn[i] < '0' || fn[i] > '9') { allDigits = false; break; }
                    }
                    if (allDigits) {
                        targetFunc = f.get();
                        callName = fn;
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
                ident->name == "eval" ||
                ident->name == "isProxy" ||
                ident->name == "assertThrowsInstanceOf" ||
                ident->name == "assertThrowsValue" ||
                ident->name == "raisesException" ||
                ident->name == "assertDeepEq" ||
                ident->name == "serialize" ||
                ident->name == "deserialize" ||
                ident->name == "testLenientAndStrict" ||
                ident->name == "createNewGlobal" ||
                ident->name == "getTimeZone" ||
                ident->name == "hasProp" ||
                ident->name == "disassemble" ||
                ident->name == "returns" ||
                ident->name == "assertThrowsInstanceOfWithMessage" ||
                ident->name == "assertThrowsInstanceOfWithMessageContains" ||
                ident->name == "completesNormally" ||
                ident->name == "Permutations" ||
                ident->name == "makeIterator" ||
                ident->name == "setTimeZone" ||
                ident->name == "setDefaultLocale" ||
                ident->name == "parseRaisesException" ||
                ident->name == "parsesSuccessfully" ||
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
            // Match args to declared params: pad short with undefined, truncate
            // long. The LLVM verifier rejects either mismatch on direct calls.
            if (args.size() < targetFunc->params.size()) {
                for (size_t i = args.size(); i < targetFunc->params.size(); ++i) {
                    args.push_back(builder_.createConstUndefined());
                }
            } else if (args.size() > targetFunc->params.size()) {
                args.resize(targetFunc->params.size());
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

        // Set ts_last_call_argc before direct calls so the 'arguments' object
        // (if the callee creates one) knows how many args were actually passed.
        {
            auto actualArgc = builder_.createConstInt(static_cast<int64_t>(node->arguments.size()));
            builder_.createCall("ts_set_last_call_argc", {actualArgc}, HIRType::makeVoid());
        }

        // Determine return type from target function if available
        auto returnType = (targetFunc && targetFunc->returnType) ? targetFunc->returnType : HIRType::makeAny();
        lastValue_ = builder_.createCall(callName, args, returnType);
        return;
    }

    // Computed method call: obj[key](args). The receiver MUST be bound as
    // `this` — the generic indirect call below would call obj[key] with no
    // receiver (e.g. `o['bump']()` ran with this=undefined → NaN; lodash
    // `getMapData(...)['delete'](key)` silently no-op'd). Mirror the dot-method
    // path but with a dynamic key: fetch obj[key], then invoke via
    // ts_call_with_this_N(func, obj, ...args). (Static obj.method() is handled
    // by the PropertyAccess block above.)
    if (auto* ea = dynamic_cast<ast::ElementAccessExpression*>(node->callee.get())) {
        if (args.size() <= 8) {
            auto obj = lowerExpression(ea->expression.get());
            auto boxedObj = boxValueIfNeeded(obj);
            auto keyVal = lowerExpression(ea->argumentExpression.get());
            auto func = builder_.createCall("ts_object_get_dynamic",
                {boxedObj, boxValueIfNeeded(keyVal)}, HIRType::makeAny());
            std::vector<std::shared_ptr<HIRValue>> callArgs = {func, boxedObj};
            for (auto& a : args) callArgs.push_back(boxValueIfNeeded(a));
            std::string callFn = "ts_call_with_this_" + std::to_string(args.size());
            lastValue_ = builder_.createCall(callFn, callArgs, HIRType::makeAny());
            return;
        }
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
                // Unknown type — route through ts_array_constructor which
                // does the JS-spec runtime dispatch: numeric arg → length,
                // else → single element. This matches `new Array(x)` spec
                // semantics in untyped JS mode where `x` might be either.
                auto argVal = lowerExpression(arg.get());
                lastValue_ = builder_.createCall("ts_array_constructor",
                                                  {argVal}, HIRType::makeAny());
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
        std::shared_ptr<HIRValue> argVal;
        bool argIsNonInt = false;  // arg is Any/Array/Object pointer, not a known number
        if (!node->arguments.empty()) {
            argVal = lowerExpression(node->arguments[0].get());
            if (argVal && argVal->type) {
                if (argVal->type->kind == HIRTypeKind::Float64) {
                    argVal = builder_.createCastF64ToI64(argVal);
                } else if (argVal->type->kind == HIRTypeKind::Any ||
                           argVal->type->kind == HIRTypeKind::Array ||
                           argVal->type->kind == HIRTypeKind::Object) {
                    argIsNonInt = true;
                }
            }
        } else {
            argVal = builder_.createConstInt(0);
        }
        // byteOffset (default 0) and byteLength (default -1 = "rest of buffer")
        // are honored only by the dispatcher when arg is an ArrayBuffer.
        std::shared_ptr<HIRValue> byteOffset = (node->arguments.size() > 1)
            ? lowerExpression(node->arguments[1].get())
            : builder_.createConstInt(0);
        std::shared_ptr<HIRValue> byteLength = (node->arguments.size() > 2)
            ? lowerExpression(node->arguments[2].get())
            : builder_.createConstInt(-1);
        auto arrType = HIRType::makeArray(HIRType::makeInt64(), true); // typed array
        const char* fn = nullptr;
        const char* wrapperFn = nullptr;
        if (className == "Uint8Array")             { fn = "ts_typed_array_create_u8";      wrapperFn = "ts_typed_array_new_u8"; }
        else if (className == "Uint32Array")       { fn = "ts_typed_array_create_u32";     wrapperFn = "ts_typed_array_new_u32"; }
        else if (className == "Float64Array")      { fn = "ts_typed_array_create_f64";     wrapperFn = "ts_typed_array_new_f64"; }
        else if (className == "Uint8ClampedArray") { fn = "ts_typed_array_create_clamped"; wrapperFn = "ts_typed_array_new_clamped"; }
        else if (className == "Int8Array")         { fn = "ts_typed_array_create_i8";      wrapperFn = "ts_typed_array_new_i8"; }
        else if (className == "Int16Array")        { fn = "ts_typed_array_create_i16";     wrapperFn = "ts_typed_array_new_i16"; }
        else if (className == "Uint16Array")       { fn = "ts_typed_array_create_u16";     wrapperFn = "ts_typed_array_new_u16"; }
        else if (className == "Int32Array")        { fn = "ts_typed_array_create_i32";     wrapperFn = "ts_typed_array_new_i32"; }
        else if (className == "Float32Array")      { fn = "ts_typed_array_create_f32";     wrapperFn = "ts_typed_array_new_f32"; }
        else if (className == "BigInt64Array")     { fn = "ts_typed_array_create_i64";     wrapperFn = nullptr; }
        else if (className == "BigUint64Array")    { fn = "ts_typed_array_create_u64";     wrapperFn = nullptr; }
        if (argIsNonInt && wrapperFn) {
            // Dispatcher: arg might be an ArrayBuffer (share buffer),
            // a TypedArray (copy), an Array (copy), or a number (length-only).
            lastValue_ = builder_.createCall(wrapperFn,
                {argVal, byteOffset, byteLength}, arrType);
        } else if (fn) {
            lastValue_ = builder_.createCall(fn, {argVal}, arrType);
        }
        return;
    }

    // Handle built-in Map class
    if (className == "Map") {
        if (node->arguments.empty()) {
            lastValue_ = builder_.createCall("ts_map_create_explicit", {}, HIRType::makeMap());
        } else {
            // new Map(iterable) — populate from [k,v] pairs per ECMA-262 24.1.1.1
            auto iter = lowerExpression(node->arguments[0].get());
            iter = boxValueIfNeeded(iter);
            lastValue_ = builder_.createCall("ts_map_create_from_iterable", {iter}, HIRType::makeMap());
        }
        return;
    }

    // Handle built-in Set class
    if (className == "Set") {
        if (node->arguments.empty()) {
            lastValue_ = builder_.createCall("ts_set_create", {}, HIRType::makeSet());
        } else {
            // new Set(iterable) — populate from the iterable per ECMA-262 24.2.1.1
            auto iter = lowerExpression(node->arguments[0].get());
            iter = boxValueIfNeeded(iter);
            lastValue_ = builder_.createCall("ts_set_create_from_iterable", {iter}, HIRType::makeSet());
        }
        return;
    }

    // Handle built-in WeakMap class (uses TsWeakMap with WMAP magic)
    if (className == "WeakMap") {
        lastValue_ = builder_.createCall("ts_weakmap_create", {}, HIRType::makeMap());
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
            // new Date(y, m [, d, h, mi, s, ms]) - ECMA-262 §21.4.2.1 step 3.
            // Missing d defaults to 1, others default to 0.
            std::vector<std::shared_ptr<HIRValue>> partsArgs;
            partsArgs.reserve(7);
            for (size_t i = 0; i < 7; ++i) {
                if (i < node->arguments.size()) {
                    partsArgs.push_back(lowerExpression(node->arguments[i].get()));
                } else {
                    partsArgs.push_back(builder_.createConstFloat(i == 2 ? 1.0 : 0.0));
                }
            }
            lastValue_ = builder_.createCall("ts_date_create_parts", partsArgs,
                                             HIRType::makeClass("Date", 0));
        }
        return;
    }

    // ArrayBuffer: `new ArrayBuffer(byteLength)` allocates a real
    // TsBuffer of the requested size. Without this dedicated path, the
    // generic ctor route allocated an empty TsMap and ignored the
    // length, leaving .byteLength undefined and downstream TypedArray
    // operations broken.
    if (className == "ArrayBuffer") {
        std::shared_ptr<HIRValue> length;
        if (!node->arguments.empty()) {
            length = lowerExpression(node->arguments[0].get());
        } else {
            length = builder_.createConstInt(0);
        }
        lastValue_ = builder_.createCall("ts_arraybuffer_create",
            {length}, HIRType::makeAny());
        return;
    }

    // DataView: `new DataView(buffer, byteOffset?, byteLength?)` wraps
    // an existing ArrayBuffer with a byte-typed view. If no buffer
    // argument is given, the runtime call throws TypeError.
    if (className == "DataView") {
        std::shared_ptr<HIRValue> buf = !node->arguments.empty()
            ? lowerExpression(node->arguments[0].get())
            : builder_.createConstNull();
        std::shared_ptr<HIRValue> byteOffset = (node->arguments.size() > 1)
            ? lowerExpression(node->arguments[1].get())
            : builder_.createConstInt(0);
        // -1 sentinel = "rest of buffer" in ts_dataview_create_full.
        std::shared_ptr<HIRValue> byteLength = (node->arguments.size() > 2)
            ? lowerExpression(node->arguments[2].get())
            : builder_.createConstInt(-1);
        lastValue_ = builder_.createCall("ts_dataview_create_full",
            {buf, byteOffset, byteLength}, HIRType::makeAny());
        return;
    }

    // AggregateError has signature (errors, message?), unlike the (message)
    // signature of all other built-in Error subclasses. Route to a dedicated
    // runtime that builds the .errors array.
    if (className == "AggregateError") {
        std::shared_ptr<HIRValue> errors;
        std::shared_ptr<HIRValue> message;
        if (!node->arguments.empty()) {
            errors = lowerExpression(node->arguments[0].get());
        } else {
            errors = builder_.createConstString("");  // will be ignored runtime-side
        }
        if (node->arguments.size() >= 2) {
            message = lowerExpression(node->arguments[1].get());
        } else {
            message = builder_.createConstString("");
        }
        lastValue_ = builder_.createCall("ts_error_create_aggregate",
            {errors, message}, HIRType::makeAny());
        return;
    }

    // Handle built-in Error classes
    if (className == "Error" || className == "TypeError" || className == "RangeError" ||
        className == "ReferenceError" || className == "SyntaxError" || className == "URIError" ||
        className == "EvalError") {
        // new Error(message) or new Error(message, { cause: ... })
        std::shared_ptr<HIRValue> message;
        if (!node->arguments.empty()) {
            message = lowerExpression(node->arguments[0].get());
        } else {
            // Create empty string
            message = builder_.createConstString("");
        }

        // Call ts_error_create or ts_error_create_typed_js (returns already-boxed TsValue*)
        if (node->arguments.size() >= 2) {
            // ES2022: Error with options { cause: ... }
            auto options = lowerExpression(node->arguments[1].get());
            lastValue_ = builder_.createCall("ts_error_create_with_options", {message, options}, HIRType::makeAny());
        } else if (className != "Error") {
            // Typed error (TypeError, RangeError, etc.) — set correct .name and .constructor
            auto nameStr = builder_.createConstString(className);
            lastValue_ = builder_.createCall("ts_error_create_typed_js", {nameStr, message}, HIRType::makeAny());
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

    // Check if this is an extension type with a constructor (e.g., URL, URLSearchParams).
    // SKIP the extension lookup if the user has defined a function declaration with
    // the same name — that user function shadows the extension. Lodash's
    // `runInContext` declares `function Hash() {...}` which would otherwise resolve
    // to the crypto.Hash extension and throw "Illegal constructor".
    //
    // We deliberately do NOT shadow on a generic variable binding: patterns like
    // `var EventEmitter = events.EventEmitter; new EventEmitter()` need to resolve
    // to the extension's constructor, not to the variable's runtime value.
    bool userShadowsExtension = false;
    if (ident) {
        for (const auto& f : module_->functions) {
            if (f->name == ident->name || f->displayName == ident->name) {
                userShadowsExtension = true;
                break;
            }
        }
        if (!userShadowsExtension && specializations_) {
            for (const auto& spec : *specializations_) {
                if (spec.originalName == ident->name) {
                    userShadowsExtension = true;
                    break;
                }
            }
        }
    }
    if (!hirClass && !userShadowsExtension) {
        auto& extReg = ext::ExtensionRegistry::instance();
        const ext::TypeDefinition* extType = extReg.findType(className);
        if (extType) {
            if (extType->constructor && !extType->constructor->call.empty()) {
                // Extension type with a constructor - call the factory function directly
                std::string hirName = extType->constructor->hirName
                    ? *extType->constructor->hirName
                    : extType->constructor->call;

                // The constructor is a static factory function that returns the
                // object. Type the result as the extension CLASS (not an untyped
                // ptr) so downstream member access / indexing dispatches against
                // the right runtime shape — e.g. `new Buffer(..)[i]` must lower
                // to ts_buffer_read_uint8, not ts_array_get_unchecked (which
                // reads a TsBuffer as a TsArray and crashes). Mirrors how the
                // static factory `Buffer.from(..)` is typed (extTypeRefToHIR).
                lastValue_ = builder_.createCall(hirName, args, HIRType::makeClass(className, 0));
                return;
            }
            // Phase 9i Bug 3: extension type exists but its contract has no
            // `constructor` block. Two interpretations:
            //   - Node.js intends this class to be internal-only and its
            //     runtime constructor body throws TypeError (the majority case
            //     for crypto.Hash, http.IncomingMessage, all zlib.*, etc.)
            //   - We forgot to wire a real C runtime constructor into the
            //     schema (the minority case for legitimately new-able classes
            //     like net.Socket).
            // Both cases must throw a TypeError at the `new` site, because
            // there is no way to materialize the correct underlying C++ object
            // without a runtime constructor function. The previous behavior
            // (silent fall-through to ts_map_create) produced a TsMap with the
            // wrong shape, leading to memory corruption when the receiver was
            // later passed to a method that dereferenced it via vtable offset.
            SPDLOG_WARN("visitNewExpression: extension type '{}' has no constructor "
                        "in its contract. Emitting TypeError throw at runtime "
                        "(matches Node.js behavior for internal-only classes). "
                        "If this class SHOULD be publicly constructable, add a "
                        "`constructor` block to its .ext.json pointing at the "
                        "C runtime factory function.", className);
            auto nameStr = builder_.createConstString("TypeError");
            auto msgStr = builder_.createConstString(
                "Illegal constructor: " + className + " cannot be constructed directly");
            auto err = builder_.createCall(
                "ts_error_create_typed_js", {nameStr, msgStr}, HIRType::makeAny());
            builder_.createThrow(err);
            // Sentinel result for downstream visitor protocol (Throw is
            // unreachable but the builder still expects lastValue_ to be set).
            lastValue_ = builder_.createConstUndefined();
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
    } else if (!hirClass && (ident
                              || dynamic_cast<ast::PropertyAccessExpression*>(node->expression.get())
                              || dynamic_cast<ast::ElementAccessExpression*>(node->expression.get())
                              || dynamic_cast<ast::BinaryExpression*>(node->expression.get())
                              || dynamic_cast<ast::ParenthesizedExpression*>(node->expression.get()))) {
        // Unknown class — treat as a constructor function call. Examples:
        //   - `new Foo()` where Foo is `function Foo() {...}` from imported JS
        //   - `new Array.prototype.concat([])` (property-access into a built-in
        //     prototype method, which is a non-constructor and must throw via
        //     the runtime `is_constructor` check).
        //   - `new holder[key]()` (computed-member receiver, e.g. lodash's
        //     `new mapCaches[kind]()` cache-interface tests) — the constructor
        //     is resolved dynamically by ElementAccess and dispatched through
        //     ts_new_from_constructor_N so `this` is bound and the prototype is
        //     linked. Without this, it fell to the plain-dynamic-object
        //     fallback: the ctor never ran and methods saw the wrong `this`.
        //   - `new (memoize.Cache || MapCache)()` (computed-receiver pattern
        //     used by lodash; receiver is a logical-or BinaryExpression wrapped
        //     in parens, not an Identifier/PropertyAccess).
        // ts_new_from_constructor_N performs the [[Construct]] dispatch and
        // throws TypeError if the target's is_constructor flag is false.
        if (ident) {
            SPDLOG_DEBUG("visitNewExpression: receiver '{}' has no registered HIRClass — "
                         "lowering to ts_new_from_constructor_N.",
                         ident->name);
        }
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
        // Build constructor call args: [this, ...args]. Truncate or pad to
        // match the constructor's declared arity — verifier rejects extras
        // and missing args are undefined.
        HIRFunction* ctor = hirClass->constructor;
        size_t expectedUserArgs = ctor->params.empty() ? 0 : ctor->params.size() - 1;
        std::vector<std::shared_ptr<HIRValue>> ctorArgs;
        ctorArgs.push_back(newObj);  // 'this' is the new object
        if (ctor->hasRestParam) {
            for (auto& arg : args) ctorArgs.push_back(arg);
        } else {
            for (size_t i = 0; i < expectedUserArgs; ++i) {
                if (i < args.size()) ctorArgs.push_back(args[i]);
                else ctorArgs.push_back(builder_.createConstUndefined());
            }
        }

        // ECMA-262 §10.1.1: new C() sets instance.[[Prototype]] = C.prototype.
        // For TsMap-backed instances (classes without a registered shape, or
        // shape but no fields), the prototype slot lives on the TsMap and
        // must be filled here. Flat-object instances no-op on this call (the
        // prototype is derived from ShapeDescriptor.constructorSlot).
        {
            auto ctorVal = builder_.createLoadFunction(ctor->name);
            auto protoKey = builder_.createConstString("prototype");
            auto protoVal = builder_.createCall(
                "ts_object_get_dynamic", {ctorVal, protoKey}, HIRType::makeAny());
            builder_.createCall(
                "ts_object_setPrototypeOf", {newObj, protoVal}, HIRType::makeVoid());
        }

        // Call the constructor
        builder_.createCall(ctor->name, ctorArgs, HIRType::makeVoid());
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
        // With spread elements, use ts_array_create and dynamic push/concat.
        // Inside a generator/async function, sub-expressions can yield —
        // splitting the body across resume-blocks where the SSA value of
        // `arr` no longer dominates the next concat/push site. Spill the
        // accumulator to an alloca so reloads work across resume boundaries.
        bool inGenerator = currentFunction_ && (currentFunction_->isGenerator || currentFunction_->isAsync);
        auto arrType = HIRType::makeArray(elemType, false);
        auto initial = builder_.createCall("ts_array_create", {}, arrType);
        std::shared_ptr<HIRValue> arrSlot;
        std::shared_ptr<HIRValue> arr = initial;
        if (inGenerator) {
            arrSlot = builder_.createAlloca(arrType, "arrlit.acc");
            builder_.createStore(initial, arrSlot);
        }
        auto reload = [&]() {
            return inGenerator ? builder_.createLoad(arrType, arrSlot) : arr;
        };
        auto store = [&](std::shared_ptr<HIRValue> v) {
            if (inGenerator) builder_.createStore(v, arrSlot);
            arr = v;
        };

        for (auto& elem : node->elements) {
            if (auto* spread = dynamic_cast<ast::SpreadElement*>(elem.get())) {
                // Spread element in array literal: per ECMA-262 13.2.4.1
                // SpreadElement evaluation uses the iterator protocol
                // (@@iterator + next()), NOT Array.prototype.concat's
                // IsConcatSpreadable. ts_array_spread_into handles both
                // TsArray fast-path and generic iterables (generators, etc.).
                auto spreadArr = lowerExpression(spread->expression.get());
                auto concat = builder_.createCall("ts_array_spread_into", {reload(), spreadArr}, arrType);
                store(concat);
            } else {
                // Regular element: push it.
                auto elemVal = lowerExpression(elem.get());
                builder_.createCall("ts_array_push", {reload(), elemVal}, HIRType::makeInt64());
            }
        }

        lastValue_ = reload();
    } else {
        // No spread elements - use efficient pre-allocated array.
        // createNewArrayBoxed lowers to ts_array_create_sized which fills
        // slots with NANBOX_HOLE. Regular elements overwrite those slots;
        // elided positions (OmittedExpression, i.e. `[, 1, 2]`) stay as
        // holes per ECMA-262 §13.2.4 ArrayLiteral Elision semantics.
        auto lenVal = builder_.createConstInt(static_cast<int64_t>(node->elements.size()));
        auto arr = builder_.createNewArrayBoxed(lenVal, elemType);

        int64_t idx = 0;
        for (auto& elem : node->elements) {
            if (dynamic_cast<ast::OmittedExpression*>(elem.get())) {
                idx++;  // leave NANBOX_HOLE sentinel in place
                continue;
            }
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

    // Strategy B Phase 4a: extend the shape lookup to non-`this` typed
    // receivers. If the receiver expression has a known class type, find
    // the matching HIRClass and look up the property type from its shape.
    // Mirrors the getter-resolution loop just below at lines ~6277-6291.
    //
    // Without this, GetPropStatic emits with propType=Any, the LLVM unbox
    // doesn't fire, and downstream typed operations on property-access
    // results lose precision (Phase 0b probe regression). This is the
    // single change that unblocks Phase 0b, 0c, and 3c.
    if (propType->kind == HIRTypeKind::Any &&
        node->expression && node->expression->inferredType &&
        node->expression->inferredType->kind == ts::TypeKind::Class) {
        auto classType = std::dynamic_pointer_cast<ts::ClassType>(node->expression->inferredType);
        if (classType) {
            for (auto& cls : module_->classes) {
                if (cls->name == classType->name && cls->shape) {
                    auto typeIt = cls->shape->propertyTypes.find(node->name);
                    if (typeIt != cls->shape->propertyTypes.end() && typeIt->second) {
                        propType = typeIt->second;
                    }
                    break;
                }
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
        // Skip nullptr placeholders inserted by the JS pre-scan at line ~771;
        // those get a real HIRFunction* later when the body is lowered.
        // Reading getterFunc->returnType on a nullptr crashes during
        // class-body expression processing (e.g. private-getter access in
        // a derived constructor before super() — visitClassDeclaration
        // lowers inner expressions before method bodies finish registering).
        if (getterIt != targetClass->methods.end() && getterIt->second) {
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

    // Phase 9c-iv-B: refine propType from `node->inferredType` for property
    // access where the analyzer knows a more precise type than codegen has
    // locally derived. This is needed for:
    //   - `stream.Readable` (namespace.Class lookup) where the analyzer knows
    //     the result is the Readable class but the propType derivation only
    //     walks module_->classes (which doesn't contain extension classes).
    //   - `extInstance.field` where the receiver is an extension class.
    //
    // Excluded:
    //   - `this.X` accesses (currentClass_ is set): the class may not yet be
    //     in module_->classes during method body lowering, and the existing
    //     shape-lookup path handles `this.X` correctly.
    //   - User-defined class instance accesses (receiver is a Class in
    //     module_->classes): SROA + the existing shape-lookup path handle
    //     these. Refining here would trip SROA expecting typed inline reads
    //     of slots the constructor lowering hasn't yet filled.
    //   - Object/structural-literal receivers: monomorphizer may reuse a
    //     typed specialization for an Any-typed call site, and the structural
    //     type may not match the actual runtime object's shape.
    bool isThisAccess = false;
    if (auto* thisIdent = dynamic_cast<ast::Identifier*>(node->expression.get())) {
        if (thisIdent->name == "this") isThisAccess = true;
    }
    bool receiverIsUserDefinedClass = false;
    if (node->expression && node->expression->inferredType &&
        node->expression->inferredType->kind == ts::TypeKind::Class) {
        auto receiverClass = std::dynamic_pointer_cast<ts::ClassType>(node->expression->inferredType);
        if (receiverClass) {
            for (auto& cls : module_->classes) {
                if (cls->name == receiverClass->name) {
                    receiverIsUserDefinedClass = true;
                    break;
                }
            }
        }
    }
    bool receiverIsObjectLiteral = false;
    if (node->expression && node->expression->inferredType &&
        node->expression->inferredType->kind == ts::TypeKind::Object) {
        receiverIsObjectLiteral = true;
    }
    if (propType->kind == HIRTypeKind::Any &&
        !isThisAccess &&
        !currentClass_ &&
        !receiverIsUserDefinedClass &&
        node->inferredType) {
        auto refined = convertType(node->inferredType);
        if (refined && refined->kind != HIRTypeKind::Any) {
            // For object-literal receivers, only refine to Class/Array/etc.
            // (extension types). NEVER refine to primitive types like f64,
            // because the monomorphizer may reuse a typed specialization for
            // an Any-typed call site, and the structural type may not match
            // the actual runtime object's shape — leading to typed loads of
            // fields that don't exist (NaN out, see Phase 9c-iv-A history).
            bool refinementIsPrimitive =
                refined->kind == HIRTypeKind::Int64 ||
                refined->kind == HIRTypeKind::Float64 ||
                refined->kind == HIRTypeKind::Bool ||
                refined->kind == HIRTypeKind::String;
            if (!receiverIsObjectLiteral || !refinementIsPrimitive) {
                propType = refined;
            }
        }
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
            // Computed property names (e.g. { [expr]: val }) use dynamic keys
            // and can't go into a flat shape — fall back to TsMap.
            if (pa->name.empty() || dynamic_cast<ast::ComputedPropertyName*>(pa->nameNode.get())) {
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

    // Inside a generator/async function, sub-expressions of properties can
    // yield. The SSA value of `obj` won't dominate later uses across resume
    // boundaries, so spill to an alloca and reload before each property
    // operation. Same pattern as the array-literal-with-spread fix above.
    bool inGenerator = currentFunction_ && (currentFunction_->isGenerator || currentFunction_->isAsync);
    bool hasYieldableProp = false;
    if (inGenerator) {
        for (auto& prop : node->properties) {
            if (dynamic_cast<ast::SpreadElement*>(prop.get()) ||
                dynamic_cast<ast::PropertyAssignment*>(prop.get()) ||
                dynamic_cast<ast::ComputedPropertyName*>(prop.get())) {
                hasYieldableProp = true;
                break;
            }
        }
    }
    std::shared_ptr<HIRValue> objSlot;
    if (inGenerator && hasYieldableProp) {
        objSlot = builder_.createAlloca(HIRType::makeAny(), "objlit.acc");
        builder_.createStore(obj, objSlot);
    }
    auto reloadObj = [&]() {
        return objSlot ? builder_.createLoad(HIRType::makeAny(), objSlot) : obj;
    };

    for (auto& prop : node->properties) {
        // Handle spread element: {...other}
        if (auto* spread = dynamic_cast<ast::SpreadElement*>(prop.get())) {
            auto spreadObj = lowerExpression(spread->expression.get());
            // Use ts_object_assign to copy properties from spreadObj to obj
            builder_.createCall("ts_object_assign", {reloadObj(), spreadObj}, HIRType::makeAny());
            continue;
        }

        // Handle MethodDefinition (including getters/setters) specially
        if (auto* method = dynamic_cast<ast::MethodDefinition*>(prop.get())) {
            // Create a function for the method
            auto funcValue = lowerMethodDefinitionToFunction(method);

            // Check for computed property name: { [expr]() {} }
            if (auto* computed = dynamic_cast<ast::ComputedPropertyName*>(method->nameNode.get())) {
                if (computed->expression && funcValue) {
                    auto keyVal = lowerExpression(computed->expression.get());
                    // For computed getters/setters we'd need __getter_<dynamic>
                    // which isn't supported. Fall back to a plain dynamic set —
                    // the getter/setter semantics won't fire but the property
                    // will at least exist on the object, preventing crashes.
                    builder_.createSetPropDynamic(reloadObj(), keyVal, funcValue);
                }
            } else {
                // Determine the property key from Identifier or name string
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
                    builder_.createSetPropStatic(reloadObj(), keyName, funcValue);
                }
            }
        } else {
            // Save the object before visiting property (which may overwrite lastValue_)
            lastValue_ = reloadObj();
            prop->accept(this);
        }
    }

    // Ensure lastValue_ is the object after all properties are set
    lastValue_ = reloadObj();
}

void ASTToHIR::visitPropertyAssignment(ast::PropertyAssignment* node) {
    setSourceLine(node);
    // Save the object before lowerExpression overwrites lastValue_
    auto obj = lastValue_;

    // Inferred name (ECMA-262 PropertyDefinitionEvaluation / NamedEvaluation):
    // { m: function(){} }, { p: () => {} }, { c: class {} } give the value the
    // property key as its .name. Only for a plain (non-computed) key and an
    // anonymous function/arrow/class initializer.
    bool clearPending = false;
    if (!node->name.empty() &&
        !dynamic_cast<ast::ComputedPropertyName*>(node->nameNode.get())) {
        bool anon = dynamic_cast<ast::ArrowFunction*>(node->initializer.get()) ||
                    dynamic_cast<ast::ClassExpression*>(node->initializer.get());
        if (!anon) {
            if (auto* fe = dynamic_cast<ast::FunctionExpression*>(node->initializer.get()))
                anon = fe->name.empty();
        }
        if (anon) { pendingClosureDisplayName_ = node->name; clearPending = true; }
    }

    auto val = lowerExpression(node->initializer.get());
    if (clearPending) pendingClosureDisplayName_.clear();

    // Check for computed property name: { [expr]: value }
    if (auto* computed = dynamic_cast<ast::ComputedPropertyName*>(node->nameNode.get())) {
        if (computed->expression && obj) {
            auto keyVal = lowerExpression(computed->expression.get());
            builder_.createSetPropDynamic(obj, keyVal, val);
        }
    } else {
        // PropertyAssignment has name (string) directly. The empty string is a
        // valid property key ({ '': v }); computed/spread keys are handled in
        // the branch above, so a non-computed PropertyAssignment with an empty
        // name is an intentional '' key — emit it (previously dropped).
        std::string propName = node->name;
        if (obj) {
            builder_.createSetPropStatic(obj, propName, val);
        }
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
            // TypedArray constructors and the %TypedArray% intrinsic
            "TypedArray",
            "Int8Array", "Uint8Array", "Uint8ClampedArray",
            "Int16Array", "Uint16Array",
            "Int32Array", "Uint32Array",
            "Float32Array", "Float64Array",
            "BigInt64Array", "BigUint64Array",
            // Buffer-backed + BigInt + generator-family constructor stubs.
            "ArrayBuffer", "DataView", "SharedArrayBuffer", "BigInt",
            "GeneratorFunction", "AsyncFunction", "AsyncGeneratorFunction",
            // Intl (ECMA-402) namespace
            "Intl",
        };
        if (jsBuiltinGlobals.count(node->name)) {
            // A local `function NAME(...) {...}` declaration MUST shadow the
            // built-in global with the same name. Lodash relies on this:
            // `function isNaN(value) {...}` inside its IIFE defines a strict
            // isNaN, and `lodash.isNaN = isNaN` should bind to that local
            // function — not the global ECMAScript isNaN. Without this
            // check `_.isNaN("foo")` returns `true` (global's coercion-
            // based answer) instead of lodash's strict `false`.
            //
            // We only honor FUNCTION-DECLARATION shadows (elemType.kind ==
            // Function), not var/let/const shadows. The lodash bundle also
            // has `var Object = context.Object, Array = context.Array, ...`
            // which hoist to undefined before their assignments execute;
            // honoring those shadows would resolve early `Object` references
            // to undefined and break the bundle.
            auto* localInfo = lookupVariableInfo(node->name);
            bool localIsFunction = localInfo && localInfo->elemType &&
                localInfo->elemType->kind == HIRTypeKind::Function;
            if (!localIsFunction) {
                SPDLOG_DEBUG("[IDENT] builtin global: {} in func={}", node->name, currentFunction_ ? currentFunction_->name : "null");
                lastValue_ = builder_.createLoadGlobal(node->name);
                return;
            }
            SPDLOG_DEBUG("[IDENT] local fn shadows builtin: {} in func={}", node->name, currentFunction_ ? currentFunction_->name : "null");
            // Fall through — local function declaration shadows the built-in.
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
            if (foundLocal) {
                // If an inner function references this variable (populated during
                // the hoisted function declaration pass), we must use LoadGlobal
                // instead of the local so we see mutations from closures
                // (e.g., let count = 0; function inc() { count++; }; inc(); console.log(count)).
                // Only applies to __module_init_* functions where variables are true
                // module-level globals, not to user_main or other user functions.
                if (!isModuleGlobalUsedByInner(node->name) ||
                    !currentFunction_ ||
                    currentFunction_->name.find("__module_init_") != 0) return;
                // Fall through to LoadGlobal path below
            }
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
                        } else if (!it->isFunctionBoundary) {
                            // Block-scoped (e.g. `for (let i ...)`, `if (let x ...)`)
                            // even inside __synthetic_user_main / __module_init_.
                            // Not a true module global — must use closure-capture
                            // mechanism so per-iteration semantics work.
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

    // Class binding lookup. Two paths reach here:
    //
    // 1. `class E { ... }` (declaration): visitClassDeclaration registers
    //    the class in module_->classes under its name `E`. visitIdentifier
    //    has nothing to load because the constructor function is named
    //    `E_constructor`, not `E`. Find the class by name and load its
    //    constructor.
    //
    // 2. `let B = class { ... }` (expression assigned to a binding):
    //    visitClassExpression registers the class as `__anon_class_N` and
    //    populates `variableToClassName_["B"] = "__anon_class_N"`. The
    //    let-decl statement lives in `module->ast->body` after the
    //    Monomorphizer's keep-class-expr-decls pass, but no later pass
    //    iterates that body to emit the binding store. Resolve `B` via
    //    the map so the binding is virtually present even without an
    //    actual store.
    auto resolveClassByName = [&](const std::string& className) -> bool {
        for (const auto& cls : module_->classes) {
            if (cls->name != className) continue;
            std::string ctorName = cls->constructor
                ? cls->constructor->name
                : cls->name + "_constructor";
            bool hasFn = false;
            for (const auto& f : module_->functions) {
                if (f->name == ctorName) { hasFn = true; break; }
            }
            if (!hasFn && specializations_) {
                for (const auto& spec : *specializations_) {
                    if (spec.specializedName == ctorName) { hasFn = true; break; }
                }
            }
            if (hasFn) {
                lastValue_ = builder_.createLoadFunction(ctorName);
                return true;
            }
            break;
        }
        return false;
    };
    if (resolveClassByName(node->name)) return;
    auto vtcIt = variableToClassName_.find(node->name);
    if (vtcIt != variableToClassName_.end()) {
        if (resolveClassByName(vtcIt->second)) return;
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
    // Strip the 'n' suffix. Input is one of: "123", "0x1F", "0o17", "0b11",
    // each followed by 'n' in the AST token.
    std::string valueStr = node->value;
    if (!valueStr.empty() && valueStr.back() == 'n') {
        valueStr.pop_back();
    }
    // Detect prefix and select the right radix for ts_bigint_create_str.
    // Without this, `0xFEDCBA9876543210n` parses as base 10 and silently
    // clamps / returns 0.
    int radixInt = 10;
    if (valueStr.size() >= 2 && valueStr[0] == '0') {
        char p = valueStr[1];
        if (p == 'x' || p == 'X')      { radixInt = 16; valueStr = valueStr.substr(2); }
        else if (p == 'o' || p == 'O') { radixInt = 8;  valueStr = valueStr.substr(2); }
        else if (p == 'b' || p == 'B') { radixInt = 2;  valueStr = valueStr.substr(2); }
    }

    // Create the string constant for the BigInt value
    auto strVal = builder_.createConstString(valueStr);

    // Call ts_bigint_create_str. Emit with BigInt type so that downstream
    // binary-op lowering can detect BigInt operands via HIRValue::type,
    // enabling `var a = 1n; var b = 2n; a + b` to pick the BigInt add path.
    auto radix = builder_.createConstInt(radixInt);
    lastValue_ = builder_.createCall("ts_bigint_create_str", {strVal, radix}, HIRType::makeBigInt());
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
        ast::Node* defaultInitializer = nullptr;
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
            arrowDestructuredParams.push_back({func->params.size(), objPat, nullptr,
                param->initializer.get()});
        } else if (auto* arrPat = dynamic_cast<ast::ArrayBindingPattern*>(param->name.get())) {
            paramName = "param" + std::to_string(func->params.size());
            paramType = HIRType::makeAny();
            arrowDestructuredParams.push_back({func->params.size(), nullptr, arrPat,
                param->initializer.get()});
        } else {
            paramName = "param" + std::to_string(func->params.size());
        }

        // Track first non-simple param for ECMA-262 §10.2.5 fn.length.
        if (func->firstNonSimpleParamIndex == SIZE_MAX) {
            bool isDestructured =
                dynamic_cast<ast::ObjectBindingPattern*>(param->name.get()) ||
                dynamic_cast<ast::ArrayBindingPattern*>(param->name.get());
            if (param->initializer || param->isRest || isDestructured) {
                func->firstNonSimpleParamIndex = i;
            }
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

    // Register parameters in the scope (with default value handling).
    // Strategy B Phase 6c: per-parameter logic factored into bindOneParameter.
    // The slot-0 __closure__ has no AST parameter; user params start at index 1.
    for (size_t i = 0; i < func->params.size(); ++i) {
        size_t astParamIdx = (i >= 1) ? (i - 1) : SIZE_MAX;
        ast::Parameter* astParam = (astParamIdx < node->parameters.size())
            ? node->parameters[astParamIdx].get() : nullptr;
        bindOneParameter(func.get(), i, astParam, /*useAlloca=*/true);
    }

    // Emit destructuring extraction for parameters with binding patterns.
    // Strategy B Phase 6c: per-parameter logic factored into extractDestructuringForParam.
    for (auto& dp : arrowDestructuredParams) {
        extractDestructuringForParam(func.get(), dp.paramIndex,
            dp.objPattern, dp.arrPattern, dp.defaultInitializer);
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
        // so subsequent reads/writes in the outer function also use the cell.
        // When MULTIPLE function expressions / arrows capture the same outer
        // var (e.g., lodash's `upperFirst` referenced from camelCase, kebabCase,
        // snakeCase, startCase, upperCase callbacks), record each additional
        // closure in info->additionalCaptures so broadcastCaptureWrite reaches
        // every one when the var is later assigned.
        int captureIdx = 0;
        for (const auto& cap : innerCaptures) {
            const std::string& capName = cap.first;
            size_t scopeIndex = 0;
            if (!isCapturedVariable(capName, &scopeIndex)) {
                // Variable is in this function's scope, mark it as captured
                auto* info = lookupVariableInfo(capName);
                if (info) {
                    auto closureAlloca = builder_.createAlloca(HIRType::makeAny(), capName + "$closure");
                    builder_.createStore(lastValue_, closureAlloca);
                    if (!info->isCapturedByNested) {
                        info->isCapturedByNested = true;
                        info->closurePtr = closureAlloca;
                        info->captureIndex = captureIdx;
                    } else {
                        info->additionalCaptures.emplace_back(closureAlloca, captureIdx);
                    }
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
    size_t feParamIdx = 0;
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

        // Track first non-simple param for ECMA-262 §10.2.5 fn.length.
        if (func->firstNonSimpleParamIndex == SIZE_MAX) {
            bool isDestructured =
                dynamic_cast<ast::ObjectBindingPattern*>(param->name.get()) ||
                dynamic_cast<ast::ArrayBindingPattern*>(param->name.get());
            if (param->initializer || param->isRest || isDestructured) {
                func->firstNonSimpleParamIndex = feParamIdx;
            }
        }
        feParamIdx++;

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
            while (func->params.size() < 10) {
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

    // Register parameters in the scope (with default value handling).
    // Strategy B Phase 6b: per-parameter logic factored into bindOneParameter.
    // The slot-0 __closure__ has no AST parameter; user params start at index 1.
    for (size_t i = 0; i < func->params.size(); ++i) {
        size_t astParamIdx = (i >= 1) ? (i - 1) : SIZE_MAX;
        ast::Parameter* astParam = (astParamIdx < node->parameters.size())
            ? node->parameters[astParamIdx].get() : nullptr;
        bindOneParameter(func.get(), i, astParam, /*useAlloca=*/true);
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
    // Pre-declare ALL `var` names in the function body — including those
    // nested in if/for/while/try blocks. ECMA-262 § 14.3.2: `var`
    // declarations hoist to the enclosing function scope, not the block
    // they appear in. Without recursive walking, code like
    //
    //   if (cond) { var x = 5; }
    //   ... x ...
    //
    // resolves the outer `x` to the surrounding scope (potentially a
    // wrong outer var or function) instead of the function-scoped local.
    // Lodash's createFind closure depends on this for `var iteratee`
    // declared inside an `if` branch.
    {
        std::vector<std::string> hoistedVars;
        for (auto& stmt : node->body) {
            collectHoistedVarNames(stmt.get(), hoistedVars);
        }
        for (auto& name : hoistedVars) {
            if (lookupVariableInfoInCurrentFunction(name)) continue;
            auto allocaVal = builder_.createAlloca(HIRType::makeAny(), name);
            builder_.createStore(builder_.createConstUndefined(), allocaVal, HIRType::makeAny());
            defineVariableAlloca(name, allocaVal, HIRType::makeAny());
        }
    }

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
        // so subsequent reads/writes in the outer function also use the cell.
        // See visitArrowFunction for the multi-capturer rationale.
        int captureIdx = 0;
        for (const auto& cap : innerCaptures) {
            const std::string& capName = cap.first;
            size_t scopeIndex = 0;
            if (!isCapturedVariable(capName, &scopeIndex)) {
                // Variable is in this function's scope, mark it as captured
                auto* info = lookupVariableInfo(capName);
                if (info) {
                    auto closureAlloca = builder_.createAlloca(HIRType::makeAny(), capName + "$closure");
                    builder_.createStore(lastValue_, closureAlloca);
                    if (!info->isCapturedByNested) {
                        info->isCapturedByNested = true;
                        info->closurePtr = closureAlloca;
                        info->captureIndex = captureIdx;
                    } else {
                        info->additionalCaptures.emplace_back(closureAlloca, captureIdx);
                    }
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
    // Method .name = the property key (ECMA-262 SetFunctionName); accessors are
    // prefixed "get "/"set ". Without this an object/class method's funcName is
    // the mangled "__method_x_N" and .name leaks the mangled form.
    if (!methodName.empty()) {
        func->displayName = node->isGetter ? ("get " + methodName)
                          : node->isSetter ? ("set " + methodName)
                          : methodName;
    }

    // Add implicit 'this' parameter for methods
    func->params.push_back({"this", HIRType::makeAny()});

    // Handle explicit parameters
    // For getters/setters, force params to be Any (TsValue*) since they will be called
    // through the runtime's dynamic dispatch which passes TsValue* arguments
    bool forceAnyParams = node->isGetter || node->isSetter;
    size_t mdParamIdx = 0;
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

        // Track first non-simple param for ECMA-262 §10.2.5 fn.length.
        if (func->firstNonSimpleParamIndex == SIZE_MAX) {
            bool isDestructured =
                dynamic_cast<ast::ObjectBindingPattern*>(param->name.get()) ||
                dynamic_cast<ast::ArrayBindingPattern*>(param->name.get());
            if (param->initializer || param->isRest || isDestructured) {
                func->firstNonSimpleParamIndex = mdParamIdx;
            }
        }
        mdParamIdx++;

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

    // Register parameters in the scope (including 'this').
    // Strategy B Phase 6d: per-parameter logic factored into bindOneParameter.
    // Methods use defineVariable (not defineVariableAlloca) — params are not
    // reassignable. Slot 0 is 'this' (synthetic, no AST param); user params
    // start at index 1. Methods don't currently support default values, but
    // bindOneParameter handles them correctly if a future change adds them.
    for (size_t i = 0; i < func->params.size(); ++i) {
        size_t astParamIdx = (i >= 1) ? (i - 1) : SIZE_MAX;
        ast::Parameter* astParam = (astParamIdx < node->parameters.size())
            ? node->parameters[astParamIdx].get() : nullptr;
        bindOneParameter(func.get(), i, astParam, /*useAlloca=*/false);
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
        // BigInt operand: route through ts_bigint_neg. Otherwise the
        // generic Neg op below would treat the NaN-boxed pointer as an
        // i64 and produce nonsense (e.g. `-(0n)` becomes a number with
        // value INT64_MIN as a double).
        bool isBigInt = false;
        if (operand && operand->type && operand->type->kind == HIRTypeKind::BigInt) {
            isBigInt = true;
        } else if (node->operand->inferredType && node->operand->inferredType->kind == ts::TypeKind::BigInt) {
            isBigInt = true;
        }
        if (isBigInt) {
            lastValue_ = builder_.createCall("ts_bigint_neg", {operand},
                HIRType::makeBigInt());
        } else {
            // Strategy B Phase 3: emit generic Neg. SpecializationPass will
            // rewrite to NegF64 or NegI64 based on the result type. Keeps the
            // AST-fallback helper logic local to ASTToHIR until Phase 4.
            // JS unary minus ALWAYS produces a Number (an IEEE double). Keep an
            // Int64 result only when the operand is statically an integer or
            // boolean (a real optimization for integer arithmetic); for every
            // other operand type — Float64, Any, String, Object/wrapper, or
            // untyped — use Float64. Typing the Neg result Int64 for those is
            // wrong two ways: (1) it truncates/garbles when the result is boxed
            // by HIR type (e.g. a ternary branch `cond ? 0 : -v` with a
            // fractional `v`), and (2) SpecializationPass cannot lower a
            // Neg(result=i64, operand=string) at all → the value never lands in
            // valueMap_ and the use reads garbage/0 (e.g. `-'2'` yielded 0).
            // NegF64 calls ts_value_get_double, which ToNumber-coerces ints,
            // numeric strings, and Number/wrapper objects correctly.
            bool isInt = false;
            if (operand && operand->type &&
                (operand->type->kind == HIRTypeKind::Int64 ||
                 operand->type->kind == HIRTypeKind::Bool)) {
                isInt = true;
            }
            auto resultType = isInt ? HIRType::makeInt64() : HIRType::makeFloat64();
            lastValue_ = builder_.createNeg(operand, resultType);
        }
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
            // Coerce bool operand to i64 (ToNumber: false=0, true=1) so the
            // i64 add/sub doesn't get a type-mismatched i1 LHS.
            if (operand && operand->type && operand->type->kind == HIRTypeKind::Bool) {
                operand = builder_.createCastBoolToI64(operand);
            }
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
                        broadcastCaptureWrite(info, result);
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
            // Same ClassName.staticField fast-path as the postfix variant.
            // ECMA-262 §13.4 UpdateExpression: read, increment, write — the
            // write must target the same storage location as the read; for a
            // static class field that's the per-class _static_ LLVM global.
            bool storedToStaticGlobal = false;
            if (auto* classNameIdent = dynamic_cast<ast::Identifier*>(prop->expression.get())) {
                for (auto& cls : module_->classes) {
                    if (cls->name == classNameIdent->name) {
                        std::string globalName = cls->name + "_static_" + prop->name;
                        auto it = staticPropertyGlobals_.find(globalName);
                        if (it != staticPropertyGlobals_.end()) {
                            builder_.createStore(result, it->second.first, it->second.second);
                            storedToStaticGlobal = true;
                        }
                        break;
                    }
                }
            }
            if (!storedToStaticGlobal) {
                auto obj = lowerExpression(prop->expression.get());
                std::string propName = prop->name;
                builder_.createSetPropStatic(obj, propName, result);
            }
        }
        // Handle element access (e.g., obj[key]++, arr[i]++)
        auto* elem = dynamic_cast<ast::ElementAccessExpression*>(node->operand.get());
        if (elem) {
            auto obj = lowerExpression(elem->expression.get());
            auto key = lowerExpression(elem->argumentExpression.get());
            builder_.createSetPropDynamic(obj, key, result);
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
        // delete obj.prop — use DeleteProp HIR opcode so the lowering goes
        // through lowerDeleteProp → getTsObjectDeleteProperty (correct i32
        // return type). The generic createCall path declared the function
        // with ptr return type, causing it to be silently unlinked.
        auto obj = lowerExpression(propAccess->expression.get());
        auto key = builder_.createConstString(propAccess->name);
        lastValue_ = builder_.createDeleteProp(obj, key);
        return;
    }

    if (auto* elemAccess = dynamic_cast<ast::ElementAccessExpression*>(node->expression.get())) {
        // delete obj["prop"] or delete obj[key]
        auto obj = lowerExpression(elemAccess->expression.get());
        auto key = lowerExpression(elemAccess->argumentExpression.get());
        lastValue_ = builder_.createDeleteProp(obj, key);
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
            // Coerce bool operand to i64 (ToNumber: false=0, true=1) so the
            // i64 add/sub doesn't get a type-mismatched i1 LHS.
            if (operand && operand->type && operand->type->kind == HIRTypeKind::Bool) {
                operand = builder_.createCastBoolToI64(operand);
                oldValue = operand;
            }
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
                        broadcastCaptureWrite(info, result);
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
            // Static class field: ClassName.field++. Mirrors the regular
            // assignment path (visitAssignmentExpression) which routes writes
            // for `ClassName.field = ...` through the per-class
            // `<ClassName>_static_<field>` LLVM global rather than the dynamic
            // property setter. Without this branch, postfix `++` reads from
            // the global (correctly) but `createSetPropStatic` writes only to
            // the class object's dynamic property map, so subsequent reads
            // through the same global stay at the old value.
            bool storedToStaticGlobal = false;
            if (auto* classNameIdent = dynamic_cast<ast::Identifier*>(prop->expression.get())) {
                for (auto& cls : module_->classes) {
                    if (cls->name == classNameIdent->name) {
                        std::string globalName = cls->name + "_static_" + prop->name;
                        auto it = staticPropertyGlobals_.find(globalName);
                        if (it != staticPropertyGlobals_.end()) {
                            builder_.createStore(result, it->second.first, it->second.second);
                            storedToStaticGlobal = true;
                        }
                        break;
                    }
                }
            }
            if (!storedToStaticGlobal) {
                auto obj = lowerExpression(prop->expression.get());
                std::string propName = prop->name;
                builder_.createSetPropStatic(obj, propName, result);
            }
        }
        // Handle element access (e.g., obj[key]++, arr[i]++)
        auto* elem = dynamic_cast<ast::ElementAccessExpression*>(node->operand.get());
        if (elem) {
            auto obj = lowerExpression(elem->expression.get());
            auto key = lowerExpression(elem->argumentExpression.get());
            builder_.createSetPropDynamic(obj, key, result);
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

    // Scan instance constructor body for this.x = expr assignments
    // (static-method "constructor" is unrelated).
    for (auto& memberPtr : node->members) {
        if (auto* method = dynamic_cast<ast::MethodDefinition*>(memberPtr.get())) {
            if (method->name == "constructor" && method->hasBody && !method->isStatic) {
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

            // Generate a unique function name for the method.
            // Static `constructor` is a static method, not the instance ctor —
            // it must NOT share the canonical "<Class>_constructor" symbol
            // (would collide with the real instance constructor and crash
            // codegen on duplicate symbols).
            std::string methodFuncName;
            std::string methodKey = methodDef->name;  // Key used for registration in class
            if (methodDef->name == "constructor" && !methodDef->isStatic) {
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
            // SetFunctionName: a class method's .name is its key (accessors are
            // prefixed "get "/"set "); the instance constructor's .name is the
            // class name (inferred binding name for an anonymous class expr).
            if (methodDef->name == "constructor" && !methodDef->isStatic) {
                std::string cn = node->name.empty() ? pendingClosureDisplayName_ : node->name;
                if (!cn.empty()) func->displayName = cn;
            } else if (!methodDef->name.empty()) {
                func->displayName = methodDef->isGetter ? ("get " + methodDef->name)
                                  : methodDef->isSetter ? ("set " + methodDef->name)
                                  : methodDef->name;
            }

            // For instance methods (and constructor), 'this' is the first parameter
            if (!methodDef->isStatic) {
                func->params.push_back({"this", HIRType::makeObject()});
            }

            // Collect destructured parameter patterns so we can emit
            // extraction at method entry — without this, `class C {
            // method([x, y, z]) {} }` produces a method with a single
            // `paramN` and no destructuring, leaving x/y/z unbound and
            // crashing on use. Mirrors the FunctionDeclaration handling.
            struct CClsDestructuredParam {
                size_t paramIndex;
                ast::ObjectBindingPattern* objPattern = nullptr;
                ast::ArrayBindingPattern* arrPattern = nullptr;
                ast::Node* defaultInitializer = nullptr;
            };
            std::vector<CClsDestructuredParam> ccDestructuredParams;

            // Add explicit parameters
            for (auto& param : methodDef->parameters) {
                auto paramType = param->type.empty()
                    ? HIRType::makeAny()
                    : convertTypeFromString(param->type);

                std::string paramName;
                if (auto* ident = dynamic_cast<ast::Identifier*>(param->name.get())) {
                    paramName = ident->name;
                } else if (auto* objPat = dynamic_cast<ast::ObjectBindingPattern*>(param->name.get())) {
                    paramName = "param" + std::to_string(func->params.size());
                    paramType = HIRType::makeAny();
                    ccDestructuredParams.push_back({func->params.size(), objPat, nullptr,
                        param->initializer.get()});
                } else if (auto* arrPat = dynamic_cast<ast::ArrayBindingPattern*>(param->name.get())) {
                    paramName = "param" + std::to_string(func->params.size());
                    paramType = HIRType::makeAny();
                    ccDestructuredParams.push_back({func->params.size(), nullptr, arrPat,
                        param->initializer.get()});
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

            // Register parameters in scope.
            // ccArgTypeOffset is 1 for instance methods (slot 0 = synthetic
            // 'this', user params start at HIR index 1) and 0 for static
            // methods. Map the HIR param index back to the AST parameter so
            // we honor default-value initializers (e.g. `method(a = 99)`).
            // The InliningPass searches module_->functions by name and picks
            // the first match — visitClassDeclaration emits this body BEFORE
            // the spec path, so this body must include the default-handling
            // branch or the inliner will fold the call site to raw `undefined`.
            //
            // CRITICAL: set nextValueId BEFORE the loop so allocas created
            // for default-handling don't collide with param HIRValue ids
            // (params already occupy ids [0..N-1]).
            func->nextValueId = static_cast<uint32_t>(func->params.size());
            size_t ccArgTypeOffset = methodDef->isStatic ? 0 : 1;
            for (size_t i = 0; i < func->params.size(); ++i) {
                const auto& [paramName, paramType] = func->params[i];
                auto paramValue = std::make_shared<HIRValue>(static_cast<uint32_t>(i), paramType, paramName);

                size_t astParamIdx = (i >= ccArgTypeOffset) ? (i - ccArgTypeOffset) : SIZE_MAX;
                ast::Parameter* astParam = (astParamIdx < methodDef->parameters.size())
                    ? methodDef->parameters[astParamIdx].get() : nullptr;
                bool isDestructured = astParam && (
                    dynamic_cast<ast::ObjectBindingPattern*>(astParam->name.get()) ||
                    dynamic_cast<ast::ArrayBindingPattern*>(astParam->name.get()));

                if (astParam && astParam->initializer && !isDestructured) {
                    // Scalar default — alloca + branch on isUndefined, assign
                    // default expression value when missing.
                    auto allocaVal = builder_.createAlloca(paramType);
                    auto isUndefined = builder_.createCall("ts_value_is_undefined",
                        {paramValue}, HIRType::makeBool());

                    auto defaultBB = func->createBlock("default_param");
                    auto usedBB = func->createBlock("use_param");
                    auto mergeBB = func->createBlock("param_merge");

                    builder_.createCondBranch(isUndefined, defaultBB, usedBB);

                    builder_.setInsertPoint(defaultBB);
                    currentBlock_ = defaultBB;
                    auto* initExpr = dynamic_cast<ast::Expression*>(astParam->initializer.get());
                    auto defaultVal = initExpr ? lowerExpression(initExpr)
                                               : builder_.createConstUndefined();
                    if (paramType->kind == HIRTypeKind::Any) {
                        defaultVal = forceBoxValue(defaultVal);
                    }
                    builder_.createStore(defaultVal, allocaVal);
                    builder_.createBranch(mergeBB);

                    builder_.setInsertPoint(usedBB);
                    currentBlock_ = usedBB;
                    builder_.createStore(paramValue, allocaVal);
                    builder_.createBranch(mergeBB);

                    builder_.setInsertPoint(mergeBB);
                    currentBlock_ = mergeBB;

                    defineVariableAlloca(paramName, allocaVal, paramType);
                } else {
                    defineVariable(paramName, paramValue);
                }
            }
            // NOTE: Do NOT reset nextValueId here. The default-handling logic
            // above creates allocas and intermediate values that bumped
            // nextValueId past params.size(); resetting it here would cause
            // the destructure loop below to re-use ids and collide.

            // Emit destructuring extraction for parameters with binding
            // patterns (mirrors the FunctionDeclaration path).
            for (auto& dp : ccDestructuredParams) {
                auto paramValue = std::make_shared<HIRValue>(
                    static_cast<uint32_t>(dp.paramIndex),
                    HIRType::makeAny(),
                    func->params[dp.paramIndex].first);
                if (auto* defaultExpr = dynamic_cast<ast::Expression*>(dp.defaultInitializer)) {
                    auto isUndef = builder_.createIsUndefined(paramValue);
                    auto defaultVal = lowerExpression(defaultExpr);
                    defaultVal = boxValueIfNeeded(defaultVal);
                    paramValue = builder_.createSelect(isUndef, defaultVal, paramValue);
                }
                if (dp.objPattern) {
                    lowerObjectBindingPattern(dp.objPattern, paramValue);
                } else if (dp.arrPattern) {
                    lowerArrayBindingPattern(dp.arrPattern, paramValue);
                }
            }

            // For instance constructors, initialize instance property defaults before user code.
            // Static `constructor` is just a static method — never an instance ctor.
            if (methodDef->name == "constructor" && !methodDef->isStatic) {
                // Get 'this' pointer (first parameter)
                auto thisValue = lookupVariable("this");
                if (thisValue) {
                    // Iterate over all property definitions and emit initializers
                    for (auto& member : node->members) {
                        if (auto* propDef = dynamic_cast<ast::PropertyDefinition*>(member.get())) {
                            if (!propDef->isStatic) {
                                // ECMA-262 15.7: every declared field
                                // is installed on the instance, even
                                // when no initializer is given — value
                                // defaults to undefined. Without this,
                                // tests like `class { 'a'; 'b' = 1; }`
                                // see only 'b' as an own property.
                                std::shared_ptr<HIRValue> initVal;
                                if (propDef->initializer) {
                                    initVal = lowerExpression(propDef->initializer.get());
                                } else {
                                    initVal = builder_.createConstUndefined();
                                }
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

            // Register method in the class. Static `constructor` is a
            // static method that happens to be named "constructor" — NOT
            // the class's instance constructor.
            HIRFunction* funcPtr = func.get();
            if (methodDef->name == "constructor" && !methodDef->isStatic) {
                hirClass->constructor = funcPtr;
            } else if (methodDef->isStatic) {
                // Use methodKey so static accessors get the
                // __getter_<name> / __setter_<name> prefix needed for
                // runtime accessor dispatch on the constructor.
                hirClass->staticMethods[methodKey] = funcPtr;
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

        // Always emit a default constructor so the class identifier
        // resolves to a real function value in untyped JS mode. Without
        // a constructor function, `typeof E` and `E.prototype` collapse
        // to undefined because visitIdentifier has nothing to load.
        // The body still calls super() when there is a base class and
        // initializes property defaults when present.
        bool needsDefaultConstructor = true;
        (void)hasPropertyInitializers;

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

            // Initialize property defaults. Every declared instance
            // field is installed on `this`, with `undefined` for
            // fields without initializers — matches ECMA-262 15.7.
            for (auto& memberPtr : node->members) {
                if (auto* propDef = dynamic_cast<ast::PropertyDefinition*>(memberPtr.get())) {
                    if (!propDef->isStatic) {
                        std::shared_ptr<HIRValue> initVal;
                        if (propDef->initializer) {
                            initVal = lowerExpression(propDef->initializer.get());
                        } else {
                            initVal = builder_.createConstUndefined();
                        }
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

    // Defer class-prototype install: emitDeferredStaticInits at user_main
    // entry will create a real prototype object holding all instance
    // methods (and `__getter_<key>` / `__setter_<key>` for accessors)
    // and assign it to `E.prototype`. Without this, `E.prototype` reads
    // the function's default `prototype` slot (an empty object) and
    // direct accessor probes like `E.prototype['<key>']` return
    // undefined.
    // Every class needs a prototype init (not just classes with user-defined
    // methods) so that `c.constructor === C`, `Object.getPrototypeOf(c) ===
    // C.prototype`, and `extends` linkage all hold.
    deferredClassPrototypes_.push_back(hirClass);

    // Restore class context
    currentClass_ = savedClass;
}

void ASTToHIR::visitClassExpression(ast::ClassExpression* node) {
    setSourceLine(node);

    // Phase 9c-i: if this AST node was already pre-registered in pass 1, skip
    // straight to the trailer (loadFunction + prototype setup) and don't
    // re-create the class. The pre-pass call had no current function so the
    // trailer was skipped; this second call (from visitVariableDeclaration in
    // a function body context) is where we emit the value-producing code.
    auto cacheIt = astClassExprToHIRClass_.find(node);
    if (cacheIt != astClassExprToHIRClass_.end()) {
        HIRClass* hirClass = cacheIt->second;
        lastGeneratedClassName_ = hirClass->name;
        if (!currentFunction_) {
            // Pre-pass: also queue the prototype install so that
            // top-level `let B = class { foo(){} }` gets `B.prototype.foo`
            // populated at user_main entry (the inline trailer below
            // never runs for top-level class expressions because the
            // node is not re-visited from a function context — the
            // let-decl statement lives in `module->ast->body` only).
            // Same widening as the declaration path — every class needs init.
            {
                bool already = false;
                for (auto* c : deferredClassPrototypes_) if (c == hirClass) { already = true; break; }
                if (!already) deferredClassPrototypes_.push_back(hirClass);
            }
            return;
        }
        // Emit the value: a reference to the constructor function.
        if (hirClass->constructor) {
            lastValue_ = builder_.createLoadFunction(hirClass->constructor->name);
        } else {
            lastValue_ = builder_.createLoadFunction(hirClass->name + "_constructor");
        }
        // Set up prototype object with instance methods for dynamic dispatch.
        if (!hirClass->methods.empty()) {
            auto ctorVal = lastValue_;
            auto proto = builder_.createCall("ts_object_create_empty", {}, HIRType::makeAny());
            for (auto& [methodKey, methodFunc] : hirClass->methods) {
                if (!methodFunc) continue;
                auto methodClosure = builder_.createLoadFunction(methodFunc->name);
                builder_.createSetPropStatic(proto, methodKey, methodClosure);
            }
            builder_.createSetPropStatic(ctorVal, "prototype", proto);
        }
        // Install static methods on the constructor for dynamic access.
        for (auto& [methodName, methodFunc] : hirClass->staticMethods) {
            if (!methodFunc) continue;
            auto methodClosure = builder_.createLoadFunction(methodFunc->name);
            builder_.createSetPropStatic(lastValue_, methodName, methodClosure);
        }
        return;
    }

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
    // Phase 9c-i: cache so a re-visit (from the var-decl lowering after the
    // pre-pass) reuses this class instead of creating a duplicate.
    astClassExprToHIRClass_[node] = hirClass;

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

    // Scan instance constructor body for this.x = expr assignments
    // (static-method "constructor" is unrelated).
    for (auto& memberPtr : node->members) {
        if (auto* method = dynamic_cast<ast::MethodDefinition*>(memberPtr.get())) {
            if (method->name == "constructor" && method->hasBody && !method->isStatic) {
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

            // Generate a unique function name for the method.
            // Static `constructor` is a static method, not the instance ctor.
            std::string methodFuncName;
            std::string methodKey = methodDef->name;  // Key used for registration in class
            if (methodDef->name == "constructor" && !methodDef->isStatic) {
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
            // SetFunctionName: a class method's .name is its key (accessors are
            // prefixed "get "/"set "); the instance constructor's .name is the
            // class name (inferred binding name for an anonymous class expr).
            if (methodDef->name == "constructor" && !methodDef->isStatic) {
                std::string cn = node->name.empty() ? pendingClosureDisplayName_ : node->name;
                if (!cn.empty()) func->displayName = cn;
            } else if (!methodDef->name.empty()) {
                func->displayName = methodDef->isGetter ? ("get " + methodDef->name)
                                  : methodDef->isSetter ? ("set " + methodDef->name)
                                  : methodDef->name;
            }

            // For instance methods (and constructor), 'this' is the first parameter
            if (!methodDef->isStatic) {
                func->params.push_back({"this", HIRType::makeObject()});
            }

            // Collect destructured parameter patterns so we can emit
            // extraction at method entry — without this, `class C {
            // method([x, y, z]) {} }` produces a method with a single
            // `paramN` and no destructuring, leaving x/y/z unbound and
            // crashing on use. Mirrors the FunctionDeclaration handling.
            struct CClsDestructuredParam {
                size_t paramIndex;
                ast::ObjectBindingPattern* objPattern = nullptr;
                ast::ArrayBindingPattern* arrPattern = nullptr;
                ast::Node* defaultInitializer = nullptr;
            };
            std::vector<CClsDestructuredParam> ccDestructuredParams;

            // Add explicit parameters
            for (auto& param : methodDef->parameters) {
                auto paramType = param->type.empty()
                    ? HIRType::makeAny()
                    : convertTypeFromString(param->type);

                std::string paramName;
                if (auto* ident = dynamic_cast<ast::Identifier*>(param->name.get())) {
                    paramName = ident->name;
                } else if (auto* objPat = dynamic_cast<ast::ObjectBindingPattern*>(param->name.get())) {
                    paramName = "param" + std::to_string(func->params.size());
                    paramType = HIRType::makeAny();
                    ccDestructuredParams.push_back({func->params.size(), objPat, nullptr,
                        param->initializer.get()});
                } else if (auto* arrPat = dynamic_cast<ast::ArrayBindingPattern*>(param->name.get())) {
                    paramName = "param" + std::to_string(func->params.size());
                    paramType = HIRType::makeAny();
                    ccDestructuredParams.push_back({func->params.size(), nullptr, arrPat,
                        param->initializer.get()});
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

            // Register parameters in scope.
            // ccArgTypeOffset is 1 for instance methods (slot 0 = synthetic
            // 'this', user params start at HIR index 1) and 0 for static
            // methods. Map the HIR param index back to the AST parameter so
            // we honor default-value initializers (e.g. `method(a = 99)`).
            // The InliningPass searches module_->functions by name and picks
            // the first match — visitClassDeclaration emits this body BEFORE
            // the spec path, so this body must include the default-handling
            // branch or the inliner will fold the call site to raw `undefined`.
            //
            // CRITICAL: set nextValueId BEFORE the loop so allocas created
            // for default-handling don't collide with param HIRValue ids
            // (params already occupy ids [0..N-1]).
            func->nextValueId = static_cast<uint32_t>(func->params.size());
            size_t ccArgTypeOffset = methodDef->isStatic ? 0 : 1;
            for (size_t i = 0; i < func->params.size(); ++i) {
                const auto& [paramName, paramType] = func->params[i];
                auto paramValue = std::make_shared<HIRValue>(static_cast<uint32_t>(i), paramType, paramName);

                size_t astParamIdx = (i >= ccArgTypeOffset) ? (i - ccArgTypeOffset) : SIZE_MAX;
                ast::Parameter* astParam = (astParamIdx < methodDef->parameters.size())
                    ? methodDef->parameters[astParamIdx].get() : nullptr;
                bool isDestructured = astParam && (
                    dynamic_cast<ast::ObjectBindingPattern*>(astParam->name.get()) ||
                    dynamic_cast<ast::ArrayBindingPattern*>(astParam->name.get()));

                if (astParam && astParam->initializer && !isDestructured) {
                    // Scalar default — alloca + branch on isUndefined, assign
                    // default expression value when missing.
                    auto allocaVal = builder_.createAlloca(paramType);
                    auto isUndefined = builder_.createCall("ts_value_is_undefined",
                        {paramValue}, HIRType::makeBool());

                    auto defaultBB = func->createBlock("default_param");
                    auto usedBB = func->createBlock("use_param");
                    auto mergeBB = func->createBlock("param_merge");

                    builder_.createCondBranch(isUndefined, defaultBB, usedBB);

                    builder_.setInsertPoint(defaultBB);
                    currentBlock_ = defaultBB;
                    auto* initExpr = dynamic_cast<ast::Expression*>(astParam->initializer.get());
                    auto defaultVal = initExpr ? lowerExpression(initExpr)
                                               : builder_.createConstUndefined();
                    if (paramType->kind == HIRTypeKind::Any) {
                        defaultVal = forceBoxValue(defaultVal);
                    }
                    builder_.createStore(defaultVal, allocaVal);
                    builder_.createBranch(mergeBB);

                    builder_.setInsertPoint(usedBB);
                    currentBlock_ = usedBB;
                    builder_.createStore(paramValue, allocaVal);
                    builder_.createBranch(mergeBB);

                    builder_.setInsertPoint(mergeBB);
                    currentBlock_ = mergeBB;

                    defineVariableAlloca(paramName, allocaVal, paramType);
                } else {
                    defineVariable(paramName, paramValue);
                }
            }
            // NOTE: Do NOT reset nextValueId here. The default-handling logic
            // above creates allocas and intermediate values that bumped
            // nextValueId past params.size(); resetting it here would cause
            // the destructure loop below to re-use ids and collide.

            // Emit destructuring extraction for parameters with binding
            // patterns (mirrors the FunctionDeclaration path).
            for (auto& dp : ccDestructuredParams) {
                auto paramValue = std::make_shared<HIRValue>(
                    static_cast<uint32_t>(dp.paramIndex),
                    HIRType::makeAny(),
                    func->params[dp.paramIndex].first);
                if (auto* defaultExpr = dynamic_cast<ast::Expression*>(dp.defaultInitializer)) {
                    auto isUndef = builder_.createIsUndefined(paramValue);
                    auto defaultVal = lowerExpression(defaultExpr);
                    defaultVal = boxValueIfNeeded(defaultVal);
                    paramValue = builder_.createSelect(isUndef, defaultVal, paramValue);
                }
                if (dp.objPattern) {
                    lowerObjectBindingPattern(dp.objPattern, paramValue);
                } else if (dp.arrPattern) {
                    lowerArrayBindingPattern(dp.arrPattern, paramValue);
                }
            }

            // For instance constructors, initialize instance property defaults before user code.
            // Static `constructor` is just a static method — never an instance ctor.
            if (methodDef->name == "constructor" && !methodDef->isStatic) {
                // Get 'this' pointer (first parameter)
                auto thisValue = lookupVariable("this");
                if (thisValue) {
                    // Iterate over all property definitions and emit initializers
                    for (auto& member : node->members) {
                        if (auto* propDef = dynamic_cast<ast::PropertyDefinition*>(member.get())) {
                            if (!propDef->isStatic) {
                                // ECMA-262 15.7: every declared field
                                // is installed on the instance, even
                                // when no initializer is given — value
                                // defaults to undefined. Without this,
                                // tests like `class { 'a'; 'b' = 1; }`
                                // see only 'b' as an own property.
                                std::shared_ptr<HIRValue> initVal;
                                if (propDef->initializer) {
                                    initVal = lowerExpression(propDef->initializer.get());
                                } else {
                                    initVal = builder_.createConstUndefined();
                                }
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

            // Register method in the class. Static `constructor` is a
            // static method that happens to be named "constructor" — NOT
            // the class's instance constructor.
            HIRFunction* funcPtr = func.get();
            if (methodDef->name == "constructor" && !methodDef->isStatic) {
                hirClass->constructor = funcPtr;
            } else if (methodDef->isStatic) {
                // Use methodKey so static accessors get the
                // __getter_<name> / __setter_<name> prefix needed for
                // runtime accessor dispatch on the constructor.
                hirClass->staticMethods[methodKey] = funcPtr;
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

        // Always emit a default constructor so the class identifier
        // resolves to a real function value in untyped JS mode. Without
        // a constructor function, `typeof E` and `E.prototype` collapse
        // to undefined because visitIdentifier has nothing to load.
        // The body still calls super() when there is a base class and
        // initializes property defaults when present.
        bool needsDefaultConstructor = true;
        (void)hasPropertyInitializers;

        if (needsDefaultConstructor) {
            std::string ctorName = className + "_constructor";
            auto defaultCtor = std::make_unique<HIRFunction>(ctorName);
            {
                // .name of the (default) constructor = the class name, or the
                // inferred binding name for an anonymous class expression.
                std::string cn = node->name.empty() ? pendingClosureDisplayName_ : node->name;
                if (!cn.empty()) defaultCtor->displayName = cn;
            }

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

            // Initialize property defaults. Every declared instance
            // field is installed on `this`, with `undefined` for
            // fields without initializers — matches ECMA-262 15.7.
            for (auto& memberPtr : node->members) {
                if (auto* propDef = dynamic_cast<ast::PropertyDefinition*>(memberPtr.get())) {
                    if (!propDef->isStatic) {
                        std::shared_ptr<HIRValue> initVal;
                        if (propDef->initializer) {
                            initVal = lowerExpression(propDef->initializer.get());
                        } else {
                            initVal = builder_.createConstUndefined();
                        }
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

    // Phase 9c-i: if invoked from the pre-scan in pass 1 of lower() — when
    // currentFunction_ is null — there's no insert point to emit the value
    // setup into. Defer prototype install to user_main entry instead;
    // visitVariableDeclaration is NOT guaranteed to re-visit the node
    // because the Monomorphizer drops top-level let-decl statements
    // from the spec body when they have a class-expression initializer,
    // so the cache-fast-path's IR emission never happens.
    if (!currentFunction_) {
        if (!hirClass->methods.empty() || !hirClass->staticMethods.empty()) {
            bool already = false;
            for (auto* c : deferredClassPrototypes_) if (c == hirClass) { already = true; break; }
            if (!already) deferredClassPrototypes_.push_back(hirClass);
        }
        return;
    }

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
    // Install static methods on the constructor itself so dynamic-dispatch
    // access like `F.method()` resolves correctly when `F` is a class-
    // expression-bound variable. visitCallExpression's Case 3 only fires
    // for class-name identifiers tracked in module_->classes, not for
    // class-expression variables.
    for (auto& [methodName, methodFunc] : hirClass->staticMethods) {
        if (!methodFunc) continue;
        auto methodClosure = builder_.createLoadFunction(methodFunc->name);
        builder_.createSetPropStatic(lastValue_, methodName, methodClosure);
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
