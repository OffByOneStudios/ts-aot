// Phase 0 of the "use fast" high-performance TypeScript subset.
// See docs/design/use-fast.md. When a file opts in with a top-level
// `"use fast"` directive (Program::isFast), this pass walks the AST and
// rejects constructs that would defeat unboxed / fixed-shape / no-GC codegen.
//
// Phase 0 is the CONTRACT only — no codegen changes. Diagnostics are half the
// product, so each rejection names the slow mechanism it prevents and the fast
// alternative. Later phases add struct value types, arena allocators, etc.
#include "Analyzer.h"
#include "../ast/AstNodes.h"
#include <string>
#include <vector>

namespace ts {

namespace {

// A recursive AST walker. The Visitor base is all-pure-virtual (~60 methods),
// so a focused hand-written walk is lighter than subclassing it. We check each
// node for banned constructs, then descend into its children.
struct FastChecker {
    Analyzer* self;

    void err(ast::Node* n, const std::string& what,
             const std::string& why, const std::string& fix) {
        self->reportError("use fast: " + what + " (line " +
                          std::to_string(n ? n->line : 0) +
                          ") is not allowed in a \"use fast\" file. " + why +
                          " " + fix);
    }

    static bool isAnyType(const std::string& t) { return t == "any"; }

    void checkParams(std::vector<std::unique_ptr<ast::Parameter>>& params) {
        for (auto& p : params) {
            if (isAnyType(p->type))
                err(p.get(), "parameter typed 'any'",
                    "'any' forces boxing and defeats unboxed codegen.",
                    "Give the parameter a concrete type.");
            if (p->initializer) walk(p->initializer.get());
        }
    }

    void walkStmts(std::vector<ast::StmtPtr>& body) {
        for (auto& s : body) walk(s.get());
    }

