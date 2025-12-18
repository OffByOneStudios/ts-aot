#pragma once

#include "../ast/AstNodes.h"
#include "Type.h"
#include <vector>
#include <memory>

namespace ts {

class Analyzer;

/**
 * The AsyncTransformer is responsible for transforming async functions into
 * state machines that can be executed by the runtime.
 * 
 * For now, we will implement a simplified version that works with the IRGenerator
 * to produce the necessary state machine logic.
 */
class AsyncTransformer {
public:
    AsyncTransformer(Analyzer& analyzer);

    // Transforms an async function body into a series of states
    void transform(ast::FunctionDeclaration* node);

private:
    Analyzer& analyzer;
};

} // namespace ts
