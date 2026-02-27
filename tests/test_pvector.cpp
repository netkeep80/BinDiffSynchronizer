#include <catch2/catch_test_macros.hpp>

#include <type_traits>

#include "pvector.h"

// =============================================================================
// Tests for pvector<T> (persistent dynamic array)
// =============================================================================

// ---------------------------------------------------------------------------
// pvector<T> — layout checks
// ---------------------------------------------------------------------------
TEST_CASE( "pvector<int>: size and capacity are uintptr_t (Phase 3)", "[pvector][layout][phase3]" )
{
    // sizeof(uintptr_t) == sizeof(void*) на любой платформе.
    // pvector<T> = size_ (uintptr_t) + capacity_ (uintptr_t) + data_ (fptr<T>) = 3 * sizeof(void*)
    REQUIRE( sizeof( pvector<int> ) == 3 * sizeof( void* ) );
    REQUIRE( sizeof( pvector<double> ) == 3 * sizeof( void* ) );
}

// ---------------------------------------------------------------------------
// pvector — default PAP allocation (empty)
// ---------------------------------------------------------------------------
TEST_CASE( "pvector<int>: zero-initialised pvector (via fptr) gives empty vector", "[pvector][construct]" )
{
    fptr<pvector<int>> fv;
    fv.New();

    REQUIRE( fv->empty() );
    REQUIRE( fv->size() == 0u );
    REQUIRE( fv->capacity() == 0u );

    fv.Delete();
}

// ---------------------------------------------------------------------------
// pvector — push_back and size
// ---------------------------------------------------------------------------
TEST_CASE( "pvector<int>: push_back increases size and stores correct values", "[pvector][push_back]" )
{
    fptr<pvector<int>> fv;
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
// pvector — capacity grows (doubling strategy)
// ---------------------------------------------------------------------------
TEST_CASE( "pvector<int>: capacity grows to accommodate elements", "[pvector][capacity]" )
{
    fptr<pvector<int>> fv;
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
// pvector — pop_back decreases size
// ---------------------------------------------------------------------------
TEST_CASE( "pvector<int>: pop_back decreases size", "[pvector][pop_back]" )
{
    fptr<pvector<int>> fv;
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
// pvector — clear resets size to 0 (does not free allocation)
// ---------------------------------------------------------------------------
TEST_CASE( "pvector<int>: clear resets size to 0", "[pvector][clear]" )
{
    fptr<pvector<int>> fv;
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
// pvector — free releases the allocation
// ---------------------------------------------------------------------------
TEST_CASE( "pvector<int>: free releases allocation and resets capacity", "[pvector][free]" )
{
    fptr<pvector<int>> fv;
    fv.New();

    fv->push_back( 42 );

    fv->free();
    REQUIRE( fv->size() == 0u );
    REQUIRE( fv->capacity() == 0u );

    fv.Delete();
}

// ---------------------------------------------------------------------------
// pvector — front and back
// ---------------------------------------------------------------------------
TEST_CASE( "pvector<int>: front() and back() return correct elements", "[pvector][access]" )
{
    fptr<pvector<int>> fv;
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
// pvector — iterator
// ---------------------------------------------------------------------------
TEST_CASE( "pvector<int>: range-based iteration produces correct values", "[pvector][iterator]" )
{
    fptr<pvector<int>> fv;
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
// pvector<double> — works with double
// ---------------------------------------------------------------------------
TEST_CASE( "pvector<double>: push_back and read back doubles", "[pvector][double]" )
{
    fptr<pvector<double>> fv;
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
