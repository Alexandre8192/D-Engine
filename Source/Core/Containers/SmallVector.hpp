#pragma once
// ============================================================================
// D-Engine - Core/Containers/SmallVector.hpp
// ----------------------------------------------------------------------------
// Purpose : Provide a small-buffer-optimised sequence container with stack
//           storage for the first `InlineCapacity` elements and automatic
//           fallback to the engine allocator via AllocatorAdapter once the
//           inline buffer is exhausted.
// Contract: Elements are stored contiguously and maintain insertion order.
//           Calls that grow past the inline capacity allocate using the
//           supplied allocator (AllocatorAdapter by default). Thread-safety
//           mirrors std::vector (none for concurrent writes).
// Notes   : Designed for hot paths where short sequences dominate. Supports
//           move-only element types; copying requires T to be copy-constructible.
//           All alignment math delegates to the allocator and placement new.
// ============================================================================

#include "Core/Diagnostics/Check.hpp"
#include "Core/Memory/AllocatorAdapter.hpp"

#include <algorithm>   // std::max
#include <cstddef>     // std::size_t
#include <initializer_list>
#include <memory>      // std::destroy_at
#include <type_traits> // std::is_* traits
#include <utility>     // std::move, std::forward, std::exchange

namespace dng::core
{
    template <class T, std::size_t InlineCapacity, class Allocator = AllocatorAdapter<T>>
    class SmallVector
    {
    public:
        static_assert(InlineCapacity > 0, "SmallVector requires InlineCapacity > 0");
        static_assert(std::is_move_constructible_v<T>, "SmallVector requires move-constructible T");

        using value_type             = T;
        using size_type              = std::size_t;
        using difference_type        = std::ptrdiff_t;
        using reference              = value_type&;
        using const_reference        = const value_type&;
        using pointer                = value_type*;
        using const_pointer          = const value_type*;
        using iterator               = value_type*;
        using const_iterator         = const value_type*;
        using allocator_type         = Allocator;

        // ---
        // Purpose : Construct an empty vector bound to the provided allocator.
        // Contract: Allocator must outlive the vector when dynamic storage is used.
        // Notes   : Inline buffer is active until size exceeds InlineCapacity.
        // ---
        explicit SmallVector(const Allocator& alloc = Allocator()) noexcept
            : m_allocator(alloc)
            , m_data(InlineStorage())
            , m_size(0)
            , m_capacity(InlineCapacity)
        {
        }

        SmallVector(const SmallVector& other)
            : m_allocator(other.m_allocator)
            , m_data(InlineStorage())
            , m_size(0)
            , m_capacity(InlineCapacity)
        {
            CopyFrom(other);
        }

        SmallVector(SmallVector&& other) noexcept(std::is_nothrow_move_constructible_v<T>)
            : m_allocator(std::move(other.m_allocator))
            , m_data(InlineStorage())
            , m_size(0)
            , m_capacity(InlineCapacity)
        {
            MoveFrom(std::move(other));
        }

        SmallVector(std::initializer_list<T> init, const Allocator& alloc = Allocator())
            : m_allocator(alloc)
            , m_data(InlineStorage())
            , m_size(0)
            , m_capacity(InlineCapacity)
        {
            ReserveFor(init.size());
            for (const T& value : init)
            {
                EmplaceBackInternal(value);
            }
        }

        ~SmallVector() noexcept
        {
            DestroyRange(m_data, m_size);
            ReleaseHeapStorage();
        }

        SmallVector& operator=(const SmallVector& other)
        {
            if (this == &other)
            {
                return *this;
            }

            if constexpr (!std::is_copy_constructible_v<T>)
            {
                DNG_CHECK(false && "SmallVector copy requires copy-constructible T");
                return *this;
            }

            DestroyRange(m_data, m_size);
            if (!UsingInline())
            {
                m_allocator.deallocate(m_data, m_capacity);
                m_data = InlineStorage();
                m_capacity = InlineCapacity;
            }

            m_size = 0;
            m_allocator = other.m_allocator;
            ReserveFor(other.m_size);
            for (size_type i = 0; i < other.m_size; ++i)
            {
                new (m_data + i) T(other.m_data[i]);
            }
            m_size = other.m_size;
            return *this;
        }

        SmallVector& operator=(SmallVector&& other) noexcept(std::is_nothrow_move_assignable_v<T>)
        {
            if (this == &other)
            {
                return *this;
            }

            DestroyRange(m_data, m_size);
            if (!UsingInline())
            {
                m_allocator.deallocate(m_data, m_capacity);
            }

            m_data = InlineStorage();
            m_capacity = InlineCapacity;
            m_size = 0;
            m_allocator = std::move(other.m_allocator);

            if (other.UsingInline())
            {
                ReserveFor(other.m_size);
                for (size_type i = 0; i < other.m_size; ++i)
                {
                    new (m_data + i) T(std::move(other.m_data[i]));
                }
                m_size = other.m_size;
                other.DestroyRange(other.m_data, other.m_size);
            }
            else
            {
                m_data = other.m_data;
                m_capacity = other.m_capacity;
                m_size = other.m_size;
                other.m_data = other.InlineStorage();
                other.m_capacity = InlineCapacity;
            }

            other.m_size = 0;
            return *this;
        }

