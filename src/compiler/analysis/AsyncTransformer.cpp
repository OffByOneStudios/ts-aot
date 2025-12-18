#include "AsyncTransformer.h"
#include "Analyzer.h"

namespace ts {

AsyncTransformer::AsyncTransformer(Analyzer& analyzer) : analyzer(analyzer) {}

void AsyncTransformer::transform(ast::FunctionDeclaration* node) {
    if (!node->isAsync) return;
    
    // TODO: Implement state machine transformation
}

} // namespace ts
