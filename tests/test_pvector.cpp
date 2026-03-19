#include <catch2/catch_test_macros.hpp>

#include <type_traits>

#include "pvector_pmm.h"
#include "fptr_pmm.h"
#include "pam_pmm.h"

using namespace pjson;

static void ensure_pmm()
{
    if ( !pam_pmm_is_initialized() )
        pam_pmm_init( nullptr );
}

// =============================================================================
// Tests for pvector_pmm<T> (persistent dynamic array)
// =============================================================================

// ---------------------------------------------------------------------------
// pvector_pmm<T> — layout checks
// ---------------------------------------------------------------------------
TEST_CASE( "pvector_pmm<int>: size and capacity are uintptr_t", "[pvector_pmm][layout]" )
{
    // pvector_pmm<T> оборачивает PamManager::parray<T> = uint32_t * 3 = 12 байт
    REQUIRE( sizeof( pvector_pmm<int> ) == sizeof( PamManager::parray<int> ) );
    REQUIRE( sizeof( pvector_pmm<double> ) == sizeof( PamManager::parray<double> ) );
    REQUIRE( sizeof( pvector_pmm<int> ) == 12u );
}

// ---------------------------------------------------------------------------
// pvector_pmm — default PAP allocation (empty)
// ---------------------------------------------------------------------------
TEST_CASE( "pvector_pmm<int>: zero-initialised pvector_pmm (via fptr) gives empty vector", "[pvector_pmm][construct]" )
{
    ensure_pmm();
    fptr<pvector_pmm<int>> fv;
    fv.New();

    REQUIRE( fv->empty() );
    REQUIRE( fv->size() == 0u );
    REQUIRE( fv->capacity() == 0u );

    fv.Delete();
}

// ---------------------------------------------------------------------------
// pvector_pmm — push_back and size
// ---------------------------------------------------------------------------
TEST_CASE( "pvector_pmm<int>: push_back increases size and stores correct values", "[pvector_pmm][push_back]" )
{
    ensure_pmm();
    fptr<pvector_pmm<int>> fv;
    fv.New();

    fv->push_back( 10 );
    fv->push_back( 20 );
    fv->push_back( 30 );

    REQUIRE( fv->size() == 3u );
    REQUIRE( ( *fv )[0] == 10 );
    REQUIRE( ( *fv )[1] == 20 );
    REQUIRE( ( *fv )[2] == 30 );

    fv->free();
    fv.Delete();
}

// ---------------------------------------------------------------------------
// pvector_pmm — capacity grows (doubling strategy)
// ---------------------------------------------------------------------------
TEST_CASE( "pvector_pmm<int>: capacity grows to accommodate elements", "[pvector_pmm][capacity]" )
{
    ensure_pmm();
    fptr<pvector_pmm<int>> fv;
    fv.New();

    for ( int i = 0; i < 20; i++ )
        fv->push_back( i );

    REQUIRE( fv->size() == 20u );
    REQUIRE( fv->capacity() >= 20u );

    for ( int i = 0; i < 20; i++ )
        REQUIRE( ( *fv )[static_cast<unsigned>( i )] == i );

    fv->free();
    fv.Delete();
}

// ---------------------------------------------------------------------------
// pvector_pmm — pop_back decreases size
// ---------------------------------------------------------------------------
TEST_CASE( "pvector_pmm<int>: pop_back decreases size", "[pvector_pmm][pop_back]" )
{
    ensure_pmm();
    fptr<pvector_pmm<int>> fv;
    fv.New();

    fv->push_back( 1 );
    fv->push_back( 2 );
    fv->push_back( 3 );
    REQUIRE( fv->size() == 3u );

    fv->pop_back();
    REQUIRE( fv->size() == 2u );
    REQUIRE( ( *fv )[0] == 1 );
    REQUIRE( ( *fv )[1] == 2 );

    fv->pop_back();
    fv->pop_back();
    REQUIRE( fv->size() == 0u );
    REQUIRE( fv->empty() );

    // pop_back on empty should be safe (no-op).
    fv->pop_back();
    REQUIRE( fv->size() == 0u );

    fv.Delete();
}

// ---------------------------------------------------------------------------
// pvector_pmm — clear resets size to 0 (does not free allocation)
// ---------------------------------------------------------------------------
TEST_CASE( "pvector_pmm<int>: clear resets size to 0", "[pvector_pmm][clear]" )
{
    ensure_pmm();
    fptr<pvector_pmm<int>> fv;
    fv.New();

    fv->push_back( 1 );
    fv->push_back( 2 );
    uintptr_t cap_before = fv->capacity();

    fv->clear();
    REQUIRE( fv->size() == 0u );
    REQUIRE( fv->empty() );
    // Capacity should be unchanged by clear().
    REQUIRE( fv->capacity() == cap_before );

    fv->free();
    fv.Delete();
}

// ---------------------------------------------------------------------------
// pvector_pmm — free releases the allocation
// ---------------------------------------------------------------------------
TEST_CASE( "pvector_pmm<int>: free releases allocation and resets capacity", "[pvector_pmm][free]" )
{
    ensure_pmm();
    fptr<pvector_pmm<int>> fv;
    fv.New();

    fv->push_back( 42 );

    fv->free();
    REQUIRE( fv->size() == 0u );
    REQUIRE( fv->capacity() == 0u );

    fv.Delete();
}

// ---------------------------------------------------------------------------
// pvector_pmm — front and back
// ---------------------------------------------------------------------------
TEST_CASE( "pvector_pmm<int>: front() and back() return correct elements", "[pvector_pmm][access]" )
{
    ensure_pmm();
    fptr<pvector_pmm<int>> fv;
    fv.New();

    fv->push_back( 10 );
    fv->push_back( 20 );
    fv->push_back( 30 );

    REQUIRE( fv->front() == 10 );
    REQUIRE( fv->back() == 30 );

    fv->free();
    fv.Delete();
}

// ---------------------------------------------------------------------------
// pvector_pmm — iterator
// ---------------------------------------------------------------------------
TEST_CASE( "pvector_pmm<int>: range-based iteration produces correct values", "[pvector_pmm][iterator]" )
{
    ensure_pmm();
    fptr<pvector_pmm<int>> fv;
    fv.New();

    fv->push_back( 1 );
    fv->push_back( 2 );
    fv->push_back( 3 );

    int sum = 0;
    for ( auto& elem : *fv )
        sum += elem;

    REQUIRE( sum == 6 );

    fv->free();
    fv.Delete();
}

// ---------------------------------------------------------------------------
// pvector_pmm<double> — works with double
// ---------------------------------------------------------------------------
TEST_CASE( "pvector_pmm<double>: push_back and read back doubles", "[pvector_pmm][double]" )
{
    ensure_pmm();
    fptr<pvector_pmm<double>> fv;
    fv.New();

    fv->push_back( 1.1 );
    fv->push_back( 2.2 );
    fv->push_back( 3.3 );

    REQUIRE( fv->size() == 3u );
    REQUIRE( ( *fv )[0] == 1.1 );
    REQUIRE( ( *fv )[1] == 2.2 );
    REQUIRE( ( *fv )[2] == 3.3 );

    fv->free();
    fv.Delete();
}
