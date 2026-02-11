#include "TsAotGC.h"

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4244)
#endif
#include <llvm/IR/DerivedTypes.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

using namespace llvm;

namespace ts {

TsAotGC::TsAotGC() {
    UseStatepoints = true;
    UseRS4GC = true;
}

std::optional<bool> TsAotGC::isGCManagedPointer(const Type *Ty) const {
    if (auto *PT = dyn_cast<PointerType>(Ty))
        return PT->getAddressSpace() == 1;
    return std::nullopt;
}

void linkTsAotGC() {
    // This function exists solely to force the linker to include this
    // translation unit. The static GCRegistry::Add below will then
    // execute its constructor and register the GC strategy.
}

} // namespace ts

// Register the GC strategy with LLVM's GC registry.
// Functions with gc "ts-aot-gc" attribute will use this strategy.
static GCRegistry::Add<ts::TsAotGC> X("ts-aot-gc", "ts-aot precise GC with statepoints");
