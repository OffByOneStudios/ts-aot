#include <catch2/catch_test_macros.hpp>
#include "codegen/BoxingPolicy.h"
#include "analysis/Type.h"
#include <stdexcept>

using namespace ts;

// Helper to create test types
static Type* makeType(TypeKind kind) {
    return new Type(kind);
}

// =============================================================================
// RUNTIME API REGISTRY TESTS
// =============================================================================

TEST_CASE("BoxingPolicy: Runtime API expectations", "[codegen][boxing]") {
    BoxingPolicy policy;

    SECTION("ts_map_set: map=raw, key=boxed, value=boxed") {
        REQUIRE(policy.runtimeExpectsBoxed("ts_map_set", 0) == false);
        REQUIRE(policy.runtimeExpectsBoxed("ts_map_set", 1) == true);
        REQUIRE(policy.runtimeExpectsBoxed("ts_map_set", 2) == true);
    }

    SECTION("ts_map_get: map=raw, key=boxed") {
        REQUIRE(policy.runtimeExpectsBoxed("ts_map_get", 0) == false);
        REQUIRE(policy.runtimeExpectsBoxed("ts_map_get", 1) == true);
    }

    SECTION("ts_call_1: func=boxed, arg=boxed") {
        REQUIRE(policy.runtimeExpectsBoxed("ts_call_1", 0) == true);
        REQUIRE(policy.runtimeExpectsBoxed("ts_call_1", 1) == true);
    }

    SECTION("ts_call_2: func=boxed, arg0=boxed, arg1=boxed") {
        REQUIRE(policy.runtimeExpectsBoxed("ts_call_2", 0) == true);
        REQUIRE(policy.runtimeExpectsBoxed("ts_call_2", 1) == true);
        REQUIRE(policy.runtimeExpectsBoxed("ts_call_2", 2) == true);
    }

    SECTION("ts_array_push: arr=raw, value=raw") {
        REQUIRE(policy.runtimeExpectsBoxed("ts_array_push", 0) == false);
        REQUIRE(policy.runtimeExpectsBoxed("ts_array_push", 1) == false);
    }

    SECTION("ts_console_log_int: raw") {
        REQUIRE(policy.runtimeExpectsBoxed("ts_console_log_int", 0) == false);
    }

    SECTION("ts_value_make_int: input raw") {
        REQUIRE(policy.runtimeExpectsBoxed("ts_value_make_int", 0) == false);
    }

    SECTION("ts_value_get_int: input boxed") {
        REQUIRE(policy.runtimeExpectsBoxed("ts_value_get_int", 0) == true);
    }

    SECTION("Unknown function: defaults to raw") {
        REQUIRE(policy.runtimeExpectsBoxed("unknown_function", 0) == false);
        REQUIRE(policy.runtimeExpectsBoxed("unknown_function", 5) == false);
    }

    SECTION("Out of bounds arg index: defaults to false") {
        REQUIRE(policy.runtimeExpectsBoxed("ts_map_set", 100) == false);
    }
}

TEST_CASE("BoxingPolicy: Runtime return values", "[codegen][boxing]") {
    BoxingPolicy policy;

    SECTION("Functions that return boxed values") {
        REQUIRE(policy.runtimeReturnsBoxed("ts_map_get") == true);
        REQUIRE(policy.runtimeReturnsBoxed("ts_value_make_int") == true);
        REQUIRE(policy.runtimeReturnsBoxed("ts_value_make_double") == true);
        REQUIRE(policy.runtimeReturnsBoxed("ts_value_make_function") == true);
        REQUIRE(policy.runtimeReturnsBoxed("ts_call_1") == true);
    }

    SECTION("Functions that return raw values") {
        REQUIRE(policy.runtimeReturnsBoxed("ts_array_push") == false);
        REQUIRE(policy.runtimeReturnsBoxed("ts_map_has") == false);
        REQUIRE(policy.runtimeReturnsBoxed("ts_array_length") == false);
    }
}

// =============================================================================
// BOXING FUNCTION SELECTION TESTS
// =============================================================================

