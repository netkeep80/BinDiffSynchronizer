#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <type_traits>

#include "pstringview.h"

// =============================================================================
// Tests for pstringview (persistent read-only interned string)
// =============================================================================

// ---------------------------------------------------------------------------
// pstringview — layout checks
// ---------------------------------------------------------------------------
TEST_CASE( "pstringview: struct size is 2 * sizeof(void*)", "[pstringview][layout]" )
{
    REQUIRE( sizeof( pstringview ) == 2 * sizeof( void* ) );
    REQUIRE( sizeof( pstringview::length ) == sizeof( void* ) );
    REQUIRE( sizeof( pstringview::chars_offset ) == sizeof( void* ) );
}

// ---------------------------------------------------------------------------
// pstringview_table — layout checks
// ---------------------------------------------------------------------------
TEST_CASE( "pstringview_table: struct size is 3 * sizeof(void*)", "[pstringview][layout]" )
{
    REQUIRE( sizeof( pstringview_table ) == 3 * sizeof( void* ) );
}

// ---------------------------------------------------------------------------
// pstringview — default allocation gives empty string
// ---------------------------------------------------------------------------
TEST_CASE( "pstringview: zero-initialised pstringview gives empty string", "[pstringview][construct]" )
{
    pstringview_manager::reset();
    PersistentAddressSpace::Get().Reset();

    fptr<pstringview> fps;
    fps.New();

    REQUIRE( fps->empty() );
    REQUIRE( fps->size() == 0u );
    REQUIRE( std::strcmp( fps->c_str(), "" ) == 0 );

    fps.Delete();
}

// ---------------------------------------------------------------------------
// pstringview — intern short string
// ---------------------------------------------------------------------------
TEST_CASE( "pstringview: intern stores correct content", "[pstringview][intern]" )
{
    pstringview_manager::reset();
    PersistentAddressSpace::Get().Reset();

    fptr<pstringview> fps;
    fps.New();

    fps->intern( "hello" );
    REQUIRE( !fps->empty() );
    REQUIRE( fps->size() == 5u );
    REQUIRE( std::strcmp( fps->c_str(), "hello" ) == 0 );

    fps.Delete();
}

// ---------------------------------------------------------------------------
// pstringview — intern returns same chars_offset for duplicate strings
// ---------------------------------------------------------------------------
TEST_CASE( "pstringview: same string always yields same chars_offset (interning)", "[pstringview][intern]" )
{
    pstringview_manager::reset();
    PersistentAddressSpace::Get().Reset();

    fptr<pstringview> fps1;
    fps1.New();
    fps1->intern( "key" );

    fptr<pstringview> fps2;
    fps2.New();
    fps2->intern( "key" );

    // Интернирование гарантирует одинаковый chars_offset для одинаковых строк.
    REQUIRE( fps1->chars_offset == fps2->chars_offset );
    REQUIRE( fps1->length == fps2->length );
    REQUIRE( std::strcmp( fps1->c_str(), fps2->c_str() ) == 0 );

    fps1.Delete();
    fps2.Delete();
}

// ---------------------------------------------------------------------------
// pstringview — different strings have different chars_offset
// ---------------------------------------------------------------------------
TEST_CASE( "pstringview: different strings have different chars_offset", "[pstringview][intern]" )
{
    pstringview_manager::reset();
    PersistentAddressSpace::Get().Reset();

    fptr<pstringview> fps1;
    fps1.New();
    fps1->intern( "foo" );

    fptr<pstringview> fps2;
    fps2.New();
    fps2->intern( "bar" );

    REQUIRE( fps1->chars_offset != fps2->chars_offset );

    fps1.Delete();
    fps2.Delete();
}

// ---------------------------------------------------------------------------
// pstringview — operator== uses chars_offset comparison (O(1))
// ---------------------------------------------------------------------------
TEST_CASE( "pstringview: operator== compares by chars_offset", "[pstringview][compare]" )
{
    pstringview_manager::reset();
    PersistentAddressSpace::Get().Reset();

    fptr<pstringview> fps1;
    fps1.New();
    fps1->intern( "hello" );

    fptr<pstringview> fps2;
    fps2.New();
    fps2->intern( "hello" );

    fptr<pstringview> fps3;
    fps3.New();
    fps3->intern( "world" );

    REQUIRE( *fps1 == *fps2 );
    REQUIRE( !( *fps1 == *fps3 ) );
    REQUIRE( *fps1 == "hello" );
    REQUIRE( !( *fps1 == "world" ) );

    fps1.Delete();
    fps2.Delete();
    fps3.Delete();
}

