#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <type_traits>

#include "jgit/persistent_map.h"
#include "persist.h"

// =============================================================================
// Task 2.2.3 — Unit tests for jgit::persistent_map<V, Capacity>
// =============================================================================

// ---------------------------------------------------------------------------
// 2.2.3.1 — Layout: fixed-size, trivially copyable
// ---------------------------------------------------------------------------
TEST_CASE("Task 2.2.3.1: persistent_map is trivially copyable and fixed-size",
          "[task2.2][persistent_map][layout]")
{
    using map4 = jgit::persistent_map<int32_t, 4>;
    REQUIRE(std::is_trivially_copyable<map4>::value);

    // sizeof must be a deterministic compile-time constant.
    // Task 3.2.2: next_node_id (uint32_t) was replaced by next_node
    // (fptr<persistent_map<V,C>>); both are 4 bytes, so the layout is unchanged.
    constexpr size_t expected =
        sizeof(jgit::persistent_map<int32_t, 4>::entry) * 4
        + sizeof(uint32_t)                                 // size
        + sizeof(fptr<jgit::persistent_map<int32_t, 4>>); // next_node
    REQUIRE(sizeof(map4) == expected);
}

// ---------------------------------------------------------------------------
// 2.2.3.2 — Default constructor: empty map
// ---------------------------------------------------------------------------
TEST_CASE("Task 2.2.3.2: persistent_map default constructor gives empty map",
          "[task2.2][persistent_map][construct]")
{
    jgit::persistent_map<int32_t, 8> m;
    REQUIRE(m.empty());
    REQUIRE(m.size == 0u);
    REQUIRE(!m.full());
    REQUIRE(m.next_node.addr() == 0u);  // Task 3.2.2: was next_node_id, now next_node
}

// ---------------------------------------------------------------------------
// 2.2.3.3 — Insert and lookup single key
// ---------------------------------------------------------------------------
TEST_CASE("Task 2.2.3.3: persistent_map insert_or_assign and find single key",
          "[task2.2][persistent_map][insert][find]")
{
    jgit::persistent_map<int32_t, 8> m;

    REQUIRE(m.insert_or_assign("answer", 42));
    REQUIRE(m.size == 1u);

    int32_t* val = m.find("answer");
    REQUIRE(val != nullptr);
    REQUIRE(*val == 42);
}

// ---------------------------------------------------------------------------
// 2.2.3.4 — Insert multiple keys maintains sorted order
// ---------------------------------------------------------------------------
TEST_CASE("Task 2.2.3.4: persistent_map inserts maintain sorted key order",
          "[task2.2][persistent_map][insert][sorted]")
{
    jgit::persistent_map<int32_t, 8> m;

    m.insert_or_assign("cherry", 3);
    m.insert_or_assign("apple",  1);
    m.insert_or_assign("banana", 2);

    REQUIRE(m.size == 3u);

    // Entries must be in lexicographic order: apple, banana, cherry
    REQUIRE(m[0].key == "apple");
    REQUIRE(m[0].value == 1);
    REQUIRE(m[1].key == "banana");
    REQUIRE(m[1].value == 2);
    REQUIRE(m[2].key == "cherry");
    REQUIRE(m[2].value == 3);
}

// ---------------------------------------------------------------------------
// 2.2.3.5 — Update existing key
// ---------------------------------------------------------------------------
TEST_CASE("Task 2.2.3.5: persistent_map insert_or_assign updates existing key",
          "[task2.2][persistent_map][update]")
{
    jgit::persistent_map<int32_t, 4> m;
    m.insert_or_assign("x", 10);
    REQUIRE(m.size == 1u);

    // Update the same key
    m.insert_or_assign("x", 99);
    REQUIRE(m.size == 1u);  // size unchanged

    int32_t* val = m.find("x");
    REQUIRE(val != nullptr);
    REQUIRE(*val == 99);
}

// ---------------------------------------------------------------------------
// 2.2.3.6 — Lookup missing key returns nullptr
// ---------------------------------------------------------------------------
TEST_CASE("Task 2.2.3.6: persistent_map find returns nullptr for missing key",
          "[task2.2][persistent_map][find][missing]")
{
    jgit::persistent_map<int32_t, 4> m;
    m.insert_or_assign("exists", 1);

    REQUIRE(m.find("missing") == nullptr);
    REQUIRE_FALSE(m.contains("missing"));
    REQUIRE(m.contains("exists"));
}

