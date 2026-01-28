#pragma once

#include "HIR.h"
#include <ostream>
#include <string>

namespace ts::hir {

/**
 * HIRPrinter - Pretty prints HIR to text format
 *
 * Output format is similar to LLVM IR but with our custom opcodes.
 *
 * Example output:
 *   module "main.ts"
 *
 *   function @user_main() -> i64 {
 *   entry:
 *       %0 = const.i64 42
 *       %1 = const.i64 10
 *       %2 = add.i64 %0, %1
 *       ret %2
 *   }
 */
class HIRPrinter {
public:
    explicit HIRPrinter(std::ostream& out) : out_(out) {}

    void print(const HIRModule& module);
    void printFunction(const HIRFunction& func);
    void printBlock(const HIRBlock& block);
    void printInstruction(const HIRInstruction& inst);
    void printType(const HIRType& type);
    void printValue(const HIRValue& value);

    // Print to string
    static std::string toString(const HIRModule& module);
    static std::string toString(const HIRFunction& func);
    static std::string toString(const HIRInstruction& inst);

private:
    std::ostream& out_;
    int indentLevel_ = 0;

    void indent();
    void newline();
};

} // namespace ts::hir
