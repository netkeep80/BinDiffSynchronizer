#include <catch2/catch_test_macros.hpp>

#include <type_traits>

#include "pmap_pmm.h"
#include "pam_pmm.h"
#include "fptr_pmm.h"

using namespace pjson;

static void ensure_pmm()
{
    if ( !pam_pmm_is_initialized() )
        pam_pmm_init( nullptr );
}

// =============================================================================
// Tests for pmap_pmm<K, V> (persistent key-value map)
// =============================================================================

// ---------------------------------------------------------------------------
// pmap_entry_pmm<K,V> — trivially copyable
// ---------------------------------------------------------------------------
TEST_CASE( "pmap_entry_pmm<int,int>: is trivially copyable", "[pmap_pmm][layout]" )
{
    REQUIRE( std::is_trivially_copyable<pmap_entry_pmm<int, int>>::value );
    REQUIRE( std::is_trivially_copyable<pmap_entry_pmm<int, double>>::value );
}

// ---------------------------------------------------------------------------
// pmap_pmm<T> — layout check
// ---------------------------------------------------------------------------
TEST_CASE( "pmap_pmm<int,int>: size is 3 * sizeof(void*) (size, capacity, data_)", "[pmap_pmm][layout]" )
{
    // pmap_pmm<K,V> = size_ (uintptr_t) + capacity_ (uintptr_t) + data_ (fptr<Entry>) = 3 * sizeof(void*)
    REQUIRE( sizeof( pmap_pmm<int, int> ) == 3 * sizeof( void* ) );
}

// ---------------------------------------------------------------------------
// pmap_pmm — default PAP allocation (empty)
// ---------------------------------------------------------------------------
TEST_CASE( "pmap_pmm<int,int>: zero-initialised pmap_pmm (via fptr) gives empty map", "[pmap_pmm][construct]" )
{
    ensure_pmm();
    fptr<pmap_pmm<int, int>> fm;
    fm.New();

    REQUIRE( fm->empty() );
    REQUIRE( fm->size() == 0u );

    fm.Delete();
}

// ---------------------------------------------------------------------------
// pmap_pmm — insert single entry and find
// ---------------------------------------------------------------------------
TEST_CASE( "pmap_pmm<int,int>: insert single entry and find it", "[pmap_pmm][insert][find]" )
{
    ensure_pmm();
    fptr<pmap_pmm<int, int>> fm;
    fm.New();

    fm->insert( 42, 100 );
    REQUIRE( fm->size() == 1u );
    REQUIRE( !fm->empty() );

    int* val = fm->find( 42 );
    REQUIRE( val != nullptr );
    REQUIRE( *val == 100 );

    fm->free();
    fm.Delete();
}

// ---------------------------------------------------------------------------
// pmap_pmm — find missing key returns nullptr
// ---------------------------------------------------------------------------
TEST_CASE( "pmap_pmm<int,int>: find missing key returns nullptr", "[pmap_pmm][find]" )
{
    ensure_pmm();
    fptr<pmap_pmm<int, int>> fm;
    fm.New();

    fm->insert( 1, 10 );
    REQUIRE( fm->find( 999 ) == nullptr );

    fm->free();
    fm.Delete();
}

// ---------------------------------------------------------------------------
// pmap_pmm — insert maintains sorted order
// ---------------------------------------------------------------------------
TEST_CASE( "pmap_pmm<int,int>: insert multiple entries in sorted order", "[pmap_pmm][insert][sorted]" )
{
    ensure_pmm();
    fptr<pmap_pmm<int, int>> fm;
    fm.New();

    fm->insert( 30, 300 );
    fm->insert( 10, 100 );
    fm->insert( 20, 200 );

    REQUIRE( fm->size() == 3u );

    // Entries must be in ascending key order: 10, 20, 30.
    auto it = fm->begin();
    REQUIRE( it->key == 10 );
    REQUIRE( it->value == 100 );
    ++it;
    REQUIRE( it->key == 20 );
    REQUIRE( it->value == 200 );
    ++it;
    REQUIRE( it->key == 30 );
    REQUIRE( it->value == 300 );
    ++it;
    REQUIRE( it == fm->end() );

    fm->free();
    fm.Delete();
}

// ---------------------------------------------------------------------------
// pmap_pmm — update existing key
// ---------------------------------------------------------------------------
TEST_CASE( "pmap_pmm<int,int>: inserting existing key updates value", "[pmap_pmm][insert][update]" )
{
    ensure_pmm();
    fptr<pmap_pmm<int, int>> fm;
    fm.New();

    fm->insert( 5, 50 );
    REQUIRE( fm->size() == 1u );

    fm->insert( 5, 99 );
    REQUIRE( fm->size() == 1u );

    int* val = fm->find( 5 );
    REQUIRE( val != nullptr );
    REQUIRE( *val == 99 );

    fm->free();
    fm.Delete();
}

