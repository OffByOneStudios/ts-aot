#include "Analyzer.h"

namespace ts {

void Analyzer::registerTTY() {
    auto ttyType = std::make_shared<ObjectType>();

    // =========================================================================
    // ReadStream class - tty.ReadStream
    // =========================================================================
    auto readStreamClass = std::make_shared<ClassType>("TTYReadStream");

    // Properties
    readStreamClass->fields["isTTY"] = std::make_shared<Type>(TypeKind::Boolean);
    readStreamClass->fields["isRaw"] = std::make_shared<Type>(TypeKind::Boolean);

    // setRawMode(mode: boolean): this
    auto setRawModeMethod = std::make_shared<FunctionType>();
    setRawModeMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Boolean));
    setRawModeMethod->returnType = readStreamClass;
    readStreamClass->fields["setRawMode"] = setRawModeMethod;

    symbols.defineType("TTYReadStream", readStreamClass);

    // =========================================================================
    // WriteStream class - tty.WriteStream
    // =========================================================================
    auto writeStreamClass = std::make_shared<ClassType>("TTYWriteStream");

    // Properties
    writeStreamClass->fields["isTTY"] = std::make_shared<Type>(TypeKind::Boolean);
    writeStreamClass->fields["columns"] = std::make_shared<Type>(TypeKind::Int);
    writeStreamClass->fields["rows"] = std::make_shared<Type>(TypeKind::Int);

    // getWindowSize(): [number, number]
    auto getWindowSizeMethod = std::make_shared<FunctionType>();
    getWindowSizeMethod->returnType = std::make_shared<ArrayType>(std::make_shared<Type>(TypeKind::Int));
    writeStreamClass->fields["getWindowSize"] = getWindowSizeMethod;

    // clearLine(dir: number): boolean
    auto clearLineMethod = std::make_shared<FunctionType>();
    clearLineMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int));
    clearLineMethod->returnType = std::make_shared<Type>(TypeKind::Boolean);
    writeStreamClass->fields["clearLine"] = clearLineMethod;

    // clearScreenDown(): boolean
    auto clearScreenDownMethod = std::make_shared<FunctionType>();
    clearScreenDownMethod->returnType = std::make_shared<Type>(TypeKind::Boolean);
    writeStreamClass->fields["clearScreenDown"] = clearScreenDownMethod;

    // cursorTo(x: number, y?: number): boolean
    auto cursorToMethod = std::make_shared<FunctionType>();
    cursorToMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int));
    cursorToMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int));  // optional y
    cursorToMethod->returnType = std::make_shared<Type>(TypeKind::Boolean);
    writeStreamClass->fields["cursorTo"] = cursorToMethod;

    // moveCursor(dx: number, dy: number): boolean
    auto moveCursorMethod = std::make_shared<FunctionType>();
    moveCursorMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int));
    moveCursorMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int));
    moveCursorMethod->returnType = std::make_shared<Type>(TypeKind::Boolean);
    writeStreamClass->fields["moveCursor"] = moveCursorMethod;

    // getColorDepth(): number
    auto getColorDepthMethod = std::make_shared<FunctionType>();
    getColorDepthMethod->returnType = std::make_shared<Type>(TypeKind::Int);
    writeStreamClass->fields["getColorDepth"] = getColorDepthMethod;

    // hasColors(count?: number): boolean
    auto hasColorsMethod = std::make_shared<FunctionType>();
    hasColorsMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int));  // optional count
    hasColorsMethod->returnType = std::make_shared<Type>(TypeKind::Boolean);
    writeStreamClass->fields["hasColors"] = hasColorsMethod;

    // write(data: string | Buffer): boolean
    auto writeMethod = std::make_shared<FunctionType>();
    writeMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    writeMethod->returnType = std::make_shared<Type>(TypeKind::Boolean);
    writeStreamClass->fields["write"] = writeMethod;

    symbols.defineType("TTYWriteStream", writeStreamClass);

    // =========================================================================
    // tty.isatty(fd: number): boolean
    // =========================================================================
    auto isattyType = std::make_shared<FunctionType>();
    isattyType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int));
    isattyType->returnType = std::make_shared<Type>(TypeKind::Boolean);
    ttyType->fields["isatty"] = isattyType;

    // =========================================================================
    // tty.ReadStream constructor - new tty.ReadStream(fd)
    // =========================================================================
    auto readStreamStaticObj = std::make_shared<ObjectType>();
    readStreamStaticObj->fields["prototype"] = readStreamClass;
    ttyType->fields["ReadStream"] = readStreamStaticObj;

    // =========================================================================
    // tty.WriteStream constructor - new tty.WriteStream(fd)
    // =========================================================================
    auto writeStreamStaticObj = std::make_shared<ObjectType>();
    writeStreamStaticObj->fields["prototype"] = writeStreamClass;
    ttyType->fields["WriteStream"] = writeStreamStaticObj;

    symbols.define("tty", ttyType);
}

} // namespace ts
