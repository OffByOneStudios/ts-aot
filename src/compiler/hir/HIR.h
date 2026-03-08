#pragma once

#include <string>
#include <vector>
#include <memory>
#include <map>
#include <set>
#include <optional>
#include <variant>
#include <cstdint>

namespace ts::hir {

// Forward declarations
struct HIRModule;
struct HIRFunction;
struct HIRBlock;
struct HIRInstruction;
struct HIRType;
struct HIRValue;
struct HIRShape;

//==============================================================================
// HIR Types - Simplified type system for IR level
//==============================================================================

enum class HIRTypeKind {
    Void,
    Bool,
    Int64,      // JavaScript integers (SMI or boxed)
    Float64,    // JavaScript numbers
    String,     // TsString*
    Object,     // Generic object (TsObject*)
    Array,      // TsArray* (may be typed or boxed)
    Map,        // TsMap*
    Set,        // TsSet*
    Symbol,     // TsSymbol*
    BigInt,     // TsBigInt*
    Function,   // Function pointer
    Class,      // Specific class instance
    Any,        // Boxed TsValue*
    Ptr,        // Raw pointer (for GC operations)
};

struct HIRType {
    HIRTypeKind kind;

    // For Array: element type
    std::shared_ptr<HIRType> elementType;

    // For Class: class name and shape ID
    std::string className;
    uint32_t shapeId = 0;

    // For Function: parameter and return types
    std::vector<std::shared_ptr<HIRType>> paramTypes;
    std::shared_ptr<HIRType> returnType;

    // Is this a typed (unboxed) array?
    bool isTypedArray = false;

    HIRType(HIRTypeKind k) : kind(k) {}

    static std::shared_ptr<HIRType> makeVoid() { return std::make_shared<HIRType>(HIRTypeKind::Void); }
    static std::shared_ptr<HIRType> makeBool() { return std::make_shared<HIRType>(HIRTypeKind::Bool); }
    static std::shared_ptr<HIRType> makeInt64() { return std::make_shared<HIRType>(HIRTypeKind::Int64); }
    static std::shared_ptr<HIRType> makeFloat64() { return std::make_shared<HIRType>(HIRTypeKind::Float64); }
    static std::shared_ptr<HIRType> makeString() { return std::make_shared<HIRType>(HIRTypeKind::String); }
    static std::shared_ptr<HIRType> makeAny() { return std::make_shared<HIRType>(HIRTypeKind::Any); }
    static std::shared_ptr<HIRType> makePtr() { return std::make_shared<HIRType>(HIRTypeKind::Ptr); }
    static std::shared_ptr<HIRType> makeObject() { return std::make_shared<HIRType>(HIRTypeKind::Object); }
    static std::shared_ptr<HIRType> makeMap() { return std::make_shared<HIRType>(HIRTypeKind::Map); }
    static std::shared_ptr<HIRType> makeSet() { return std::make_shared<HIRType>(HIRTypeKind::Set); }
    static std::shared_ptr<HIRType> makeSymbol() { return std::make_shared<HIRType>(HIRTypeKind::Symbol); }
    static std::shared_ptr<HIRType> makeBigInt() { return std::make_shared<HIRType>(HIRTypeKind::BigInt); }
    static std::shared_ptr<HIRType> makeDouble() { return makeFloat64(); }  // Alias for convenience

    static std::shared_ptr<HIRType> makeArray(std::shared_ptr<HIRType> elem, bool typed = false) {
        auto t = std::make_shared<HIRType>(HIRTypeKind::Array);
        t->elementType = elem;
        t->isTypedArray = typed;
        return t;
    }

    static std::shared_ptr<HIRType> makeClass(const std::string& name, uint32_t shape = 0) {
        auto t = std::make_shared<HIRType>(HIRTypeKind::Class);
        t->className = name;
        t->shapeId = shape;
        return t;
    }

    static std::shared_ptr<HIRType> makeFunction() {
        return std::make_shared<HIRType>(HIRTypeKind::Function);
    }

    std::string toString() const;
    bool operator==(const HIRType& other) const;
};

//==============================================================================
// HIR Values - SSA values with explicit types
//==============================================================================

struct HIRValue {
    uint32_t id;                    // Unique ID within function (%0, %1, ...)
    std::shared_ptr<HIRType> type;  // Type is ALWAYS present
    std::string name;               // Optional debug name

