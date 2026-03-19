#include <catch2/catch_test_macros.hpp>

#include <limits>
#include <type_traits>
#include <vector>
#include <cstddef>
#include <new>

#include "pam_pmm.h"

// =============================================================================
// Тесты для PamManager::pallocator<T> (pmm::pallocator<T, PamManager>)
//
// Эти тесты проверяют PMM STL-совместимый аллокатор напрямую,
// без обёртки pallocator_pmm.h (удалена в рамках Issue #143, План 1.3).
// =============================================================================

using namespace pjson;

// ---------------------------------------------------------------------------
// PamManager::pallocator — типы соответствуют требованиям STL (через allocator_traits)
// ---------------------------------------------------------------------------
TEST_CASE( "PamManager::pallocator<int>: satisfies STL allocator type requirements", "[pallocator][layout]" )
{
    using A = PamManager::pallocator<int>;

    REQUIRE( ( std::is_same<A::value_type, int>::value ) );
    REQUIRE( ( std::is_same<A::size_type, std::size_t>::value ) );
    REQUIRE( ( std::is_same<A::difference_type, std::ptrdiff_t>::value ) );
}

// ---------------------------------------------------------------------------
// PamManager::pallocator — rebind через allocator_traits
// ---------------------------------------------------------------------------
TEST_CASE( "PamManager::pallocator<int>: rebind<double> gives pallocator<double>", "[pallocator][rebind]" )
{
    using Rebind = std::allocator_traits<PamManager::pallocator<int>>::rebind_alloc<double>;
    REQUIRE( ( std::is_same<Rebind, PamManager::pallocator<double>>::value ) );
}

// ---------------------------------------------------------------------------
// PamManager::pallocator — конструкторы по умолчанию и копирования
// ---------------------------------------------------------------------------
TEST_CASE( "PamManager::pallocator<int>: default and copy constructor work", "[pallocator][construct]" )
{
    PamManager::pallocator<int> a1;
    PamManager::pallocator<int> a2( a1 );
    REQUIRE( a1 == a2 );
    REQUIRE( !( a1 != a2 ) );
}

// ---------------------------------------------------------------------------
// PamManager::pallocator — конструктор копирования между разными типами
// ---------------------------------------------------------------------------
TEST_CASE( "PamManager::pallocator: cross-type copy constructor compiles", "[pallocator][construct]" )
{
    PamManager::pallocator<int>    ai;
    PamManager::pallocator<double> ad( ai );
    PamManager::pallocator<int>    ai2( ad );
    REQUIRE( ai == ai2 );
}

// ---------------------------------------------------------------------------
// PamManager::pallocator — allocate возвращает ненулевой указатель
// ---------------------------------------------------------------------------
TEST_CASE( "PamManager::pallocator<int>: allocate(n) returns non-null pointer for n > 0", "[pallocator][allocate]" )
{
    if ( !pam_pmm_is_initialized() )
        pam_pmm_init( nullptr );

    PamManager::pallocator<int> a;
    int*                        p = a.allocate( 4 );
    REQUIRE( p != nullptr );

    a.deallocate( p, 4 );
}

// ---------------------------------------------------------------------------
// PamManager::pallocator — allocate(0) бросает std::bad_alloc
// ---------------------------------------------------------------------------
TEST_CASE( "PamManager::pallocator<int>: allocate(0) throws std::bad_alloc", "[pallocator][allocate]" )
{
    if ( !pam_pmm_is_initialized() )
        pam_pmm_init( nullptr );

    PamManager::pallocator<int> a;
    REQUIRE_THROWS_AS( a.allocate( 0 ), std::bad_alloc );
}

// ---------------------------------------------------------------------------
// PamManager::pallocator — выделенная память доступна для чтения и записи
// ---------------------------------------------------------------------------
TEST_CASE( "PamManager::pallocator<int>: allocated memory is readable and writable",
           "[pallocator][allocate][readwrite]" )
{
    if ( !pam_pmm_is_initialized() )
        pam_pmm_init( nullptr );

    PamManager::pallocator<int> a;
    const std::size_t           N = 5;
    int*                        p = a.allocate( N );
    REQUIRE( p != nullptr );

    for ( std::size_t i = 0; i < N; i++ )
        p[i] = static_cast<int>( i * 11 );

    for ( std::size_t i = 0; i < N; i++ )
        REQUIRE( p[i] == static_cast<int>( i * 11 ) );

    a.deallocate( p, N );
}

