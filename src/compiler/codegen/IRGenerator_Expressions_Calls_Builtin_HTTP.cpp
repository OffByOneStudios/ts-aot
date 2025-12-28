#include "IRGenerator.h"

namespace ts {

bool IRGenerator::tryGenerateHTTPCall(ast::CallExpression* node, ast::PropertyAccessExpression* prop) {
    auto id = dynamic_cast<ast::Identifier*>(prop->expression.get());
    if (!id) return false;

    if (id->name == "http") {
        if (prop->name == "createServer") {
            // TODO: Implement http.createServer
            return true;
        }
    } else if (id->name == "fetch") {
        // TODO: Implement fetch
        return true;
    }

    return false;
}

} // namespace ts
