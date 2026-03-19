#include <catch2/catch_test_macros.hpp>

#include <limits>
#include <type_traits>
#include <vector>
#include <cstddef>

#include "pallocator_pmm.h"

// =============================================================================
// Тесты для pallocator_pmm<T>
//
// Эти тесты проверяют PMM-версию STL-совместимого аллокатора.
// Тесты аналогичны test_pallocator.cpp, но используют pjson::pallocator_pmm<T>.
// =============================================================================

using namespace pjson;

// ---------------------------------------------------------------------------
// pallocator_pmm — типы соответствуют требованиям STL
// ---------------------------------------------------------------------------
TEST_CASE( "pallocator_pmm<int>: satisfies STL allocator type requirements", "[pallocator_pmm][layout]" )
{
    using A = pallocator_pmm<int>;

    REQUIRE( ( std::is_same<A::value_type, int>::value ) );
    REQUIRE( ( std::is_same<A::pointer, int*>::value ) );
    REQUIRE( ( std::is_same<A::const_pointer, const int*>::value ) );
    REQUIRE( ( std::is_same<A::reference, int&>::value ) );
    REQUIRE( ( std::is_same<A::const_reference, const int&>::value ) );
    REQUIRE( ( std::is_same<A::size_type, std::size_t>::value ) );
    REQUIRE( ( std::is_same<A::difference_type, std::ptrdiff_t>::value ) );
}

// ---------------------------------------------------------------------------
// pallocator_pmm — rebind производит корректный тип
// ---------------------------------------------------------------------------
TEST_CASE( "pallocator_pmm<int>: rebind<double> gives pallocator_pmm<double>", "[pallocator_pmm][rebind]" )
{
    using Rebind = pallocator_pmm<int>::rebind<double>::other;
    REQUIRE( ( std::is_same<Rebind, pallocator_pmm<double>>::value ) );
}

// ---------------------------------------------------------------------------
// pallocator_pmm — конструкторы по умолчанию и копирования
// ---------------------------------------------------------------------------
TEST_CASE( "pallocator_pmm<int>: default and copy constructor work", "[pallocator_pmm][construct]" )
{
    pallocator_pmm<int> a1;
    pallocator_pmm<int> a2( a1 );
    REQUIRE( a1 == a2 );
    REQUIRE( !( a1 != a2 ) );
}

// ---------------------------------------------------------------------------
// pallocator_pmm — конструктор копирования между разными типами
// ---------------------------------------------------------------------------
TEST_CASE( "pallocator_pmm: cross-type copy constructor compiles", "[pallocator_pmm][construct]" )
{
    pallocator_pmm<int>    ai;
    pallocator_pmm<double> ad( ai );
    pallocator_pmm<int>    ai2( ad );
    REQUIRE( ai == ai2 );
}

// ---------------------------------------------------------------------------
// pallocator_pmm — allocate возвращает ненулевой указатель
// ---------------------------------------------------------------------------
TEST_CASE( "pallocator_pmm<int>: allocate(n) returns non-null pointer for n > 0",
           "[pallocator_pmm][allocate]" )
{
    if ( !pam_pmm_is_initialized() )
        pam_pmm_init( nullptr );

    pallocator_pmm<int> a;
    int*                p = a.allocate( 4 );
    REQUIRE( p != nullptr );

    // Деаллоцируем, чтобы не утекала память.
    a.deallocate( p, 4 );
}

// ---------------------------------------------------------------------------
// pallocator_pmm — allocate(0) возвращает nullptr
// ---------------------------------------------------------------------------
TEST_CASE( "pallocator_pmm<int>: allocate(0) returns nullptr", "[pallocator_pmm][allocate]" )
{
    if ( !pam_pmm_is_initialized() )
        pam_pmm_init( nullptr );

    pallocator_pmm<int> a;
    int*                p = a.allocate( 0 );
    REQUIRE( p == nullptr );
}

// ---------------------------------------------------------------------------
// pallocator_pmm — выделенная память доступна для чтения и записи
// ---------------------------------------------------------------------------
TEST_CASE( "pallocator_pmm<int>: allocated memory is readable and writable",
           "[pallocator_pmm][allocate][readwrite]" )
{
    if ( !pam_pmm_is_initialized() )
        pam_pmm_init( nullptr );

    pallocator_pmm<int> a;
    const std::size_t   N = 5;
    int*                p = a.allocate( N );
    REQUIRE( p != nullptr );

    for ( std::size_t i = 0; i < N; i++ )
        p[i] = static_cast<int>( i * 11 );

    for ( std::size_t i = 0; i < N; i++ )
        REQUIRE( p[i] == static_cast<int>( i * 11 ) );

    a.deallocate( p, N );
}