    // For global variables
    bool isGlobal = false;
    std::string globalName;
    std::shared_ptr<HIRType> globalType;

    HIRValue(uint32_t i, std::shared_ptr<HIRType> t, const std::string& n = "")
        : id(i), type(t), name(n) {}

    std::string toString() const {
        if (isGlobal && !globalName.empty()) return "@" + globalName;
        if (!name.empty()) return "%" + name;
        return "%" + std::to_string(id);
    }
};

//==============================================================================
// HIR Instructions - SSA instructions with typed results
//==============================================================================

enum class HIROpcode {
    // Constants
    ConstInt,           // %r = const.i64 <value>
    ConstFloat,         // %r = const.f64 <value>
    ConstBool,          // %r = const.bool <value>
    ConstString,        // %r = const.string "<value>"
    ConstCString,       // %r = const.cstring "<value>" (raw C string pointer)
    ConstNull,          // %r = const.null
    ConstUndefined,     // %r = const.undefined

    // Arithmetic (typed)
    AddI64,             // %r = add.i64 %a, %b
    SubI64,             // %r = sub.i64 %a, %b
    MulI64,             // %r = mul.i64 %a, %b
    DivI64,             // %r = div.i64 %a, %b
    ModI64,             // %r = mod.i64 %a, %b
    NegI64,             // %r = neg.i64 %a

    AddF64,             // %r = add.f64 %a, %b
    SubF64,             // %r = sub.f64 %a, %b
    MulF64,             // %r = mul.f64 %a, %b
    DivF64,             // %r = div.f64 %a, %b
    ModF64,             // %r = mod.f64 %a, %b
    NegF64,             // %r = neg.f64 %a

    // String operations
    StringConcat,       // %r = string.concat %a, %b

    // Checked arithmetic (overflow → branch)
    AddI64Checked,      // %r = add.i64.checked %a, %b, overflow: %block
    SubI64Checked,      // %r = sub.i64.checked %a, %b, overflow: %block
    MulI64Checked,      // %r = mul.i64.checked %a, %b, overflow: %block

    // Bitwise
    AndI64,             // %r = and.i64 %a, %b
    OrI64,              // %r = or.i64 %a, %b
    XorI64,             // %r = xor.i64 %a, %b
    ShlI64,             // %r = shl.i64 %a, %b
    ShrI64,             // %r = shr.i64 %a, %b (arithmetic)
    UShrI64,            // %r = ushr.i64 %a, %b (logical)
    NotI64,             // %r = not.i64 %a

    // Comparison
    CmpEqI64,           // %r = cmp.eq.i64 %a, %b
    CmpNeI64,           // %r = cmp.ne.i64 %a, %b
    CmpLtI64,           // %r = cmp.lt.i64 %a, %b
    CmpLeI64,           // %r = cmp.le.i64 %a, %b
    CmpGtI64,           // %r = cmp.gt.i64 %a, %b
    CmpGeI64,           // %r = cmp.ge.i64 %a, %b

    CmpEqF64,           // %r = cmp.eq.f64 %a, %b
    CmpNeF64,           // %r = cmp.ne.f64 %a, %b
    CmpLtF64,           // %r = cmp.lt.f64 %a, %b
    CmpLeF64,           // %r = cmp.le.f64 %a, %b
    CmpGtF64,           // %r = cmp.gt.f64 %a, %b
    CmpGeF64,           // %r = cmp.ge.f64 %a, %b

    CmpEqPtr,           // %r = cmp.eq.ptr %a, %b (pointer equality)
    CmpNePtr,           // %r = cmp.ne.ptr %a, %b

    // Boolean
    LogicalAnd,         // %r = and.bool %a, %b
    LogicalOr,          // %r = or.bool %a, %b
    LogicalNot,         // %r = not.bool %a

    // Type conversions
    CastI64ToF64,       // %r = cast.i64_to_f64 %a
    CastF64ToI64,       // %r = cast.f64_to_i64 %a
    CastBoolToI64,      // %r = cast.bool_to_i64 %a

