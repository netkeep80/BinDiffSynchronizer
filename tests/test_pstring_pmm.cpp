/**
 * @file test_pstring_pmm.cpp
 * @brief Тесты для PamManager::pstring (pmm::pstring<PamManager>).
 *
 * Тесты проверяют корректность работы PMM pstring
 * в качестве строки для JSON string-value узлов.
 */

#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <type_traits>

#include "pam_pmm_config.h"

using namespace pjson;

/// Тип PMM pstring для удобства.
using pmm_pstring = PamManager::pstring;

// ═══════════════════════════════════════════════════════════════════════════
// ТЕСТЫ LAYOUT
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE( "pmm_pstring: struct size is 12 bytes", "[pmm_pstring][layout]" )
{
    // PMM pstring: { uint32_t _length; uint32_t _capacity; uint32_t _data_idx; } = 12 bytes.
    REQUIRE( sizeof( pmm_pstring ) == 12 );
}

TEST_CASE( "pmm_pstring: is trivially copyable", "[pmm_pstring][layout]" )
{
    REQUIRE( std::is_trivially_copyable<pmm_pstring>::value );
}

// ═══════════════════════════════════════════════════════════════════════════
// ТЕСТЫ БАЗОВЫХ ОПЕРАЦИЙ
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE( "pmm_pstring: default-constructed gives empty string", "[pmm_pstring][construct]" )
{
    PamManager::create( 64 * 1024 );

    pmm_pstring ps{};

    REQUIRE( ps.empty() );
    REQUIRE( ps.size() == 0u );
    REQUIRE( std::strcmp( ps.c_str(), "" ) == 0 );

    PamManager::destroy();
}

TEST_CASE( "pmm_pstring: assign short string stores correct content", "[pmm_pstring][assign]" )
{
    PamManager::create( 64 * 1024 );

    pmm_pstring ps{};
    REQUIRE( ps.assign( "hello" ) );

    REQUIRE( !ps.empty() );
    REQUIRE( ps.size() == 5u );
    REQUIRE( std::strcmp( ps.c_str(), "hello" ) == 0 );

    ps.free_data();
    PamManager::destroy();
}

TEST_CASE( "pmm_pstring: assign longer string stores correct content", "[pmm_pstring][assign]" )
{
    PamManager::create( 64 * 1024 );

    const char* long_str = "The quick brown fox jumps over the lazy dog";

    pmm_pstring ps{};
    REQUIRE( ps.assign( long_str ) );

    REQUIRE( ps.size() == std::strlen( long_str ) );
    REQUIRE( std::strcmp( ps.c_str(), long_str ) == 0 );

    ps.free_data();
    PamManager::destroy();
}

TEST_CASE( "pmm_pstring: reassigning stores new content", "[pmm_pstring][reassign]" )
{
    PamManager::create( 64 * 1024 );

    pmm_pstring ps{};
    REQUIRE( ps.assign( "first" ) );
    REQUIRE( ps.size() == 5u );

    REQUIRE( ps.assign( "second value" ) );
    REQUIRE( ps.size() == 12u );
    REQUIRE( std::strcmp( ps.c_str(), "second value" ) == 0 );

    ps.free_data();
    PamManager::destroy();
}

TEST_CASE( "pmm_pstring: assign empty string sets length to zero", "[pmm_pstring][assign]" )
{
    PamManager::create( 64 * 1024 );

    pmm_pstring ps{};
    REQUIRE( ps.assign( "nonempty" ) );
    REQUIRE( !ps.empty() );

    REQUIRE( ps.assign( "" ) );
    REQUIRE( ps.size() == 0u );

    ps.free_data();
    PamManager::destroy();
}

TEST_CASE( "pmm_pstring: assign nullptr gives empty string", "[pmm_pstring][assign]" )
{
    PamManager::create( 64 * 1024 );

    pmm_pstring ps{};
    REQUIRE( ps.assign( "test" ) );

    REQUIRE( ps.assign( nullptr ) );
    REQUIRE( ps.size() == 0u );
    REQUIRE( std::strcmp( ps.c_str(), "" ) == 0 );

    ps.free_data();
    PamManager::destroy();
}