TEST_CASE("BoxingPolicy: Boxing function selection", "[codegen][boxing]") {
    SECTION("Int type uses ts_value_make_int") {
        auto intType = makeType(TypeKind::Int);
        REQUIRE(BoxingPolicy::getBoxingFunction(intType) == "ts_value_make_int");
        delete intType;
    }

    SECTION("Double type uses ts_value_make_double") {
        auto dblType = makeType(TypeKind::Double);
        REQUIRE(BoxingPolicy::getBoxingFunction(dblType) == "ts_value_make_double");
        delete dblType;
    }

    SECTION("Boolean type uses ts_value_make_bool") {
        auto boolType = makeType(TypeKind::Boolean);
        REQUIRE(BoxingPolicy::getBoxingFunction(boolType) == "ts_value_make_bool");
        delete boolType;
    }

    SECTION("String type uses ts_value_make_string") {
        auto strType = makeType(TypeKind::String);
        REQUIRE(BoxingPolicy::getBoxingFunction(strType) == "ts_value_make_string");
        delete strType;
    }

    SECTION("Function type uses ts_value_make_function") {
        auto fnType = makeType(TypeKind::Function);
        REQUIRE(BoxingPolicy::getBoxingFunction(fnType) == "ts_value_make_function");
        delete fnType;
    }

    SECTION("Array type uses ts_value_make_object") {
        auto arrType = makeType(TypeKind::Array);
        REQUIRE(BoxingPolicy::getBoxingFunction(arrType) == "ts_value_make_object");
        delete arrType;
    }
}

TEST_CASE("BoxingPolicy: Unboxing function selection", "[codegen][boxing]") {
    SECTION("Int type uses ts_value_get_int") {
        auto intType = makeType(TypeKind::Int);
        REQUIRE(BoxingPolicy::getUnboxingFunction(intType) == "ts_value_get_int");
        delete intType;
    }

    SECTION("Double type uses ts_value_get_double") {
        auto dblType = makeType(TypeKind::Double);
        REQUIRE(BoxingPolicy::getUnboxingFunction(dblType) == "ts_value_get_double");
        delete dblType;
    }

    SECTION("String type uses ts_value_get_string") {
        auto strType = makeType(TypeKind::String);
        REQUIRE(BoxingPolicy::getUnboxingFunction(strType) == "ts_value_get_string");
        delete strType;
    }

    SECTION("Object type uses ts_value_get_object") {
        auto objType = makeType(TypeKind::Object);
        REQUIRE(BoxingPolicy::getUnboxingFunction(objType) == "ts_value_get_object");
        delete objType;
    }
}

// =============================================================================
// CONTEXT-BASED DECISION TESTS
// =============================================================================

TEST_CASE("BoxingPolicy: UserFunctionArg decisions", "[codegen][boxing]") {
    BoxingPolicy policy;

    SECTION("Typed parameter = no boxing (optimization)") {
        auto intType = makeType(TypeKind::Int);
        auto decision = policy.decide(
            intType,                        // value type
            BoxingContext::UserFunctionArg, // context
            "",                             // no runtime func
            -1,                             // no arg index
            intType,                        // target type = typed
            false                           // not already boxed
        );
        REQUIRE(decision.needsBoxing == false);
        delete intType;
    }

    SECTION("Any parameter = needs boxing") {
        auto intType = makeType(TypeKind::Int);
        auto anyType = makeType(TypeKind::Any);
        auto decision = policy.decide(
            intType,                        // value type
            BoxingContext::UserFunctionArg,
            "",
            -1,
            anyType,                        // target type = any
            false
        );
        REQUIRE(decision.needsBoxing == true);
        REQUIRE(decision.boxingFunction == "ts_value_make_int");
        delete intType;
        delete anyType;
    }

    SECTION("Function callback = always box") {
        auto fnType = makeType(TypeKind::Function);
        auto decision = policy.decide(
            fnType,                         // value type = function
            BoxingContext::UserFunctionArg,
            "",
            -1,
            fnType,                         // target type = function
            false
        );
        REQUIRE(decision.needsBoxing == true);
        REQUIRE(decision.boxingFunction == "ts_value_make_function");
        delete fnType;
    }

    SECTION("Already boxed = no double-boxing") {
        auto intType = makeType(TypeKind::Int);
        auto anyType = makeType(TypeKind::Any);
        auto decision = policy.decide(
            intType,
            BoxingContext::UserFunctionArg,
            "",
            -1,
            anyType,
            true                            // ALREADY BOXED
        );
        REQUIRE(decision.needsBoxing == false);  // Already boxed!
        delete intType;
        delete anyType;
    }
}

