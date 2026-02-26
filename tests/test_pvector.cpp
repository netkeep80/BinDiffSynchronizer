#include <catch2/catch_test_macros.hpp>

#include <type_traits>

#include "pvector.h"

// =============================================================================
// Tests for pvector<T> (persistent dynamic array)
// =============================================================================

// ---------------------------------------------------------------------------
// pvector_data<T> — trivially copyable layout
// ---------------------------------------------------------------------------
TEST_CASE("pvector_data<int>: is trivially copyable",
          "[pvector][layout]")
{
    REQUIRE(std::is_trivially_copyable<pvector_data<int>>::value);
    REQUIRE(std::is_trivially_copyable<pvector_data<double>>::value);
    // Phase 3: size (sizeof(void*)) + capacity (sizeof(void*)) + fptr<T> (sizeof(void*)) = 3 * sizeof(void*).
    REQUIRE(sizeof(pvector_data<int>) == 3 * sizeof(void*));
}

// ---------------------------------------------------------------------------
// pvector_data<T> — Phase 3: поля size и capacity имеют тип uintptr_t
// ---------------------------------------------------------------------------
TEST_CASE("pvector_data<int>: size and capacity are uintptr_t (Phase 3)",
          "[pvector][layout][phase3]")
{
    // sizeof(uintptr_t) == sizeof(void*) на любой платформе.
    REQUIRE(sizeof(pvector_data<int>::size) == sizeof(void*));
    REQUIRE(sizeof(pvector_data<int>::capacity) == sizeof(void*));
    // Поле data (fptr<T>) также хранит uintptr_t.
    REQUIRE(sizeof(pvector_data<int>::data) == sizeof(void*));
}

// ---------------------------------------------------------------------------
// pvector — default data (empty)
// ---------------------------------------------------------------------------
TEST_CASE("pvector<int>: zero-initialised pvector_data gives empty vector",
          "[pvector][construct]")
{
    pvector_data<int> vd{};
    pvector<int> v(vd);

    REQUIRE(v.empty());
    REQUIRE(v.size() == 0u);
    REQUIRE(v.capacity() == 0u);
}

// ---------------------------------------------------------------------------
// pvector — push_back and size
// ---------------------------------------------------------------------------
TEST_CASE("pvector<int>: push_back increases size and stores correct values",
          "[pvector][push_back]")
{
    pvector_data<int> vd{};
    pvector<int> v(vd);

    v.push_back(10);
    v.push_back(20);
    v.push_back(30);

    REQUIRE(v.size() == 3u);
    REQUIRE(v[0] == 10);
    REQUIRE(v[1] == 20);
    REQUIRE(v[2] == 30);
}

// ---------------------------------------------------------------------------
// pvector — capacity grows (doubling strategy)
// ---------------------------------------------------------------------------
TEST_CASE("pvector<int>: capacity grows to accommodate elements",
          "[pvector][capacity]")
{
    pvector_data<int> vd{};
    pvector<int> v(vd);

    for( int i = 0; i < 20; i++ )
        v.push_back(i);

    REQUIRE(v.size() == 20u);
    REQUIRE(v.capacity() >= 20u);

    for( int i = 0; i < 20; i++ )
        REQUIRE(v[static_cast<unsigned>(i)] == i);
}

// ---------------------------------------------------------------------------
// pvector — pop_back decreases size
// ---------------------------------------------------------------------------
TEST_CASE("pvector<int>: pop_back decreases size",
          "[pvector][pop_back]")
{
    pvector_data<int> vd{};
    pvector<int> v(vd);

    v.push_back(1);
    v.push_back(2);
    v.push_back(3);
    REQUIRE(v.size() == 3u);

    v.pop_back();
    REQUIRE(v.size() == 2u);
    REQUIRE(v[0] == 1);
    REQUIRE(v[1] == 2);

    v.pop_back();
    v.pop_back();
    REQUIRE(v.size() == 0u);
    REQUIRE(v.empty());

    // pop_back on empty should be safe (no-op).
    v.pop_back();
    REQUIRE(v.size() == 0u);
}

// ---------------------------------------------------------------------------
// pvector — clear resets size to 0 (does not free allocation)
// ---------------------------------------------------------------------------
TEST_CASE("pvector<int>: clear resets size to 0",
          "[pvector][clear]")
{
    pvector_data<int> vd{};
    pvector<int> v(vd);

    v.push_back(1);
    v.push_back(2);
    unsigned cap_before = v.capacity();

    v.clear();
    REQUIRE(v.size() == 0u);
    REQUIRE(v.empty());
    // Capacity should be unchanged by clear().
    REQUIRE(v.capacity() == cap_before);
}

// ---------------------------------------------------------------------------
// pvector — free releases the allocation
// ---------------------------------------------------------------------------
TEST_CASE("pvector<int>: free releases allocation and resets capacity",
          "[pvector][free]")
{
    pvector_data<int> vd{};
    pvector<int> v(vd);

    v.push_back(42);
    REQUIRE(vd.data.addr() != 0u);

    v.free();
    REQUIRE(v.size() == 0u);
    REQUIRE(v.capacity() == 0u);
    REQUIRE(vd.data.addr() == 0u);
}

// ---------------------------------------------------------------------------
// pvector — front and back
// ---------------------------------------------------------------------------
TEST_CASE("pvector<int>: front() and back() return correct elements",
          "[pvector][access]")
{
    pvector_data<int> vd{};
    pvector<int> v(vd);

    v.push_back(10);
    v.push_back(20);
    v.push_back(30);

    REQUIRE(v.front() == 10);
    REQUIRE(v.back() == 30);
}

// ---------------------------------------------------------------------------
// pvector — iterator
// ---------------------------------------------------------------------------
TEST_CASE("pvector<int>: range-based iteration produces correct values",
          "[pvector][iterator]")
{
    pvector_data<int> vd{};
    pvector<int> v(vd);

    v.push_back(1);
    v.push_back(2);
    v.push_back(3);

    int sum = 0;
    for( auto& elem : v )
        sum += elem;

    REQUIRE(sum == 6);
}

// ---------------------------------------------------------------------------
// pvector<double> — works with double
// ---------------------------------------------------------------------------
TEST_CASE("pvector<double>: push_back and read back doubles",
          "[pvector][double]")
{
    pvector_data<double> vd{};
    pvector<double> v(vd);

    v.push_back(1.1);
    v.push_back(2.2);
    v.push_back(3.3);

    REQUIRE(v.size() == 3u);
    REQUIRE(v[0] == 1.1);
    REQUIRE(v[1] == 2.2);
    REQUIRE(v[2] == 3.3);
}
