#pragma once

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4244)
#endif
#include <llvm/IR/GCStrategy.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <optional>

namespace ts {

/// Custom GC strategy for ts-aot precise garbage collection.
///
/// Uses LLVM statepoints infrastructure:
/// - Pointers in address space 1 are GC-managed
/// - Pointers in address space 0 are raw (C/libuv/OpenSSL)
/// - RS4GC pass automatically inserts gc.statepoint/gc.relocate
/// - Backend emits .llvm_stackmaps section for runtime stack scanning
class TsAotGC : public llvm::GCStrategy {
public:
    TsAotGC();
    std::optional<bool> isGCManagedPointer(const llvm::Type *Ty) const override;
};

/// Force-link anchor: call from main() to ensure the GC strategy
/// static registration is not stripped by the linker.
void linkTsAotGC();

} // namespace ts