// ---------------------------------------------------------------------------
// pallocator_pmm — construct и destroy
// ---------------------------------------------------------------------------
TEST_CASE( "pallocator_pmm<int>: construct places value and destroy cleans up",
           "[pallocator_pmm][construct_destroy]" )
{
    if ( !pam_pmm_is_initialized() )
        pam_pmm_init( nullptr );

    pallocator_pmm<int> a;
    int*                p = a.allocate( 1 );
    REQUIRE( p != nullptr );

    a.construct( p, 42 );
    REQUIRE( *p == 42 );

    a.destroy( p );
    a.deallocate( p, 1 );
}

// ---------------------------------------------------------------------------
// pallocator_pmm — max_size возвращает положительное значение
// ---------------------------------------------------------------------------
TEST_CASE( "pallocator_pmm<int>: max_size() returns a positive value", "[pallocator_pmm][max_size]" )
{
    pallocator_pmm<int> a;
    REQUIRE( a.max_size() > 0u );
}

// ---------------------------------------------------------------------------
// pallocator_pmm — operator== и operator!=
// ---------------------------------------------------------------------------
TEST_CASE( "pallocator_pmm: any two pallocator_pmm instances compare equal", "[pallocator_pmm][equality]" )
{
    pallocator_pmm<int> a1;
    pallocator_pmm<int> a2;
    REQUIRE( a1 == a2 );
    REQUIRE( !( a1 != a2 ) );
}

// ---------------------------------------------------------------------------
// pallocator_pmm — использование с std::vector
// ---------------------------------------------------------------------------
TEST_CASE( "pallocator_pmm<int>: std::vector<int, pallocator_pmm<int>> push_back and read back",
           "[pallocator_pmm][std_vector]" )
{
    if ( !pam_pmm_is_initialized() )
        pam_pmm_init( nullptr );

    std::vector<int, pallocator_pmm<int>> v;
    v.push_back( 10 );
    v.push_back( 20 );
    v.push_back( 30 );

    REQUIRE( v.size() == 3u );
    REQUIRE( v[0] == 10 );
    REQUIRE( v[1] == 20 );
    REQUIRE( v[2] == 30 );
}

// ---------------------------------------------------------------------------
// pallocator_pmm — std::vector рост ёмкости
// ---------------------------------------------------------------------------
TEST_CASE( "pallocator_pmm<int>: std::vector grows capacity using pallocator_pmm",
           "[pallocator_pmm][std_vector][capacity]" )
{
    if ( !pam_pmm_is_initialized() )
        pam_pmm_init( nullptr );

    std::vector<int, pallocator_pmm<int>> v;
    for ( int i = 0; i < 20; i++ )
        v.push_back( i );

    REQUIRE( v.size() == 20u );
    for ( int i = 0; i < 20; i++ )
        REQUIRE( v[static_cast<std::size_t>( i )] == i );
}

// ---------------------------------------------------------------------------
// pallocator_pmm — несколько независимых аллокаций не перекрываются
// ---------------------------------------------------------------------------
TEST_CASE( "pallocator_pmm<double>: multiple independent allocations do not alias",
           "[pallocator_pmm][allocate][no_alias]" )
{
    if ( !pam_pmm_is_initialized() )
        pam_pmm_init( nullptr );

    pallocator_pmm<double> a;
    double*                p1 = a.allocate( 3 );
    double*                p2 = a.allocate( 3 );

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
// pallocator_pmm — deallocate(nullptr) безопасен
// ---------------------------------------------------------------------------
TEST_CASE( "pallocator_pmm<int>: deallocate(nullptr) is safe", "[pallocator_pmm][deallocate]" )
{
    pallocator_pmm<int> a;
    // Не должен крашиться.
    a.deallocate( nullptr, 0 );
}

// ---------------------------------------------------------------------------
// pjson::pallocator алиас = pallocator_pmm
// ---------------------------------------------------------------------------
TEST_CASE( "pjson::pallocator<T> alias works as pallocator_pmm<T>", "[pallocator_pmm][alias]" )
{
    if ( !pam_pmm_is_initialized() )
        pam_pmm_init( nullptr );

    pjson::pallocator<int> a;
    int*                   p = a.allocate( 2 );
    REQUIRE( p != nullptr );

    p[0] = 100;
    p[1] = 200;
    REQUIRE( p[0] == 100 );
    REQUIRE( p[1] == 200 );

    a.deallocate( p, 2 );
}