// ---------------------------------------------------------------------------
// 2.2.3.7 — Erase key
// ---------------------------------------------------------------------------
TEST_CASE("Task 2.2.3.7: persistent_map erase removes the key",
          "[task2.2][persistent_map][erase]")
{
    jgit::persistent_map<int32_t, 8> m;
    m.insert_or_assign("a", 1);
    m.insert_or_assign("b", 2);
    m.insert_or_assign("c", 3);

    REQUIRE(m.erase("b"));
    REQUIRE(m.size == 2u);
    REQUIRE(m.find("b") == nullptr);
    REQUIRE(m[0].key == "a");
    REQUIRE(m[1].key == "c");

    // Erasing a missing key returns false
    REQUIRE_FALSE(m.erase("b"));
}

// ---------------------------------------------------------------------------
// 2.2.3.8 — Capacity overflow returns false
// ---------------------------------------------------------------------------
TEST_CASE("Task 2.2.3.8: persistent_map insert_or_assign returns false when full",
          "[task2.2][persistent_map][capacity]")
{
    jgit::persistent_map<int32_t, 3> m;
    REQUIRE(m.insert_or_assign("a", 1));
    REQUIRE(m.insert_or_assign("b", 2));
    REQUIRE(m.insert_or_assign("c", 3));
    REQUIRE(m.full());

    // Inserting a new key when full should fail
    REQUIRE_FALSE(m.insert_or_assign("d", 4));
    REQUIRE(m.size == 3u);

    // Updating an existing key still works (no new slot needed)
    REQUIRE(m.insert_or_assign("a", 99));
    REQUIRE(m.size == 3u);
}

// ---------------------------------------------------------------------------
// 2.2.3.9 — Range-based for loop iteration
// ---------------------------------------------------------------------------
TEST_CASE("Task 2.2.3.9: persistent_map supports range-based for loop",
          "[task2.2][persistent_map][iterator]")
{
    jgit::persistent_map<int32_t, 8> m;
    m.insert_or_assign("one",   1);
    m.insert_or_assign("three", 3);
    m.insert_or_assign("two",   2);

    int32_t sum = 0;
    for (const auto& entry : m) {
        sum += entry.value;
    }
    REQUIRE(sum == 6);
}

// ---------------------------------------------------------------------------
// 2.2.3.10 — Raw-byte copy equivalence (persist<T> compatibility)
// ---------------------------------------------------------------------------
TEST_CASE("Task 2.2.3.10: persistent_map survives raw-byte copy (persist<T> pattern)",
          "[task2.2][persistent_map][persist-compat]")
{
    jgit::persistent_map<int32_t, 4> original;
    original.insert_or_assign("key1", 100);
    original.insert_or_assign("key2", 200);

    alignas(jgit::persistent_map<int32_t, 4>)
        unsigned char buf[sizeof(jgit::persistent_map<int32_t, 4>)];
    std::memcpy(buf, &original, sizeof(original));

    jgit::persistent_map<int32_t, 4> restored;
    std::memcpy(&restored, buf, sizeof(restored));

    REQUIRE(restored.size == original.size);
    int32_t* v1 = restored.find("key1");
    REQUIRE(v1 != nullptr);
    REQUIRE(*v1 == 100);
    int32_t* v2 = restored.find("key2");
    REQUIRE(v2 != nullptr);
    REQUIRE(*v2 == 200);
}

// ---------------------------------------------------------------------------
// 2.2.3.11 — Works with jgit::persistent_string values
// ---------------------------------------------------------------------------
TEST_CASE("Task 2.2.3.11: persistent_map<persistent_string> stores string values",
          "[task2.2][persistent_map][persistent_string]")
{
    jgit::persistent_map<jgit::persistent_string, 4> m;

    jgit::persistent_string val1("hello");
    jgit::persistent_string val2("world");

    REQUIRE(m.insert_or_assign("greeting", val1));
    REQUIRE(m.insert_or_assign("subject",  val2));

    auto* found = m.find("greeting");
    REQUIRE(found != nullptr);
    REQUIRE(*found == "hello");
}
