// ============================================================================
// Purpose : Build-only smoke test ensuring the global new/delete override
//           compiles, links, and routes allocations through the engine.
// Contract: Defines DNG_ROUTE_GLOBAL_NEW prior to including GlobalNewDelete so
//           the operators are emitted. The translation unit intentionally keeps
//           runtime interaction minimal (new/delete) to verify linkage.
// Notes   : Success criteria is simply that the TU links without multiple
//           definition errors and the basic allocations execute without crash.
// ============================================================================

#define DNG_ROUTE_GLOBAL_NEW 1

#include "Core/Memory/GlobalNewDelete.hpp"
#include "Core/Memory/MemorySystem.hpp"

int main()
{
    ::dng::memory::MemorySystem::Init();

    int* p = new int(42);
    delete p;

    void* raw = ::operator new(64);
    ::operator delete(raw);

    ::dng::memory::MemorySystem::Shutdown();
    return 0;
}