TEST_CASE("BoxingPolicy: MapKey/MapValue decisions", "[codegen][boxing]") {
    BoxingPolicy policy;

    SECTION("Map key always needs boxing") {
        auto intType = makeType(TypeKind::Int);
        auto decision = policy.decide(
            intType,
            BoxingContext::MapKey,
            "",
            -1,
            nullptr,
            false
        );
        REQUIRE(decision.needsBoxing == true);
        REQUIRE(decision.boxingFunction == "ts_value_make_int");
        delete intType;
    }

    SECTION("Map value always needs boxing") {
        auto strType = makeType(TypeKind::String);
        auto decision = policy.decide(
            strType,
            BoxingContext::MapValue,
            "",
            -1,
            nullptr,
            false
        );
        REQUIRE(decision.needsBoxing == true);
        REQUIRE(decision.boxingFunction == "ts_value_make_string");
        delete strType;
    }

    SECTION("Already boxed map key = no double-boxing") {
        auto intType = makeType(TypeKind::Int);
        auto decision = policy.decide(
            intType,
            BoxingContext::MapKey,
            "",
            -1,
            nullptr,
            true  // already boxed
        );
        REQUIRE(decision.needsBoxing == false);
        delete intType;
    }
}

TEST_CASE("BoxingPolicy: ClosureCapture decisions", "[codegen][boxing]") {
    BoxingPolicy policy;

    SECTION("Primitive values get boxed") {
        auto intType = makeType(TypeKind::Int);
        auto decision = policy.decide(
            intType,
            BoxingContext::ClosureCapture,
            "",
            -1,
            nullptr,
            false
        );
        REQUIRE(decision.needsBoxing == true);
        REQUIRE(decision.boxingFunction == "ts_value_make_int");
        delete intType;
    }

    SECTION("Already boxed function = no double-boxing") {
        auto fnType = makeType(TypeKind::Function);
        auto decision = policy.decide(
            fnType,
            BoxingContext::ClosureCapture,
            "",
            -1,
            nullptr,
            true   // ALREADY BOXED - this is the critical case!
        );
        REQUIRE(decision.needsBoxing == false);  // Don't double-box!
        delete fnType;
    }

    SECTION("Unboxed function gets boxed") {
        auto fnType = makeType(TypeKind::Function);
        auto decision = policy.decide(
            fnType,
            BoxingContext::ClosureCapture,
            "",
            -1,
            nullptr,
            false  // not already boxed
        );
        REQUIRE(decision.needsBoxing == true);
        REQUIRE(decision.boxingFunction == "ts_value_make_function");
        delete fnType;
    }
}

TEST_CASE("BoxingPolicy: RuntimeApiReturn decisions", "[codegen][boxing]") {
    BoxingPolicy policy;

    SECTION("ts_map_get result needs unboxing to int") {
        auto intType = makeType(TypeKind::Int);
        auto decision = policy.decide(
            nullptr,
            BoxingContext::RuntimeApiReturn,
            "ts_map_get",
            -1,
            intType,  // expected type
            false
        );
        REQUIRE(decision.needsUnboxing == true);
        REQUIRE(decision.unboxingFunction == "ts_value_get_int");
        delete intType;
    }

    SECTION("ts_map_get result with Any type = no unboxing") {
        auto anyType = makeType(TypeKind::Any);
        auto decision = policy.decide(
            nullptr,
            BoxingContext::RuntimeApiReturn,
            "ts_map_get",
            -1,
            anyType,  // Any = keep boxed
            false
        );
        REQUIRE(decision.needsUnboxing == false);
        delete anyType;
    }

    SECTION("Raw-returning function = no unboxing") {
        auto intType = makeType(TypeKind::Int);
        auto decision = policy.decide(
            nullptr,
            BoxingContext::RuntimeApiReturn,
            "ts_array_length",  // returns raw int
            -1,
            intType,
            false
        );
        REQUIRE(decision.needsUnboxing == false);
        delete intType;
    }
}

// =============================================================================
// SPECIALIZED ARRAY TESTS
// =============================================================================

