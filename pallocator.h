#pragma once
#include "persist.h"
#include <cstddef>
#include <limits>

// pallocator<T> — a persistent STL-compatible allocator.
//
// Backed by AddressManager<T>. Allocates/deallocates contiguous arrays of T
// in the persistent address space via CreateArray/DeleteArray.
//
// Limitations:
//   - T must be trivially copyable (enforced by AddressManager<T>).
//   - Returns raw C++ pointers (via AddressManager slot → pointer resolution).
//     The pointer is valid while the AddressManager<T> singleton is alive.
//   - Standard STL containers that use this allocator (e.g. std::vector<T, pallocator<T>>)
//     will live only as long as the AddressManager<T> singleton is alive.
//   - The allocator does NOT provide cross-process persistence by itself —
//     persistence requires the caller to persist the slot indices (fptr<T>) separately.
//
// Typical use:
//   std::vector<int, pallocator<int>> v;
//   v.push_back(42);

template<typename T>
class pallocator
{
public:
    using value_type      = T;
    using pointer         = T*;
    using const_pointer   = const T*;
    using reference       = T&;
    using const_reference = const T&;
    using size_type       = std::size_t;
    using difference_type = std::ptrdiff_t;

    template<typename U>
    struct rebind { using other = pallocator<U>; };

    pallocator() noexcept = default;
    pallocator(const pallocator&) noexcept = default;

    template<typename U>
    explicit pallocator(const pallocator<U>&) noexcept {}

    ~pallocator() noexcept = default;

    // allocate: create n objects in the persistent address space.
    // Returns a raw pointer valid for the lifetime of AddressManager<T>.
    pointer allocate(size_type n)
    {
        if( n == 0 ) return nullptr;
        unsigned slot = AddressManager<T>::CreateArray(
            static_cast<unsigned>(n), nullptr);
        if( slot == 0 )
            throw std::bad_alloc{};
        return &AddressManager<T>::GetArrayElement(slot, 0);
    }

    // deallocate: free the array at the given pointer.
    // Finds the slot by scanning the AddressManager for a matching pointer.
    void deallocate(pointer p, size_type /*n*/) noexcept
    {
        if( p == nullptr ) return;
        // Scan AddressManager to find the slot whose pointer matches p.
        auto& mgr = AddressManager<T>::GetManager();
        for( unsigned i = 1; i < ADDRESS_SPACE; i++ )
        {
            if( mgr.__itable[i].__used && mgr.__itable[i].__ptr == p )
            {
                AddressManager<T>::DeleteArray(i);
                return;
            }
        }
        // If not found in persistent space, fall back to raw delete[].
        // This handles the case where the pointer came from a non-persistent source.
        delete[] reinterpret_cast<char*>(p);
    }

    size_type max_size() const noexcept
    {
        return std::numeric_limits<size_type>::max() / sizeof(T);
    }

    template<typename U, typename... Args>
    void construct(U* p, Args&&... args)
    {
        ::new(static_cast<void*>(p)) U(std::forward<Args>(args)...);
    }

    template<typename U>
    void destroy(U* p)
    {
        p->~U();
    }

    bool operator==(const pallocator&) const noexcept { return true; }
    bool operator!=(const pallocator&) const noexcept { return false; }
};
