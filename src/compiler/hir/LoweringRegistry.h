#pragma once

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/DerivedTypes.h>
#include <functional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace hir {

/// How to convert an HIR value to LLVM IR
enum class ArgConversion {
    None,           // Pass through as-is
    Box,            // Box to TsValue*
    Unbox,          // Unbox from TsValue*
    ToI64,          // Convert to i64
    ToF64,          // Convert to f64
    ToI32,          // Convert to i32
    ToBool,         // Convert to i1
    PtrToInt,       // Convert ptr to i64
    IntToPtr,       // Convert i64 to ptr
};

/// How to handle the return value
enum class ReturnHandling {
    Void,           // No return value (return null ptr)
    Raw,            // Return as-is
    Boxed,          // Return is already boxed TsValue*
    BoxResult,      // Box the raw result before returning
};

/// How to handle variadic/rest parameters
enum class VariadicHandling {
    None,           // Fixed args, no special handling
    PackArray,      // Pack rest args into TsArray before call
    TypeDispatch,   // Emit type-specific calls (ts_console_log_int, etc.)
    Inline          // Handled inline in HIRToLLVM (Math.max, etc.)
};

/// Type factory for LLVM types
using LLVMTypeFactory = std::function<llvm::Type*(llvm::LLVMContext&)>;

/// Specification for lowering a single runtime call
struct LoweringSpec {
    std::string runtimeFuncName;        // e.g., "ts_console_log"

    // LLVM return type (nullptr factory for void)
    LLVMTypeFactory returnType;

    // LLVM argument types
    std::vector<LLVMTypeFactory> argTypes;

    // How to convert each argument
    std::vector<ArgConversion> argConversions;

    // How to handle return value
    ReturnHandling returnHandling = ReturnHandling::Raw;

    // Whether function is variadic (LLVM-level varargs)
    bool isVariadic = false;

    // How to handle variadic/rest parameters at HIR level
    VariadicHandling variadicHandling = VariadicHandling::None;

    // Index where rest parameters start (SIZE_MAX if none)
    size_t restParamIndex = SIZE_MAX;

    // Type dispatch configuration (for VariadicHandling::TypeDispatch)
    // Suffixes like "_int", "_double", "_string", "_bool", "_object"
    std::vector<std::string> typeDispatchSuffixes;

    // Default suffix when no specific type matches (usually "_object")
    std::string defaultSuffix = "_object";
};

/// Builder pattern for creating LoweringSpec
class LoweringSpecBuilder {
public:
    explicit LoweringSpecBuilder(const std::string& runtimeFunc)
        : spec_{} {
        spec_.runtimeFuncName = runtimeFunc;
    }

    LoweringSpecBuilder& returns(LLVMTypeFactory type) {
        spec_.returnType = std::move(type);
        return *this;
    }

    LoweringSpecBuilder& returnsVoid() {
        spec_.returnType = nullptr;
        spec_.returnHandling = ReturnHandling::Void;
        return *this;
    }

    LoweringSpecBuilder& returnsPtr() {
        spec_.returnType = [](llvm::LLVMContext& ctx) {
            return llvm::PointerType::get(ctx, 0);
        };
        return *this;
    }

    LoweringSpecBuilder& returnsI64() {
        spec_.returnType = [](llvm::LLVMContext& ctx) {
            return llvm::Type::getInt64Ty(ctx);
        };
        return *this;
    }

    LoweringSpecBuilder& returnsF64() {
        spec_.returnType = [](llvm::LLVMContext& ctx) {
            return llvm::Type::getDoubleTy(ctx);
        };
        return *this;
    }

    LoweringSpecBuilder& returnsBool() {
        spec_.returnType = [](llvm::LLVMContext& ctx) {
            return llvm::Type::getInt1Ty(ctx);
        };
        return *this;
    }

    LoweringSpecBuilder& returnsBoxed() {
        spec_.returnType = [](llvm::LLVMContext& ctx) {
            return llvm::PointerType::get(ctx, 0);
        };
        spec_.returnHandling = ReturnHandling::Boxed;
        return *this;
    }

    LoweringSpecBuilder& arg(LLVMTypeFactory type,
                             ArgConversion conv = ArgConversion::None) {
        spec_.argTypes.push_back(std::move(type));
        spec_.argConversions.push_back(conv);
        return *this;
    }

    LoweringSpecBuilder& ptrArg(ArgConversion conv = ArgConversion::None) {
        return arg([](llvm::LLVMContext& ctx) {
            return llvm::PointerType::get(ctx, 0);
        }, conv);
    }

    LoweringSpecBuilder& boxedArg() {
        return ptrArg(ArgConversion::Box);
    }

    LoweringSpecBuilder& i64Arg(ArgConversion conv = ArgConversion::None) {
        return arg([](llvm::LLVMContext& ctx) {
            return llvm::Type::getInt64Ty(ctx);
        }, conv);
    }

    LoweringSpecBuilder& i32Arg(ArgConversion conv = ArgConversion::None) {
        return arg([](llvm::LLVMContext& ctx) {
            return llvm::Type::getInt32Ty(ctx);
        }, conv);
    }

    LoweringSpecBuilder& f64Arg(ArgConversion conv = ArgConversion::None) {
        return arg([](llvm::LLVMContext& ctx) {
            return llvm::Type::getDoubleTy(ctx);
        }, conv);
    }

    LoweringSpecBuilder& boolArg(ArgConversion conv = ArgConversion::None) {
        return arg([](llvm::LLVMContext& ctx) {
            return llvm::Type::getInt1Ty(ctx);
        }, conv);
    }

    LoweringSpecBuilder& variadic() {
        spec_.isVariadic = true;
        return *this;
    }

    LoweringSpecBuilder& variadicHandling(VariadicHandling handling, size_t restIndex = 0) {
        spec_.variadicHandling = handling;
        spec_.restParamIndex = restIndex;
        return *this;
    }

    LoweringSpecBuilder& typeDispatchSuffixes(std::initializer_list<std::string> suffixes) {
        spec_.typeDispatchSuffixes = suffixes;
        return *this;
    }

    LoweringSpecBuilder& defaultSuffix(const std::string& suffix) {
        spec_.defaultSuffix = suffix;
        return *this;
    }

    LoweringSpec build() { return std::move(spec_); }

private:
    LoweringSpec spec_;
};

/// Convenience function to start building a LoweringSpec
inline LoweringSpecBuilder lowering(const std::string& runtimeFunc) {
    return LoweringSpecBuilder(runtimeFunc);
}

/// Central registry mapping HIR function names to their LLVM lowering specifications
class LoweringRegistry {
public:
    /// Get the singleton instance
    static LoweringRegistry& instance();

    /// Register a lowering spec for a runtime function
    void registerLowering(const std::string& hirFuncName, LoweringSpec spec);

    /// Look up lowering spec (returns nullptr if not found)
    const LoweringSpec* lookup(const std::string& hirFuncName) const;

    /// Check if a function has a registered lowering
    bool hasLowering(const std::string& hirFuncName) const;

    /// Initialize with all builtin lowerings
    static void registerBuiltins();

    /// Register lowerings from extension contracts
    void registerFromExtensions();

    /// Clear all registrations (for testing)
    void clear();

private:
    LoweringRegistry() = default;
    void registerBuiltinsImpl();  // Internal implementation to avoid recursion
    std::unordered_map<std::string, LoweringSpec> registry_;
    static bool builtinsRegistered_;
    static bool extensionsRegistered_;
};

} // namespace hir