    // Boxing/Unboxing
    BoxInt,             // %r = box.int %a       (i64 → TsValue*)
    BoxFloat,           // %r = box.float %a     (f64 → TsValue*)
    BoxBool,            // %r = box.bool %a      (bool → TsValue*)
    BoxString,          // %r = box.string %a    (TsString* → TsValue*)
    BoxObject,          // %r = box.object %a    (TsObject* → TsValue*)

    UnboxInt,           // %r = unbox.int %a     (TsValue* → i64)
    UnboxFloat,         // %r = unbox.float %a   (TsValue* → f64)
    UnboxBool,          // %r = unbox.bool %a    (TsValue* → bool)
    UnboxString,        // %r = unbox.string %a  (TsValue* → TsString*)
    UnboxObject,        // %r = unbox.object %a  (TsValue* → TsObject*)

    // Type checking
    TypeCheck,          // %r = typecheck %val, <type>  (→ bool)
    TypeOf,             // %r = typeof %val             (→ string)
    InstanceOf,         // %r = instanceof %val, %ctor  (→ bool)

    // GC Operations (stub to Boehm initially)
    GCAlloc,            // %r = gc.alloc <type>, <size>
    GCAllocArray,       // %r = gc.alloc.array <elem_type>, %len
    GCStore,            // gc.store %ptr, %val (write barrier)
    GCLoad,             // %r = gc.load <type>, %ptr (read barrier)
    Safepoint,          // safepoint (GC can run here)
    SafepointPoll,      // safepoint.poll (conditional check)

    // Memory (low-level, after GC lowering)
    Alloca,             // %r = alloca <type> (stack allocation for locals)
    Load,               // %r = load <type>, %ptr
    Store,              // store %val, %ptr
    GetElementPtr,      // %r = gep %ptr, %idx

    // Objects (high-level)
    NewObject,          // %r = new_object <shape>
    NewObjectDynamic,   // %r = new_object_dynamic
    GetPropStatic,      // %r = get_prop.static %obj, "name", <type>
    GetPropDynamic,     // %r = get_prop.dynamic %obj, %key
    SetPropStatic,      // set_prop.static %obj, "name", %val
    SetPropDynamic,     // set_prop.dynamic %obj, %key, %val
    HasProp,            // %r = has_prop %obj, %key
    DeleteProp,         // %r = delete_prop %obj, %key

    // Arrays (high-level)
    NewArrayBoxed,      // %r = new_array.boxed %len
    NewArrayTyped,      // %r = new_array.typed <elem_type>, %len
    GetElem,            // %r = get_elem %arr, %idx
    SetElem,            // set_elem %arr, %idx, %val
    GetElemTyped,       // %r = get_elem.typed %arr, %idx
    SetElemTyped,       // set_elem.typed %arr, %idx, %val
    ArrayLength,        // %r = array.length %arr
    ArrayPush,          // %r = array.push %arr, %val

    // Calls
    Call,               // %r = call @func(%args...)
    CallMethod,         // %r = call_method %obj, "method", (%args...)
    CallVirtual,        // %r = call_virtual %obj, <vtable_idx>, (%args...)
    CallIndirect,       // %r = call_indirect %fn_ptr, (%args...)

    // Globals
    LoadGlobal,         // %r = load_global "name" (console, Math, JSON, etc.)
    StoreGlobal,        // store_global "name", %val (store to module-scoped global)
    LoadFunction,       // %r = load_function "name" (get function pointer)

    // Closures
    MakeClosure,        // %r = make_closure @funcName, (%v1, %v2, ...) (create closure object)
    LoadCapture,        // %r = load_capture "varName" (load captured variable from closure env)
    StoreCapture,       // store_capture "varName", %val (store to captured variable in closure env)
    LoadCaptureFromClosure,  // %r = load_capture_from_closure %closure, index (load from specific closure)
    StoreCaptureFromClosure, // store_capture_from_closure %closure, index, %val (store to specific closure)

    // Control flow (terminators)
    Branch,             // br %target
    CondBranch,         // condbr %cond, %then, %else
    Switch,             // switch %val, %default, [cases...]
    Return,             // ret %val
    ReturnVoid,         // ret void
    Unreachable,        // unreachable

    // Phi (for SSA)
    Phi,                // %r = phi [%val1, %block1], [%val2, %block2], ...

