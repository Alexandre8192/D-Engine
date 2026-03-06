// Compile-only self containment check for MemorySystem.hpp
#include "Core/Memory/MemorySystem.hpp"

namespace
{
    int TouchMemorySystemHeaderOnly()
    {
        ::dng::core::MemoryConfig cfg{};
        (void)::dng::memory::MemorySystem::IsConfigCompatible(cfg);
        return ::dng::memory::MemorySystem::IsInitialized() ? 1 : 0;
    }
}
