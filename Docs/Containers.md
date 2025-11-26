# D-Engine Containers Module

## Overview
The `Source/Core/Containers` module provides memory-aware containers that integrate with the D-Engine memory system.

> **Golden Rule**: Avoid using raw STL containers (`std::vector`, `std::map`) in public APIs. Use D-Engine aliases or custom containers to ensure allocations are tracked and guarded.

## Standard Aliases (`StdAliases.hpp`)

We provide aliases for standard containers that automatically wire up the `AllocatorAdapter`. This ensures that even "standard" containers use the engine's memory backends.

| Alias | Underlying Type | Notes |
| :--- | :--- | :--- |
| `dng::vector<T>` | `std::vector<T, Adapter<T>>` | Drop-in replacement for `std::vector`. |
| `dng::string` | `std::basic_string<char, ...>` | Allocator-aware string. |
| `dng::map<K, V>` | `std::map<K, V, ...>` | Node allocations are tracked. |
| `dng::unordered_map` | `std::unordered_map<...>` | Bucket and node allocations are tracked. |

**Usage:**
```cpp
#include "Core/Containers/StdAliases.hpp"

// Allocates from the default engine allocator (tracked!)
dng::vector<int> myNumbers;
myNumbers.push_back(42);
```

## Custom Containers

### `SmallVector<T, N>`
A vector optimized for small element counts. It stores the first `N` elements inline (on the stack or inside the parent object) to avoid heap allocation.

*   **Use Case**: Hot paths, temporary lists, or when the number of elements is usually small (e.g., `< 16`).
*   **Fallback**: If size exceeds `N`, it allocates from the heap using `AllocatorAdapter`.
*   **Performance**: Drastically reduces allocator pressure for common cases.

```cpp
#include "Core/Containers/SmallVector.hpp"

// Stores 16 integers inline. No heap alloc unless we push 17+ items.
dng::core::SmallVector<int, 16> scratchPad;
```

### `FlatMap<K, V, N>`
An associative container implemented as a sorted `SmallVector`.

*   **Use Case**: Small maps (N < 64) where read performance is critical.
*   **Complexity**: O(log N) lookup (binary search), O(N) insertion/removal.
*   **Memory**: Cache-friendly (contiguous memory), no node overhead.

```cpp
#include "Core/Containers/FlatMap.hpp"

// A small lookup table for resource IDs
dng::core::FlatMap<int, ResourceHandle, 8> resourceCache;
```

## Best Practices

1.  **Prefer `SmallVector`** over `dng::vector` for local variables and small member arrays.
2.  **Prefer `FlatMap`** over `dng::map` for small lookups.
3.  **Always use `dng::` aliases** instead of `std::` when you need a full-featured standard container.
4.  **Avoid `std::shared_ptr`** in containers; prefer unique ownership or handles.
