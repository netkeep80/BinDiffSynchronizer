#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <type_traits>
#include <stdexcept>

#include "jgit/persistent_array.h"

// =============================================================================
// Task 2.2.2 — Unit tests for jgit::persistent_array<T, Capacity>
// =============================================================================

// ---------------------------------------------------------------------------
// 2.2.2.1 — Layout: fixed-size, trivially copyable
// ---------------------------------------------------------------------------
TEST_CASE("Task 2.2.2.1: persistent_array is trivially copyable and fixed-size",
          "[task2.2][persistent_array][layout]")
{
    using arr4 = jgit::persistent_array<int32_t, 4>;
    REQUIRE(std::is_trivially_copyable<arr4>::value);

    // sizeof must be deterministic at compile time
    constexpr size_t expected = sizeof(int32_t) * 4 + sizeof(uint32_t) * 2;
    REQUIRE(sizeof(arr4) == expected);
}

// ---------------------------------------------------------------------------
// 2.2.2.2 — Default constructor: empty slab
// ---------------------------------------------------------------------------
TEST_CASE("Task 2.2.2.2: persistent_array default constructor gives empty slab",
          "[task2.2][persistent_array][construct]")
{
    jgit::persistent_array<int32_t, 8> arr;
    REQUIRE(arr.empty());
    REQUIRE(arr.size == 0u);
    REQUIRE(!arr.full());
    REQUIRE(arr.next_slab.addr() == 0u);
}

// ---------------------------------------------------------------------------
// 2.2.2.3 — push_back fills the slab
// ---------------------------------------------------------------------------
TEST_CASE("Task 2.2.2.3: persistent_array push_back fills up to capacity",
          "[task2.2][persistent_array][push_back]")
{
    jgit::persistent_array<int32_t, 4> arr;

    REQUIRE(arr.push_back(10));
    REQUIRE(arr.push_back(20));
    REQUIRE(arr.push_back(30));
    REQUIRE(arr.push_back(40));
    REQUIRE(arr.size == 4u);
    REQUIRE(arr.full());

    // Slab is full — further push_back must fail
    REQUIRE_FALSE(arr.push_back(50));
    REQUIRE(arr.size == 4u);  // unchanged
}

// ---------------------------------------------------------------------------
// 2.2.2.4 — Element access: operator[] and at()
// ---------------------------------------------------------------------------
TEST_CASE("Task 2.2.2.4: persistent_array element access",
          "[task2.2][persistent_array][access]")
{
    jgit::persistent_array<int32_t, 8> arr;
    arr.push_back(1);
    arr.push_back(2);
    arr.push_back(3);

    REQUIRE(arr[0] == 1);
    REQUIRE(arr[1] == 2);
    REQUIRE(arr[2] == 3);

    REQUIRE(arr.at(0) == 1);
    REQUIRE_THROWS_AS(arr.at(99), std::out_of_range);
}

// ---------------------------------------------------------------------------
// 2.2.2.5 — pop_back reduces size
// ---------------------------------------------------------------------------
TEST_CASE("Task 2.2.2.5: persistent_array pop_back reduces size",
          "[task2.2][persistent_array][pop_back]")
{
    jgit::persistent_array<int32_t, 4> arr;
    arr.push_back(100);
    arr.push_back(200);
    REQUIRE(arr.size == 2u);

    arr.pop_back();
    REQUIRE(arr.size == 1u);
    REQUIRE(arr[0] == 100);

    arr.pop_back();
    REQUIRE(arr.empty());

    // pop_back on empty slab is a no-op
    arr.pop_back();
    REQUIRE(arr.empty());
}

// ---------------------------------------------------------------------------
// 2.2.2.6 — erase removes element by index and shifts
// ---------------------------------------------------------------------------
TEST_CASE("Task 2.2.2.6: persistent_array erase shifts remaining elements",
          "[task2.2][persistent_array][erase]")
{
    jgit::persistent_array<int32_t, 8> arr;
    arr.push_back(1);
    arr.push_back(2);
    arr.push_back(3);
    arr.push_back(4);

    arr.erase(1);  // remove element with value 2
    REQUIRE(arr.size == 3u);
    REQUIRE(arr[0] == 1);
    REQUIRE(arr[1] == 3);
    REQUIRE(arr[2] == 4);
}

// ---------------------------------------------------------------------------
// 2.2.2.7 — clear resets the slab
// ---------------------------------------------------------------------------
TEST_CASE("Task 2.2.2.7: persistent_array clear resets size to zero",
          "[task2.2][persistent_array][clear]")
{
    jgit::persistent_array<int32_t, 4> arr;
    arr.push_back(1);
    arr.push_back(2);
    arr.clear();
    REQUIRE(arr.empty());
    REQUIRE(arr.size == 0u);
}

// ---------------------------------------------------------------------------
// 2.2.2.8 — Range-based for loop iteration
// ---------------------------------------------------------------------------
TEST_CASE("Task 2.2.2.8: persistent_array supports range-based for loop",
          "[task2.2][persistent_array][iterator]")
{
    jgit::persistent_array<int32_t, 8> arr;
    arr.push_back(10);
    arr.push_back(20);
    arr.push_back(30);

    int32_t sum = 0;
    for (const auto& v : arr) sum += v;
    REQUIRE(sum == 60);
}

// ---------------------------------------------------------------------------
// 2.2.2.9 — next_slab_id chaining metadata
// ---------------------------------------------------------------------------
TEST_CASE("Task 2.2.2.9: persistent_array next_slab_id supports multi-slab chains",
          "[task2.2][persistent_array][chain]")
{
    jgit::persistent_array<int32_t, 2> slab1;
    jgit::persistent_array<int32_t, 2> slab2;

    slab1.push_back(1);
    slab1.push_back(2);
    REQUIRE(slab1.full());

    // Simulate linking: slab1 points to slab2 at address index 7
    // (using set_addr() to directly set the fptr address index for testing)
    slab1.next_slab.set_addr(7);
    slab2.push_back(3);
    slab2.push_back(4);

    REQUIRE(slab1.next_slab.addr() == 7u);
    REQUIRE(slab2[0] == 3);
    REQUIRE(slab2[1] == 4);
    REQUIRE(slab2.next_slab.addr() == 0u);  // end of chain
}

// ---------------------------------------------------------------------------
// 2.2.2.10 — Raw-byte copy equivalence (persist<T> compatibility)
// ---------------------------------------------------------------------------
TEST_CASE("Task 2.2.2.10: persistent_array survives raw-byte copy (persist<T> pattern)",
          "[task2.2][persistent_array][persist-compat]")
{
    jgit::persistent_array<int32_t, 4> original;
    original.push_back(11);
    original.push_back(22);
    original.push_back(33);

    alignas(jgit::persistent_array<int32_t, 4>)
        unsigned char buf[sizeof(jgit::persistent_array<int32_t, 4>)];
    std::memcpy(buf, &original, sizeof(original));

    jgit::persistent_array<int32_t, 4> restored;
    std::memcpy(&restored, buf, sizeof(restored));

    REQUIRE(restored.size == original.size);
    REQUIRE(restored[0] == 11);
    REQUIRE(restored[1] == 22);
    REQUIRE(restored[2] == 33);
}