    // ---- expressions ----------------------------------------------------
    void walk(ast::Node* n) {
        if (!n) return;

        // --- banned expression forms ---
        if (auto* d = dynamic_cast<ast::DeleteExpression*>(n)) {
            err(n, "'delete'",
                "deleting a property forces a dynamic (hash-map) object shape.",
                "Use a fixed struct field or a sentinel value instead.");
            walk(d->expression.get());
            return;
        }
        if (auto* a = dynamic_cast<ast::AwaitExpression*>(n)) {
            err(n, "'await'",
                "async code needs a heap-allocated suspension frame.",
                "Keep async code outside the fast file.");
            walk(a->expression.get());
            return;
        }
        if (auto* y = dynamic_cast<ast::YieldExpression*>(n)) {
            err(n, "'yield'",
                "generators need a heap-allocated frame.",
                "Keep generator code outside the fast file.");
            if (y->expression) walk(y->expression.get());
            return;
        }
        if (dynamic_cast<ast::DynamicImport*>(n)) {
            err(n, "dynamic import()",
                "dynamic import allocates and suspends.",
                "Use a static top-level import.");
            return;
        }

        // --- expression containers (recurse) ---
        if (auto* id = dynamic_cast<ast::Identifier*>(n)) {
            if (id->name == "arguments")
                err(n, "'arguments'",
                    "the arguments object is a heap-allocated array-like.",
                    "Use explicit named or rest parameters.");
            return;
        }
        if (auto* be = dynamic_cast<ast::BinaryExpression*>(n)) {
            walk(be->left.get()); walk(be->right.get()); return;
        }
        if (auto* ce = dynamic_cast<ast::ConditionalExpression*>(n)) {
            walk(ce->condition.get()); walk(ce->whenTrue.get());
            walk(ce->whenFalse.get()); return;
        }
        if (auto* ae = dynamic_cast<ast::AssignmentExpression*>(n)) {
            walk(ae->left.get()); walk(ae->right.get()); return;
        }
        if (auto* call = dynamic_cast<ast::CallExpression*>(n)) {
            if (auto* cid = dynamic_cast<ast::Identifier*>(call->callee.get())) {
                if (cid->name == "eval")
                    err(n, "eval()",
                        "eval invokes the dynamic interpreter.",
                        "Remove eval; fast code must be statically compiled.");
            }
            walk(call->callee.get());
            for (auto& arg : call->arguments) walk(arg.get());
            return;
        }
        if (auto* ne = dynamic_cast<ast::NewExpression*>(n)) {
            walk(ne->expression.get());
            for (auto& arg : ne->arguments) walk(arg.get());
            return;
        }
        if (auto* pae = dynamic_cast<ast::PropertyAccessExpression*>(n)) {
            walk(pae->expression.get()); return;
        }
        if (auto* eae = dynamic_cast<ast::ElementAccessExpression*>(n)) {
            walk(eae->expression.get());
            walk(eae->argumentExpression.get());
            return;
        }
        if (auto* pre = dynamic_cast<ast::PrefixUnaryExpression*>(n)) {
            walk(pre->operand.get()); return;
        }
        if (auto* post = dynamic_cast<ast::PostfixUnaryExpression*>(n)) {
            walk(post->operand.get()); return;
        }
        if (auto* paren = dynamic_cast<ast::ParenthesizedExpression*>(n)) {
            walk(paren->expression.get()); return;
        }
        if (auto* asx = dynamic_cast<ast::AsExpression*>(n)) {
            walk(asx->expression.get()); return;
        }
        if (auto* nn = dynamic_cast<ast::NonNullExpression*>(n)) {
            walk(nn->expression.get()); return;
        }
        if (auto* sp = dynamic_cast<ast::SpreadElement*>(n)) {
            walk(sp->expression.get()); return;
        }
        if (auto* arr = dynamic_cast<ast::ArrayLiteralExpression*>(n)) {
            for (auto& el : arr->elements) walk(el.get());
            return;
        }
        if (auto* obj = dynamic_cast<ast::ObjectLiteralExpression*>(n)) {
            for (auto& prop : obj->properties) walk(prop.get());
            return;
        }
        if (auto* pa = dynamic_cast<ast::PropertyAssignment*>(n)) {
            if (pa->initializer) walk(pa->initializer.get());
            return;
        }
        if (auto* te = dynamic_cast<ast::TemplateExpression*>(n)) {
            for (auto& span : te->spans) walk(span.expression.get());
            return;
        }
        if (auto* arrow = dynamic_cast<ast::ArrowFunction*>(n)) {
            if (arrow->isAsync)
                err(n, "async arrow function",
                    "async needs a heap-allocated frame.",
                    "Keep async code outside the fast file.");
            checkParams(arrow->parameters);
            walk(arrow->body.get());
            return;
        }
        if (auto* fe = dynamic_cast<ast::FunctionExpression*>(n)) {
            if (fe->isAsync || fe->isGenerator)
                err(n, "async/generator function expression",
                    "they need a heap-allocated frame.",
                    "Keep them outside the fast file.");
            if (isAnyType(fe->returnType))
                err(n, "function returning 'any'",
                    "'any' returns force boxing.",
                    "Annotate a concrete return type.");
            checkParams(fe->parameters);
            walkStmts(fe->body);
            return;
        }

        // --- statements ---
        if (auto* es = dynamic_cast<ast::ExpressionStatement*>(n)) {
            walk(es->expression.get()); return;
        }
        if (auto* vd = dynamic_cast<ast::VariableDeclaration*>(n)) {
            if (isAnyType(vd->type))
                err(n, "variable typed 'any'",
                    "'any' forces boxing and defeats unboxed codegen.",
                    "Give the variable a concrete type.");
            if (vd->initializer) walk(vd->initializer.get());
            return;
        }
        if (auto* b = dynamic_cast<ast::BlockStatement*>(n)) {
            if (b->withHead) {
                err(n, "'with' statement",
                    "with installs a dynamic scope object and defeats static "
                    "name resolution.",
                    "Reference fields explicitly.");
                walk(b->withHead.get());
            }
            walkStmts(b->statements);
            return;
        }
        if (auto* i = dynamic_cast<ast::IfStatement*>(n)) {
            walk(i->condition.get()); walk(i->thenStatement.get());
            walk(i->elseStatement.get()); return;
        }
        if (auto* w = dynamic_cast<ast::WhileStatement*>(n)) {
            walk(w->condition.get()); walk(w->body.get()); return;
        }
        if (auto* f = dynamic_cast<ast::ForStatement*>(n)) {
            walk(f->initializer.get()); walk(f->condition.get());
            walk(f->incrementor.get()); walk(f->body.get()); return;
        }
        if (auto* fo = dynamic_cast<ast::ForOfStatement*>(n)) {
            if (fo->isAwait)
                err(n, "'for await...of'",
                    "async iteration needs a heap-allocated frame.",
                    "Iterate a NativeArray with a plain for loop.");
            walk(fo->initializer.get()); walk(fo->expression.get());
            walk(fo->body.get()); return;
        }
        if (auto* fi = dynamic_cast<ast::ForInStatement*>(n)) {
            err(n, "'for...in'",
                "for-in enumerates dynamic string keys.",
                "Iterate a NativeArray by integer index.");
            walk(fi->expression.get()); walk(fi->body.get()); return;
        }
        if (auto* r = dynamic_cast<ast::ReturnStatement*>(n)) {
            if (r->expression) walk(r->expression.get()); return;
        }
        if (auto* th = dynamic_cast<ast::ThrowStatement*>(n)) {
            walk(th->expression.get()); return;
        }
        if (auto* t = dynamic_cast<ast::TryStatement*>(n)) {
            walkStmts(t->tryBlock);
            if (t->catchClause) walkStmts(t->catchClause->block);
            walkStmts(t->finallyBlock);
            return;
        }
        if (auto* sw = dynamic_cast<ast::SwitchStatement*>(n)) {
            walk(sw->expression.get());
            for (auto& cl : sw->clauses) {
                if (auto* cc = dynamic_cast<ast::CaseClause*>(cl.get())) {
                    walk(cc->expression.get());
                    walkStmts(cc->statements);
                } else if (auto* dc = dynamic_cast<ast::DefaultClause*>(cl.get())) {
                    walkStmts(dc->statements);
                }
            }
            return;
        }
        if (auto* l = dynamic_cast<ast::LabeledStatement*>(n)) {
            walk(l->statement.get()); return;
        }
        if (auto* fd = dynamic_cast<ast::FunctionDeclaration*>(n)) {
            if (fd->isAsync || fd->isGenerator)
                err(n, "async/generator function",
                    "they need a heap-allocated frame.",
                    "Keep them outside the fast file.");
            if (isAnyType(fd->returnType))
                err(n, "function returning 'any'",
                    "'any' returns force boxing.",
                    "Annotate a concrete return type.");
            checkParams(fd->parameters);
            walkStmts(fd->body);
            return;
        }
        if (auto* cd = dynamic_cast<ast::ClassDeclaration*>(n)) {
            for (auto& m : cd->members) {
                if (auto* md = dynamic_cast<ast::MethodDefinition*>(m.get())) {
                    if (md->isAsync || md->isGenerator)
                        err(md, "async/generator method",
                            "they need a heap-allocated frame.",
                            "Keep them outside the fast file.");
                    checkParams(md->parameters);
                    walkStmts(md->body);
                } else if (auto* pd = dynamic_cast<ast::PropertyDefinition*>(m.get())) {
                    if (pd->initializer) walk(pd->initializer.get());
                }
            }
            return;
        }
        // Type-only / import / export / literal nodes: nothing to check.
    }
};

}  // namespace

void Analyzer::performFastCheck(ast::Program* program) {
    if (!program || !program->isFast) return;
    FastChecker fc{this};
    for (auto& s : program->body) fc.walk(s.get());
}

}  // namespace ts
