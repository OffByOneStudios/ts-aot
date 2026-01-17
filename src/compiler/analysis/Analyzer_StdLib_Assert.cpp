#include "Analyzer.h"

namespace ts {

using namespace ast;

void Analyzer::registerAssert() {
    // =========================================================================
    // assert module - Testing utilities
    //
    // Provides assertion functions for testing and debugging.
    // =========================================================================
    auto assertType = std::make_shared<ObjectType>();

    // assert(value, message?) - basic assertion
    auto assertFn = std::make_shared<FunctionType>();
    assertFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));    // value
    assertFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::String)); // message (optional)
    assertFn->returnType = std::make_shared<Type>(TypeKind::Void);
    assertType->fields["default"] = assertFn;  // assert() itself

    // assert.ok(value, message?) - same as assert()
    auto okFn = std::make_shared<FunctionType>();
    okFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    okFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    okFn->returnType = std::make_shared<Type>(TypeKind::Void);
    assertType->fields["ok"] = okFn;

    // assert.equal(actual, expected, message?) - loose equality
    auto equalFn = std::make_shared<FunctionType>();
    equalFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));    // actual
    equalFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));    // expected
    equalFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::String)); // message
    equalFn->returnType = std::make_shared<Type>(TypeKind::Void);
    assertType->fields["equal"] = equalFn;

    // assert.notEqual(actual, expected, message?) - loose inequality
    auto notEqualFn = std::make_shared<FunctionType>();
    notEqualFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    notEqualFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    notEqualFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    notEqualFn->returnType = std::make_shared<Type>(TypeKind::Void);
    assertType->fields["notEqual"] = notEqualFn;

    // assert.strictEqual(actual, expected, message?) - strict equality (===)
    auto strictEqualFn = std::make_shared<FunctionType>();
    strictEqualFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    strictEqualFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    strictEqualFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    strictEqualFn->returnType = std::make_shared<Type>(TypeKind::Void);
    assertType->fields["strictEqual"] = strictEqualFn;

    // assert.notStrictEqual(actual, expected, message?) - strict inequality (!==)
    auto notStrictEqualFn = std::make_shared<FunctionType>();
    notStrictEqualFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    notStrictEqualFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    notStrictEqualFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    notStrictEqualFn->returnType = std::make_shared<Type>(TypeKind::Void);
    assertType->fields["notStrictEqual"] = notStrictEqualFn;

    // assert.deepEqual(actual, expected, message?) - deep equality
    auto deepEqualFn = std::make_shared<FunctionType>();
    deepEqualFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    deepEqualFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    deepEqualFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    deepEqualFn->returnType = std::make_shared<Type>(TypeKind::Void);
    assertType->fields["deepEqual"] = deepEqualFn;

    // assert.notDeepEqual(actual, expected, message?)
    auto notDeepEqualFn = std::make_shared<FunctionType>();
    notDeepEqualFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    notDeepEqualFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    notDeepEqualFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    notDeepEqualFn->returnType = std::make_shared<Type>(TypeKind::Void);
    assertType->fields["notDeepEqual"] = notDeepEqualFn;

    // assert.deepStrictEqual(actual, expected, message?) - deep strict equality
    auto deepStrictEqualFn = std::make_shared<FunctionType>();
    deepStrictEqualFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    deepStrictEqualFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    deepStrictEqualFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    deepStrictEqualFn->returnType = std::make_shared<Type>(TypeKind::Void);
    assertType->fields["deepStrictEqual"] = deepStrictEqualFn;

    // assert.notDeepStrictEqual(actual, expected, message?)
    auto notDeepStrictEqualFn = std::make_shared<FunctionType>();
    notDeepStrictEqualFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    notDeepStrictEqualFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    notDeepStrictEqualFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    notDeepStrictEqualFn->returnType = std::make_shared<Type>(TypeKind::Void);
    assertType->fields["notDeepStrictEqual"] = notDeepStrictEqualFn;

    // assert.throws(fn, error?, message?) - expects function to throw
    auto throwsFn = std::make_shared<FunctionType>();
    throwsFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::Function)); // fn
    throwsFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));      // error (optional)
    throwsFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));   // message (optional)
    throwsFn->returnType = std::make_shared<Type>(TypeKind::Void);
    assertType->fields["throws"] = throwsFn;

    // assert.doesNotThrow(fn, error?, message?) - expects function to not throw
    auto doesNotThrowFn = std::make_shared<FunctionType>();
    doesNotThrowFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::Function));
    doesNotThrowFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    doesNotThrowFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    doesNotThrowFn->returnType = std::make_shared<Type>(TypeKind::Void);
    assertType->fields["doesNotThrow"] = doesNotThrowFn;

    // assert.rejects(asyncFn, error?, message?) - expects async function to reject
    auto rejectsFn = std::make_shared<FunctionType>();
    rejectsFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any)); // asyncFn or promise
    rejectsFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any)); // error (optional)
    rejectsFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    rejectsFn->returnType = std::make_shared<ClassType>("Promise");
    assertType->fields["rejects"] = rejectsFn;

    // assert.doesNotReject(asyncFn, error?, message?)
    auto doesNotRejectFn = std::make_shared<FunctionType>();
    doesNotRejectFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    doesNotRejectFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    doesNotRejectFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    doesNotRejectFn->returnType = std::make_shared<ClassType>("Promise");
    assertType->fields["doesNotReject"] = doesNotRejectFn;

    // assert.fail(message?) - always fails
    auto failFn = std::make_shared<FunctionType>();
    failFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    failFn->returnType = std::make_shared<Type>(TypeKind::Void);
    assertType->fields["fail"] = failFn;

    // assert.ifError(value) - fails if value is truthy (for error-first callbacks)
    auto ifErrorFn = std::make_shared<FunctionType>();
    ifErrorFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    ifErrorFn->returnType = std::make_shared<Type>(TypeKind::Void);
    assertType->fields["ifError"] = ifErrorFn;

    // assert.match(string, regexp, message?) - tests if string matches regexp
    auto matchFn = std::make_shared<FunctionType>();
    matchFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    matchFn->paramTypes.push_back(std::make_shared<ClassType>("RegExp"));
    matchFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    matchFn->returnType = std::make_shared<Type>(TypeKind::Void);
    assertType->fields["match"] = matchFn;

    // assert.doesNotMatch(string, regexp, message?)
    auto doesNotMatchFn = std::make_shared<FunctionType>();
    doesNotMatchFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    doesNotMatchFn->paramTypes.push_back(std::make_shared<ClassType>("RegExp"));
    doesNotMatchFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    doesNotMatchFn->returnType = std::make_shared<Type>(TypeKind::Void);
    assertType->fields["doesNotMatch"] = doesNotMatchFn;

    // assert.strict - strict mode version (same functions but with strict semantics)
    // Note: For simplicity, we don't include self-reference which could cause issues
    // Users should use assert.strictEqual directly instead of assert.strict.equal

    // AssertionError class
    auto assertionErrorClass = std::make_shared<ClassType>("AssertionError");
    assertionErrorClass->fields["message"] = std::make_shared<Type>(TypeKind::String);
    assertionErrorClass->fields["actual"] = std::make_shared<Type>(TypeKind::Any);
    assertionErrorClass->fields["expected"] = std::make_shared<Type>(TypeKind::Any);
    assertionErrorClass->fields["operator"] = std::make_shared<Type>(TypeKind::String);
    assertionErrorClass->fields["code"] = std::make_shared<Type>(TypeKind::String);
    symbols.defineType("AssertionError", assertionErrorClass);
    assertType->fields["AssertionError"] = assertionErrorClass;

    symbols.define("assert", assertType);
}

} // namespace ts
