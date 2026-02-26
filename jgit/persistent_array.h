#pragma once

#include <cstdint>
#include <cstring>
#include <cassert>
#include <stdexcept>

#include "persist.h"

// =============================================================================
// Task 2.2.2 — jgit::persistent_array<T, Capacity>
//
// Drop-in persistent replacement for std::vector<T> with bounded capacity.
//
// Design:
//   - Fixed-capacity inline array stored entirely within the struct — no heap
//     allocation of any kind.
//   - Capacity is a compile-time template parameter (default: 64 elements).
//   - For dynamic growth beyond a single slab, callers chain slabs via
//     next_slab (an fptr<persistent_array<T,Capacity>>) pointing to the next
//     slab in AddressManager<persistent_array<T,Capacity>>.
//     When next_slab.addr() == 0 the current slab is the last one.
//   - Entire struct is trivially copyable → compatible with persist<T>.
//     (fptr<T> stores only an unsigned address index, so it is trivially
//     copyable and does not affect the struct's trivial copyability.)
//
// Task 3.4: next_slab_id (raw uint32_t) replaced with typed
//   fptr<persistent_array<T,Capacity>> next_slab.
//   Both are 4 bytes; the change adds type-safety and makes slab chaining
//   consistent with fptr<T> semantics (New/Delete through AddressManager).
//
// IMPORTANT MEMORY MANAGEMENT CONSTRAINT:
//   - The constructor and destructor only load/save state.
//   - Allocation of overflow slabs is explicit via next_slab.New() or
//     next_slab.NewArray() on the AddressManager for persistent_array<T,C>.
//   - Deallocation is explicit via next_slab.Delete().
//
// Key property: sizeof(persistent_array<T, C>) is a compile-time constant with
// NO regular heap-allocated members.
// =============================================================================

namespace jgit {

template<typename T, size_t Capacity = 64>
struct persistent_array {
    static constexpr size_t CAPACITY = Capacity;

    // ---- data members (all fixed-size, no regular heap pointers) ----

    T        data[Capacity];   // inline element storage
    uint32_t size;             // number of valid elements in this slab
    // Task 3.4: typed persistent pointer to the next overflow slab.
    // Replaces the raw uint32_t next_slab_id.  fptr<persistent_array<T,C>>
    // stores only an unsigned address index (trivially copyable).
    // next_slab.addr() == 0 means this is the last slab in the chain.
    fptr<persistent_array<T, Capacity>> next_slab;

    // ---- static assertions ----
    static_assert(Capacity > 0, "persistent_array Capacity must be > 0");

    // ---- constructors ----

    // Default constructor: empty slab.
    // Only LOADS state (no persistent allocation).
    // next_slab default-constructs to __addr=0 (null, no next slab).
    persistent_array() noexcept : size(0) {
        // Value-initialise elements so the struct is well-defined when loaded
        // from disk as raw bytes (avoids reading uninitialised garbage).
        for (size_t i = 0; i < Capacity; ++i) {
            data[i] = T{};
        }
        // next_slab is default-constructed by fptr<T>() to addr=0.
    }

    // ---- capacity / size queries ----

    bool full()  const noexcept { return size >= static_cast<uint32_t>(Capacity); }
    bool empty() const noexcept { return size == 0; }

    // ---- element access ----

    T& operator[](uint32_t index) noexcept {
        assert(index < size);
        return data[index];
    }

    const T& operator[](uint32_t index) const noexcept {
        assert(index < size);
        return data[index];
    }

    T& at(uint32_t index) {
        if (index >= size) {
            throw std::out_of_range("persistent_array::at: index out of range");
        }
        return data[index];
    }

    const T& at(uint32_t index) const {
        if (index >= size) {
            throw std::out_of_range("persistent_array::at: index out of range");
        }
        return data[index];
    }

    // ---- mutation ----

    // Append an element to this slab.  Returns true on success.
    // Returns false (without modifying) if the slab is full — caller should
    // allocate a new slab via AddressManager and set next_slab via next_slab.New().
    bool push_back(const T& value) noexcept {
        if (full()) return false;
        data[size++] = value;
        return true;
    }

    // Remove the last element from this slab.
    void pop_back() noexcept {
        if (size > 0) --size;
    }

    // Remove element at index by shifting subsequent elements left.
    void erase(uint32_t index) noexcept {
        if (index >= size) return;
        for (uint32_t i = index; i + 1 < size; ++i) {
            data[i] = data[i + 1];
        }
        --size;
    }

    // Clear all elements in this slab (does NOT follow next_slab chain).
    void clear() noexcept {
        size = 0;
    }

    // ---- iterator support (range-based for) ----

    T*       begin()       noexcept { return data; }
    const T* begin() const noexcept { return data; }
    T*       end()         noexcept { return data + size; }
    const T* end()   const noexcept { return data + size; }
};

// Verify trivially copyable for use with persist<T>.
static_assert(std::is_trivially_copyable<persistent_array<int32_t, 4>>::value,
              "jgit::persistent_array must be trivially copyable for use with persist<T>");

} // namespace jgit
