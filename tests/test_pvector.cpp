/**
 * @file test_pvector.cpp
 * @brief Дополнительные тесты для PamManager::parray<T>.
 *
 * Тесты capacity growth, front/back, data pointer iteration, double type.
 * (Основные тесты parray — в test_pmem_array_pmm.cpp)
 */

#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <type_traits>

#include "pam_pmm.h"

using namespace pjson;

static void ensure_pmm()
{
    if ( !pam_pmm_is_initialized() )
        pam_pmm_init( nullptr );
}

// =============================================================================
// Tests for PamManager::parray<T> — capacity growth
// =============================================================================

TEST_CASE( "parray<int>: capacity grows to accommodate elements", "[parray][capacity]" )
{
    ensure_pmm();

    auto arr_pptr = PamManager::template allocate_typed<PamManager::parray<int>>();
    std::memset( arr_pptr.resolve(), 0, sizeof( PamManager::parray<int> ) );

    for ( int i = 0; i < 20; i++ )
        arr_pptr->push_back( i );

    REQUIRE( arr_pptr->size() == 20u );
    REQUIRE( arr_pptr->capacity() >= 20u );

    int* d = arr_pptr->data();
    for ( int i = 0; i < 20; i++ )
        REQUIRE( d[i] == i );

    arr_pptr->free_data();
    PamManager::template deallocate_typed<PamManager::parray<int>>( arr_pptr );
}

// =============================================================================
// parray — front and back via data()
// =============================================================================

TEST_CASE( "parray<int>: front and back via data()", "[parray][access]" )
{
    ensure_pmm();

    auto arr_pptr = PamManager::template allocate_typed<PamManager::parray<int>>();
    std::memset( arr_pptr.resolve(), 0, sizeof( PamManager::parray<int> ) );

    arr_pptr->push_back( 10 );
    arr_pptr->push_back( 20 );
    arr_pptr->push_back( 30 );

    int* d = arr_pptr->data();
    REQUIRE( d[0] == 10 );
    REQUIRE( d[arr_pptr->size() - 1] == 30 );

    arr_pptr->free_data();
    PamManager::template deallocate_typed<PamManager::parray<int>>( arr_pptr );
}

// =============================================================================
// parray — pointer iteration over data()
// =============================================================================

TEST_CASE( "parray<int>: pointer iteration over data() produces correct sum", "[parray][iterator]" )
{
    ensure_pmm();

    auto arr_pptr = PamManager::template allocate_typed<PamManager::parray<int>>();
    std::memset( arr_pptr.resolve(), 0, sizeof( PamManager::parray<int> ) );

    arr_pptr->push_back( 1 );
    arr_pptr->push_back( 2 );
    arr_pptr->push_back( 3 );

    int  sum = 0;
    int* d   = arr_pptr->data();
    for ( uint32_t i = 0; i < arr_pptr->size(); ++i )
        sum += d[i];

    REQUIRE( sum == 6 );

    arr_pptr->free_data();
    PamManager::template deallocate_typed<PamManager::parray<int>>( arr_pptr );
}

// =============================================================================
// parray<double> — works with double
// =============================================================================

TEST_CASE( "parray<double>: push_back and read back doubles", "[parray][double]" )
{
    ensure_pmm();

    auto arr_pptr = PamManager::template allocate_typed<PamManager::parray<double>>();
    std::memset( arr_pptr.resolve(), 0, sizeof( PamManager::parray<double> ) );

    arr_pptr->push_back( 1.1 );
    arr_pptr->push_back( 2.2 );
    arr_pptr->push_back( 3.3 );

    REQUIRE( arr_pptr->size() == 3u );

    double* d = arr_pptr->data();
    REQUIRE( d[0] == 1.1 );
    REQUIRE( d[1] == 2.2 );
    REQUIRE( d[2] == 3.3 );

    arr_pptr->free_data();
    PamManager::template deallocate_typed<PamManager::parray<double>>( arr_pptr );
}

// =============================================================================
// parray — empty after clear then push_back works
// =============================================================================

TEST_CASE( "parray<int>: push_back after clear works correctly", "[parray][clear][push_back]" )
{
    ensure_pmm();

    auto arr_pptr = PamManager::template allocate_typed<PamManager::parray<int>>();
    std::memset( arr_pptr.resolve(), 0, sizeof( PamManager::parray<int> ) );

    arr_pptr->push_back( 1 );
    arr_pptr->push_back( 2 );
    arr_pptr->clear();

    REQUIRE( arr_pptr->size() == 0u );
    REQUIRE( arr_pptr->empty() );

    arr_pptr->push_back( 42 );
    REQUIRE( arr_pptr->size() == 1u );

    int* d = arr_pptr->data();
    REQUIRE( d[0] == 42 );

    arr_pptr->free_data();
    PamManager::template deallocate_typed<PamManager::parray<int>>( arr_pptr );
}

// =============================================================================
// parray — pop_back on empty is safe (no-op)
// =============================================================================

TEST_CASE( "parray<int>: pop_back on empty is no-op", "[parray][pop_back]" )
{
    ensure_pmm();

    auto arr_pptr = PamManager::template allocate_typed<PamManager::parray<int>>();
    std::memset( arr_pptr.resolve(), 0, sizeof( PamManager::parray<int> ) );

    // pop_back on empty should be safe
    arr_pptr->pop_back();
    REQUIRE( arr_pptr->size() == 0u );

    arr_pptr->free_data();
    PamManager::template deallocate_typed<PamManager::parray<int>>( arr_pptr );
}
