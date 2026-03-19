#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <type_traits>

#include "pstring_pmm.h"
#include "fptr_pmm.h"
#include "pam_pmm.h"

using namespace pjson;

// Вспомогательная функция: инициализировать PMM перед каждым тестом.
static void ensure_pmm()
{
    if ( !pam_pmm_is_initialized() )
        pam_pmm_init( nullptr );
}

// =============================================================================
// Tests for pstring_pmm (persistent string)
// =============================================================================

// ---------------------------------------------------------------------------
// pstring_pmm — layout checks
// ---------------------------------------------------------------------------
TEST_CASE( "pstring_pmm: length field is uintptr_t (Phase 3)", "[pstring_pmm][layout][phase3]" )
{
    // sizeof(uintptr_t) == sizeof(void*) на любой платформе.
    REQUIRE( sizeof( pstring_pmm::length ) == sizeof( void* ) );
    // Поле chars_off (uintptr_t) — смещение массива char в ПАП.
    REQUIRE( sizeof( pstring_pmm::chars_off ) == sizeof( void* ) );
    // Phase 3: pstring_pmm должен занимать 2 * sizeof(void*) байт.
    REQUIRE( sizeof( pstring_pmm ) == 2 * sizeof( void* ) );
}

// ---------------------------------------------------------------------------
// pstring_pmm — default PAP allocation (null/empty)
// ---------------------------------------------------------------------------
TEST_CASE( "pstring_pmm: zero-initialised pstring_pmm (via fptr) gives empty string", "[pstring_pmm][construct]" )
{
    ensure_pmm();
    fptr<pstring_pmm> fps;
    fps.New();

    REQUIRE( fps->empty() );
    REQUIRE( fps->size() == 0u );
    REQUIRE( std::strcmp( fps->c_str(), "" ) == 0 );

    fps.Delete();
}

// ---------------------------------------------------------------------------
// pstring_pmm — assign short string
// ---------------------------------------------------------------------------
TEST_CASE( "pstring_pmm: assign short string stores correct content", "[pstring_pmm][assign]" )
{
    ensure_pmm();
    fptr<pstring_pmm> fps;
    fps.New();

    fps->assign( "hello" );
    REQUIRE( !fps->empty() );
    REQUIRE( fps->size() == 5u );
    REQUIRE( std::strcmp( fps->c_str(), "hello" ) == 0 );

    fps->clear();
    fps.Delete();
}

// ---------------------------------------------------------------------------
// pstring_pmm — assign longer string
// ---------------------------------------------------------------------------
TEST_CASE( "pstring_pmm: assign longer string stores correct content", "[pstring_pmm][assign]" )
{
    ensure_pmm();
    fptr<pstring_pmm> fps;
    fps.New();

    const char* long_str = "The quick brown fox jumps over the lazy dog";
    fps->assign( long_str );
    REQUIRE( fps->size() == std::strlen( long_str ) );
    REQUIRE( std::strcmp( fps->c_str(), long_str ) == 0 );

    fps->clear();
    fps.Delete();
}

// ---------------------------------------------------------------------------
// pstring_pmm — reassign to different string
// ---------------------------------------------------------------------------
TEST_CASE( "pstring_pmm: reassigning frees old allocation and stores new content", "[pstring_pmm][reassign]" )
{
    ensure_pmm();
    fptr<pstring_pmm> fps;
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
// pstring_pmm — assign empty string
// ---------------------------------------------------------------------------
TEST_CASE( "pstring_pmm: assign empty string clears content", "[pstring_pmm][assign]" )
{
    ensure_pmm();
    fptr<pstring_pmm> fps;
    fps.New();

    fps->assign( "nonempty" );
    REQUIRE( !fps->empty() );

    fps->assign( "" );
    REQUIRE( fps->empty() );
    REQUIRE( fps->size() == 0u );

    fps.Delete();
}

// ---------------------------------------------------------------------------
// pstring_pmm — assign nullptr
// ---------------------------------------------------------------------------
TEST_CASE( "pstring_pmm: assign nullptr gives empty string", "[pstring_pmm][assign]" )
{
    ensure_pmm();
    fptr<pstring_pmm> fps;
    fps.New();

    fps->assign( nullptr );
    REQUIRE( fps->empty() );
    REQUIRE( fps->size() == 0u );
    REQUIRE( std::strcmp( fps->c_str(), "" ) == 0 );

    fps.Delete();
}

// ---------------------------------------------------------------------------
// pstring_pmm — clear
// ---------------------------------------------------------------------------
TEST_CASE( "pstring_pmm: clear resets to empty", "[pstring_pmm][clear]" )
{
    ensure_pmm();
    fptr<pstring_pmm> fps;
    fps.New();

    fps->assign( "hello world" );
    REQUIRE( !fps->empty() );

    fps->clear();
    REQUIRE( fps->empty() );
    REQUIRE( fps->size() == 0u );
    REQUIRE( fps->chars_off == 0u );

    fps.Delete();
}

// ---------------------------------------------------------------------------
// pstring_pmm — operator==
// ---------------------------------------------------------------------------
TEST_CASE( "pstring_pmm: operator== compares correctly", "[pstring_pmm][compare]" )
{
    ensure_pmm();
    fptr<pstring_pmm> fps1;
    fps1.New();
    fps1->assign( "hello" );

    fptr<pstring_pmm> fps2;
    fps2.New();
    fps2->assign( "hello" );

    fptr<pstring_pmm> fps3;
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
// pstring_pmm — operator[] character access
// ---------------------------------------------------------------------------
TEST_CASE( "pstring_pmm: operator[] accesses individual characters", "[pstring_pmm][index]" )
{
    ensure_pmm();
    fptr<pstring_pmm> fps;
    fps.New();
    fps->assign( "abc" );

    REQUIRE( ( *fps )[0] == 'a' );
    REQUIRE( ( *fps )[1] == 'b' );
    REQUIRE( ( *fps )[2] == 'c' );

    fps->clear();
    fps.Delete();
}

// ---------------------------------------------------------------------------
// pstring_pmm — operator< (lexicographic order)
// ---------------------------------------------------------------------------
TEST_CASE( "pstring_pmm: operator< gives lexicographic order", "[pstring_pmm][compare]" )
{
    ensure_pmm();
    fptr<pstring_pmm> fps_a;
    fps_a.New();
    fps_a->assign( "apple" );

    fptr<pstring_pmm> fps_b;
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
// pstring_pmm — chars_off reflects allocation state
// ---------------------------------------------------------------------------
TEST_CASE( "pstring_pmm: chars_off is non-zero after assign, zero after clear", "[pstring_pmm][data]" )
{
    ensure_pmm();
    fptr<pstring_pmm> fps;
    fps.New();

    REQUIRE( fps->chars_off == 0u );

    fps->assign( "test" );
    REQUIRE( fps->chars_off != 0u );
    REQUIRE( fps->length == 4u );

    fps->clear();
    REQUIRE( fps->chars_off == 0u );
    REQUIRE( fps->length == 0u );

    fps.Delete();
}