        SmallVector& operator=(std::initializer_list<T> init)
        {
            DestroyRange(m_data, m_size);
            if (!UsingInline())
            {
                m_allocator.deallocate(m_data, m_capacity);
                m_data = InlineStorage();
                m_capacity = InlineCapacity;
            }

            m_size = 0;
            ReserveFor(init.size());
            for (const T& value : init)
            {
                new (m_data + m_size) T(value);
                ++m_size;
            }
            return *this;
        }

        [[nodiscard]] allocator_type get_allocator() const noexcept { return m_allocator; }

        // Capacity ----------------------------------------------------------
        [[nodiscard]] size_type size() const noexcept { return m_size; }
        [[nodiscard]] size_type capacity() const noexcept { return m_capacity; }
        [[nodiscard]] static constexpr size_type inline_capacity() noexcept { return InlineCapacity; }
        [[nodiscard]] bool empty() const noexcept { return m_size == 0; }

        void reserve(size_type newCapacity)
        {
            if (newCapacity > m_capacity)
            {
                Grow(newCapacity);
            }
        }

        void clear() noexcept
        {
            DestroyRange(m_data, m_size);
            m_size = 0;
        }

        void shrink_to_fit()
        {
            if (UsingInline() || m_size == 0)
            {
                return;
            }

            if (m_size <= InlineCapacity)
            {
                pointer inlinePtr = InlineStorage();
                MoveElements(m_data, inlinePtr, m_size);
                m_allocator.deallocate(m_data, m_capacity);
                m_data = inlinePtr;
                m_capacity = InlineCapacity;
                return;
            }

            Grow(m_size);
        }

        // Element access ----------------------------------------------------
        [[nodiscard]] reference operator[](size_type index) noexcept
        {
            DNG_CHECK(index < m_size);
            return m_data[index];
        }

        [[nodiscard]] const_reference operator[](size_type index) const noexcept
        {
            DNG_CHECK(index < m_size);
            return m_data[index];
        }

        [[nodiscard]] reference front() noexcept
        {
            DNG_CHECK(m_size > 0);
            return m_data[0];
        }

        [[nodiscard]] const_reference front() const noexcept
        {
            DNG_CHECK(m_size > 0);
            return m_data[0];
        }

        [[nodiscard]] reference back() noexcept
        {
            DNG_CHECK(m_size > 0);
            return m_data[m_size - 1];
        }

        [[nodiscard]] const_reference back() const noexcept
        {
            DNG_CHECK(m_size > 0);
            return m_data[m_size - 1];
        }

        [[nodiscard]] pointer data() noexcept { return m_data; }
        [[nodiscard]] const_pointer data() const noexcept { return m_data; }

        // Iteration ---------------------------------------------------------
        [[nodiscard]] iterator begin() noexcept { return m_data; }
        [[nodiscard]] const_iterator begin() const noexcept { return m_data; }
        [[nodiscard]] const_iterator cbegin() const noexcept { return m_data; }

        [[nodiscard]] iterator end() noexcept { return m_data + m_size; }
        [[nodiscard]] const_iterator end() const noexcept { return m_data + m_size; }
        [[nodiscard]] const_iterator cend() const noexcept { return m_data + m_size; }

        // Modifiers ---------------------------------------------------------
        template <class... Args>
        reference emplace_back(Args&&... args)
        {
            return EmplaceBackInternal(std::forward<Args>(args)...);
        }

        void push_back(const T& value)
        {
            EmplaceBackInternal(value);
        }

        void push_back(T&& value)
        {
            EmplaceBackInternal(std::move(value));
        }

        void pop_back() noexcept
        {
            DNG_CHECK(m_size > 0);
            if (m_size == 0)
            {
                return;
            }
            --m_size;
            std::destroy_at(m_data + m_size);
        }

        iterator insert(const_iterator pos, const T& value)
        {
            const size_type index = static_cast<size_type>(pos - m_data);
            DNG_CHECK(index <= m_size);
            return InsertImpl(index, value);
        }

        iterator insert(const_iterator pos, T&& value)
        {
            const size_type index = static_cast<size_type>(pos - m_data);
            DNG_CHECK(index <= m_size);
            return InsertImpl(index, std::move(value));
        }

        iterator erase(const_iterator pos)
        {
            const size_type index = static_cast<size_type>(pos - m_data);
            DNG_CHECK(index < m_size);

            for (size_type i = index; (i + 1) < m_size; ++i)
            {
                m_data[i] = std::move(m_data[i + 1]);
            }

            --m_size;
            std::destroy_at(m_data + m_size);
            return m_data + index;
        }

        iterator erase(const_iterator first, const_iterator last)
        {
            const size_type start = static_cast<size_type>(first - m_data);
            const size_type finish = static_cast<size_type>(last - m_data);
            DNG_CHECK(start <= finish && finish <= m_size);

            const size_type count = finish - start;
            if (count == 0)
            {
                return m_data + start;
            }

            for (size_type i = start; i + count < m_size; ++i)
            {
                m_data[i] = std::move(m_data[i + count]);
            }

            for (size_type i = 0; i < count; ++i)
            {
                std::destroy_at(m_data + (m_size - 1 - i));
            }

            m_size -= count;
            return m_data + start;
        }