TEST_CASE("BoxingPolicy: Specialized array detection", "[codegen][boxing]") {
    SECTION("Int array = specialized (raw storage)") {
        auto intType = makeType(TypeKind::Int);
        REQUIRE(BoxingPolicy::shouldBoxForSpecializedArray(intType) == false);
        delete intType;
    }

    SECTION("Double array = specialized (raw storage)") {
        auto dblType = makeType(TypeKind::Double);
        REQUIRE(BoxingPolicy::shouldBoxForSpecializedArray(dblType) == false);
        delete dblType;
    }

    SECTION("String array = specialized (raw storage)") {
        auto strType = makeType(TypeKind::String);
        REQUIRE(BoxingPolicy::shouldBoxForSpecializedArray(strType) == false);
        delete strType;
    }

    SECTION("Object array = generic (boxed storage)") {
        auto objType = makeType(TypeKind::Object);
        REQUIRE(BoxingPolicy::shouldBoxForSpecializedArray(objType) == true);
        delete objType;
    }

    SECTION("Any array = generic (boxed storage)") {
        auto anyType = makeType(TypeKind::Any);
        REQUIRE(BoxingPolicy::shouldBoxForSpecializedArray(anyType) == true);
        delete anyType;
    }

    SECTION("Null type = generic (boxed storage)") {
        REQUIRE(BoxingPolicy::shouldBoxForSpecializedArray(nullptr) == true);
    }
}

// =============================================================================
// ENFORCEMENT TESTS - Compiler refuses unregistered functions
// =============================================================================

TEST_CASE("BoxingPolicy: hasRuntimeApiRegistered", "[codegen][boxing][enforcement]") {
    BoxingPolicy policy;

    SECTION("Registered functions return true") {
        REQUIRE(policy.hasRuntimeApiRegistered("ts_map_set") == true);
        REQUIRE(policy.hasRuntimeApiRegistered("ts_call_1") == true);
        REQUIRE(policy.hasRuntimeApiRegistered("ts_array_push") == true);
        REQUIRE(policy.hasRuntimeApiRegistered("ts_value_make_int") == true);
    }

    SECTION("Unregistered functions return false") {
        REQUIRE(policy.hasRuntimeApiRegistered("ts_unknown_function") == false);
        REQUIRE(policy.hasRuntimeApiRegistered("some_other_func") == false);
        REQUIRE(policy.hasRuntimeApiRegistered("") == false);
    }
}

TEST_CASE("BoxingPolicy: assertRuntimeApiRegistered", "[codegen][boxing][enforcement]") {
    BoxingPolicy policy;

    SECTION("Registered functions do not throw") {
        REQUIRE_NOTHROW(policy.assertRuntimeApiRegistered("ts_map_set"));
        REQUIRE_NOTHROW(policy.assertRuntimeApiRegistered("ts_call_1"));
        REQUIRE_NOTHROW(policy.assertRuntimeApiRegistered("ts_array_push"));
    }

    SECTION("Unregistered functions throw with helpful message") {
        REQUIRE_THROWS_AS(policy.assertRuntimeApiRegistered("ts_new_api"), std::runtime_error);
        
        try {
            policy.assertRuntimeApiRegistered("ts_new_api");
            REQUIRE(false); // Should not reach here
        } catch (const std::runtime_error& e) {
            std::string msg = e.what();
            // Check that error message contains helpful info
            REQUIRE(msg.find("ts_new_api") != std::string::npos);
            REQUIRE(msg.find("RUNTIME_ARG_BOXING") != std::string::npos);
            REQUIRE(msg.find("BoxingPolicy.cpp") != std::string::npos);
        }
    }
}

TEST_CASE("BoxingPolicy: strictMode in runtimeExpectsBoxed", "[codegen][boxing][enforcement]") {
    BoxingPolicy policy;

    SECTION("Strict mode throws for unregistered function") {
        REQUIRE_THROWS_AS(policy.runtimeExpectsBoxed("unknown_func", 0, true), std::runtime_error);
    }

    SECTION("Strict mode throws for out-of-bounds arg") {
        // ts_map_set has 3 args (0,1,2), so arg 5 is out of bounds
        REQUIRE_THROWS_AS(policy.runtimeExpectsBoxed("ts_map_set", 5, true), std::runtime_error);
    }

    SECTION("Non-strict mode returns false for unregistered (backwards compat)") {
        REQUIRE(policy.runtimeExpectsBoxed("unknown_func", 0, false) == false);
        REQUIRE(policy.runtimeExpectsBoxed("unknown_func", 0) == false); // default is false
    }

    SECTION("Strict mode works normally for registered functions") {
        REQUIRE(policy.runtimeExpectsBoxed("ts_map_set", 1, true) == true);
        REQUIRE(policy.runtimeExpectsBoxed("ts_array_push", 0, true) == false);
    }
}
