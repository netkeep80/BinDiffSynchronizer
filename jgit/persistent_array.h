#pragma once

#include <cstdint>
#include <cstring>
#include <cassert>
#include <stdexcept>

// =============================================================================
// Task 2.2.2 — jgit::persistent_array<T, Capacity>
//
// Drop-in persistent replacement for std::vector<T> with bounded capacity.
//
// Design:
//   - Fixed-capacity inline array stored entirely within the struct — no heap
//     allocation of any kind.
//   - Capacity is a compile-time template parameter (default: 64 elements).
//   - For dynamic growth beyond a single slab, callers can chain slabs via
//     the next_slab_id field (index into an AddressManager or PageDevice pool).
//     When next_slab_id == 0 the current slab is the last one in the chain.
//   - Entire struct is trivially copyable → compatible with persist<T>.
//
// Key property: sizeof(persistent_array<T, C>) is a compile-time constant with
// NO heap-allocated members.
// =============================================================================

namespace jgit {

template<typename T, size_t Capacity = 64>
struct persistent_array {
    static constexpr size_t CAPACITY = Capacity;

    // ---- data members (all fixed-size, no pointers) ----

    T        data[Capacity];   // inline element storage
    uint32_t size;             // number of valid elements in this slab
    uint32_t next_slab_id;     // fptr-style index into a pool; 0 = no next slab

    // ---- static assertions ----
    static_assert(Capacity > 0, "persistent_array Capacity must be > 0");

    // ---- constructors ----

    // Default constructor: empty slab
    persistent_array() noexcept : size(0), next_slab_id(0) {
        // Value-initialise elements so the struct is well-defined when loaded
        // from disk as raw bytes (avoids reading uninitialised garbage).
        for (size_t i = 0; i < Capacity; ++i) {
            data[i] = T{};
        }
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
    // allocate a new slab via AddressManager and set next_slab_id.
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

    // Clear all elements in this slab (does NOT follow next_slab_id chain).
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