TEST_CASE( "pmm_pstring: clear resets length to zero", "[pmm_pstring][clear]" )
{
    PamManager::create( 64 * 1024 );

    pmm_pstring ps{};
    REQUIRE( ps.assign( "hello world" ) );
    REQUIRE( !ps.empty() );

    ps.clear();

    REQUIRE( ps.empty() );
    REQUIRE( ps.size() == 0u );

    ps.free_data();
    PamManager::destroy();
}

TEST_CASE( "pmm_pstring: free_data releases all resources", "[pmm_pstring][free_data]" )
{
    PamManager::create( 64 * 1024 );

    pmm_pstring ps{};
    REQUIRE( ps.assign( "hello world" ) );
    REQUIRE( !ps.empty() );

    ps.free_data();

    REQUIRE( ps.empty() );
    REQUIRE( ps.size() == 0u );

    PamManager::destroy();
}

// ═══════════════════════════════════════════════════════════════════════════
// ТЕСТЫ СРАВНЕНИЯ
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE( "pmm_pstring: operator== compares correctly", "[pmm_pstring][compare]" )
{
    PamManager::create( 64 * 1024 );

    pmm_pstring ps1{};
    ps1.assign( "hello" );
    pmm_pstring ps2{};
    ps2.assign( "hello" );
    pmm_pstring ps3{};
    ps3.assign( "world" );

    REQUIRE( ps1 == ps2 );
    REQUIRE( !( ps1 == ps3 ) );
    REQUIRE( ps1 == "hello" );
    REQUIRE( !( ps1 == "world" ) );

    ps1.free_data();
    ps2.free_data();
    ps3.free_data();
    PamManager::destroy();
}

TEST_CASE( "pmm_pstring: operator< gives lexicographic order", "[pmm_pstring][compare]" )
{
    PamManager::create( 64 * 1024 );

    pmm_pstring ps_a{};
    ps_a.assign( "apple" );
    pmm_pstring ps_b{};
    ps_b.assign( "banana" );

    REQUIRE( ps_a < ps_b );
    REQUIRE( !( ps_b < ps_a ) );

    ps_a.free_data();
    ps_b.free_data();
    PamManager::destroy();
}

// ═══════════════════════════════════════════════════════════════════════════
// ТЕСТЫ ДОСТУПА К СИМВОЛАМ
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE( "pmm_pstring: operator[] accesses individual characters", "[pmm_pstring][index]" )
{
    PamManager::create( 64 * 1024 );

    pmm_pstring ps{};
    ps.assign( "abc" );

    REQUIRE( ps[0] == 'a' );
    REQUIRE( ps[1] == 'b' );
    REQUIRE( ps[2] == 'c' );

    ps.free_data();
    PamManager::destroy();
}

// ═══════════════════════════════════════════════════════════════════════════
// ТЕСТЫ МНОЖЕСТВЕННЫХ СТРОК
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE( "pmm_pstring: multiple strings are independent", "[pmm_pstring][multiple]" )
{
    PamManager::create( 64 * 1024 );

    pmm_pstring ps1{};
    ps1.assign( "first" );
    pmm_pstring ps2{};
    ps2.assign( "second" );
    pmm_pstring ps3{};
    ps3.assign( "third" );

    REQUIRE( std::strcmp( ps1.c_str(), "first" ) == 0 );
    REQUIRE( std::strcmp( ps2.c_str(), "second" ) == 0 );
    REQUIRE( std::strcmp( ps3.c_str(), "third" ) == 0 );

    // Изменение одной не влияет на другие
    ps2.assign( "modified" );

    REQUIRE( std::strcmp( ps1.c_str(), "first" ) == 0 );
    REQUIRE( std::strcmp( ps2.c_str(), "modified" ) == 0 );
    REQUIRE( std::strcmp( ps3.c_str(), "third" ) == 0 );

    ps1.free_data();
    ps2.free_data();
    ps3.free_data();
    PamManager::destroy();
}

// ═══════════════════════════════════════════════════════════════════════════
// ТЕСТЫ APPEND
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE( "pmm_pstring: append extends string content", "[pmm_pstring][append]" )
{
    PamManager::create( 64 * 1024 );

    pmm_pstring ps{};
    REQUIRE( ps.assign( "hello" ) );
    REQUIRE( ps.append( " world" ) );

    REQUIRE( ps.size() == 11u );
    REQUIRE( std::strcmp( ps.c_str(), "hello world" ) == 0 );

    ps.free_data();
    PamManager::destroy();
}
