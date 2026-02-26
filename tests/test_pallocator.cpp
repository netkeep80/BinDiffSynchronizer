#include <catch2/catch_test_macros.hpp>

#include <type_traits>
#include <limits>
#include <vector>
#include <cstddef>

#include "pallocator.h"

// =============================================================================
// Tests for pallocator<T> (persistent STL-compatible allocator)
// =============================================================================

// ---------------------------------------------------------------------------
// pallocator — type aliases and concept compliance
// ---------------------------------------------------------------------------
TEST_CASE("pallocator<int>: satisfies STL allocator type requirements",
          "[pallocator][layout]")
{
    using A = pallocator<int>;

    REQUIRE((std::is_same<A::value_type, int>::value));
    REQUIRE((std::is_same<A::pointer, int*>::value));
    REQUIRE((std::is_same<A::const_pointer, const int*>::value));
    REQUIRE((std::is_same<A::reference, int&>::value));
    REQUIRE((std::is_same<A::const_reference, const int&>::value));
    REQUIRE((std::is_same<A::size_type, std::size_t>::value));
    REQUIRE((std::is_same<A::difference_type, std::ptrdiff_t>::value));
}

// ---------------------------------------------------------------------------
// pallocator — rebind produces correct allocator type
// ---------------------------------------------------------------------------
TEST_CASE("pallocator<int>: rebind<double> gives pallocator<double>",
          "[pallocator][rebind]")
{
    using Rebind = pallocator<int>::rebind<double>::other;
    REQUIRE((std::is_same<Rebind, pallocator<double>>::value));
}

// ---------------------------------------------------------------------------
// pallocator — default constructor and copy constructor compile and work
// ---------------------------------------------------------------------------
TEST_CASE("pallocator<int>: default and copy constructor work",
          "[pallocator][construct]")
{
    pallocator<int> a1;
    pallocator<int> a2(a1);
    REQUIRE(a1 == a2);
    REQUIRE(!(a1 != a2));
}

// ---------------------------------------------------------------------------
// pallocator — template copy constructor from different type
// ---------------------------------------------------------------------------
TEST_CASE("pallocator: cross-type copy constructor compiles",
          "[pallocator][construct]")
{
    pallocator<int> ai;
    pallocator<double> ad(ai);
    pallocator<int> ai2(ad);
    REQUIRE(ai == ai2);
}

// ---------------------------------------------------------------------------
// pallocator — allocate returns non-null pointer
// ---------------------------------------------------------------------------
TEST_CASE("pallocator<int>: allocate(n) returns non-null pointer for n > 0",
          "[pallocator][allocate]")
{
    pallocator<int> a;
    int* p = a.allocate(4);
    REQUIRE(p != nullptr);
    // Deallocate to avoid leaking the persistent slot.
    a.deallocate(p, 4);
}

// ---------------------------------------------------------------------------
// pallocator — allocate(0) returns nullptr
// ---------------------------------------------------------------------------
TEST_CASE("pallocator<int>: allocate(0) returns nullptr",
          "[pallocator][allocate]")
{
    pallocator<int> a;
    int* p = a.allocate(0);
    REQUIRE(p == nullptr);
}

// ---------------------------------------------------------------------------
// pallocator — write and read back allocated memory
// ---------------------------------------------------------------------------
TEST_CASE("pallocator<int>: allocated memory is readable and writable",
          "[pallocator][allocate][readwrite]")
{
    pallocator<int> a;
    const std::size_t N = 5;
    int* p = a.allocate(N);
    REQUIRE(p != nullptr);

    for( std::size_t i = 0; i < N; i++ )
        p[i] = static_cast<int>(i * 11);

    for( std::size_t i = 0; i < N; i++ )
        REQUIRE(p[i] == static_cast<int>(i * 11));

    a.deallocate(p, N);
}

// ---------------------------------------------------------------------------
// pallocator — construct and destroy
// ---------------------------------------------------------------------------
TEST_CASE("pallocator<int>: construct places value and destroy cleans up",
          "[pallocator][construct_destroy]")
{
    pallocator<int> a;
    int* p = a.allocate(1);
    REQUIRE(p != nullptr);

    a.construct(p, 42);
    REQUIRE(*p == 42);

    a.destroy(p);
    a.deallocate(p, 1);
}

// ---------------------------------------------------------------------------
// pallocator — max_size is positive
// ---------------------------------------------------------------------------
TEST_CASE("pallocator<int>: max_size() returns a positive value",
          "[pallocator][max_size]")
{
    pallocator<int> a;
    REQUIRE(a.max_size() > 0u);
}

// ---------------------------------------------------------------------------
// pallocator — operator== and operator!=
// ---------------------------------------------------------------------------
TEST_CASE("pallocator: any two pallocator instances compare equal",
          "[pallocator][equality]")
{
    pallocator<int> a1;
    pallocator<int> a2;
    REQUIRE(a1 == a2);
    REQUIRE(!(a1 != a2));
}

// ---------------------------------------------------------------------------
// pallocator — used with std::vector
// ---------------------------------------------------------------------------
TEST_CASE("pallocator<int>: std::vector<int, pallocator<int>> push_back and read back",
          "[pallocator][std_vector]")
{
    std::vector<int, pallocator<int>> v;
    v.push_back(10);
    v.push_back(20);
    v.push_back(30);

    REQUIRE(v.size() == 3u);
    REQUIRE(v[0] == 10);
    REQUIRE(v[1] == 20);
    REQUIRE(v[2] == 30);
}

// ---------------------------------------------------------------------------
// pallocator — std::vector capacity growth
// ---------------------------------------------------------------------------
TEST_CASE("pallocator<int>: std::vector grows capacity using pallocator",
          "[pallocator][std_vector][capacity]")
{
    std::vector<int, pallocator<int>> v;
    for( int i = 0; i < 20; i++ )
        v.push_back(i);

    REQUIRE(v.size() == 20u);
    for( int i = 0; i < 20; i++ )
        REQUIRE(v[static_cast<std::size_t>(i)] == i);
}

// ---------------------------------------------------------------------------
// pallocator — allocate multiple independent allocations
// ---------------------------------------------------------------------------
TEST_CASE("pallocator<double>: multiple independent allocations do not alias",
          "[pallocator][allocate][no_alias]")
{
    pallocator<double> a;
    double* p1 = a.allocate(3);
    double* p2 = a.allocate(3);

    REQUIRE(p1 != nullptr);
    REQUIRE(p2 != nullptr);
    REQUIRE(p1 != p2);

    p1[0] = 1.1; p1[1] = 2.2; p1[2] = 3.3;
    p2[0] = 4.4; p2[1] = 5.5; p2[2] = 6.6;

    REQUIRE(p1[0] == 1.1);
    REQUIRE(p2[0] == 4.4);

    a.deallocate(p1, 3);
    a.deallocate(p2, 3);
}

// ---------------------------------------------------------------------------
// pallocator — deallocate nullptr is safe (no crash)
// ---------------------------------------------------------------------------
TEST_CASE("pallocator<int>: deallocate(nullptr) is safe",
          "[pallocator][deallocate]")
{
    pallocator<int> a;
    // Should not crash.
    a.deallocate(nullptr, 0);
}
