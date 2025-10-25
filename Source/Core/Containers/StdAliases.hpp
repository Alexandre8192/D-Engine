#pragma once
// ============================================================================
// D-Engine - Core/Containers/StdAliases.hpp
// ----------------------------------------------------------------------------
// Purpose : Provide STL container aliases that automatically wire the engine's
//           AllocatorAdapter so that standard containers allocate from the
//           D-Engine memory system.
// Contract: Header-only, minimal includes (only the containers we alias) and
//           safe to include from header-only code. Requires AllocatorAdapter.
//           Thread-safety and complexity guarantees remain identical to the
//           corresponding standard containers.
// Notes   : Prefer these aliases in engine code to ensure allocations are
//           tracked/guarded (when enabled). Specialized allocators (arena,
//           stack, etc.) should be opted into explicitly at call sites.
//           Including this header pulls standard containers; avoid re-exporting
//           it from frequently-included umbrella headers.
// ============================================================================

#include "Core/Memory/AllocatorAdapter.hpp" // ::dng::core::AllocatorAdapter

#include <deque>
#include <list>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace dng
{
    // -------------------------------------------------------------------------
    // Guidance table (documentation only)
    // -------------------------------------------------------------------------
    // Purpose : Quick reference when choosing an allocation strategy.
    // Notes   : Not compiled; purely informative.
    //
    //   • Transient, scope-tied data .......... -> ArenaAllocator / StackAllocator
    //   • Many sub-1 KiB small objects ........ -> SmallObjectAllocator (via these aliases)
    //   • Long-lived / irregular sizes ........ -> DefaultAllocator (via these aliases)
    //   • External/STL-heavy codebases ........ -> Consider DNG_ROUTE_GLOBAL_NEW=1
    // -------------------------------------------------------------------------

    // ---
    // Purpose : Shorthand for std::pair to improve readability in engine code.
    // Contract: Exactly std::pair semantics.
    // Notes   : Alias only; does not change allocation behavior by itself.
    // ---
    template<class A, class B>
    using pair = std::pair<A, B>;

    // ---
    // Purpose : std::vector replacement using AllocatorAdapter.
    // Contract: Same semantics and thread-safety as std::vector.
    // Notes   : Ensures internal storage participates in tracking/guarding.
    // ---
    template<class T>
    using vector = std::vector<T, ::dng::core::AllocatorAdapter<T>>;

    // ---
    // Purpose : Narrow string (std::string) backed by the engine allocator.
    // Contract: Same semantics as std::string; SBO remains implementation-defined.
    // Notes   : Use for engine-owned text to surface allocations to diagnostics.
    // ---
    using string = std::basic_string<char, std::char_traits<char>, ::dng::core::AllocatorAdapter<char>>;

    // ---
    // Purpose : UTF-8 string alias using the engine allocator.
    // Contract: Mirrors std::u8string behavior.
    // Notes   : Keeps type parity with the standard while routing allocations.
    // ---
    using u8string = std::basic_string<char8_t, std::char_traits<char8_t>, ::dng::core::AllocatorAdapter<char8_t>>;

    // ---
    // Purpose : Hash map with allocator-aware node storage.
    // Contract: Same hashing/equality semantics as std::unordered_map.
    // Notes   : Default Hash/Eq come from the standard library.
    // ---
    template<class Key, class Value, class Hash = std::hash<Key>, class Eq = std::equal_to<Key>>
    using unordered_map = std::unordered_map<Key, Value, Hash, Eq, ::dng::core::AllocatorAdapter<std::pair<const Key, Value>>>;

    // ---
    // Purpose : Hash set routed through the engine allocator.
    // Contract: Identical behavior to std::unordered_set.
    // Notes   : Useful for tracked hash-based membership structures.
    // ---
    template<class Key, class Hash = std::hash<Key>, class Eq = std::equal_to<Key>>
    using unordered_set = std::unordered_set<Key, Hash, Eq, ::dng::core::AllocatorAdapter<Key>>;

    // ---
    // Purpose : Ordered map with allocator-aware node storage.
    // Contract: Same ordering guarantees as std::map.
    // Notes   : Drop-in replacement that exposes allocations to tracking.
    // ---
    template<class Key, class Value, class Compare = std::less<Key>>
    using map = std::map<Key, Value, Compare, ::dng::core::AllocatorAdapter<std::pair<const Key, Value>>>;

    // ---
    // Purpose : Ordered set using the engine allocator.
    // Contract: Same semantics as std::set.
    // Notes   : Node allocations are tracked/guarded via the adapter.
    // ---
    template<class Key, class Compare = std::less<Key>>
    using set = std::set<Key, Compare, ::dng::core::AllocatorAdapter<Key>>;

    // ---
    // Purpose : Deque with allocator-aware internal blocks.
    // Contract: Same amortized guarantees as std::deque.
    // Notes   : Suitable for queue-like structures with tracked allocations.
    // ---
    template<class T>
    using deque = std::deque<T, ::dng::core::AllocatorAdapter<T>>;

    // ---
    // Purpose : Linked list with allocator-aware node storage.
    // Contract: Same semantics as std::list.
    // Notes   : Helps surface leaks in list-heavy code paths.
    // ---
    template<class T>
    using list = std::list<T, ::dng::core::AllocatorAdapter<T>>;

} // namespace dng