// ---------------------------------------------------------------------------
// pmap_pmm — erase existing key
// ---------------------------------------------------------------------------
TEST_CASE( "pmap_pmm<int,int>: erase removes the correct entry", "[pmap_pmm][erase]" )
{
    ensure_pmm();
    fptr<pmap_pmm<int, int>> fm;
    fm.New();

    fm->insert( 1, 10 );
    fm->insert( 2, 20 );
    fm->insert( 3, 30 );
    REQUIRE( fm->size() == 3u );

    bool ok = fm->erase( 2 );
    REQUIRE( ok );
    REQUIRE( fm->size() == 2u );
    REQUIRE( fm->find( 2 ) == nullptr );
    REQUIRE( fm->find( 1 ) != nullptr );
    REQUIRE( fm->find( 3 ) != nullptr );

    fm->free();
    fm.Delete();
}

// ---------------------------------------------------------------------------
// pmap_pmm — erase missing key returns false
// ---------------------------------------------------------------------------
TEST_CASE( "pmap_pmm<int,int>: erase missing key returns false", "[pmap_pmm][erase]" )
{
    ensure_pmm();
    fptr<pmap_pmm<int, int>> fm;
    fm.New();

    fm->insert( 1, 10 );
    bool ok = fm->erase( 999 );
    REQUIRE( !ok );
    REQUIRE( fm->size() == 1u );

    fm->free();
    fm.Delete();
}

// ---------------------------------------------------------------------------
// pmap_pmm — operator[]
// ---------------------------------------------------------------------------
TEST_CASE( "pmap_pmm<int,int>: operator[] inserts default value for missing key", "[pmap_pmm][operator_index]" )
{
    ensure_pmm();
    fptr<pmap_pmm<int, int>> fm;
    fm.New();

    // Key 7 does not exist; operator[] should insert it with default value 0.
    ( *fm )[7] = 77;
    REQUIRE( fm->size() == 1u );
    REQUIRE( fm->find( 7 ) != nullptr );
    REQUIRE( *fm->find( 7 ) == 77 );

    fm->free();
    fm.Delete();
}

// ---------------------------------------------------------------------------
// pmap_pmm — clear resets size to 0
// ---------------------------------------------------------------------------
TEST_CASE( "pmap_pmm<int,int>: clear resets size to 0", "[pmap_pmm][clear]" )
{
    ensure_pmm();
    fptr<pmap_pmm<int, int>> fm;
    fm.New();

    fm->insert( 1, 1 );
    fm->insert( 2, 2 );
    REQUIRE( fm->size() == 2u );

    fm->clear();
    REQUIRE( fm->empty() );
    REQUIRE( fm->size() == 0u );

    fm->free();
    fm.Delete();
}

// ---------------------------------------------------------------------------
// pmap_pmm — free releases allocation
// ---------------------------------------------------------------------------
TEST_CASE( "pmap_pmm<int,int>: free releases underlying allocation", "[pmap_pmm][free]" )
{
    ensure_pmm();
    fptr<pmap_pmm<int, int>> fm;
    fm.New();

    fm->insert( 10, 100 );
    REQUIRE( fm->size() == 1u );

    fm->free();
    REQUIRE( fm->size() == 0u );

    fm.Delete();
}

// ---------------------------------------------------------------------------
// pmap_pmm<int, double> — works with double values
// ---------------------------------------------------------------------------
TEST_CASE( "pmap_pmm<int,double>: insert and find double values", "[pmap_pmm][double]" )
{
    ensure_pmm();
    fptr<pmap_pmm<int, double>> fm;
    fm.New();

    fm->insert( 1, 1.1 );
    fm->insert( 2, 2.2 );
    fm->insert( 3, 3.3 );

    REQUIRE( fm->size() == 3u );
    REQUIRE( *fm->find( 1 ) == 1.1 );
    REQUIRE( *fm->find( 2 ) == 2.2 );
    REQUIRE( *fm->find( 3 ) == 3.3 );

    fm->free();
    fm.Delete();
}

// ---------------------------------------------------------------------------
// pmap_pmm — range-based iteration
// ---------------------------------------------------------------------------
TEST_CASE( "pmap_pmm<int,int>: range-based iteration visits all entries", "[pmap_pmm][iterator]" )
{
    ensure_pmm();
    fptr<pmap_pmm<int, int>> fm;
    fm.New();

    fm->insert( 1, 10 );
    fm->insert( 2, 20 );
    fm->insert( 3, 30 );

    int key_sum = 0;
    int val_sum = 0;
    for ( auto& entry : *fm )
    {
        key_sum += entry.key;
        val_sum += entry.value;
    }
    REQUIRE( key_sum == 6 );
    REQUIRE( val_sum == 60 );

    fm->free();
    fm.Delete();
}