    // Miscellaneous
    Select,             // %r = select %cond, %true_val, %false_val
    Copy,               // %r = copy %val (for debugging/clarity)

    // Exception handling
    SetupTry,           // %r = setup_try %catchBlock (push handler, call setjmp, returns bool: true=exception, false=normal)
    Throw,              // throw %exception (call ts_throw, does not return)
    GetException,       // %r = get_exception (get current exception from ts_get_exception)
    ClearException,     // clear_exception (call ts_set_exception(nullptr))
    PopHandler,         // pop_handler (call ts_pop_exception_handler)

    // Async/Await
    Await,              // %r = await %promise (await a promise, result is resolved value)
    AsyncReturn,        // async_return %val (resolve promise and return from async function)

    // Generator/Yield
    Yield,              // %r = yield %value (yield a value, returns value sent by next())
    YieldStar,          // %r = yield* %iterable (delegate to another generator/iterable)
};

// Instruction operand
using HIROperand = std::variant<
    std::shared_ptr<HIRValue>,      // SSA value
    int64_t,                        // Integer constant
    double,                         // Float constant
    bool,                           // Boolean constant
    std::string,                    // String constant or name
    HIRBlock*,                      // Block reference (for branches)
    std::shared_ptr<HIRType>        // Type operand
>;

struct HIRInstruction {
    HIROpcode opcode;
    std::shared_ptr<HIRValue> result;   // nullptr for void instructions
    std::vector<HIROperand> operands;

    // For Phi instructions: (value, block) pairs
    std::vector<std::pair<std::shared_ptr<HIRValue>, HIRBlock*>> phiIncoming;

    // For Switch: (case_value, target_block) pairs
    std::vector<std::pair<int64_t, HIRBlock*>> switchCases;
    HIRBlock* switchDefault = nullptr;

    // Escape analysis
    bool escapes = true;            // Conservative default: object escapes function scope
    bool scalarReplaceable = false;  // True if all uses are GetPropStatic/SetPropStatic (SROA)

    // Flat object shape (for NewObjectDynamic with known property names)
    HIRShape* objectShape = nullptr;

    // Metadata
    uint32_t sourceLocation = 0;    // Line number for debugging

    HIRInstruction(HIROpcode op) : opcode(op) {}

    bool isTerminator() const {
        return opcode == HIROpcode::Branch ||
               opcode == HIROpcode::CondBranch ||
               opcode == HIROpcode::Switch ||
               opcode == HIROpcode::Return ||
               opcode == HIROpcode::ReturnVoid ||
               opcode == HIROpcode::Unreachable ||
               opcode == HIROpcode::Throw;
    }

    std::string toString() const;
};

//==============================================================================
// HIR Blocks - Basic blocks with phi nodes as parameters
//==============================================================================

struct HIRBlock {
    std::string label;
    HIRFunction* parent = nullptr;

    // Block parameters (SSA phi nodes represented as block params)
    std::vector<std::shared_ptr<HIRValue>> params;

    // Instructions (last one is terminator)
    std::vector<std::unique_ptr<HIRInstruction>> instructions;

    // Predecessors and successors (computed during construction)
    std::vector<HIRBlock*> predecessors;
    std::vector<HIRBlock*> successors;

    HIRBlock(const std::string& name) : label(name) {}

    HIRInstruction* getTerminator() {
        if (instructions.empty()) return nullptr;
        auto& last = instructions.back();
        return last->isTerminator() ? last.get() : nullptr;
    }

    void addInstruction(std::unique_ptr<HIRInstruction> inst) {
        instructions.push_back(std::move(inst));
    }
};

//==============================================================================
// HIR Functions
//==============================================================================

struct HIRFunction {
    std::string name;
    std::string mangledName;            // For linking
    std::shared_ptr<HIRType> returnType;

    // Parameters with types
    std::vector<std::pair<std::string, std::shared_ptr<HIRType>>> params;

    // Basic blocks (first is entry)
    std::vector<std::unique_ptr<HIRBlock>> blocks;

    // Local variable allocations (for debugging)
    std::map<std::string, std::shared_ptr<HIRType>> locals;

    // Captured variables from enclosing scope (for closures)
    // Each capture has a name and type
    std::vector<std::pair<std::string, std::shared_ptr<HIRType>>> captures;

