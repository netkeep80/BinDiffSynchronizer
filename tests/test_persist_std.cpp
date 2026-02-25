#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>
#include <map>
#include <cstdint>
#include <cstring>

#include "persist.h"

// =============================================================================
// Task 2.1 — Feasibility Study: Wrapping std Classes with persist<T>
// =============================================================================
//
// Goal: Verify which C++ types can be safely wrapped by persist<T>.
//
// persist<T> works by saving and loading sizeof(T) raw bytes to/from a file.
// This is safe only for types whose entire state fits in their fixed-size
// in-memory representation (i.e., POD / trivially copyable types with no
// heap-allocated members).
//
// The std types used by nlohmann/json internally are:
//   - std::string*                      (for JSON string nodes)
//   - std::vector<basic_json>*          (for JSON array nodes)
//   - std::map<std::string, basic_json>*(for JSON object nodes)
//   - bool, int64_t, double             (for JSON scalar nodes)
//
// These tests document the result of the feasibility study.
// =============================================================================

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace {

// Returns true if all bytes of the object's representation are contained
// within the object's own storage (i.e., no external heap pointer is needed).
template<typename T>
bool data_is_self_contained(const T& obj, const void* data_ptr)
{
    const char* obj_start = reinterpret_cast<const char*>(&obj);
    const char* obj_end   = obj_start + sizeof(T);
    const char* data      = reinterpret_cast<const char*>(data_ptr);
    return (data >= obj_start && data < obj_end);
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// 2.1.1 — persist<bool>: POD type, no heap allocation
// ---------------------------------------------------------------------------
TEST_CASE("Task 2.1.1: persist<bool> works — POD type, no heap allocation",
          "[task2.1][persist][pod]")
{
    // A bool is 1 byte of raw data — persist<bool> stores exactly that.
    REQUIRE(sizeof(bool) == 1u);

    // Constructing from a value preserves the value
    {
        persist<bool> p(true);
        bool val = static_cast<bool>(p);
        REQUIRE(val == true);
    }
    {
        persist<bool> p(false);
        bool val = static_cast<bool>(p);
        REQUIRE(val == false);
    }

    // Raw bytes == the value: the entire state is in sizeof(bool) bytes
    bool b = true;
    unsigned char raw[sizeof(bool)];
    std::memcpy(raw, &b, sizeof(bool));
    REQUIRE(raw[0] != 0);  // non-zero == true

    b = false;
    std::memcpy(raw, &b, sizeof(bool));
    REQUIRE(raw[0] == 0);  // zero == false
}

// ---------------------------------------------------------------------------
// 2.1.2 — persist<int64_t>: POD type, no heap allocation
// ---------------------------------------------------------------------------
TEST_CASE("Task 2.1.2: persist<int64_t> works — POD type, no heap allocation",
          "[task2.1][persist][pod]")
{
    REQUIRE(sizeof(int64_t) == 8u);

    persist<int64_t> p(INT64_C(9876543210));
    int64_t val = static_cast<int64_t>(p);
    REQUIRE(val == INT64_C(9876543210));

    persist<int64_t> neg(INT64_C(-1));
    REQUIRE(static_cast<int64_t>(neg) == INT64_C(-1));
}

// ---------------------------------------------------------------------------
// 2.1.3 — persist<double>: POD type, no heap allocation
// ---------------------------------------------------------------------------
TEST_CASE("Task 2.1.3: persist<double> works — POD type, no heap allocation",
          "[task2.1][persist][pod]")
{
    REQUIRE(sizeof(double) == 8u);

    persist<double> p(2.718281828);
    double val = static_cast<double>(p);
    REQUIRE(val == 2.718281828);
}

// ---------------------------------------------------------------------------
// 2.1.4 — std::string: NOT safe for persist<T> — heap-owned data
// ---------------------------------------------------------------------------
TEST_CASE("Task 2.1.4: std::string CANNOT be safely wrapped by persist<T>",
          "[task2.1][persist][std-types]")
{
    // For long strings (beyond SSO threshold), std::string stores the character
    // data on the heap and keeps only a pointer inside its fixed-size struct.
    // persist<T> would save that raw pointer — which becomes dangling after the
    // owning std::string is destroyed or after process restart.
    //
    // Test: create a long string (past any reasonable SSO threshold) and verify
    // its data pointer is NOT contained within the string object's own storage.

    std::string long_str(64, 'X');  // 64 chars — past any reasonable SSO threshold
    REQUIRE(long_str.size() == 64u);

    // For a heap-allocated (non-SSO) string, the data pointer points OUTSIDE
    // the string object's own storage. This means persist<T> would save a
    // dangling heap pointer, not the actual string data.
    bool self_contained = data_is_self_contained(long_str, long_str.data());
    REQUIRE_FALSE(self_contained);

    // The fixed struct size is much smaller than the string's content
    REQUIRE(sizeof(std::string) < long_str.size());
}

// ---------------------------------------------------------------------------
// 2.1.5 — std::vector<int>: NOT safe for persist<T> — heap-owned data
// ---------------------------------------------------------------------------
TEST_CASE("Task 2.1.5: std::vector<int> CANNOT be safely wrapped by persist<T>",
          "[task2.1][persist][std-types]")
{
    // std::vector always stores its elements on the heap. sizeof(std::vector<int>)
    // is fixed (typically 24 bytes on 64-bit), but it contains three pointers:
    //   begin, end, capacity_end  (implementation-specific but always heap ptrs)
    //
    // persist<T> would save these raw pointers which are invalid after process restart.

    std::vector<int> v = {1, 2, 3, 4, 5};
    REQUIRE(v.size() == 5u);

    // Data pointer is always outside the vector's own storage —
    // proving the actual element data is heap-allocated, not inline.
    bool self_contained = data_is_self_contained(v, v.data());
    REQUIRE_FALSE(self_contained);

    // For large vectors, the struct size is dwarfed by the data on the heap.
    // Use a vector with 100 elements to confirm the struct cannot hold the data.
    std::vector<int> large_v(100, 42);
    REQUIRE(sizeof(large_v) < large_v.size() * sizeof(int));
}

// ---------------------------------------------------------------------------
// 2.1.6 — std::map<std::string, int>: NOT safe for persist<T> — heap-owned data
// ---------------------------------------------------------------------------
TEST_CASE("Task 2.1.6: std::map CANNOT be safely wrapped by persist<T>",
          "[task2.1][persist][std-types]")
{
    // std::map is a red-black tree. All tree nodes are individually heap-allocated.
    // sizeof(std::map<...>) is fixed (typically 48 bytes on 64-bit), containing
    // only the tree header (root/sentinel pointers, size).
    //
    // persist<T> would save the raw tree header with dangling node pointers.

    using map_t = std::map<std::string, int>;
    map_t m;
    m["key1"] = 1;
    m["key2"] = 2;
    REQUIRE(m.size() == 2u);

    // The map's fixed-size representation is much smaller than the data it holds
    REQUIRE(sizeof(map_t) < m.size() * (sizeof(std::string) + sizeof(int)));

    // A map with content has heap-allocated nodes
    // We verify this structurally: the first map element's address is outside
    // the map object's own storage, confirming heap allocation.
    bool first_node_inline = data_is_self_contained(m, &(m.begin()->second));
    REQUIRE_FALSE(first_node_inline);
}

// ---------------------------------------------------------------------------
// 2.1.7 — Conclusion: only POD types work with persist<T>
// ---------------------------------------------------------------------------
TEST_CASE("Task 2.1.7: persist<T> is safe only for POD types",
          "[task2.1][persist][conclusion]")
{
    // Verify that the types nlohmann/json uses for scalar values are trivially
    // copyable (POD-like), while the container types are not.

    // Scalar types used by nlohmann/json — SAFE with persist<T>
    REQUIRE(std::is_trivially_copyable<bool>::value);
    REQUIRE(std::is_trivially_copyable<int64_t>::value);
    REQUIRE(std::is_trivially_copyable<double>::value);

    // Container types used by nlohmann/json — UNSAFE with persist<T>
    REQUIRE_FALSE(std::is_trivially_copyable<std::string>::value);
    REQUIRE_FALSE(std::is_trivially_copyable<std::vector<int>>::value);
    REQUIRE_FALSE(std::is_trivially_copyable<std::map<std::string, int>>::value);

    // CONCLUSION: Custom persistent analogs are required for Phase 2 (Task 2.2):
    //   - jgit::persistent_string  to replace std::string
    //   - jgit::persistent_array   to replace std::vector<basic_json>
    //   - jgit::persistent_map     to replace std::map<std::string, basic_json>
}
