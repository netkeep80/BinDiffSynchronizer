#include <catch2/catch_test_macros.hpp>

#include <type_traits>

#include "pmap.h"

// =============================================================================
// Tests for pmap<K, V> (persistent key-value map)
// =============================================================================

// ---------------------------------------------------------------------------
// pmap_data<K,V> — trivially copyable layout
// ---------------------------------------------------------------------------
TEST_CASE("pmap_data<int,int>: is trivially copyable",
          "[pmap][layout]")
{
    REQUIRE(std::is_trivially_copyable<pmap_data<int, int>>::value);
    REQUIRE(std::is_trivially_copyable<pmap_data<int, double>>::value);
    REQUIRE(std::is_trivially_copyable<pmap_entry<int, double>>::value);
}

// ---------------------------------------------------------------------------
// pmap — default data (empty)
// ---------------------------------------------------------------------------
TEST_CASE("pmap<int,int>: zero-initialised pmap_data gives empty map",
          "[pmap][construct]")
{
    pmap_data<int, int> md{};
    pmap<int, int> m(md);

    REQUIRE(m.empty());
    REQUIRE(m.size() == 0u);
}

// ---------------------------------------------------------------------------
// pmap — insert single entry and find
// ---------------------------------------------------------------------------
TEST_CASE("pmap<int,int>: insert single entry and find it",
          "[pmap][insert][find]")
{
    pmap_data<int, int> md{};
    pmap<int, int> m(md);

    m.insert(42, 100);
    REQUIRE(m.size() == 1u);
    REQUIRE(!m.empty());

    int* val = m.find(42);
    REQUIRE(val != nullptr);
    REQUIRE(*val == 100);
}

// ---------------------------------------------------------------------------
// pmap — find missing key returns nullptr
// ---------------------------------------------------------------------------
TEST_CASE("pmap<int,int>: find missing key returns nullptr",
          "[pmap][find]")
{
    pmap_data<int, int> md{};
    pmap<int, int> m(md);

    m.insert(1, 10);
    REQUIRE(m.find(999) == nullptr);
}

// ---------------------------------------------------------------------------
// pmap — insert maintains sorted order
// ---------------------------------------------------------------------------
TEST_CASE("pmap<int,int>: insert multiple entries in sorted order",
          "[pmap][insert][sorted]")
{
    pmap_data<int, int> md{};
    pmap<int, int> m(md);

    m.insert(30, 300);
    m.insert(10, 100);
    m.insert(20, 200);

    REQUIRE(m.size() == 3u);

    // Entries must be in ascending key order: 10, 20, 30.
    auto it = m.begin();
    REQUIRE(it->key == 10);   REQUIRE(it->value == 100); ++it;
    REQUIRE(it->key == 20);   REQUIRE(it->value == 200); ++it;
    REQUIRE(it->key == 30);   REQUIRE(it->value == 300); ++it;
    REQUIRE(it == m.end());
}

// ---------------------------------------------------------------------------
// pmap — update existing key
// ---------------------------------------------------------------------------
TEST_CASE("pmap<int,int>: inserting existing key updates value",
          "[pmap][insert][update]")
{
    pmap_data<int, int> md{};
    pmap<int, int> m(md);

    m.insert(5, 50);
    REQUIRE(m.size() == 1u);

    m.insert(5, 99);
    REQUIRE(m.size() == 1u);

    int* val = m.find(5);
    REQUIRE(val != nullptr);
    REQUIRE(*val == 99);
}

// ---------------------------------------------------------------------------
// pmap — erase existing key
// ---------------------------------------------------------------------------
TEST_CASE("pmap<int,int>: erase removes the correct entry",
          "[pmap][erase]")
{
    pmap_data<int, int> md{};
    pmap<int, int> m(md);

    m.insert(1, 10);
    m.insert(2, 20);
    m.insert(3, 30);
    REQUIRE(m.size() == 3u);

    bool ok = m.erase(2);
    REQUIRE(ok);
    REQUIRE(m.size() == 2u);
    REQUIRE(m.find(2) == nullptr);
    REQUIRE(m.find(1) != nullptr);
    REQUIRE(m.find(3) != nullptr);
}

// ---------------------------------------------------------------------------
// pmap — erase missing key returns false
// ---------------------------------------------------------------------------
TEST_CASE("pmap<int,int>: erase missing key returns false",
          "[pmap][erase]")
{
    pmap_data<int, int> md{};
    pmap<int, int> m(md);

    m.insert(1, 10);
    bool ok = m.erase(999);
    REQUIRE(!ok);
    REQUIRE(m.size() == 1u);
}

// ---------------------------------------------------------------------------
// pmap — operator[]
// ---------------------------------------------------------------------------
TEST_CASE("pmap<int,int>: operator[] inserts default value for missing key",
          "[pmap][operator_index]")
{
    pmap_data<int, int> md{};
    pmap<int, int> m(md);

    // Key 7 does not exist; operator[] should insert it with default value 0.
    m[7] = 77;
    REQUIRE(m.size() == 1u);
    REQUIRE(m.find(7) != nullptr);
    REQUIRE(*m.find(7) == 77);
}

// ---------------------------------------------------------------------------
// pmap — clear resets size to 0
// ---------------------------------------------------------------------------
TEST_CASE("pmap<int,int>: clear resets size to 0",
          "[pmap][clear]")
{
    pmap_data<int, int> md{};
    pmap<int, int> m(md);

    m.insert(1, 1);
    m.insert(2, 2);
    REQUIRE(m.size() == 2u);

    m.clear();
    REQUIRE(m.empty());
    REQUIRE(m.size() == 0u);
}

// ---------------------------------------------------------------------------
// pmap — free releases allocation
// ---------------------------------------------------------------------------
TEST_CASE("pmap<int,int>: free releases underlying allocation",
          "[pmap][free]")
{
    pmap_data<int, int> md{};
    pmap<int, int> m(md);

    m.insert(10, 100);
    REQUIRE(md.data.addr() != 0u);

    m.free();
    REQUIRE(m.size() == 0u);
    REQUIRE(md.data.addr() == 0u);
}

// ---------------------------------------------------------------------------
// pmap<int, double> — works with double values
// ---------------------------------------------------------------------------
TEST_CASE("pmap<int,double>: insert and find double values",
          "[pmap][double]")
{
    pmap_data<int, double> md{};
    pmap<int, double> m(md);

    m.insert(1, 1.1);
    m.insert(2, 2.2);
    m.insert(3, 3.3);

    REQUIRE(m.size() == 3u);
    REQUIRE(*m.find(1) == 1.1);
    REQUIRE(*m.find(2) == 2.2);
    REQUIRE(*m.find(3) == 3.3);
}

// ---------------------------------------------------------------------------
// pmap — range-based iteration
// ---------------------------------------------------------------------------
TEST_CASE("pmap<int,int>: range-based iteration visits all entries",
          "[pmap][iterator]")
{
    pmap_data<int, int> md{};
    pmap<int, int> m(md);

    m.insert(1, 10);
    m.insert(2, 20);
    m.insert(3, 30);

    int key_sum = 0;
    int val_sum = 0;
    for( auto& entry : m )
    {
        key_sum += entry.key;
        val_sum += entry.value;
    }
    REQUIRE(key_sum == 6);
    REQUIRE(val_sum == 60);
}