// ---------------------------------------------------------------------------
// PamManager::pallocator — construct и destroy через allocator_traits
// ---------------------------------------------------------------------------
TEST_CASE( "PamManager::pallocator<int>: construct/destroy via allocator_traits", "[pallocator][construct_destroy]" )
{
    if ( !pam_pmm_is_initialized() )
        pam_pmm_init( nullptr );

    PamManager::pallocator<int> a;
    int*                        p = a.allocate( 1 );
    REQUIRE( p != nullptr );

    std::allocator_traits<PamManager::pallocator<int>>::construct( a, p, 42 );
    REQUIRE( *p == 42 );

    std::allocator_traits<PamManager::pallocator<int>>::destroy( a, p );
    a.deallocate( p, 1 );
}

// ---------------------------------------------------------------------------
// PamManager::pallocator — max_size возвращает положительное значение
// ---------------------------------------------------------------------------
TEST_CASE( "PamManager::pallocator<int>: max_size() returns a positive value", "[pallocator][max_size]" )
{
    PamManager::pallocator<int> a;
    REQUIRE( a.max_size() > 0u );
}

// ---------------------------------------------------------------------------
// PamManager::pallocator — operator== и operator!=
// ---------------------------------------------------------------------------
TEST_CASE( "PamManager::pallocator: any two instances compare equal", "[pallocator][equality]" )
{
    PamManager::pallocator<int> a1;
    PamManager::pallocator<int> a2;
    REQUIRE( a1 == a2 );
    REQUIRE( !( a1 != a2 ) );
}

// ---------------------------------------------------------------------------
// PamManager::pallocator — использование с std::vector
// ---------------------------------------------------------------------------
TEST_CASE( "PamManager::pallocator<int>: std::vector push_back and read back", "[pallocator][std_vector]" )
{
    if ( !pam_pmm_is_initialized() )
        pam_pmm_init( nullptr );

    std::vector<int, PamManager::pallocator<int>> v;
    v.push_back( 10 );
    v.push_back( 20 );
    v.push_back( 30 );

    REQUIRE( v.size() == 3u );
    REQUIRE( v[0] == 10 );
    REQUIRE( v[1] == 20 );
    REQUIRE( v[2] == 30 );
}

// ---------------------------------------------------------------------------
// PamManager::pallocator — std::vector рост ёмкости
// ---------------------------------------------------------------------------
TEST_CASE( "PamManager::pallocator<int>: std::vector grows capacity", "[pallocator][std_vector][capacity]" )
{
    if ( !pam_pmm_is_initialized() )
        pam_pmm_init( nullptr );

    std::vector<int, PamManager::pallocator<int>> v;
    for ( int i = 0; i < 20; i++ )
        v.push_back( i );

    REQUIRE( v.size() == 20u );
    for ( int i = 0; i < 20; i++ )
        REQUIRE( v[static_cast<std::size_t>( i )] == i );
}

// ---------------------------------------------------------------------------
// PamManager::pallocator — несколько независимых аллокаций не перекрываются
// ---------------------------------------------------------------------------
TEST_CASE( "PamManager::pallocator<double>: multiple independent allocations do not alias",
           "[pallocator][allocate][no_alias]" )
{
    if ( !pam_pmm_is_initialized() )
        pam_pmm_init( nullptr );

    PamManager::pallocator<double> a;
    double*                        p1 = a.allocate( 3 );
    double*                        p2 = a.allocate( 3 );

    REQUIRE( p1 != nullptr );
    REQUIRE( p2 != nullptr );
    REQUIRE( p1 != p2 );

    p1[0] = 1.1;
    p1[1] = 2.2;
    p1[2] = 3.3;
    p2[0] = 4.4;
    p2[1] = 5.5;
    p2[2] = 6.6;

    REQUIRE( p1[0] == 1.1 );
    REQUIRE( p2[0] == 4.4 );

    a.deallocate( p1, 3 );
    a.deallocate( p2, 3 );
}

// ---------------------------------------------------------------------------
// PamManager::pallocator — deallocate(nullptr) безопасен
// ---------------------------------------------------------------------------
TEST_CASE( "PamManager::pallocator<int>: deallocate(nullptr) is safe", "[pallocator][deallocate]" )
{
    PamManager::pallocator<int> a;
    a.deallocate( nullptr, 0 );
}