        void resize(size_type newSize)
        {
            if (newSize < m_size)
            {
                for (size_type i = newSize; i < m_size; ++i)
                {
                    std::destroy_at(m_data + i);
                }
                m_size = newSize;
                return;
            }

            ReserveFor(newSize);
            while (m_size < newSize)
            {
                EmplaceBackInternal();
            }
        }

        void resize(size_type newSize, const T& value)
        {
            if (newSize < m_size)
            {
                resize(newSize);
                return;
            }

            ReserveFor(newSize);
            while (m_size < newSize)
            {
                EmplaceBackInternal(value);
            }
        }

    private:
        [[nodiscard]] pointer InlineStorage() noexcept
        {
            return reinterpret_cast<pointer>(m_inline);
        }

        [[nodiscard]] const_pointer InlineStorage() const noexcept
        {
            return reinterpret_cast<const_pointer>(m_inline);
        }

        [[nodiscard]] bool UsingInline() const noexcept
        {
            return m_data == InlineStorage();
        }

        void ReserveFor(size_type desired)
        {
            if (desired > m_capacity)
            {
                Grow(desired);
            }
        }

        void Grow(size_type newCapacity)
        {
            if (newCapacity <= m_capacity)
            {
                return;
            }

            pointer newStorage = m_allocator.allocate(newCapacity);
            size_type i = 0;
            try
            {
                for (; i < m_size; ++i)
                {
                    new (newStorage + i) T(std::move(m_data[i]));
                }
            }
            catch (...)
            {
                for (size_type j = 0; j < i; ++j)
                {
                    std::destroy_at(newStorage + j);
                }
                m_allocator.deallocate(newStorage, newCapacity);
                throw;
            }

            DestroyRange(m_data, m_size);
            ReleaseHeapStorage();

            m_data = newStorage;
            m_capacity = newCapacity;
        }

        template <class U>
        reference EmplaceBackInternal(U&& value)
        {
            ReserveFor(m_size + 1);
            new (m_data + m_size) T(std::forward<U>(value));
            ++m_size;
            return m_data[m_size - 1];
        }

        reference EmplaceBackInternal()
        {
            ReserveFor(m_size + 1);
            new (m_data + m_size) T();
            ++m_size;
            return m_data[m_size - 1];
        }

        template <class U>
        iterator InsertImpl(size_type index, U&& value)
        {
            ReserveFor(m_size + 1);

            if (index == m_size)
            {
                new (m_data + m_size) T(std::forward<U>(value));
                ++m_size;
                return m_data + index;
            }

            T temp(std::forward<U>(value));
            new (m_data + m_size) T(std::move(m_data[m_size - 1]));

            for (size_type i = m_size - 1; i > index; --i)
            {
                m_data[i] = std::move(m_data[i - 1]);
            }

            m_data[index] = std::move(temp);
            ++m_size;
            return m_data + index;
        }

        void CopyFrom(const SmallVector& other)
        {
            if (other.m_size == 0)
            {
                return;
            }

            ReserveFor(other.m_size);
            for (size_type i = 0; i < other.m_size; ++i)
            {
                new (m_data + i) T(other.m_data[i]);
            }
            m_size = other.m_size;
        }

        void MoveFrom(SmallVector&& other)
        {
            if (!other.UsingInline())
            {
                m_data     = other.m_data;
                m_size     = other.m_size;
                m_capacity = other.m_capacity;

                other.m_data     = other.InlineStorage();
                other.m_size     = 0;
                other.m_capacity = InlineCapacity;
                return;
            }

            ReserveFor(other.m_size);
            for (size_type i = 0; i < other.m_size; ++i)
            {
                new (m_data + i) T(std::move(other.m_data[i]));
            }
            m_size = other.m_size;
            other.DestroyRange(other.m_data, other.m_size);
            other.m_size = 0;
        }

        void MoveElements(pointer from, pointer to, size_type count)
        {
            for (size_type i = 0; i < count; ++i)
            {
                new (to + i) T(std::move(from[i]));
                std::destroy_at(from + i);
            }
        }

        void DestroyRange(pointer ptr, size_type count) noexcept
        {
            if constexpr (!std::is_trivially_destructible_v<T>)
            {
                for (size_type i = 0; i < count; ++i)
                {
                    std::destroy_at(ptr + i);
                }
            }
        }

        void ReleaseHeapStorage() noexcept
        {
            if (!UsingInline() && m_capacity >= InlineCapacity)
            {
                m_allocator.deallocate(m_data, m_capacity);
                m_data = InlineStorage();
                m_capacity = InlineCapacity;
            }
        }

        allocator_type m_allocator;
        pointer        m_data;
        size_type      m_size;
        size_type      m_capacity;
        alignas(T) unsigned char m_inline[InlineCapacity * sizeof(T)];
    };

} // namespace dng::core