    // Display name for .name property (e.g., variable name for arrow functions)
    std::string displayName;

    // Metadata
    bool isAsync = false;
    bool isGenerator = false;
    bool isExported = false;
    bool isExtern = false;              // External function (no body)
    bool hasClosure = false;            // True if this function captures variables
    bool hasRestParam = false;          // True if last parameter is a rest parameter (...args)
    size_t restParamIndex = 0;          // Index of the rest parameter (if hasRestParam is true)

    // Source location (for debug info)
    uint32_t sourceLine = 0;            // Line number of function definition
    std::string sourceFile;             // Source file path

    // Value counter for SSA naming
    uint32_t nextValueId = 0;

    HIRFunction(const std::string& name) : name(name), mangledName(name) {}

    HIRBlock* createBlock(const std::string& label) {
        auto block = std::make_unique<HIRBlock>(label);
        block->parent = this;
        HIRBlock* ptr = block.get();
        blocks.push_back(std::move(block));
        return ptr;
    }

    HIRBlock* getEntryBlock() {
        return blocks.empty() ? nullptr : blocks[0].get();
    }

    std::shared_ptr<HIRValue> createValue(std::shared_ptr<HIRType> type, const std::string& name = "") {
        return std::make_shared<HIRValue>(nextValueId++, type, name);
    }
};

//==============================================================================
// HIR Classes and Shapes
//==============================================================================

struct HIRShape {
    uint32_t id = UINT32_MAX;  // UINT32_MAX = unregistered (not in module_->shapes)
    std::string className;

    // Property name → offset mapping
    std::map<std::string, uint32_t> propertyOffsets;

    // Property name → type
    std::map<std::string, std::shared_ptr<HIRType>> propertyTypes;

    // Total size in bytes
    uint32_t size = 0;

    // Parent shape (for inheritance)
    HIRShape* parent = nullptr;
};

struct HIRClass {
    std::string name;
    std::shared_ptr<HIRShape> shape;

    // Methods
    std::map<std::string, HIRFunction*> methods;
    std::map<std::string, HIRFunction*> staticMethods;

    // VTable for virtual dispatch
    std::vector<std::pair<std::string, HIRFunction*>> vtable;

    // Constructor
    HIRFunction* constructor = nullptr;

    // Base class
    HIRClass* baseClass = nullptr;
};

//==============================================================================
// HIR Module - Top-level container
//==============================================================================

struct HIRModule {
    std::string name;
    std::string sourcePath;

    // All functions in this module
    std::vector<std::unique_ptr<HIRFunction>> functions;

    // All classes in this module
    std::vector<std::unique_ptr<HIRClass>> classes;

    // All shapes (shared across classes and object literals)
    std::vector<std::shared_ptr<HIRShape>> shapes;

    // Global variables
    std::map<std::string, std::shared_ptr<HIRType>> globals;

    // String constants (for interning)
    std::vector<std::string> stringConstants;

    // Imported modules
    std::vector<std::string> imports;

    // Exported symbols
    std::set<std::string> exports;

    HIRModule(const std::string& name) : name(name) {}

    HIRFunction* createFunction(const std::string& name) {
        auto fn = std::make_unique<HIRFunction>(name);
        HIRFunction* ptr = fn.get();
        functions.push_back(std::move(fn));
        return ptr;
    }

    HIRClass* createClass(const std::string& name) {
        auto cls = std::make_unique<HIRClass>();
        cls->name = name;
        HIRClass* ptr = cls.get();
        classes.push_back(std::move(cls));
        return ptr;
    }

    HIRShape* createShape(const std::string& className) {
        auto shape = std::make_unique<HIRShape>();
        shape->id = static_cast<uint32_t>(shapes.size());
        shape->className = className;
        HIRShape* ptr = shape.get();
        shapes.push_back(std::move(shape));
        return ptr;
    }

    HIRFunction* getFunction(const std::string& name) {
        for (auto& fn : functions) {
            if (fn->name == name) return fn.get();
        }
        return nullptr;
    }

    HIRClass* getClass(const std::string& name) {
        for (auto& cls : classes) {
            if (cls->name == name) return cls.get();
        }
        return nullptr;
    }
};

} // namespace ts::hir