// ---------------------------------------------------------------------------
// pstringview — operator< gives lexicographic order
// ---------------------------------------------------------------------------
TEST_CASE( "pstringview: operator< gives lexicographic order", "[pstringview][compare]" )
{
    pstringview_manager::reset();
    PersistentAddressSpace::Get().Reset();

    fptr<pstringview> fps_a;
    fps_a.New();
    fps_a->intern( "apple" );

    fptr<pstringview> fps_b;
    fps_b.New();
    fps_b->intern( "banana" );

    REQUIRE( *fps_a < *fps_b );
    REQUIRE( !( *fps_b < *fps_a ) );

    fps_a.Delete();
    fps_b.Delete();
}

// ---------------------------------------------------------------------------
// pstringview — intern empty string
// ---------------------------------------------------------------------------
TEST_CASE( "pstringview: intern empty string gives empty result", "[pstringview][intern]" )
{
    pstringview_manager::reset();
    PersistentAddressSpace::Get().Reset();

    fptr<pstringview> fps;
    fps.New();

    fps->intern( "" );
    REQUIRE( fps->empty() );
    REQUIRE( fps->size() == 0u );
    REQUIRE( std::strcmp( fps->c_str(), "" ) == 0 );

    fps.Delete();
}

// ---------------------------------------------------------------------------
// pstringview — intern nullptr treated as empty
// ---------------------------------------------------------------------------
TEST_CASE( "pstringview: intern nullptr treated as empty string", "[pstringview][intern]" )
{
    pstringview_manager::reset();
    PersistentAddressSpace::Get().Reset();

    fptr<pstringview> fps;
    fps.New();

    fps->intern( nullptr );
    REQUIRE( fps->empty() );
    REQUIRE( fps->size() == 0u );

    fps.Delete();
}

// ---------------------------------------------------------------------------
// pstringview — chars_offset is non-zero after intern (non-empty)
// ---------------------------------------------------------------------------
TEST_CASE( "pstringview: chars_offset is non-zero after intern non-empty string", "[pstringview][intern]" )
{
    pstringview_manager::reset();
    PersistentAddressSpace::Get().Reset();

    fptr<pstringview> fps;
    fps.New();

    REQUIRE( fps->chars_offset == 0u );

    fps->intern( "test" );
    REQUIRE( fps->chars_offset != 0u );
    REQUIRE( fps->length == 4u );

    fps.Delete();
}

// ---------------------------------------------------------------------------
// pstringview — many distinct strings all interned correctly
// ---------------------------------------------------------------------------
TEST_CASE( "pstringview: many distinct strings are all interned correctly", "[pstringview][intern]" )
{
    pstringview_manager::reset();
    PersistentAddressSpace::Get().Reset();

    static const char* words[] = { "alpha", "beta", "gamma", "delta", "epsilon",
                                   "zeta",  "eta",  "theta", "iota",  "kappa" };
    constexpr int      N       = 10;

    fptr<pstringview> fps[N];
    for ( int i = 0; i < N; i++ )
    {
        fps[i].New();
        fps[i]->intern( words[i] );
        REQUIRE( std::strcmp( fps[i]->c_str(), words[i] ) == 0 );
    }

    // Повторное интернирование — должны вернуться те же смещения.
    for ( int i = 0; i < N; i++ )
    {
        fptr<pstringview> dup;
        dup.New();
        dup->intern( words[i] );
        REQUIRE( dup->chars_offset == fps[i]->chars_offset );
        dup.Delete();
    }

    for ( int i = 0; i < N; i++ )
        fps[i].Delete();
}

// ---------------------------------------------------------------------------
// pstringview — pstringview_table trivially copyable
// ---------------------------------------------------------------------------
TEST_CASE( "pstringview_entry: is trivially copyable", "[pstringview][layout]" )
{
    REQUIRE( std::is_trivially_copyable<pstringview_entry>::value );
}
