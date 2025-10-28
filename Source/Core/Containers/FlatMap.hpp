#pragma once
// ============================================================================
// D-Engine - Core/Containers/FlatMap.hpp
// ----------------------------------------------------------------------------
// Purpose : Provide a cache-friendly associative container implemented as a
//           sorted SmallVector. Ideal for small cardinalities and read-heavy
//           workloads where node-based maps carry too much overhead.
// Contract: Keys remain sorted under the supplied comparator; insertions keep
//           ordering via lower_bound + vector insert. Thread-safety matches the
//           underlying SmallVector (no concurrent writes).
// Notes   : Prefer FlatMap for hot paths with small N where lookups dominate.
//           Falls back to engine allocators once the inline buffer is exceeded.
// ============================================================================

#include "Core/Containers/SmallVector.hpp"
#include "Core/Diagnostics/Check.hpp"

#include <algorithm>  // std::lower_bound
#include <functional> // std::less
#include <utility>    // std::forward

namespace dng::core
{
    template <class Key,
              class Value,
              std::size_t InlineCapacity = 16,
              class Compare = std::less<Key>>
    class FlatMap
    {
    public:
        using value_type      = std::pair<Key, Value>;
        using storage_type    = SmallVector<value_type, InlineCapacity>;
        using size_type       = typename storage_type::size_type;
        using iterator        = typename storage_type::iterator;
        using const_iterator  = typename storage_type::const_iterator;
        using reference       = value_type&;
        using const_reference = const value_type&;

        // ---
        // Purpose : Construct an empty flat map with optional comparator/allocator.
        // Contract: Comparator must impose strict weak ordering compatible with Key.
        // Notes   : Storage inherits allocator behaviour from SmallVector.
        // ---
        explicit FlatMap(const Compare& comp = Compare(), const typename storage_type::allocator_type& alloc = {})
            : m_storage(alloc)
            , m_compare(comp)
        {
        }

        [[nodiscard]] size_type size() const noexcept { return m_storage.size(); }
        [[nodiscard]] bool empty() const noexcept { return m_storage.empty(); }
        [[nodiscard]] size_type capacity() const noexcept { return m_storage.capacity(); }

        void reserve(size_type n) { m_storage.reserve(n); }
        void clear() noexcept { m_storage.clear(); }

        [[nodiscard]] iterator begin() noexcept { return m_storage.begin(); }
        [[nodiscard]] const_iterator begin() const noexcept { return m_storage.begin(); }
        [[nodiscard]] iterator end() noexcept { return m_storage.end(); }
        [[nodiscard]] const_iterator end() const noexcept { return m_storage.end(); }

        [[nodiscard]] iterator find(const Key& key) noexcept
        {
            iterator it = lower_bound(key);
            if (it != end() && !IsLess(key, it->first) && !IsLess(it->first, key))
            {
                return it;
            }
            return end();
        }

        [[nodiscard]] const_iterator find(const Key& key) const noexcept
        {
            const_iterator it = lower_bound(key);
            if (it != end() && !IsLess(key, it->first) && !IsLess(it->first, key))
            {
                return it;
            }
            return end();
        }

        [[nodiscard]] bool contains(const Key& key) const noexcept
        {
            return find(key) != end();
        }

        [[nodiscard]] iterator lower_bound(const Key& key) noexcept
        {
            return std::lower_bound(m_storage.begin(), m_storage.end(), key,
                [this](const value_type& element, const Key& probe) { return m_compare(element.first, probe); });
        }

        [[nodiscard]] const_iterator lower_bound(const Key& key) const noexcept
        {
            return std::lower_bound(m_storage.begin(), m_storage.end(), key,
                [this](const value_type& element, const Key& probe) { return m_compare(element.first, probe); });
        }

        template <class ValueLike>
        std::pair<iterator, bool> insert_or_assign(const Key& key, ValueLike&& value)
        {
            iterator it = lower_bound(key);
            if (it != end() && !IsLess(key, it->first) && !IsLess(it->first, key))
            {
                it->second = std::forward<ValueLike>(value);
                return { it, false };
            }

            value_type entry{ key, std::forward<ValueLike>(value) };
            const size_type index = static_cast<size_type>(it - begin());
            iterator inserted = m_storage.insert(it, std::move(entry));
            return { inserted, true };
        }

        template <class ValueLike>
        std::pair<iterator, bool> insert_or_assign(Key&& key, ValueLike&& value)
        {
            const Key& probe = key;
            iterator it = lower_bound(probe);
            if (it != end() && !IsLess(probe, it->first) && !IsLess(it->first, probe))
            {
                it->second = std::forward<ValueLike>(value);
                return { it, false };
            }

            value_type entry{ std::move(key), std::forward<ValueLike>(value) };
            const size_type index = static_cast<size_type>(it - begin());
            iterator inserted = m_storage.insert(it, std::move(entry));
            return { inserted, true };
        }

        std::pair<iterator, bool> insert(const value_type& v)
        {
            return insert_or_assign(v.first, v.second);
        }

        std::pair<iterator, bool> insert(value_type&& v)
        {
            return insert_or_assign(std::move(v.first), std::move(v.second));
        }

        [[nodiscard]] Value& operator[](const Key& key)
        {
            auto [it, inserted] = insert_or_assign(key, Value{});
            return it->second;
        }

        Value& at(const Key& key)
        {
            iterator it = find(key);
            DNG_CHECK(it != end());
            return it->second;
        }

        const Value& at(const Key& key) const
        {
            const_iterator it = find(key);
            DNG_CHECK(it != end());
            return it->second;
        }

        size_type erase(const Key& key)
        {
            iterator it = find(key);
            if (it == end())
            {
                return 0;
            }
            m_storage.erase(it);
            return 1;
        }

        iterator erase(const_iterator pos)
        {
            const size_type index = static_cast<size_type>(pos - begin());
            return m_storage.erase(begin() + index);
        }

    private:
        [[nodiscard]] bool IsLess(const Key& lhs, const Key& rhs) const noexcept
        {
            return m_compare(lhs, rhs);
        }

        storage_type m_storage;
        Compare      m_compare;
    };

} // namespace dng::core
