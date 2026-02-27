#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <type_traits>

#include "pstring.h"

// =============================================================================
// Tests for pstring (persistent string)
// =============================================================================

// ---------------------------------------------------------------------------
// pstring — layout checks
// ---------------------------------------------------------------------------
TEST_CASE( "pstring: length field is uintptr_t (Phase 3)", "[pstring][layout][phase3]" )
{
    // sizeof(uintptr_t) == sizeof(void*) на любой платформе.
    REQUIRE( sizeof( pstring::length ) == sizeof( void* ) );
    // Поле chars (fptr<char>) также хранит uintptr_t.
    REQUIRE( sizeof( pstring::chars ) == sizeof( void* ) );
    // Phase 3: pstring должен занимать 2 * sizeof(void*) байт.
    REQUIRE( sizeof( pstring ) == 2 * sizeof( void* ) );
}

// ---------------------------------------------------------------------------
// pstring — default PAP allocation (null/empty)
// ---------------------------------------------------------------------------
TEST_CASE( "pstring: zero-initialised pstring (via fptr) gives empty string", "[pstring][construct]" )
{
    fptr<pstring> fps;
    fps.New();

    REQUIRE( fps->empty() );
    REQUIRE( fps->size() == 0u );
    REQUIRE( std::strcmp( fps->c_str(), "" ) == 0 );

    fps.Delete();
}

// ---------------------------------------------------------------------------
// pstring — assign short string
// ---------------------------------------------------------------------------
TEST_CASE( "pstring: assign short string stores correct content", "[pstring][assign]" )
{
    fptr<pstring> fps;
    fps.New();

    fps->assign( "hello" );
    REQUIRE( !fps->empty() );
    REQUIRE( fps->size() == 5u );
    REQUIRE( std::strcmp( fps->c_str(), "hello" ) == 0 );

    fps->clear();
    fps.Delete();
}

// ---------------------------------------------------------------------------
// pstring — assign longer string
// ---------------------------------------------------------------------------
TEST_CASE( "pstring: assign longer string stores correct content", "[pstring][assign]" )
{
    fptr<pstring> fps;
    fps.New();

    const char* long_str = "The quick brown fox jumps over the lazy dog";
    fps->assign( long_str );
    REQUIRE( fps->size() == std::strlen( long_str ) );
    REQUIRE( std::strcmp( fps->c_str(), long_str ) == 0 );

    fps->clear();
    fps.Delete();
}

// ---------------------------------------------------------------------------
// pstring — reassign to different string
// ---------------------------------------------------------------------------
TEST_CASE( "pstring: reassigning frees old allocation and stores new content", "[pstring][reassign]" )
{
    fptr<pstring> fps;
    fps.New();

    fps->assign( "first" );
    REQUIRE( fps->size() == 5u );

    fps->assign( "second value" );
    REQUIRE( fps->size() == 12u );
    REQUIRE( std::strcmp( fps->c_str(), "second value" ) == 0 );

    fps->clear();
    fps.Delete();
}

// ---------------------------------------------------------------------------
// pstring — assign empty string
// ---------------------------------------------------------------------------
TEST_CASE( "pstring: assign empty string clears content", "[pstring][assign]" )
{
    fptr<pstring> fps;
    fps.New();

    fps->assign( "nonempty" );
    REQUIRE( !fps->empty() );

    fps->assign( "" );
    REQUIRE( fps->empty() );
    REQUIRE( fps->size() == 0u );

    fps.Delete();
}

// ---------------------------------------------------------------------------
// pstring — assign nullptr
// ---------------------------------------------------------------------------
TEST_CASE( "pstring: assign nullptr gives empty string", "[pstring][assign]" )
{
    fptr<pstring> fps;
    fps.New();

    fps->assign( nullptr );
    REQUIRE( fps->empty() );
    REQUIRE( fps->size() == 0u );
    REQUIRE( std::strcmp( fps->c_str(), "" ) == 0 );

    fps.Delete();
}

// ---------------------------------------------------------------------------
// pstring — clear
// ---------------------------------------------------------------------------
TEST_CASE( "pstring: clear resets to empty", "[pstring][clear]" )
{
    fptr<pstring> fps;
    fps.New();

    fps->assign( "hello world" );
    REQUIRE( !fps->empty() );

    fps->clear();
    REQUIRE( fps->empty() );
    REQUIRE( fps->size() == 0u );
    REQUIRE( fps->chars.addr() == 0u );

    fps.Delete();
}

// ---------------------------------------------------------------------------
// pstring — operator==
// ---------------------------------------------------------------------------
TEST_CASE( "pstring: operator== compares correctly", "[pstring][compare]" )
{
    fptr<pstring> fps1;
    fps1.New();
    fps1->assign( "hello" );

    fptr<pstring> fps2;
    fps2.New();
    fps2->assign( "hello" );

    fptr<pstring> fps3;
    fps3.New();
    fps3->assign( "world" );

    REQUIRE( *fps1 == *fps2 );
    REQUIRE( !( *fps1 == *fps3 ) );
    REQUIRE( *fps1 == "hello" );
    REQUIRE( !( *fps1 == "world" ) );

    fps1->clear();
    fps2->clear();
    fps3->clear();
    fps1.Delete();
    fps2.Delete();
    fps3.Delete();
}

// ---------------------------------------------------------------------------
// pstring — operator[] character access
// ---------------------------------------------------------------------------
TEST_CASE( "pstring: operator[] accesses individual characters", "[pstring][index]" )
{
    fptr<pstring> fps;
    fps.New();
    fps->assign( "abc" );

    REQUIRE( ( *fps )[0] == 'a' );
    REQUIRE( ( *fps )[1] == 'b' );
    REQUIRE( ( *fps )[2] == 'c' );

    fps->clear();
    fps.Delete();
}

// ---------------------------------------------------------------------------
// pstring — operator< (lexicographic order)
// ---------------------------------------------------------------------------
TEST_CASE( "pstring: operator< gives lexicographic order", "[pstring][compare]" )
{
    fptr<pstring> fps_a;
    fps_a.New();
    fps_a->assign( "apple" );

    fptr<pstring> fps_b;
    fps_b.New();
    fps_b->assign( "banana" );

    REQUIRE( *fps_a < *fps_b );
    REQUIRE( !( *fps_b < *fps_a ) );

    fps_a->clear();
    fps_b->clear();
    fps_a.Delete();
    fps_b.Delete();
}

// ---------------------------------------------------------------------------
// pstring — chars.addr() reflects allocation state
// ---------------------------------------------------------------------------
TEST_CASE( "pstring: chars.addr() is non-zero after assign, zero after clear", "[pstring][data]" )
{
    fptr<pstring> fps;
    fps.New();

    REQUIRE( fps->chars.addr() == 0u );

    fps->assign( "test" );
    REQUIRE( fps->chars.addr() != 0u );
    REQUIRE( fps->length == 4u );

    fps->clear();
    REQUIRE( fps->chars.addr() == 0u );
    REQUIRE( fps->length == 0u );

    fps.Delete();
}
