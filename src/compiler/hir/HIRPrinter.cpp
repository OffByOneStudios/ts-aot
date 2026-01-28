#include "HIRPrinter.h"
#include <sstream>
#include <iomanip>

namespace ts::hir {

void HIRPrinter::indent() {
    for (int i = 0; i < indentLevel_; ++i) {
        out_ << "    ";
    }
}

void HIRPrinter::newline() {
    out_ << "\n";
}

void HIRPrinter::print(const HIRModule& module) {
    out_ << "; HIR Module: " << module.name;
    if (!module.sourcePath.empty()) {
        out_ << " (from " << module.sourcePath << ")";
    }
    newline();
    newline();

    // Print imports
    if (!module.imports.empty()) {
        out_ << "; Imports:";
        newline();
        for (const auto& imp : module.imports) {
            out_ << ";   " << imp;
            newline();
        }
        newline();
    }

    // Print global variables
    if (!module.globals.empty()) {
        out_ << "; Globals:";
        newline();
        for (const auto& [name, type] : module.globals) {
            out_ << "global @" << name << " : " << type->toString();
            newline();
        }
        newline();
    }

    // Print shapes
    if (!module.shapes.empty()) {
        out_ << "; Shapes:";
        newline();
        for (const auto& shape : module.shapes) {
            out_ << "shape #" << shape->id << " \"" << shape->className << "\" {";
            newline();
            for (const auto& [propName, offset] : shape->propertyOffsets) {
                out_ << "    " << propName << " @ offset " << offset;
                if (shape->propertyTypes.count(propName)) {
                    out_ << " : " << shape->propertyTypes.at(propName)->toString();
                }
                newline();
            }
            out_ << "} ; size = " << shape->size;
            newline();
        }
        newline();
    }

    // Print classes
    for (const auto& cls : module.classes) {
        out_ << "class @" << cls->name;
        if (cls->baseClass) {
            out_ << " extends @" << cls->baseClass->name;
        }
        if (cls->shape) {
            out_ << " shape=#" << cls->shape->id;
        }
        out_ << " {";
        newline();

        // VTable
        if (!cls->vtable.empty()) {
            out_ << "    ; vtable:";
            newline();
            for (size_t i = 0; i < cls->vtable.size(); ++i) {
                out_ << "    ;   [" << i << "] " << cls->vtable[i].first;
                if (cls->vtable[i].second) {
                    out_ << " -> @" << cls->vtable[i].second->name;
                }
                newline();
            }
        }

        // Methods
        for (const auto& [methodName, method] : cls->methods) {
            out_ << "    method " << methodName << " = @" << method->name;
            newline();
        }

        // Static methods
        for (const auto& [methodName, method] : cls->staticMethods) {
            out_ << "    static " << methodName << " = @" << method->name;
            newline();
        }

        out_ << "}";
        newline();
        newline();
    }

    // Print functions
    for (const auto& func : module.functions) {
        printFunction(*func);
        newline();
    }

    // Print exports
    if (!module.exports.empty()) {
        out_ << "; Exports:";
        newline();
        for (const auto& exp : module.exports) {
            out_ << ";   " << exp;
            newline();
        }
    }
}

void HIRPrinter::printFunction(const HIRFunction& func) {
    // Function signature
    if (func.isExtern) {
        out_ << "declare ";
    } else {
        out_ << "define ";
    }

    if (func.isAsync) out_ << "async ";
    if (func.isGenerator) out_ << "generator ";

    out_ << "@" << func.name << "(";

    // Parameters
    for (size_t i = 0; i < func.params.size(); ++i) {
        if (i > 0) out_ << ", ";
        out_ << func.params[i].second->toString() << " %" << func.params[i].first;
    }

    out_ << ") -> " << (func.returnType ? func.returnType->toString() : "void");

    if (func.isExtern) {
        newline();
        return;
    }

    out_ << " {";
    newline();

    // Blocks
    ++indentLevel_;
    for (const auto& block : func.blocks) {
        printBlock(*block);
    }
    --indentLevel_;

    out_ << "}";
    newline();
}

void HIRPrinter::printBlock(const HIRBlock& block) {
    // Block label
    out_ << block.label << ":";

    // Block parameters (phi nodes)
    if (!block.params.empty()) {
        out_ << "(";
        for (size_t i = 0; i < block.params.size(); ++i) {
            if (i > 0) out_ << ", ";
            out_ << block.params[i]->type->toString() << " " << block.params[i]->toString();
        }
        out_ << ")";
    }

    // Predecessors comment
    if (!block.predecessors.empty()) {
        out_ << " ; preds: ";
        for (size_t i = 0; i < block.predecessors.size(); ++i) {
            if (i > 0) out_ << ", ";
            out_ << "%" << block.predecessors[i]->label;
        }
    }

    newline();

    // Instructions
    ++indentLevel_;
    for (const auto& inst : block.instructions) {
        printInstruction(*inst);
    }
    --indentLevel_;
}

void HIRPrinter::printInstruction(const HIRInstruction& inst) {
    indent();
    out_ << inst.toString();
    newline();
}

void HIRPrinter::printType(const HIRType& type) {
    out_ << type.toString();
}

void HIRPrinter::printValue(const HIRValue& value) {
    out_ << value.toString();
}

std::string HIRPrinter::toString(const HIRModule& module) {
    std::ostringstream ss;
    HIRPrinter printer(ss);
    printer.print(module);
    return ss.str();
}

std::string HIRPrinter::toString(const HIRFunction& func) {
    std::ostringstream ss;
    HIRPrinter printer(ss);
    printer.printFunction(func);
    return ss.str();
}

std::string HIRPrinter::toString(const HIRInstruction& inst) {
    return inst.toString();
}

} // namespace ts::hir
