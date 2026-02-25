#pragma once

#include <cstdint>
#include <cstring>
#include <cassert>
#include <stdexcept>

#include "persistent_string.h"

// =============================================================================
// Task 2.2.3 — jgit::persistent_map<V, Capacity>
//
// Drop-in persistent replacement for std::map<std::string, V>.
//
// Design:
//   - Fixed-capacity sorted array of key-value pairs stored entirely within the
//     struct — no heap allocation, no pointers.
//   - Keys are jgit::persistent_string; values are template parameter V.
//   - Entries are kept sorted by key to allow O(log N) binary search.
//   - Capacity is a compile-time template parameter (default: 32 entries).
//   - For overflow beyond a single node, callers can chain nodes via
//     next_node_id (index into an AddressManager or PageDevice pool).
//     next_node_id == 0 means this is the only / last node.
//   - Entire struct is trivially copyable → compatible with persist<T>.
//
// Key property: sizeof(persistent_map<V, C>) is a compile-time constant with
// NO heap-allocated members.
// =============================================================================

namespace jgit {

template<typename V, size_t Capacity = 32>
struct persistent_map {
    static constexpr size_t CAPACITY = Capacity;

    // ---- key-value pair (fixed-size, no heap) ----
    struct entry {
        persistent_string key;
        V                 value;
    };

    // ---- data members (all fixed-size, no pointers) ----

    entry    entries[Capacity];   // sorted array of key-value pairs
    uint32_t size;                // number of valid entries in this node
    uint32_t next_node_id;        // fptr-style index into a pool; 0 = no overflow

    // ---- static assertions ----
    static_assert(Capacity > 0, "persistent_map Capacity must be > 0");

    // ---- constructors ----

    persistent_map() noexcept : size(0), next_node_id(0) {
        // Value-initialise all entries so raw-byte loads are well-defined.
        for (size_t i = 0; i < Capacity; ++i) {
            entries[i].key   = persistent_string{};
            entries[i].value = V{};
        }
    }

    // ---- capacity / size queries ----

    bool full()  const noexcept { return size >= static_cast<uint32_t>(Capacity); }
    bool empty() const noexcept { return size == 0; }

    // ---- internal helpers ----

    // Binary search for key.  Returns the index where key is or should be.
    uint32_t lower_bound(const char* key) const noexcept {
        uint32_t lo = 0, hi = size;
        while (lo < hi) {
            uint32_t mid = lo + (hi - lo) / 2;
            if (std::strcmp(entries[mid].key.c_str(), key) < 0) {
                lo = mid + 1;
            } else {
                hi = mid;
            }
        }
        return lo;
    }

    // ---- lookup ----

    // Returns a pointer to the value associated with key, or nullptr if absent.
    V* find(const char* key) noexcept {
        uint32_t idx = lower_bound(key);
        if (idx < size && entries[idx].key == key) {
            return &entries[idx].value;
        }
        return nullptr;
    }

    const V* find(const char* key) const noexcept {
        uint32_t idx = lower_bound(key);
        if (idx < size && entries[idx].key == key) {
            return &entries[idx].value;
        }
        return nullptr;
    }

    V* find(const std::string& key) noexcept {
        return find(key.c_str());
    }

    const V* find(const std::string& key) const noexcept {
        return find(key.c_str());
    }

    bool contains(const char* key) const noexcept {
        return find(key) != nullptr;
    }

    bool contains(const std::string& key) const noexcept {
        return find(key.c_str()) != nullptr;
    }

    // ---- insertion / update ----

    // Insert or update the entry for key.
    // Returns true on success, false if the node is full and no room to insert.
    bool insert_or_assign(const char* key, const V& value) noexcept {
        uint32_t idx = lower_bound(key);
        if (idx < size && entries[idx].key == key) {
            // Update existing entry.
            entries[idx].value = value;
            return true;
        }
        // Need to insert a new entry at position idx.
        if (full()) return false;
        // Shift entries right to make room.
        for (uint32_t i = size; i > idx; --i) {
            entries[i] = entries[i - 1];
        }
        entries[idx].key   = persistent_string{};
        entries[idx].key.assign(key);
        entries[idx].value = value;
        ++size;
        return true;
    }

    bool insert_or_assign(const std::string& key, const V& value) noexcept {
        return insert_or_assign(key.c_str(), value);
    }

    // ---- removal ----

    // Erase entry with the given key.  Returns true if it was found and erased.
    bool erase(const char* key) noexcept {
        uint32_t idx = lower_bound(key);
        if (idx >= size || !(entries[idx].key == key)) return false;
        for (uint32_t i = idx; i + 1 < size; ++i) {
            entries[i] = entries[i + 1];
        }
        --size;
        return true;
    }

    bool erase(const std::string& key) noexcept {
        return erase(key.c_str());
    }

    // Clear all entries in this node (does NOT follow next_node_id chain).
    void clear() noexcept {
        size = 0;
    }

    // ---- element access ----

    entry&       operator[](uint32_t index)       noexcept { assert(index < size); return entries[index]; }
    const entry& operator[](uint32_t index) const noexcept { assert(index < size); return entries[index]; }

    // ---- iterator support (range-based for over entries) ----

    entry*       begin()       noexcept { return entries; }
    const entry* begin() const noexcept { return entries; }
    entry*       end()         noexcept { return entries + size; }
    const entry* end()   const noexcept { return entries + size; }
};

// Verify trivially copyable for use with persist<T>.
static_assert(std::is_trivially_copyable<persistent_map<int32_t, 4>>::value,
              "jgit::persistent_map must be trivially copyable for use with persist<T>");

} // namespace jgit
