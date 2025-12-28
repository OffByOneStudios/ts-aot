#include "IRGenerator.h"

namespace ts {

bool IRGenerator::tryGenerateNetCall(ast::CallExpression* node, ast::PropertyAccessExpression* prop) {
    auto id = dynamic_cast<ast::Identifier*>(prop->expression.get());
    if (!id || id->name != "net") return false;

    if (prop->name == "createServer") {
        // TODO: Implement net.createServer
        return true;
    }

    return false;
}

} // namespace ts
